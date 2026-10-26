#include "netplay_protocol.hpp"
#include "netplay_pacing_policy.hpp"
#include "netplay_timeline.hpp"
#include "rollback_state_store.hpp"
#include "sequence_window.hpp"

#include "gekkonet.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <deque>
#include <random>
#include <span>
#include <vector>

namespace {

struct RollbackInput {
    std::int16_t throttle = 0;
    std::int16_t steering = 0;
};
static_assert(sizeof(RollbackInput) == 4U);

constexpr std::size_t kPlayerCount = 4U;

struct RollbackState {
    std::array<std::int64_t, kPlayerCount> position{};
    std::array<std::int64_t, kPlayerCount> heading{};
    std::uint32_t ticks = 0U;
    std::uint32_t guard = 0xD1DD1E5U;
};

struct RollbackToken {
    std::uint64_t magic = 0U;
    std::uint64_t checksum = 0U;
    std::int32_t frame = -1;
    std::uint32_t reserved = 0U;
};
static_assert(sizeof(RollbackToken) == 24U);

constexpr std::uint64_t kRollbackMagic = 0x444B524144565253ULL;

std::uint64_t rollback_checksum(const RollbackState& state) {
    const auto bytes = std::as_bytes(std::span{&state, 1U});
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::byte byte : bytes) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void advance_rollback_state(RollbackState& state, const RollbackInput* inputs) {
    for (std::size_t player = 0U; player < kPlayerCount; ++player) {
        state.heading[player] += inputs[player].steering;
        state.position[player] += inputs[player].throttle * 8 + state.heading[player] / 16;
    }
    ++state.ticks;
}

RollbackInput rollback_input_for(std::uint32_t tick, std::size_t player) {
    if (tick >= 1000U)
        return {50, 0};
    return {
        static_cast<std::int16_t>(40 + ((tick + player * 7U) % 13U)),
        static_cast<std::int16_t>(static_cast<int>((tick * (player + 3U)) % 19U) - 9),
    };
}

struct SimulatedPacket {
    std::uint32_t deliver_at = 0U;
    std::uint8_t source = 0U;
    std::uint8_t target = 0U;
    std::vector<std::uint8_t> bytes;
};

std::deque<SimulatedPacket> g_simulated_packets;
std::vector<GekkoNetResult*> g_simulated_results;
std::uint32_t g_simulated_tick = 0U;
std::uint32_t g_simulated_serial = 0U;
std::uint8_t g_simulated_endpoint = 0U;

void simulated_send(GekkoNetAddress* address, const char* data, int length) {
    assert(address != nullptr && address->data != nullptr && address->size == 1U && data != nullptr && length > 0);
    const std::uint8_t target = *static_cast<const std::uint8_t*>(address->data);
    assert(target < kPlayerCount && target != g_simulated_endpoint);
    const std::uint32_t serial = ++g_simulated_serial;
    // Let the handshake settle, then inject deterministic 5.9% packet loss,
    // 1-6 ticks of jitter, reordering and occasional duplication. The quiet
    // tail proves that all predictions eventually converge exactly.
    const bool adverse = g_simulated_tick >= 100U && g_simulated_tick < 1000U;
    if (adverse && (serial % 17U) == 7U)
        return;
    const std::uint32_t delay = adverse ? 1U + ((serial * 7U + g_simulated_endpoint * 3U) % 6U) : 1U;
    SimulatedPacket packet{};
    packet.deliver_at = g_simulated_tick + delay;
    packet.source = g_simulated_endpoint;
    packet.target = target;
    packet.bytes.assign(reinterpret_cast<const std::uint8_t*>(data),
                        reinterpret_cast<const std::uint8_t*>(data) + length);
    g_simulated_packets.push_back(packet);
    if (adverse && (serial % 41U) == 9U) {
        packet.deliver_at += 2U;
        g_simulated_packets.push_back(std::move(packet));
    }
}

GekkoNetResult** simulated_receive(int* length) {
    assert(length != nullptr);
    g_simulated_results.clear();
    for (auto packet = g_simulated_packets.begin(); packet != g_simulated_packets.end();) {
        if (packet->target != g_simulated_endpoint || packet->deliver_at > g_simulated_tick) {
            ++packet;
            continue;
        }
        auto* result = static_cast<GekkoNetResult*>(std::calloc(1U, sizeof(GekkoNetResult)));
        assert(result != nullptr);
        result->addr.size = 1U;
        result->addr.data = std::malloc(1U);
        result->data_len = static_cast<unsigned int>(packet->bytes.size());
        result->data = std::malloc(packet->bytes.size());
        assert(result->addr.data != nullptr && result->data != nullptr);
        *static_cast<std::uint8_t*>(result->addr.data) = packet->source;
        std::memcpy(result->data, packet->bytes.data(), packet->bytes.size());
        g_simulated_results.push_back(result);
        packet = g_simulated_packets.erase(packet);
    }
    *length = static_cast<int>(g_simulated_results.size());
    return g_simulated_results.data();
}

void simulated_free(void* data) {
    std::free(data);
}

GekkoNetAdapter g_simulated_adapter{simulated_send, simulated_receive, simulated_free};

struct RollbackEndpoint {
    GekkoSession* session = nullptr;
    int local_handle = -1;
    RollbackState state{};
    dkr::runtime::netplay::RollbackStateStore snapshots{sizeof(RollbackState), 64U, 4U};
    std::array<std::uint8_t, sizeof(RollbackState)> capture{};
    std::array<std::uint8_t, sizeof(RollbackState)> restore{};
    std::uint32_t rollbacks = 0U;
};

void initialise_endpoint(RollbackEndpoint& endpoint, std::uint8_t local_slot) {
    assert(gekko_create(&endpoint.session, GekkoGameSession));
    GekkoConfig config{};
    config.num_players = static_cast<unsigned int>(kPlayerCount);
    config.input_size = sizeof(RollbackInput);
    config.state_size = sizeof(RollbackToken);
    config.input_prediction_window = 12U;
    config.desync_detection = true;
    config.check_distance = 20U;
    gekko_start(endpoint.session, &config);
    gekko_net_adapter_set(endpoint.session, &g_simulated_adapter);
    gekko_set_disconnect_timeout(endpoint.session, 0U);
    for (std::uint8_t slot = 0U; slot < kPlayerCount; ++slot) {
        int handle = -1;
        if (slot == local_slot) {
            handle = gekko_add_actor(endpoint.session, GekkoLocalPlayer, nullptr);
            endpoint.local_handle = handle;
        } else {
            GekkoNetAddress address{&slot, 1U};
            handle = gekko_add_actor(endpoint.session, GekkoRemotePlayer, &address);
        }
        assert(handle == static_cast<int>(slot));
    }
    assert(endpoint.local_handle == static_cast<int>(local_slot));
    gekko_set_local_delay(endpoint.session, endpoint.local_handle, 2U);
    gekko_set_runahead(endpoint.session, 0U);
}

void update_endpoint(RollbackEndpoint& endpoint, std::uint8_t endpoint_id, std::uint32_t input_tick) {
    g_simulated_endpoint = endpoint_id;
    const RollbackInput local = rollback_input_for(input_tick, endpoint_id);
    gekko_add_local_input(endpoint.session, endpoint.local_handle, const_cast<RollbackInput*>(&local));
    int event_count = 0;
    GekkoGameEvent** events = gekko_update_session(endpoint.session, &event_count);
    for (int index = 0; index < event_count; ++index) {
        GekkoGameEvent* event = events[index];
        assert(event != nullptr);
        if (event->type == GekkoSaveEvent) {
            assert(event->data.save.frame >= -1);
            std::memcpy(endpoint.capture.data(), &endpoint.state, sizeof(endpoint.state));
            const std::uint64_t hash = rollback_checksum(endpoint.state);
            const std::uint32_t frame = static_cast<std::uint32_t>(event->data.save.frame + 1);
            assert(endpoint.snapshots.save(frame, endpoint.capture, hash));
            const RollbackToken token{kRollbackMagic, hash, event->data.save.frame, 0U};
            std::memcpy(event->data.save.state, &token, sizeof(token));
            *event->data.save.state_len = sizeof(token);
            *event->data.save.checksum = static_cast<std::uint32_t>(hash ^ (hash >> 32U));
        } else if (event->type == GekkoLoadEvent) {
            assert(event->data.load.state_len == sizeof(RollbackToken));
            RollbackToken token{};
            std::memcpy(&token, event->data.load.state, sizeof(token));
            assert(token.magic == kRollbackMagic && token.frame >= -1);
            std::uint64_t stored_hash = 0U;
            const std::uint32_t frame = static_cast<std::uint32_t>(token.frame + 1);
            assert(endpoint.snapshots.load(frame, endpoint.restore, &stored_hash));
            assert(stored_hash == token.checksum);
            std::memcpy(&endpoint.state, endpoint.restore.data(), sizeof(endpoint.state));
            endpoint.snapshots.discard_after(frame);
            ++endpoint.rollbacks;
        } else if (event->type == GekkoAdvanceEvent) {
            assert(event->data.adv.input_len == sizeof(RollbackInput) * kPlayerCount);
            advance_rollback_state(endpoint.state, reinterpret_cast<const RollbackInput*>(event->data.adv.inputs));
        }
    }
    int session_event_count = 0;
    GekkoSessionEvent** session_events = gekko_session_events(endpoint.session, &session_event_count);
    for (int index = 0; index < session_event_count; ++index) {
        assert(session_events[index] != nullptr);
        assert(session_events[index]->type != GekkoDesyncDetected);
        assert(session_events[index]->type != GekkoPlayerDisconnected);
    }
}

void exercise_rollback_under_adverse_network() {
    g_simulated_packets.clear();
    g_simulated_results.clear();
    g_simulated_tick = 0U;
    g_simulated_serial = 0U;
    std::array<RollbackEndpoint, kPlayerCount> endpoints{};
    for (std::uint8_t endpoint = 0U; endpoint < kPlayerCount; ++endpoint) {
        initialise_endpoint(endpoints[endpoint], endpoint);
    }

    constexpr std::uint32_t kTicks = 1500U;
    for (std::uint32_t tick = 0U; tick < kTicks; ++tick) {
        g_simulated_tick = tick;
        // Rotate update order so no one of four simultaneous racers receives
        // a permanent zero-latency advantage from the test harness.
        for (std::uint8_t step = 0U; step < kPlayerCount; ++step) {
            const std::uint8_t endpoint = static_cast<std::uint8_t>((tick + step) % kPlayerCount);
            update_endpoint(endpoints[endpoint], endpoint, tick);
        }
    }

    for (const RollbackEndpoint& endpoint : endpoints) {
        assert(endpoint.rollbacks > 0U);
    }
    assert(endpoints[0].state.ticks > 1000U);
    for (std::size_t endpoint = 1U; endpoint < kPlayerCount; ++endpoint) {
        assert(endpoints[0].state.ticks == endpoints[endpoint].state.ticks);
        assert(endpoints[0].state.position == endpoints[endpoint].state.position);
        assert(endpoints[0].state.heading == endpoints[endpoint].state.heading);
    }
    for (RollbackEndpoint& endpoint : endpoints) {
        assert(endpoint.state.guard == 0xD1DD1E5U);
        assert(gekko_destroy(&endpoint.session));
    }
}

} // namespace

int main() {
    using namespace dkr::runtime::netplay;

    // A frontend client that temporarily stops presenting must neither retain
    // permanent debt nor force Player 1 to run unboundedly ahead. Model the
    // production combination: ordinary 8-frame host lead ceiling plus the
    // existing outer-VI controller, which can raise one visible authored tick
    // stream as high as 40 Hz without nesting game loops.
    {
        ClientCatchUpController controller;
        constexpr std::uint32_t target_debt = 2U;
        constexpr std::uint32_t maximum_lead = 8U;
        std::uint32_t host_cursor = 0U;
        std::uint32_t client_cursor = 0U;
        std::uint32_t client_credit = 0U;
        std::uint32_t maximum_observed_debt = 0U;
        bool host_held = false;
        bool client_accelerated = false;
        for (std::uint32_t outer = 0U; outer < 360U; ++outer) {
            const bool hold = host_backpressure_required(host_cursor, client_cursor, maximum_lead);
            host_held = host_held || hold;
            if (!hold)
                ++host_cursor;

            const bool client_hitched = outer >= 30U && outer < 55U;
            const std::uint32_t debt = host_cursor > client_cursor ? host_cursor - client_cursor : 0U;
            maximum_observed_debt = (std::max)(maximum_observed_debt, debt);
            const auto decision = controller.update(ClientCatchUpSample{
                .eligible = !client_hitched,
                .frame_debt = debt,
                .target_debt = target_debt,
                .contiguous_commits = host_cursor > client_cursor ? 1U : 0U,
            });
            client_accelerated = client_accelerated || decision.target_simulation_hz > 30U;
            if (client_hitched)
                continue;
            client_credit += decision.target_simulation_hz;
            while (client_credit >= 30U && client_cursor < host_cursor) {
                client_credit -= 30U;
                ++client_cursor;
            }
        }
        assert(host_held);
        assert(client_accelerated);
        assert(maximum_observed_debt <= maximum_lead + 1U);
        assert(host_cursor >= client_cursor);
        assert(host_cursor - client_cursor <= target_debt + 2U);
    }

    InputTimeline timeline(512U);
    std::vector<protocol::InputBatch> packets;
    std::vector<PackedInput> truth;
    for (std::uint32_t frame = 0; frame < 180U; ++frame) {
        truth.push_back({static_cast<std::uint16_t>(frame & 0xFFFFU), static_cast<std::int8_t>((frame % 81U) - 40),
                         static_cast<std::int8_t>(40 - (frame % 81U))});
        protocol::InputBatch batch{};
        batch.epoch = 1U;
        batch.player_slot = 1U;
        batch.first_frame = frame > 7U ? frame - 7U : 0U;
        for (std::uint32_t input_frame = batch.first_frame; input_frame <= frame; ++input_frame) {
            batch.inputs.push_back(truth[input_frame]);
        }
        // Deterministic 12.5% loss. Redundant history in later packets must
        // recover every frame except the deliberately absent tail.
        if ((frame % 8U) != 3U)
            packets.push_back(std::move(batch));
    }
    std::mt19937 random(0x444B5252U);
    for (std::size_t start = 0; start < packets.size(); start += 5U) {
        const std::size_t end = (std::min)(start + 5U, packets.size());
        std::shuffle(packets.begin() + static_cast<std::ptrdiff_t>(start),
                     packets.begin() + static_cast<std::ptrdiff_t>(end), random);
    }
    for (const auto& batch : packets) {
        const auto bytes = protocol::encode_input_batch(batch);
        protocol::InputBatch decoded{};
        std::string error;
        assert(protocol::decode_input_batch(bytes, decoded, error));
        for (std::size_t index = 0; index < decoded.inputs.size(); ++index) {
            timeline.set_remote(decoded.player_slot, decoded.first_frame + static_cast<std::uint32_t>(index),
                                decoded.inputs[index]);
        }
    }
    for (std::uint32_t frame = 0; frame < 173U; ++frame) {
        assert(timeline.inputs_for(frame)[1] == truth[frame]);
    }

    // A burst longer than the historical eight-frame redundancy window must
    // not strand one old predicted input forever. Model the protocol's
    // contiguous first-missing acknowledgement: after 25 consecutive sends
    // disappear, the next datagram starts at the unacknowledged hole and
    // repairs it in one bounded batch.
    InputTimeline burst_timeline(512U);
    std::uint32_t first_missing = 0U;
    for (std::uint32_t newest = 0U; newest < truth.size(); ++newest) {
        if (newest >= 20U && newest < 45U)
            continue;
        protocol::InputBatch repair{};
        repair.epoch = 1U;
        repair.player_slot = 1U;
        repair.first_frame = first_missing;
        for (std::uint32_t frame = first_missing; frame <= newest && repair.inputs.size() < 64U; ++frame) {
            repair.inputs.push_back(truth[frame]);
        }
        protocol::InputBatch decoded_repair{};
        std::string repair_error;
        assert(protocol::decode_input_batch(protocol::encode_input_batch(repair), decoded_repair, repair_error));
        for (std::size_t index = 0U; index < decoded_repair.inputs.size(); ++index) {
            burst_timeline.set_remote(decoded_repair.player_slot,
                                      decoded_repair.first_frame + static_cast<std::uint32_t>(index),
                                      decoded_repair.inputs[index]);
        }
        while (first_missing < truth.size() && burst_timeline.slot_confirmed(1U, first_missing)) {
            ++first_missing;
        }
    }
    assert(first_missing == truth.size());
    for (std::uint32_t frame = 0U; frame < truth.size(); ++frame) {
        assert(burst_timeline.inputs_for(frame)[1] == truth[frame]);
    }

    // Recovery fragments must remain below the conservative application MTU
    // after their protocol header is added. This leaves room for the secure
    // envelope and virtual-LAN tunnel rather than relying on IP fragmentation.
    protocol::StateSnapshotChunk recovery_chunk{};
    recovery_chunk.scene_epoch = 7U;
    recovery_chunk.frame = 900U;
    recovery_chunk.checksum = 0x123456789ABCDEF0ULL;
    recovery_chunk.total_size = 3713U;
    recovery_chunk.chunk_index = 0U;
    recovery_chunk.chunk_count = 5U;
    recovery_chunk.bytes.assign(900U, 0xA5U);
    const auto recovery_payload = protocol::encode_state_snapshot_chunk(recovery_chunk);
    assert(!recovery_payload.empty());
    protocol::Datagram recovery_datagram{{protocol::MessageType::StateSnapshot, 1U, 1U, recovery_chunk.frame},
                                         recovery_payload};
    assert(protocol::encode(recovery_datagram).size() <= protocol::kMaximumDatagramBytes);

    const protocol::TransitionBarrierPayload transition{7U, 901U, 1U, 0U, protocol::TransitionBarrierStage::Begin};
    const auto transition_bytes = protocol::encode_transition_barrier(transition);
    protocol::TransitionBarrierPayload decoded_transition{};
    assert(protocol::decode_transition_barrier(transition_bytes, decoded_transition));
    assert(decoded_transition.scene_epoch == transition.scene_epoch);
    assert(decoded_transition.frame == transition.frame);
    assert(decoded_transition.transition_kind == transition.transition_kind);
    assert(decoded_transition.stage == transition.stage);

    // A selective resend request identifies only absent pieces and is itself
    // strictly validated, preventing unbounded or malformed recovery bursts.
    const protocol::StateRequestPayload missing{7U, 900U, 5U, static_cast<std::uint16_t>((1U << 1U) | (1U << 4U))};
    const auto request_bytes = protocol::encode_state_request(missing);
    protocol::StateRequestPayload decoded_missing{};
    assert(protocol::decode_state_request(request_bytes, decoded_missing));
    assert(decoded_missing.missing_chunks == missing.missing_chunks);
    assert(protocol::encode_state_request({7U, 900U, 0U, 1U}).empty());

    const protocol::StateAcknowledgePayload acknowledgement{7U, 900U, recovery_chunk.checksum, 2U};
    const auto acknowledgement_bytes = protocol::encode_state_acknowledge(acknowledgement);
    protocol::StateAcknowledgePayload decoded_acknowledgement{};
    assert(protocol::decode_state_acknowledge(acknowledgement_bytes, decoded_acknowledgement));
    assert(decoded_acknowledgement.scene_epoch == acknowledgement.scene_epoch);
    assert(decoded_acknowledgement.frame == acknowledgement.frame);
    assert(decoded_acknowledgement.checksum == acknowledgement.checksum);
    assert(decoded_acknowledgement.player_slot == acknowledgement.player_slot);
    assert(protocol::encode_state_acknowledge({7U, 900U, 0U, 2U}).empty());

    // Independent Quick Join streams can push a reliable unordered frame
    // commit far behind the peer's global encryption sequence. Accept
    // legitimate traffic throughout the configured latency horizon while
    // still rejecting duplicates and packets exactly outside that bound.
    ReceiveSequenceWindow receive_window{};
    assert(receive_window.accept(1U));
    constexpr std::uint64_t newest = kReceiveSequenceWindowBits + 300U;
    assert(receive_window.accept(newest));
    assert(receive_window.accept(newest - 100U));
    assert(!receive_window.accept(newest - 100U));
    assert(!receive_window.accept(newest - kReceiveSequenceWindowBits));
    assert(receive_window.accept(newest + 1U));
    assert(!receive_window.accept(1U));

    exercise_rollback_under_adverse_network();
}
