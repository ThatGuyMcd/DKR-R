#include "netplay_protocol.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dkr::runtime::netplay::protocol {
namespace {

void Put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void Put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

bool Take16(std::span<const std::uint8_t> bytes, std::size_t& cursor,
            std::uint16_t& value) {
    if (cursor + 2U > bytes.size()) return false;
    value = static_cast<std::uint16_t>(bytes[cursor] << 8U) |
            bytes[cursor + 1U];
    cursor += 2U;
    return true;
}

bool Take32(std::span<const std::uint8_t> bytes, std::size_t& cursor,
            std::uint32_t& value) {
    if (cursor + 4U > bytes.size()) return false;
    value = 0U;
    for (int i = 0; i < 4; ++i) value = (value << 8U) | bytes[cursor++];
    return true;
}

bool Take64(std::span<const std::uint8_t> bytes, std::size_t& cursor,
            std::uint64_t& value) {
    if (cursor + 8U > bytes.size()) return false;
    value = 0U;
    for (int i = 0; i < 8; ++i) value = (value << 8U) | bytes[cursor++];
    return true;
}

bool ValidType(std::uint8_t type) {
    return type >= static_cast<std::uint8_t>(MessageType::Hello) &&
           type <=
               static_cast<std::uint8_t>(MessageType::OnlineSaveStatus);
}

bool PutString(std::vector<std::uint8_t>& out, std::string_view value,
               std::size_t maximum) {
    if (value.size() > maximum || value.size() > 255U) return false;
    out.push_back(static_cast<std::uint8_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

bool TakeString(std::span<const std::uint8_t> bytes, std::size_t& cursor,
                std::string& value, std::size_t maximum) {
    if (cursor >= bytes.size()) return false;
    const std::size_t length = bytes[cursor++];
    if (length > maximum || cursor + length > bytes.size()) return false;
    value.assign(reinterpret_cast<const char*>(bytes.data() + cursor), length);
    cursor += length;
    return true;
}

bool PutManifest(std::vector<std::uint8_t>& out,
                 const CompatibilityManifest& manifest) {
    Put32(out, manifest.protocol_version);
    if (!PutString(out, manifest.release_version,
                   kMaximumReleaseVersionBytes) ||
        !PutString(out, manifest.build_fingerprint,
                   kMaximumBuildFingerprintBytes)) {
        return false;
    }
    out.push_back(static_cast<std::uint8_t>(manifest.revision));
    Put64(out, manifest.canonical_rom_hash);
    Put64(out, manifest.patch_policy_hash);
    Put64(out, manifest.gameplay_settings_hash);
    Put64(out, manifest.magic_codes_hash);
    Put64(out, manifest.session_save_hash);
    Put32(out, manifest.simulation_rate);
    return PutString(out, manifest.architecture, 32U) &&
           PutString(out, manifest.floating_point_mode, 48U);
}

bool TakeManifest(std::span<const std::uint8_t> bytes, std::size_t& cursor,
                  CompatibilityManifest& manifest) {
    std::uint8_t revision = 0U;
    if (!Take32(bytes, cursor, manifest.protocol_version) ||
        !TakeString(bytes, cursor, manifest.release_version,
                    kMaximumReleaseVersionBytes) ||
        !TakeString(bytes, cursor, manifest.build_fingerprint,
                    kMaximumBuildFingerprintBytes) ||
        cursor >= bytes.size()) {
        return false;
    }
    revision = bytes[cursor++];
    if (revision > static_cast<std::uint8_t>(Revision::UsV80) ||
        !Take64(bytes, cursor, manifest.canonical_rom_hash) ||
        !Take64(bytes, cursor, manifest.patch_policy_hash) ||
        !Take64(bytes, cursor, manifest.gameplay_settings_hash) ||
        !Take64(bytes, cursor, manifest.magic_codes_hash) ||
        !Take64(bytes, cursor, manifest.session_save_hash) ||
        !Take32(bytes, cursor, manifest.simulation_rate) ||
        !TakeString(bytes, cursor, manifest.architecture, 32U) ||
        !TakeString(bytes, cursor, manifest.floating_point_mode, 48U)) {
        return false;
    }
    manifest.revision = static_cast<Revision>(revision);
    return !manifest.release_version.empty() &&
           !manifest.build_fingerprint.empty() &&
           !manifest.architecture.empty() &&
           !manifest.floating_point_mode.empty();
}

} // namespace

std::vector<std::uint8_t> encode(
    const Datagram& datagram, std::size_t maximum_datagram_bytes) {
    if (maximum_datagram_bytes < 32U ||
        maximum_datagram_bytes > kMaximumQuickJoinDatagramBytes ||
        datagram.payload.size() + 32U > maximum_datagram_bytes ||
        datagram.payload.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(32U + datagram.payload.size());
    Put32(out, kMagic);
    Put32(out, kProtocolVersion);
    out.push_back(static_cast<std::uint8_t>(datagram.header.type));
    out.push_back(0U);
    Put16(out, static_cast<std::uint16_t>(datagram.payload.size()));
    Put64(out, datagram.header.match_id);
    Put64(out, datagram.header.sequence);
    Put32(out, datagram.header.frame);
    out.insert(out.end(), datagram.payload.begin(), datagram.payload.end());
    return out;
}

bool decode(std::span<const std::uint8_t> bytes, Datagram& datagram,
            std::string& error, std::size_t maximum_datagram_bytes) {
    datagram = {};
    if (maximum_datagram_bytes < 32U ||
        maximum_datagram_bytes > kMaximumQuickJoinDatagramBytes ||
        bytes.size() < 32U || bytes.size() > maximum_datagram_bytes) {
        error = "Datagram size is outside the protocol limits.";
        return false;
    }
    std::size_t cursor = 0U;
    std::uint32_t magic = 0U;
    std::uint32_t version = 0U;
    if (!Take32(bytes, cursor, magic) || !Take32(bytes, cursor, version) ||
        magic != kMagic || version != kProtocolVersion) {
        error = "Datagram magic or protocol version is invalid.";
        return false;
    }
    const std::uint8_t type = bytes[cursor++];
    const std::uint8_t reserved = bytes[cursor++];
    std::uint16_t payload_size = 0U;
    if (!ValidType(type) || reserved != 0U ||
        !Take16(bytes, cursor, payload_size) ||
        !Take64(bytes, cursor, datagram.header.match_id) ||
        !Take64(bytes, cursor, datagram.header.sequence) ||
        !Take32(bytes, cursor, datagram.header.frame) ||
        cursor + payload_size != bytes.size()) {
        error = "Datagram header or payload length is invalid.";
        return false;
    }
    datagram.header.type = static_cast<MessageType>(type);
    datagram.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                            bytes.end());
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_input_batch(const InputBatch& batch) {
    if (batch.epoch == 0U || batch.player_slot >= kMaximumPlayers ||
        batch.inputs.empty() ||
        batch.inputs.size() > 64U ||
        (!batch.revisions.empty() && batch.revisions.size() != batch.inputs.size()) ||
        batch.first_frame >
            std::numeric_limits<std::uint32_t>::max() -
                static_cast<std::uint32_t>(batch.inputs.size() - 1U)) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(10U + batch.inputs.size() * 8U +
                (batch.simulation_progress_present ? 8U : 0U));
    Put32(out, batch.epoch);
    out.push_back(batch.player_slot);
    out.push_back(static_cast<std::uint8_t>(batch.inputs.size()));
    Put32(out, batch.first_frame);
    for (std::size_t i = 0; i < batch.inputs.size(); ++i) {
        const PackedInput input = batch.inputs[i];
        const auto revision = batch.revisions.empty() ? 1U : batch.revisions[i];
        if (revision == 0U) return {};
        Put16(out, input.buttons);
        out.push_back(static_cast<std::uint8_t>(input.stick_x));
        out.push_back(static_cast<std::uint8_t>(input.stick_y));
        Put32(out, revision);
    }
    if (batch.simulation_progress_present) {
        Put32(out, batch.simulation_scene_epoch);
        Put32(out, batch.simulation_completed_frame);
    }
    return out;
}

bool decode_input_batch(std::span<const std::uint8_t> bytes,
                        InputBatch& batch, std::string& error) {
    batch = {};
    if (bytes.size() < 18U) {
        error = "Input batch is truncated.";
        return false;
    }
    std::size_t cursor = 0U;
    if (!Take32(bytes, cursor, batch.epoch)) {
        error = "Input batch epoch is truncated.";
        return false;
    }
    batch.player_slot = bytes[cursor++];
    const std::uint8_t count = bytes[cursor++];
    const std::size_t input_bytes =
        10U + static_cast<std::size_t>(count) * 8U;
    if (batch.epoch == 0U || batch.player_slot >= kMaximumPlayers ||
        count == 0U || count > 64U ||
        (bytes.size() != input_bytes && bytes.size() != input_bytes + 8U) ||
        !Take32(bytes, cursor, batch.first_frame) ||
        batch.first_frame >
            std::numeric_limits<std::uint32_t>::max() -
                static_cast<std::uint32_t>(count - 1U)) {
        error = "Input batch metadata is invalid.";
        return false;
    }
    batch.inputs.reserve(count);
    for (std::uint8_t i = 0U; i < count; ++i) {
        PackedInput input{};
        if (!Take16(bytes, cursor, input.buttons)) {
            error = "Input batch is truncated.";
            return false;
        }
        input.stick_x = static_cast<std::int8_t>(bytes[cursor++]);
        input.stick_y = static_cast<std::int8_t>(bytes[cursor++]);
        std::uint32_t revision = 0U;
        if (!Take32(bytes, cursor, revision) || revision == 0U) {
            error = "Input sample revision is invalid.";
            return false;
        }
        batch.inputs.push_back(input);
        batch.revisions.push_back(revision);
    }
    if (bytes.size() == input_bytes + 8U) {
        batch.simulation_progress_present = true;
        if (!Take32(bytes, cursor, batch.simulation_scene_epoch) ||
            !Take32(bytes, cursor, batch.simulation_completed_frame)) {
            error = "Input batch simulation progress is invalid.";
            batch = {};
            return false;
        }
    }
    if (cursor != bytes.size()) {
        error = "Input batch has trailing data.";
        batch = {};
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_input_acknowledge(
    const InputAcknowledgePayload& acknowledgement) {
    if (acknowledgement.epoch == 0U ||
        acknowledgement.player_slot >= kMaximumPlayers) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(13U);
    Put32(out, acknowledgement.epoch);
    out.push_back(acknowledgement.player_slot);
    Put32(out, acknowledgement.newest_frame);
    Put32(out, acknowledgement.first_missing_frame);
    return out;
}

bool decode_input_acknowledge(
    std::span<const std::uint8_t> bytes,
    InputAcknowledgePayload& acknowledgement) {
    acknowledgement = {};
    std::size_t cursor = 0U;
    return bytes.size() == 13U &&
           Take32(bytes, cursor, acknowledgement.epoch) &&
           cursor < bytes.size() &&
           (acknowledgement.player_slot = bytes[cursor++]) <
               kMaximumPlayers &&
           Take32(bytes, cursor, acknowledgement.newest_frame) &&
           Take32(bytes, cursor, acknowledgement.first_missing_frame) &&
           acknowledgement.epoch != 0U && cursor == bytes.size();
}

std::vector<std::uint8_t> encode_input_repair_request(
    const InputRepairRequestPayload& request) {
    if (request.epoch == 0U || request.player_slot >= kMaximumPlayers) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(9U);
    Put32(out, request.epoch);
    out.push_back(request.player_slot);
    Put32(out, request.first_missing_frame);
    return out;
}

bool decode_input_repair_request(
    std::span<const std::uint8_t> bytes,
    InputRepairRequestPayload& request) {
    request = {};
    std::size_t cursor = 0U;
    return bytes.size() == 9U && Take32(bytes, cursor, request.epoch) &&
           cursor < bytes.size() &&
           (request.player_slot = bytes[cursor++]) < kMaximumPlayers &&
           Take32(bytes, cursor, request.first_missing_frame) &&
           request.epoch != 0U && cursor == bytes.size();
}

std::vector<std::uint8_t> encode_frame_commit_request(
    const FrameCommitRequestPayload& request) {
    if (request.epoch == 0U || request.player_slot >= kMaximumPlayers) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(9U);
    Put32(out, request.epoch);
    out.push_back(request.player_slot);
    Put32(out, request.first_missing_frame);
    return out;
}

bool decode_frame_commit_request(
    std::span<const std::uint8_t> bytes,
    FrameCommitRequestPayload& request) {
    request = {};
    std::size_t cursor = 0U;
    return bytes.size() == 9U && Take32(bytes, cursor, request.epoch) &&
           cursor < bytes.size() &&
           (request.player_slot = bytes[cursor++]) < kMaximumPlayers &&
           Take32(bytes, cursor, request.first_missing_frame) &&
           request.epoch != 0U && cursor == bytes.size();
}

std::vector<std::uint8_t> encode_gameplay_handoff(
    const GameplayHandoffPayload& handoff) {
    if (handoff.current_input_epoch == 0U ||
        handoff.player_slot >= kMaximumPlayers ||
        handoff.stage > GameplayHandoffStage::Suspend ||
        (handoff.stage == GameplayHandoffStage::Request &&
         handoff.resume_input_epoch != 0U) ||
        (handoff.stage == GameplayHandoffStage::Suspend &&
         handoff.resume_input_epoch == 0U)) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(18U);
    Put32(out, handoff.current_input_epoch);
    Put32(out, handoff.resume_input_epoch);
    Put32(out, handoff.boundary_frame);
    Put32(out, handoff.map);
    out.push_back(handoff.player_slot);
    out.push_back(static_cast<std::uint8_t>(handoff.stage));
    return out;
}

bool decode_gameplay_handoff(std::span<const std::uint8_t> bytes,
                             GameplayHandoffPayload& handoff) {
    handoff = {};
    std::size_t cursor = 0U;
    std::uint8_t stage = 0U;
    if (bytes.size() != 18U ||
        !Take32(bytes, cursor, handoff.current_input_epoch) ||
        !Take32(bytes, cursor, handoff.resume_input_epoch) ||
        !Take32(bytes, cursor, handoff.boundary_frame) ||
        !Take32(bytes, cursor, handoff.map) || cursor >= bytes.size() ||
        (handoff.player_slot = bytes[cursor++]) >= kMaximumPlayers ||
        cursor >= bytes.size() ||
        (stage = bytes[cursor++]) >
            static_cast<std::uint8_t>(GameplayHandoffStage::Suspend) ||
        handoff.current_input_epoch == 0U || cursor != bytes.size()) {
        return false;
    }
    handoff.stage = static_cast<GameplayHandoffStage>(stage);
    return (handoff.stage == GameplayHandoffStage::Request &&
            handoff.resume_input_epoch == 0U) ||
           (handoff.stage == GameplayHandoffStage::Suspend &&
            handoff.resume_input_epoch != 0U);
}

std::vector<std::uint8_t> encode_rollback_payload(
    const RollbackPayload& payload) {
    constexpr std::size_t kHeaderBytes = 10U;
    if (payload.scene_epoch == 0U || payload.transport_epoch == 0U ||
        payload.source_slot >= kMaximumPlayers ||
        payload.target_slot >= kMaximumPlayers || payload.bytes.empty() ||
        payload.bytes.size() + kHeaderBytes > kMaximumDatagramBytes - 64U) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderBytes + payload.bytes.size());
    Put32(out, payload.scene_epoch);
    Put32(out, payload.transport_epoch);
    out.push_back(payload.source_slot);
    out.push_back(payload.target_slot);
    out.insert(out.end(), payload.bytes.begin(), payload.bytes.end());
    return out;
}

bool decode_rollback_payload(std::span<const std::uint8_t> bytes,
                             RollbackPayload& payload,
                             std::string& error) {
    payload = {};
    constexpr std::size_t kHeaderBytes = 10U;
    std::size_t cursor = 0U;
    if (bytes.size() <= kHeaderBytes ||
        bytes.size() > kMaximumDatagramBytes - 64U ||
        !Take32(bytes, cursor, payload.scene_epoch) ||
        !Take32(bytes, cursor, payload.transport_epoch) ||
        payload.scene_epoch == 0U || payload.transport_epoch == 0U ||
        bytes[cursor] >= kMaximumPlayers ||
        bytes[cursor + 1U] >= kMaximumPlayers) {
        error = "Rollback transport payload is invalid.";
        return false;
    }
    payload.source_slot = bytes[cursor++];
    payload.target_slot = bytes[cursor++];
    payload.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                         bytes.end());
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_gameplay_barrier(
    const GameplayBarrierPayload& barrier) {
    if (barrier.scene_epoch == 0U ||
        barrier.racer_count > 10U ||
        barrier.player_slot >= kMaximumPlayers ||
        barrier.stage > GameplayBarrierStage::Go) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(14U);
    Put32(out, barrier.scene_epoch);
    Put32(out, barrier.map);
    Put32(out, barrier.racer_count);
    out.push_back(barrier.player_slot);
    out.push_back(static_cast<std::uint8_t>(barrier.stage));
    return out;
}

bool decode_gameplay_barrier(std::span<const std::uint8_t> bytes,
                             GameplayBarrierPayload& barrier) {
    barrier = {};
    std::size_t cursor = 0U;
    std::uint8_t stage = 0U;
    return bytes.size() == 14U &&
           Take32(bytes, cursor, barrier.scene_epoch) &&
           Take32(bytes, cursor, barrier.map) &&
           Take32(bytes, cursor, barrier.racer_count) &&
           cursor < bytes.size() &&
           (barrier.player_slot = bytes[cursor++]) < kMaximumPlayers &&
           cursor < bytes.size() &&
           (stage = bytes[cursor++]) <=
               static_cast<std::uint8_t>(GameplayBarrierStage::Go) &&
           (barrier.stage = static_cast<GameplayBarrierStage>(stage), true) &&
           barrier.scene_epoch != 0U &&
           barrier.racer_count <= 10U && cursor == bytes.size();
}

std::uint64_t frame_commit_hash(std::uint64_t match_id,
                                const FrameCommitPayload& commit) {
    // FNV-1a is used only as an ordering/checkpoint fingerprint inside an
    // already authenticated packet.  Monocypher provides authenticity; this
    // hash makes missing, reordered or accidentally mutated commits explicit.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&hash](std::uint64_t value, unsigned bytes) {
        for (unsigned index = 0U; index < bytes; ++index) {
            hash ^= static_cast<std::uint8_t>(value >> (index * 8U));
            hash *= 1099511628211ULL;
        }
    };
    add(match_id, 8U);
    add(commit.epoch, 4U);
    add(commit.frame, 4U);
    add(commit.revision, 2U);
    add(commit.occupied_mask, 1U);
    add(commit.predicted_mask, 1U);
    for (const PackedInput input : commit.inputs) {
        add(input.buttons, 2U);
        add(static_cast<std::uint8_t>(input.stick_x), 1U);
        add(static_cast<std::uint8_t>(input.stick_y), 1U);
    }
    add(commit.previous_hash, 8U);
    return hash != 0U ? hash : 1U;
}

std::vector<std::uint8_t> encode_frame_commit_batch(
    const FrameCommitBatch& batch) {
    if (batch.commits.empty() || batch.commits.size() > 16U) return {};
    if (batch.correction &&
        (batch.generation == 0U ||
         batch.correction_first > batch.correction_last)) return {};
    std::vector<std::uint8_t> out;
    out.reserve(14U + batch.commits.size() * 44U);
    out.push_back(static_cast<std::uint8_t>(batch.commits.size()));
    out.push_back(batch.correction ? 1U : 0U);
    Put32(out, batch.generation);
    Put32(out, batch.correction_first);
    Put32(out, batch.correction_last);
    for (const FrameCommitPayload& commit : batch.commits) {
        if (commit.epoch == 0U || commit.occupied_mask == 0U ||
            commit.commit_hash == 0U) return {};
        Put32(out, commit.epoch);
        Put32(out, commit.frame);
        Put16(out, commit.revision);
        out.push_back(commit.occupied_mask);
        out.push_back(commit.predicted_mask);
        for (const PackedInput input : commit.inputs) {
            Put16(out, input.buttons);
            out.push_back(static_cast<std::uint8_t>(input.stick_x));
            out.push_back(static_cast<std::uint8_t>(input.stick_y));
        }
        Put64(out, commit.previous_hash);
        Put64(out, commit.commit_hash);
    }
    return out;
}

bool decode_frame_commit_batch(std::span<const std::uint8_t> bytes,
                               FrameCommitBatch& batch,
                               std::string& error) {
    batch = {};
    if (bytes.size() < 14U || bytes[0] == 0U || bytes[0] > 16U ||
        bytes[1] > 1U ||
        bytes.size() != 14U + static_cast<std::size_t>(bytes[0]) * 44U) {
        error = "Frame commit batch metadata is invalid.";
        return false;
    }
    std::size_t cursor = 2U;
    if (!Take32(bytes, cursor, batch.generation) ||
        !Take32(bytes, cursor, batch.correction_first) ||
        !Take32(bytes, cursor, batch.correction_last)) {
        error = "Frame commit batch metadata is truncated.";
        return false;
    }
    batch.correction = bytes[1] != 0U;
    if (batch.correction &&
        (batch.generation == 0U ||
         batch.correction_first > batch.correction_last)) {
        error = "Frame correction range is invalid.";
        return false;
    }
    batch.commits.reserve(bytes[0]);
    for (std::uint8_t index = 0U; index < bytes[0]; ++index) {
        FrameCommitPayload commit{};
        if (!Take32(bytes, cursor, commit.epoch) ||
            !Take32(bytes, cursor, commit.frame) ||
            !Take16(bytes, cursor, commit.revision) ||
            cursor + 2U > bytes.size()) {
            error = "Frame commit batch is truncated.";
            return false;
        }
        commit.occupied_mask = bytes[cursor++];
        commit.predicted_mask = bytes[cursor++];
        for (PackedInput& input : commit.inputs) {
            if (!Take16(bytes, cursor, input.buttons) ||
                cursor + 2U > bytes.size()) {
                error = "Frame commit inputs are truncated.";
                return false;
            }
            input.stick_x = static_cast<std::int8_t>(bytes[cursor++]);
            input.stick_y = static_cast<std::int8_t>(bytes[cursor++]);
        }
        if (!Take64(bytes, cursor, commit.previous_hash) ||
            !Take64(bytes, cursor, commit.commit_hash) ||
            commit.epoch == 0U || commit.occupied_mask == 0U ||
            (commit.predicted_mask & ~commit.occupied_mask) != 0U ||
            commit.commit_hash == 0U) {
            error = "Frame commit chain is invalid.";
            return false;
        }
        batch.commits.push_back(commit);
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_frame_correction_acknowledge(
    const FrameCorrectionAcknowledgePayload& acknowledgement) {
    if (acknowledgement.epoch == 0U || acknowledgement.generation == 0U ||
        acknowledgement.player_slot >= kMaximumPlayers) return {};
    std::vector<std::uint8_t> out;
    out.reserve(9U);
    Put32(out, acknowledgement.epoch);
    Put32(out, acknowledgement.generation);
    out.push_back(acknowledgement.player_slot);
    return out;
}

bool decode_frame_correction_acknowledge(
    std::span<const std::uint8_t> bytes,
    FrameCorrectionAcknowledgePayload& acknowledgement) {
    acknowledgement = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 9U ||
        !Take32(bytes, cursor, acknowledgement.epoch) ||
        !Take32(bytes, cursor, acknowledgement.generation) ||
        cursor >= bytes.size()) return false;
    acknowledgement.player_slot = bytes[cursor];
    return acknowledgement.epoch != 0U &&
           acknowledgement.generation != 0U &&
           acknowledgement.player_slot < kMaximumPlayers;
}

std::vector<std::uint8_t> encode_state_snapshot_chunk(
    const StateSnapshotChunk& chunk, std::size_t maximum_datagram_bytes) {
    constexpr std::size_t header_bytes = 24U;
    if (maximum_datagram_bytes < header_bytes + 32U ||
        maximum_datagram_bytes > kMaximumQuickJoinDatagramBytes ||
        chunk.scene_epoch == 0U || chunk.checksum == 0U ||
        chunk.total_size == 0U ||
        chunk.chunk_count == 0U || chunk.chunk_index >= chunk.chunk_count ||
        chunk.bytes.empty() ||
        chunk.bytes.size() + header_bytes > maximum_datagram_bytes - 32U) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(header_bytes + chunk.bytes.size());
    Put32(out, chunk.scene_epoch);
    Put32(out, chunk.frame);
    Put64(out, chunk.checksum);
    Put32(out, chunk.total_size);
    Put16(out, chunk.chunk_index);
    Put16(out, chunk.chunk_count);
    out.insert(out.end(), chunk.bytes.begin(), chunk.bytes.end());
    return out;
}

bool decode_state_snapshot_chunk(std::span<const std::uint8_t> bytes,
                                 StateSnapshotChunk& chunk,
                                 std::string& error) {
    chunk = {};
    std::size_t cursor = 0U;
    if (bytes.size() <= 24U ||
        !Take32(bytes, cursor, chunk.scene_epoch) ||
        !Take32(bytes, cursor, chunk.frame) ||
        !Take64(bytes, cursor, chunk.checksum) ||
        !Take32(bytes, cursor, chunk.total_size) ||
        !Take16(bytes, cursor, chunk.chunk_index) ||
        !Take16(bytes, cursor, chunk.chunk_count) ||
        chunk.scene_epoch == 0U || chunk.checksum == 0U ||
        chunk.total_size == 0U || chunk.total_size > 14336U ||
        chunk.chunk_count == 0U || chunk.chunk_count > 16U ||
        chunk.chunk_index >= chunk.chunk_count) {
        error = "Authoritative snapshot chunk metadata is invalid.";
        return false;
    }
    chunk.bytes.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.end());
    if (chunk.bytes.empty()) {
        error = "Authoritative snapshot chunk is empty.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_state_request(
    const StateRequestPayload& request) {
    std::vector<std::uint8_t> out;
    if (request.scene_epoch == 0U || request.chunk_count == 0U ||
        request.chunk_count > 16U || request.missing_chunks == 0U) {
        return {};
    }
    Put32(out, request.scene_epoch);
    Put32(out, request.frame);
    Put16(out, request.chunk_count);
    Put16(out, request.missing_chunks);
    return out;
}

bool decode_state_request(std::span<const std::uint8_t> bytes,
                          StateRequestPayload& request) {
    request = {};
    std::size_t cursor = 0U;
    return bytes.size() == 12U &&
           Take32(bytes, cursor, request.scene_epoch) &&
           Take32(bytes, cursor, request.frame) &&
           Take16(bytes, cursor, request.chunk_count) &&
           Take16(bytes, cursor, request.missing_chunks) &&
           request.scene_epoch != 0U && request.chunk_count > 0U &&
           request.chunk_count <= 16U && request.missing_chunks != 0U &&
           cursor == bytes.size();
}

std::vector<std::uint8_t> encode_state_acknowledge(
    const StateAcknowledgePayload& acknowledgement) {
    if (acknowledgement.scene_epoch == 0U ||
        acknowledgement.checksum == 0U ||
        acknowledgement.player_slot >= kMaximumPlayers) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(17U);
    Put32(out, acknowledgement.scene_epoch);
    Put32(out, acknowledgement.frame);
    Put64(out, acknowledgement.checksum);
    out.push_back(acknowledgement.player_slot);
    return out;
}

bool decode_state_acknowledge(
    std::span<const std::uint8_t> bytes,
    StateAcknowledgePayload& acknowledgement) {
    acknowledgement = {};
    std::size_t cursor = 0U;
    return bytes.size() == 17U &&
           Take32(bytes, cursor, acknowledgement.scene_epoch) &&
           Take32(bytes, cursor, acknowledgement.frame) &&
           Take64(bytes, cursor, acknowledgement.checksum) &&
           cursor < bytes.size() &&
           (acknowledgement.player_slot = bytes[cursor++]) <
               kMaximumPlayers &&
           acknowledgement.scene_epoch != 0U &&
           acknowledgement.checksum != 0U && cursor == bytes.size();
}

std::vector<std::uint8_t> encode_recovery(
    const RecoveryPayload& recovery) {
    if (recovery.scene_epoch == 0U || recovery.player_slot >= kMaximumPlayers) {
        return {};
    }
    std::vector<std::uint8_t> out;
    Put32(out, recovery.scene_epoch);
    Put32(out, recovery.frame);
    out.push_back(recovery.player_slot);
    return out;
}

bool decode_recovery(std::span<const std::uint8_t> bytes,
                     RecoveryPayload& recovery) {
    recovery = {};
    std::size_t cursor = 0U;
    return bytes.size() == 9U &&
           Take32(bytes, cursor, recovery.scene_epoch) &&
           Take32(bytes, cursor, recovery.frame) &&
           cursor < bytes.size() &&
           (recovery.player_slot = bytes[cursor++]) < kMaximumPlayers &&
           recovery.scene_epoch != 0U && cursor == bytes.size();
}

std::vector<std::uint8_t> encode_transition_barrier(
    const TransitionBarrierPayload& transition) {
    if (transition.scene_epoch == 0U ||
        transition.transition_kind == 0U ||
        transition.player_slot >= kMaximumPlayers ||
        transition.stage > TransitionBarrierStage::Resume) {
        return {};
    }
    std::vector<std::uint8_t> out;
    Put32(out, transition.scene_epoch);
    Put32(out, transition.frame);
    out.push_back(transition.transition_kind);
    out.push_back(transition.player_slot);
    out.push_back(static_cast<std::uint8_t>(transition.stage));
    return out;
}

bool decode_transition_barrier(std::span<const std::uint8_t> bytes,
                               TransitionBarrierPayload& transition) {
    transition = {};
    std::size_t cursor = 0U;
    std::uint8_t stage = 0U;
    return bytes.size() == 11U &&
           Take32(bytes, cursor, transition.scene_epoch) &&
           Take32(bytes, cursor, transition.frame) &&
           cursor < bytes.size() &&
           (transition.transition_kind = bytes[cursor++]) != 0U &&
           cursor < bytes.size() &&
           (transition.player_slot = bytes[cursor++]) < kMaximumPlayers &&
           cursor < bytes.size() &&
           (stage = bytes[cursor++]) <=
               static_cast<std::uint8_t>(TransitionBarrierStage::Resume) &&
           (transition.stage = static_cast<TransitionBarrierStage>(stage), true) &&
           transition.scene_epoch != 0U && cursor == bytes.size();
}

std::vector<std::uint8_t> encode_simulation_progress(
    const SimulationProgressPayload& progress) {
    if (progress.input_epoch == 0U ||
        progress.player_slot >= kMaximumPlayers) {
        return {};
    }
    std::vector<std::uint8_t> out;
    Put32(out, progress.scene_epoch);
    Put32(out, progress.input_epoch);
    Put32(out, progress.completed_frame);
    out.push_back(progress.player_slot);
    return out;
}

bool decode_simulation_progress(std::span<const std::uint8_t> bytes,
                                SimulationProgressPayload& progress) {
    progress = {};
    std::size_t cursor = 0U;
    return bytes.size() == 13U &&
           Take32(bytes, cursor, progress.scene_epoch) &&
           Take32(bytes, cursor, progress.input_epoch) &&
           Take32(bytes, cursor, progress.completed_frame) &&
           cursor < bytes.size() &&
           (progress.player_slot = bytes[cursor++]) < kMaximumPlayers &&
           progress.input_epoch != 0U &&
           cursor == bytes.size();
}

std::vector<std::uint8_t> encode_racer_orientation(
    const RacerOrientationPayload& orientation) {
    if (orientation.scene_epoch == 0U || orientation.state.empty() ||
        orientation.state.size() > kMaximumRacerOrientationBytes) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(10U + orientation.state.size());
    Put32(out, orientation.scene_epoch);
    Put32(out, orientation.frame);
    Put16(out, static_cast<std::uint16_t>(orientation.state.size()));
    out.insert(out.end(), orientation.state.begin(), orientation.state.end());
    return out;
}

bool decode_racer_orientation(std::span<const std::uint8_t> bytes,
                              RacerOrientationPayload& orientation) {
    orientation = {};
    std::size_t cursor = 0U;
    std::uint16_t state_size = 0U;
    if (bytes.size() < 11U ||
        !Take32(bytes, cursor, orientation.scene_epoch) ||
        !Take32(bytes, cursor, orientation.frame) ||
        !Take16(bytes, cursor, state_size) ||
        orientation.scene_epoch == 0U || state_size == 0U ||
        state_size > kMaximumRacerOrientationBytes ||
        cursor + state_size != bytes.size()) {
        return false;
    }
    orientation.state.assign(bytes.begin() +
                                 static_cast<std::ptrdiff_t>(cursor),
                             bytes.end());
    return true;
}

std::vector<std::uint8_t> encode_hello(const HelloPayload& payload) {
    std::vector<std::uint8_t> out;
    if (!PutString(out, payload.display_name, kMaximumPlayerNameBytes) ||
        !PutManifest(out, payload.manifest)) return {};
    out.insert(out.end(), payload.friend_admission.begin(),
               payload.friend_admission.end());
    return out;
}

bool decode_hello(std::span<const std::uint8_t> bytes, HelloPayload& payload,
                  std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (!TakeString(bytes, cursor, payload.display_name, kMaximumPlayerNameBytes) ||
        !TakeManifest(bytes, cursor, payload.manifest) ||
        cursor + payload.friend_admission.size() != bytes.size() ||
        !valid_display_name(payload.display_name)) {
        error = "Hello payload is invalid.";
        return false;
    }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                payload.friend_admission.size(),
                payload.friend_admission.begin());
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_hello_ack(const HelloAckPayload& payload) {
    if (payload.player_slot >= kMaximumPlayers ||
        (!payload.synchronized_save.empty() &&
         payload.synchronized_save.size() != 512U) ||
        (payload.accepted &&
         (payload.synchronized_save.size() != 512U ||
          payload.online_save_generation == 0U ||
          payload.online_save_hash == 0U))) return {};
    std::vector<std::uint8_t> out{
        static_cast<std::uint8_t>(payload.accepted ? 1U : 0U), payload.player_slot};
    if (!PutString(out, payload.message, 160U)) return {};
    Put16(out, static_cast<std::uint16_t>(payload.synchronized_save.size()));
    out.insert(out.end(), payload.synchronized_save.begin(),
               payload.synchronized_save.end());
    out.push_back(payload.compatibility_offer.has_value() ? 1U : 0U);
    if (payload.compatibility_offer &&
        !PutManifest(out, *payload.compatibility_offer)) {
        return {};
    }
    Put32(out, payload.online_save_generation);
    Put64(out, payload.online_save_hash);
    return out;
}

bool decode_hello_ack(std::span<const std::uint8_t> bytes,
                      HelloAckPayload& payload, std::string& error) {
    payload = {};
    if (bytes.size() < 6U || bytes[0] > 1U || bytes[1] >= kMaximumPlayers) {
        error = "Hello acknowledgement is invalid.";
        return false;
    }
    payload.accepted = bytes[0] != 0U;
    payload.player_slot = bytes[1];
    std::size_t cursor = 2U;
    std::uint16_t save_size = 0U;
    if (!TakeString(bytes, cursor, payload.message, 160U) ||
        !Take16(bytes, cursor, save_size) ||
        (save_size != 0U && save_size != 512U) ||
        cursor + save_size >= bytes.size()) {
        error = "Hello acknowledgement is truncated.";
        return false;
    }
    payload.synchronized_save.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
        bytes.begin() + static_cast<std::ptrdiff_t>(cursor + save_size));
    cursor += save_size;
    if (bytes[cursor] > 1U) {
        error = "Hello acknowledgement compatibility offer is invalid.";
        return false;
    }
    const bool has_offer = bytes[cursor++] != 0U;
    if (has_offer) {
        CompatibilityManifest offer{};
        if (!TakeManifest(bytes, cursor, offer)) {
            error = "Hello acknowledgement compatibility offer is truncated.";
            return false;
        }
        payload.compatibility_offer = std::move(offer);
    }
    if (!Take32(bytes, cursor, payload.online_save_generation) ||
        !Take64(bytes, cursor, payload.online_save_hash) ||
        cursor != bytes.size() ||
        (payload.accepted &&
         (payload.synchronized_save.size() != 512U ||
          payload.online_save_generation == 0U ||
          payload.online_save_hash == 0U))) {
        error = "Hello acknowledgement has invalid online save metadata.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_online_save_ready(
    const OnlineSaveReadyPayload& payload) {
    if (payload.player_slot >= kMaximumPlayers || payload.generation == 0U ||
        payload.hash == 0U) {
        return {};
    }
    std::vector<std::uint8_t> out{payload.player_slot};
    Put32(out, payload.generation);
    Put64(out, payload.hash);
    return out;
}

bool decode_online_save_ready(std::span<const std::uint8_t> bytes,
                              OnlineSaveReadyPayload& payload,
                              std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 13U || bytes[0] >= kMaximumPlayers) {
        error = "Online save readiness payload is invalid.";
        return false;
    }
    payload.player_slot = bytes[cursor++];
    if (!Take32(bytes, cursor, payload.generation) ||
        !Take64(bytes, cursor, payload.hash) || payload.generation == 0U ||
        payload.hash == 0U || cursor != bytes.size()) {
        error = "Online save readiness identity is invalid.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_online_save_status(const OnlineSaveStatusPayload& payload) {
    if (!payload.room_generation || !payload.save_generation || !payload.save_hash ||
        !(payload.verified_mask & 1U) || (payload.verified_mask >> kMaximumPlayers)) return {};
    std::vector<std::uint8_t> out;
    Put64(out, payload.room_generation);
    Put32(out, payload.save_generation);
    Put64(out, payload.save_hash);
    out.push_back(payload.verified_mask);
    return out;
}

bool decode_online_save_status(std::span<const std::uint8_t> bytes,
                               OnlineSaveStatusPayload& payload, std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 21U || !Take64(bytes, cursor, payload.room_generation) ||
        !Take32(bytes, cursor, payload.save_generation) || !Take64(bytes, cursor, payload.save_hash)) {
        error = "Online save status payload is invalid.";
        return false;
    }
    payload.verified_mask = bytes[cursor];
    if (encode_online_save_status(payload).empty()) {
        error = "Online save status identity is invalid.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_lobby_state(const LobbyStatePayload& payload) {
    std::vector<std::uint8_t> out;
    Put64(out, payload.generation);
    out.push_back(static_cast<std::uint8_t>(payload.phase));
    if (!PutString(out, payload.room_name, kMaximumLobbyNameBytes) ||
        !valid_room_name(payload.room_name) || !valid_rules(payload.rules) ||
        payload.visibility > Visibility::Lan) return {};
    out.push_back(static_cast<std::uint8_t>(payload.visibility));
    out.push_back(static_cast<std::uint8_t>(payload.rules.host_control));
    out.push_back(payload.rules.maximum_players);
    std::uint8_t rule_flags = payload.rules.automatic_input_delay ? 1U : 0U;
    rule_flags |= payload.rules.relay_only ? 2U : 0U;
    rule_flags |= payload.rules.record_replay ? 4U : 0U;
    out.push_back(rule_flags);
    out.push_back(payload.rules.manual_input_delay);
    out.push_back(payload.rules.rollback_window);
    out.push_back(static_cast<std::uint8_t>(payload.rules.synchronization));
    if (payload.input_delay_frames > 9U) return {};
    out.push_back(payload.input_delay_frames);
    out.push_back(payload.countdown_active ? 1U : 0U);
    Put32(out, payload.countdown_remaining_ms);
    Put32(out, payload.countdown_generation);
    for (std::size_t index = 0; index < payload.players.size(); ++index) {
        const auto& player = payload.players[index];
        if (player.slot != index ||
            (player.occupied && !valid_display_name(player.display_name)) ||
            player.route > Route::Relay) return {};
        std::uint8_t flags = player.occupied ? 1U : 0U;
        flags |= player.ready ? 2U : 0U;
        flags |= player.loaded ? 4U : 0U;
        out.push_back(flags);
        if (!PutString(out, player.display_name, kMaximumPlayerNameBytes)) return {};
        out.push_back(static_cast<std::uint8_t>(player.route));
        Put16(out, player.ping_ms);
        Put16(out, player.jitter_ms);
        Put16(out, static_cast<std::uint16_t>(std::lround(
            std::clamp(player.packet_loss_percent, 0.0F, 100.0F) * 100.0F)));
    }
    return out;
}

bool decode_lobby_state(std::span<const std::uint8_t> bytes,
                        LobbyStatePayload& payload, std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (!Take64(bytes, cursor, payload.generation) || cursor >= bytes.size() ||
        bytes[cursor] > static_cast<std::uint8_t>(RoomPhase::Finished)) {
        error = "Lobby state header is invalid.";
        return false;
    }
    payload.phase = static_cast<RoomPhase>(bytes[cursor++]);
    if (!TakeString(bytes, cursor, payload.room_name, kMaximumLobbyNameBytes) ||
        !valid_room_name(payload.room_name) || cursor + 17U > bytes.size()) {
        error = "Lobby metadata is invalid.";
        return false;
    }
    const std::uint8_t visibility = bytes[cursor++];
    const std::uint8_t host_control = bytes[cursor++];
    payload.rules.maximum_players = bytes[cursor++];
    const std::uint8_t rule_flags = bytes[cursor++];
    payload.rules.manual_input_delay = bytes[cursor++];
    payload.rules.rollback_window = bytes[cursor++];
    const std::uint8_t synchronization = bytes[cursor++];
    payload.input_delay_frames = bytes[cursor++];
    const std::uint8_t countdown_active = bytes[cursor++];
    if (!Take32(bytes, cursor, payload.countdown_remaining_ms) ||
        !Take32(bytes, cursor, payload.countdown_generation)) {
        error = "Lobby countdown state is truncated.";
        return false;
    }
    if (visibility > static_cast<std::uint8_t>(Visibility::Lan) ||
        host_control > static_cast<std::uint8_t>(HostControlPolicy::EveryAssignedPort) ||
        (rule_flags & ~7U) != 0U ||
        !valid_synchronization_mode(static_cast<SynchronizationMode>(synchronization)) ||
        payload.input_delay_frames > 9U || countdown_active > 1U ||
        payload.countdown_remaining_ms > 5000U) {
        error = "Lobby rules are invalid.";
        return false;
    }
    payload.visibility = static_cast<Visibility>(visibility);
    payload.rules.host_control = static_cast<HostControlPolicy>(host_control);
    payload.rules.automatic_input_delay = (rule_flags & 1U) != 0U;
    payload.rules.relay_only = (rule_flags & 2U) != 0U;
    payload.rules.record_replay = (rule_flags & 4U) != 0U;
    payload.rules.synchronization =
        static_cast<SynchronizationMode>(synchronization);
    payload.countdown_active = countdown_active != 0U;
    if (!valid_rules(payload.rules)) {
        error = "Lobby rules are outside the supported limits.";
        return false;
    }
    for (std::size_t index = 0; index < payload.players.size(); ++index) {
        if (cursor >= bytes.size()) {
            error = "Lobby player list is truncated.";
            return false;
        }
        const std::uint8_t flags = bytes[cursor++];
        if ((flags & ~7U) != 0U) {
            error = "Lobby player flags are invalid.";
            return false;
        }
        auto& player = payload.players[index];
        player.slot = static_cast<std::uint8_t>(index);
        player.occupied = (flags & 1U) != 0U;
        player.ready = (flags & 2U) != 0U;
        player.loaded = (flags & 4U) != 0U;
        if (!TakeString(bytes, cursor, player.display_name,
                        kMaximumPlayerNameBytes) ||
            (player.occupied && !valid_display_name(player.display_name))) {
            error = "Lobby player name is invalid.";
            return false;
        }
        if (cursor >= bytes.size() ||
            bytes[cursor] > static_cast<std::uint8_t>(Route::Relay)) {
            error = "Lobby player route is invalid.";
            return false;
        }
        player.route = static_cast<Route>(bytes[cursor++]);
        std::uint16_t loss_hundredths = 0U;
        if (!Take16(bytes, cursor, player.ping_ms) ||
            !Take16(bytes, cursor, player.jitter_ms) ||
            !Take16(bytes, cursor, loss_hundredths) || loss_hundredths > 10000U) {
            error = "Lobby player network metrics are invalid.";
            return false;
        }
        player.packet_loss_percent = static_cast<float>(loss_hundredths) / 100.0F;
    }
    if (cursor != bytes.size()) {
        error = "Lobby state has trailing bytes.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_state_hash(const StateHashPayload& payload) {
    if (payload.player_slot >= kMaximumPlayers ||
        payload.racer_count > payload.racer_hashes.size()) return {};
    std::vector<std::uint8_t> out;
    Put32(out, payload.scene_epoch);
    out.push_back(payload.player_slot);
    Put64(out, payload.hash);
    Put64(out, payload.globals_hash);
    Put64(out, payload.roster_hash);
    Put64(out, payload.racers_hash);
    Put32(out, payload.racer_count);
    for (const std::uint64_t racer_hash : payload.racer_hashes) {
        Put64(out, racer_hash);
    }
    return out;
}

bool decode_state_hash(std::span<const std::uint8_t> bytes,
                       StateHashPayload& payload, std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    constexpr std::size_t kPayloadSize = 37U + 4U + 10U * 8U;
    if (bytes.size() != kPayloadSize ||
        !Take32(bytes, cursor, payload.scene_epoch) ||
        cursor >= bytes.size() || bytes[cursor] >= kMaximumPlayers) {
        error = "State hash payload is invalid.";
        return false;
    }
    payload.player_slot = bytes[cursor++];
    if (!Take64(bytes, cursor, payload.hash) ||
        !Take64(bytes, cursor, payload.globals_hash) ||
        !Take64(bytes, cursor, payload.roster_hash) ||
        !Take64(bytes, cursor, payload.racers_hash) ||
        !Take32(bytes, cursor, payload.racer_count) ||
        payload.racer_count > payload.racer_hashes.size()) {
        error = "State hash payload is truncated.";
        return false;
    }
    for (std::uint64_t& racer_hash : payload.racer_hashes) {
        if (!Take64(bytes, cursor, racer_hash)) {
            error = "State hash racer details are truncated.";
            return false;
        }
    }
    if (cursor != bytes.size()) {
        error = "State hash payload contains trailing data.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_loaded(const LoadedPayload& payload) {
    if (payload.player_slot >= kMaximumPlayers ||
        payload.bootstrap_hash == 0U ||
        payload.online_save_generation == 0U ||
        payload.online_save_hash == 0U) {
        return {};
    }
    std::vector<std::uint8_t> out{payload.player_slot};
    Put64(out, payload.bootstrap_hash);
    Put32(out, payload.online_save_generation);
    Put64(out, payload.online_save_hash);
    return out;
}

bool decode_loaded(std::span<const std::uint8_t> bytes,
                   LoadedPayload& payload, std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 21U || bytes[0] >= kMaximumPlayers) {
        error = "Loaded checkpoint payload is invalid.";
        return false;
    }
    payload.player_slot = bytes[cursor++];
    if (!Take64(bytes, cursor, payload.bootstrap_hash) ||
        !Take32(bytes, cursor, payload.online_save_generation) ||
        !Take64(bytes, cursor, payload.online_save_hash) ||
        payload.bootstrap_hash == 0U ||
        payload.online_save_generation == 0U ||
        payload.online_save_hash == 0U || cursor != bytes.size()) {
        error = "Loaded checkpoint hash is invalid.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_start(const StartPayload& payload) {
    if (payload.stage > 1U || !valid_launch_descriptor(payload.descriptor) ||
        payload.descriptor_hash != launch_descriptor_hash(payload.descriptor)) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(39U);
    out.push_back(payload.stage);
    Put64(out, payload.descriptor.match_id);
    Put64(out, payload.descriptor.lobby_generation);
    Put64(out, payload.descriptor.compatibility_hash);
    out.push_back(payload.descriptor.occupied_mask);
    out.push_back(payload.descriptor.player_count);
    out.push_back(payload.descriptor.input_delay_frames);
    out.push_back(payload.descriptor.rollback_window);
    out.push_back(static_cast<std::uint8_t>(
        payload.descriptor.synchronization));
    out.push_back(static_cast<std::uint8_t>(payload.descriptor.host_control));
    Put64(out, payload.descriptor_hash);
    return out;
}

bool decode_start(std::span<const std::uint8_t> bytes, StartPayload& payload,
                  std::string& error) {
    payload = {};
    if (bytes.size() != 39U || bytes[0] > 1U) {
        error = "Start descriptor is truncated.";
        return false;
    }
    std::size_t cursor = 0U;
    payload.stage = bytes[cursor++];
    if (!Take64(bytes, cursor, payload.descriptor.match_id) ||
        !Take64(bytes, cursor, payload.descriptor.lobby_generation) ||
        !Take64(bytes, cursor, payload.descriptor.compatibility_hash)) {
        error = "Start descriptor identity is invalid.";
        return false;
    }
    payload.descriptor.occupied_mask = bytes[cursor++];
    payload.descriptor.player_count = bytes[cursor++];
    payload.descriptor.input_delay_frames = bytes[cursor++];
    payload.descriptor.rollback_window = bytes[cursor++];
    if (!valid_synchronization_mode(static_cast<SynchronizationMode>(bytes[cursor]))) {
        error = "Start descriptor synchronization mode is invalid.";
        return false;
    }
    payload.descriptor.synchronization =
        static_cast<SynchronizationMode>(bytes[cursor++]);
    if (bytes[cursor] > static_cast<std::uint8_t>(
                            HostControlPolicy::EveryAssignedPort)) {
        error = "Start descriptor host policy is invalid.";
        return false;
    }
    payload.descriptor.host_control =
        static_cast<HostControlPolicy>(bytes[cursor++]);
    if (!Take64(bytes, cursor, payload.descriptor_hash) ||
        cursor != bytes.size() ||
        !valid_launch_descriptor(payload.descriptor) ||
        payload.descriptor_hash != launch_descriptor_hash(payload.descriptor)) {
        error = "Start descriptor failed validation.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_ready_request(
    const ReadyRequestPayload& payload) {
    if (payload.request_id == 0U || payload.player_slot >= kMaximumPlayers) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(6U);
    Put32(out, payload.request_id);
    out.push_back(payload.player_slot);
    out.push_back(payload.ready ? 1U : 0U);
    return out;
}

bool decode_ready_request(std::span<const std::uint8_t> bytes,
                          ReadyRequestPayload& payload, std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    std::uint32_t request_id = 0U;
    if (bytes.size() != 6U || !Take32(bytes, cursor, request_id) ||
        request_id == 0U || bytes[cursor] >= kMaximumPlayers ||
        bytes[cursor + 1U] > 1U) {
        error = "Ready request payload is invalid.";
        return false;
    }
    payload.request_id = request_id;
    payload.player_slot = bytes[cursor++];
    payload.ready = bytes[cursor++] != 0U;
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_ready_ack(const ReadyAckPayload& payload) {
    if (payload.request_id == 0U || payload.player_slot >= kMaximumPlayers ||
        payload.lobby_generation == 0U) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(14U);
    Put32(out, payload.request_id);
    out.push_back(payload.player_slot);
    out.push_back(payload.ready ? 1U : 0U);
    Put64(out, payload.lobby_generation);
    return out;
}

bool decode_ready_ack(std::span<const std::uint8_t> bytes,
                      ReadyAckPayload& payload, std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 14U || !Take32(bytes, cursor, payload.request_id) ||
        payload.request_id == 0U || bytes[cursor] >= kMaximumPlayers ||
        bytes[cursor + 1U] > 1U) {
        error = "Ready acknowledgement payload is invalid.";
        return false;
    }
    payload.player_slot = bytes[cursor++];
    payload.ready = bytes[cursor++] != 0U;
    if (!Take64(bytes, cursor, payload.lobby_generation) ||
        payload.lobby_generation == 0U || cursor != bytes.size()) {
        error = "Ready acknowledgement generation is invalid.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_countdown_ack(
    const CountdownAckPayload& payload) {
    if (payload.countdown_generation == 0U ||
        payload.player_slot >= kMaximumPlayers) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(5U);
    Put32(out, payload.countdown_generation);
    out.push_back(payload.player_slot);
    return out;
}

bool decode_countdown_ack(std::span<const std::uint8_t> bytes,
                          CountdownAckPayload& payload, std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 5U ||
        !Take32(bytes, cursor, payload.countdown_generation) ||
        payload.countdown_generation == 0U ||
        bytes[cursor] >= kMaximumPlayers) {
        error = "Countdown acknowledgement payload is invalid.";
        return false;
    }
    payload.player_slot = bytes[cursor++];
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_launch_prepare(
    const LaunchPreparePayload& payload) {
    const std::vector<std::uint8_t> start = encode_start(payload.start);
    if (payload.launch_epoch == 0U || payload.lobby_generation == 0U ||
        payload.countdown_generation == 0U || start.empty()) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(55U);
    Put32(out, payload.launch_epoch);
    Put64(out, payload.lobby_generation);
    Put32(out, payload.countdown_generation);
    out.insert(out.end(), start.begin(), start.end());
    return out;
}

bool decode_launch_prepare(std::span<const std::uint8_t> bytes,
                           LaunchPreparePayload& payload, std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 55U ||
        !Take32(bytes, cursor, payload.launch_epoch) ||
        !Take64(bytes, cursor, payload.lobby_generation) ||
        !Take32(bytes, cursor, payload.countdown_generation) ||
        payload.launch_epoch == 0U || payload.lobby_generation == 0U ||
        payload.countdown_generation == 0U ||
        !decode_start(bytes.subspan(cursor), payload.start, error) ||
        payload.start.stage != 0U) {
        if (error.empty()) error = "Launch prepare payload is invalid.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_launch_prepare_ack(
    const LaunchPrepareAckPayload& payload) {
    if (payload.launch_epoch == 0U || payload.player_slot >= kMaximumPlayers) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(6U);
    Put32(out, payload.launch_epoch);
    out.push_back(payload.player_slot);
    out.push_back(payload.accepted ? 1U : 0U);
    return out;
}

bool decode_launch_prepare_ack(std::span<const std::uint8_t> bytes,
                               LaunchPrepareAckPayload& payload,
                               std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 6U || !Take32(bytes, cursor, payload.launch_epoch) ||
        payload.launch_epoch == 0U || bytes[cursor] >= kMaximumPlayers ||
        bytes[cursor + 1U] > 1U) {
        error = "Launch prepare acknowledgement payload is invalid.";
        return false;
    }
    payload.player_slot = bytes[cursor++];
    payload.accepted = bytes[cursor++] != 0U;
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_launch_commit(
    const LaunchCommitPayload& payload) {
    if (payload.launch_epoch == 0U) return {};
    std::vector<std::uint8_t> out;
    out.reserve(4U);
    Put32(out, payload.launch_epoch);
    return out;
}

bool decode_launch_commit(std::span<const std::uint8_t> bytes,
                          LaunchCommitPayload& payload, std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 4U || !Take32(bytes, cursor, payload.launch_epoch) ||
        payload.launch_epoch == 0U) {
        error = "Launch commit payload is invalid.";
        return false;
    }
    error.clear();
    return true;
}

namespace {

template <typename Payload>
std::vector<std::uint8_t> EncodeLaunchSlotAck(const Payload& payload) {
    if (payload.launch_epoch == 0U || payload.player_slot >= kMaximumPlayers) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(5U);
    Put32(out, payload.launch_epoch);
    out.push_back(payload.player_slot);
    return out;
}

template <typename Payload>
bool DecodeLaunchSlotAck(std::span<const std::uint8_t> bytes,
                         Payload& payload, std::string& error,
                         std::string_view label) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 5U || !Take32(bytes, cursor, payload.launch_epoch) ||
        payload.launch_epoch == 0U || bytes[cursor] >= kMaximumPlayers) {
        error = std::string(label) + " payload is invalid.";
        return false;
    }
    payload.player_slot = bytes[cursor];
    error.clear();
    return true;
}

} // namespace

std::vector<std::uint8_t> encode_launch_commit_ack(
    const LaunchCommitAckPayload& payload) {
    return EncodeLaunchSlotAck(payload);
}

bool decode_launch_commit_ack(std::span<const std::uint8_t> bytes,
                              LaunchCommitAckPayload& payload,
                              std::string& error) {
    return DecodeLaunchSlotAck(bytes, payload, error,
                               "Launch commit acknowledgement");
}

std::vector<std::uint8_t> encode_launch_release(
    const LaunchReleasePayload& payload) {
    return encode_launch_commit({payload.launch_epoch});
}

bool decode_launch_release(std::span<const std::uint8_t> bytes,
                           LaunchReleasePayload& payload, std::string& error) {
    LaunchCommitPayload commit{};
    if (!decode_launch_commit(bytes, commit, error)) return false;
    payload.launch_epoch = commit.launch_epoch;
    return true;
}

std::vector<std::uint8_t> encode_launch_release_ack(
    const LaunchReleaseAckPayload& payload) {
    return EncodeLaunchSlotAck(payload);
}

bool decode_launch_release_ack(std::span<const std::uint8_t> bytes,
                               LaunchReleaseAckPayload& payload,
                               std::string& error) {
    return DecodeLaunchSlotAck(bytes, payload, error,
                               "Launch release acknowledgement");
}

std::vector<std::uint8_t> encode_launch_cancel(
    const LaunchCancelPayload& payload) {
    if (payload.launch_epoch == 0U) return {};
    std::vector<std::uint8_t> out;
    out.reserve(4U);
    Put32(out, payload.launch_epoch);
    return out;
}

bool decode_launch_cancel(std::span<const std::uint8_t> bytes,
                          LaunchCancelPayload& payload, std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 4U || !Take32(bytes, cursor, payload.launch_epoch) ||
        payload.launch_epoch == 0U) {
        error = "Launch cancel payload is invalid.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_preflight_begin(
    const PreflightBeginPayload& payload) {
    if (payload.test_id == 0U || payload.duration_ms < 1000U ||
        payload.duration_ms > 15000U) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(6U);
    Put32(out, payload.test_id);
    Put16(out, payload.duration_ms);
    return out;
}

bool decode_preflight_begin(std::span<const std::uint8_t> bytes,
                            PreflightBeginPayload& payload,
                            std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 6U || !Take32(bytes, cursor, payload.test_id) ||
        !Take16(bytes, cursor, payload.duration_ms) || payload.test_id == 0U ||
        payload.duration_ms < 1000U || payload.duration_ms > 15000U) {
        error = "Connection pre-flight begin payload is invalid.";
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_preflight_probe(
    const PreflightProbePayload& payload) {
    if (payload.test_id == 0U || payload.sequence == 0U ||
        payload.sent_time_us == 0U || payload.padding.size() > 920U) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(19U + payload.padding.size());
    Put32(out, payload.test_id);
    Put32(out, payload.sequence);
    Put64(out, payload.sent_time_us);
    out.push_back(payload.echo ? 1U : 0U);
    Put16(out, static_cast<std::uint16_t>(payload.padding.size()));
    out.insert(out.end(), payload.padding.begin(), payload.padding.end());
    return out;
}

bool decode_preflight_probe(std::span<const std::uint8_t> bytes,
                            PreflightProbePayload& payload,
                            std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    std::uint16_t padding_size = 0U;
    if (bytes.size() < 19U ||
        !Take32(bytes, cursor, payload.test_id) ||
        !Take32(bytes, cursor, payload.sequence) ||
        !Take64(bytes, cursor, payload.sent_time_us) ||
        cursor >= bytes.size() || bytes[cursor] > 1U) {
        error = "Connection pre-flight probe payload is invalid.";
        return false;
    }
    payload.echo = bytes[cursor++] != 0U;
    if (!Take16(bytes, cursor, padding_size) || padding_size > 920U ||
        cursor + padding_size != bytes.size() || payload.test_id == 0U ||
        payload.sequence == 0U || payload.sent_time_us == 0U) {
        error = "Connection pre-flight probe payload is invalid.";
        return false;
    }
    payload.padding.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.end());
    error.clear();
    return true;
}

std::vector<std::uint8_t> encode_preflight_result(
    const PreflightResultPayload& payload) {
    if (payload.test_id == 0U || payload.player_slot >= kMaximumPlayers ||
        payload.score < 1U || payload.score > 10U) {
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(16U);
    Put32(out, payload.test_id);
    out.push_back(payload.player_slot);
    out.push_back(payload.score);
    Put16(out, payload.p95_rtt_ms);
    Put16(out, payload.jitter_ms);
    Put16(out, payload.loss_tenths_percent);
    Put16(out, payload.late_tenths_percent);
    out.push_back(payload.queues_drained ? 1U : 0U);
    return out;
}

bool decode_preflight_result(std::span<const std::uint8_t> bytes,
                             PreflightResultPayload& payload,
                             std::string& error) {
    payload = {};
    std::size_t cursor = 0U;
    if (bytes.size() != 15U || !Take32(bytes, cursor, payload.test_id) ||
        cursor + 2U > bytes.size()) {
        error = "Connection pre-flight result payload is invalid.";
        return false;
    }
    payload.player_slot = bytes[cursor++];
    payload.score = bytes[cursor++];
    if (!Take16(bytes, cursor, payload.p95_rtt_ms) ||
        !Take16(bytes, cursor, payload.jitter_ms) ||
        !Take16(bytes, cursor, payload.loss_tenths_percent) ||
        !Take16(bytes, cursor, payload.late_tenths_percent) ||
        cursor >= bytes.size() || bytes[cursor] > 1U ||
        payload.test_id == 0U || payload.player_slot >= kMaximumPlayers ||
        payload.score < 1U || payload.score > 10U) {
        error = "Connection pre-flight result payload is invalid.";
        return false;
    }
    payload.queues_drained = bytes[cursor] != 0U;
    error.clear();
    return true;
}

} // namespace dkr::runtime::netplay::protocol
