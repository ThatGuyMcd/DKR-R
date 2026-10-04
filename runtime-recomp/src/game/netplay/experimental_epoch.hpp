#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dkr::runtime::netplay::experimental {

enum class EpochMessageKind : std::uint8_t { Boundary, Prepare, Prepared, Release, Acknowledged };
struct EpochMessage {
    EpochMessageKind kind = EpochMessageKind::Boundary;
    std::uint8_t players = 0;
    std::uint64_t epoch = 0, next_epoch = 0;
    std::uint32_t confirmed_end = 0;
    std::uint64_t boundary_hash = 0, schema = 0, scene_hash = 0, baseline_hash = 0;
    bool operator==(const EpochMessage&) const = default;
};
std::vector<std::uint8_t> encode_epoch_message(const EpochMessage& message);
std::optional<EpochMessage> decode_epoch_message(std::span<const std::uint8_t> bytes);
enum class EpochResult { Accepted, Duplicate, Pending, Stale, Rejected, Conflict };

// Experimental-only, single-owner protocol state. No guest memory, queues,
// renderer resources, files or callbacks are touched by receive(). The live
// adapter must first confirm the boundary, then perform a quiescent scene
// transaction and mark its canonical baseline ready. A hash supplied by the
// caller is NOT evidence that its snapshot is complete or replay-qualified.
// Transport authenticates the peer slot, independently of this payload.
class EpochGate final {
public:
    bool begin(std::uint64_t epoch, std::uint8_t players, std::uint8_t local, std::uint64_t schema);
    bool confirm_boundary(std::uint32_t confirmed_end, std::uint64_t boundary_hash);
    // Host only, after all four/three/two boundary votes match its own.
    bool propose(std::uint64_t next_epoch, std::uint64_t scene_hash);
    bool can_prepare_scene() const;
    bool mark_prepared(std::uint64_t canonical_baseline_hash);
    EpochResult receive_authenticated(std::uint8_t peer, const EpochMessage& message);
    std::optional<EpochMessage> message_for(std::uint8_t peer) const;
    // Keep one retired control transaction available during the next epoch.
    // A lost release ACK must not strand the host after a client has begun.
    // The eventual transport sends this alongside current control retries;
    // it never resumes simulation or applies a scene transaction a second time.
    std::optional<EpochMessage> previous_message_for(std::uint8_t peer) const;
    bool all_boundaries_confirmed() const;
    bool released() const { return released_ && !failed_; }
    bool failed() const { return failed_; }
    std::uint64_t next_epoch() const { return proposal_ ? proposal_->next_epoch : 0; }
private:
    bool matches_boundary(const EpochMessage& message) const;
    bool matches_proposal(const EpochMessage& message) const;
    bool validate_votes();
    void try_release();
    EpochMessage boundary_{};
    std::optional<EpochMessage> proposal_;
    std::array<std::optional<EpochMessage>,4> votes_{};
    std::array<std::uint64_t,4> baselines_{};
    std::array<bool,4> acknowledged_{};
    std::optional<EpochMessage> previous_;
    std::uint8_t local_ = 0;
    bool confirmed_ = false, prepared_ = false, release_available_ = false, released_ = false, failed_ = false;
};
}
