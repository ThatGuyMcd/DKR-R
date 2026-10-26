#include "netplay/rollback_state_store.hpp"

#include "gekkonet.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

namespace {

struct Input {
    std::int16_t throttle = 0;
    std::int16_t steering = 0;
};
static_assert(sizeof(Input) == 4U);

struct State {
    std::array<std::int64_t, 2> position{};
    std::array<std::int64_t, 2> heading{};
    std::uint32_t ticks = 0U;
    std::uint32_t guard = 0xD1DD1E5U;
};

struct Token {
    std::uint64_t magic = 0U;
    std::uint64_t checksum = 0U;
    std::int32_t frame = -1;
    std::uint32_t reserved = 0U;
};
static_assert(sizeof(Token) == 24U);

constexpr std::uint64_t kMagic = 0x444B525445535431ULL;

std::uint64_t checksum(const State& state) {
    const auto bytes = std::as_bytes(std::span{&state, 1U});
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::byte byte : bytes) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void advance(State& state, const Input* inputs) {
    for (std::size_t player = 0U; player < 2U; ++player) {
        state.heading[player] += inputs[player].steering;
        state.position[player] += inputs[player].throttle * 8 + state.heading[player] / 16;
    }
    ++state.ticks;
}

Input input_for(std::uint32_t tick, std::size_t player) {
    return {
        static_cast<std::int16_t>(40 + ((tick + player * 7U) % 13U)),
        static_cast<std::int16_t>(static_cast<int>((tick * (player + 3U)) % 19U) - 9),
    };
}

} // namespace

int main() {
    using dkr::runtime::netplay::RollbackStateStore;

    GekkoSession* session = nullptr;
    assert(gekko_create(&session, GekkoStressSession));
    GekkoConfig config{};
    config.num_players = 2U;
    config.input_size = sizeof(Input);
    config.state_size = sizeof(Token);
    config.input_prediction_window = 12U;
    config.check_distance = 10U;
    config.desync_detection = true;
    gekko_start(session, &config);
    assert(gekko_add_actor(session, GekkoLocalPlayer, nullptr) == 0);
    assert(gekko_add_actor(session, GekkoLocalPlayer, nullptr) == 1);

    RollbackStateStore snapshots(sizeof(State), 64U, 4U);
    State state{};
    State expected{};
    std::array<std::uint8_t, sizeof(State)> capture{};
    std::array<std::uint8_t, sizeof(State)> restore{};
    constexpr std::uint32_t kTicks = 180U;

    for (std::uint32_t tick = 0U; tick < kTicks; ++tick) {
        std::array<Input, 2> authored{input_for(tick, 0U), input_for(tick, 1U)};
        for (int player = 0; player < 2; ++player) {
            gekko_add_local_input(session, player, &authored[player]);
        }
        advance(expected, authored.data());

        int count = 0;
        GekkoGameEvent** events = gekko_update_session(session, &count);
        for (int index = 0; index < count; ++index) {
            GekkoGameEvent* event = events[index];
            assert(event != nullptr);
            if (event->type == GekkoSaveEvent) {
                assert(event->data.save.frame >= -1);
                std::memcpy(capture.data(), &state, sizeof(state));
                const std::uint64_t hash = checksum(state);
                const std::uint32_t storage_frame = static_cast<std::uint32_t>(event->data.save.frame + 1);
                assert(snapshots.save(storage_frame, capture, hash));
                const Token token{kMagic, hash, event->data.save.frame, 0U};
                std::memcpy(event->data.save.state, &token, sizeof(token));
                *event->data.save.state_len = sizeof(token);
                *event->data.save.checksum = static_cast<std::uint32_t>(hash ^ (hash >> 32U));
            } else if (event->type == GekkoLoadEvent) {
                assert(event->data.load.state_len == sizeof(Token));
                Token token{};
                std::memcpy(&token, event->data.load.state, sizeof(token));
                assert(token.magic == kMagic && token.frame >= -1);
                std::uint64_t stored_hash = 0U;
                const std::uint32_t storage_frame = static_cast<std::uint32_t>(token.frame + 1);
                assert(snapshots.load(storage_frame, restore, &stored_hash));
                assert(stored_hash == token.checksum);
                std::memcpy(&state, restore.data(), sizeof(state));
                snapshots.discard_after(storage_frame);
            } else if (event->type == GekkoAdvanceEvent) {
                assert(event->data.adv.input_len == sizeof(Input) * 2U);
                advance(state, reinterpret_cast<const Input*>(event->data.adv.inputs));
            }
        }

        int session_count = 0;
        GekkoSessionEvent** session_events = gekko_session_events(session, &session_count);
        for (int index = 0; index < session_count; ++index) {
            assert(session_events[index] != nullptr);
            assert(session_events[index]->type != GekkoDesyncDetected);
        }
    }

    assert(state.position == expected.position);
    assert(state.heading == expected.heading);
    assert(state.ticks == expected.ticks);
    assert(state.guard == expected.guard);
    assert(snapshots.memory_bytes() < sizeof(State) * 80U);
    assert(gekko_destroy(&session));
    return 0;
}
