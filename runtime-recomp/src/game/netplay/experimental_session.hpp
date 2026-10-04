#pragma once
#include "experimental_epoch.hpp"
#include "experimental_timeline.hpp"

namespace dkr::runtime::netplay::experimental {

struct BoundaryIntent { std::uint64_t boundary_hash = 0, scene_hash = 0; };
enum class PreparationStep { Pending, Ready, Failed };

// Implemented by an OWNED replay-qualified world, not the live DirectSession
// world. Neither method may run from a transport callback. prepare_scene()
// must perform a bounded increment of its transaction and keep the UI/network
// serviceable between calls. It may mutate the world only after the old epoch
// has become irreversible; the baseline hash must cover canonical owned state,
// not native pointer addresses or the sparse authority correction packet.
class SceneSimulation : public Simulation {
public:
    virtual bool confirmed_boundary(BoundaryIntent& intent, std::string& error) = 0;
    virtual PreparationStep prepare_scene(std::uint64_t scene_hash,
                                          std::uint64_t& baseline_hash,
                                          std::string& error) = 0;
};

enum class SessionStep { Advanced, Replayed, WaitingForInput, PredictionLimit,
                         WaitingForConfirmation, WaitingForPeers, PreparingScene,
                         SceneStarted, Failed, WaitingForPresentation };
enum class SessionInputResult { Accepted, Duplicate, Stale, Pending, Rejected, Conflict };

// Experimental coordinator. Combines the independently qualified timeline and
// scene protocol; it is NOT attached to the current stable runtime/transport.
// Callers authenticate/enqueue messages and invoke this on one owner only.
// Every step performs at most ONE simulation tick or ONE preparation increment.
class Session final {
public:
    explicit Session(SceneSimulation& world) : world_(world), timeline_(world) {}
    bool start(TimelineConfiguration configuration, std::string& error);
    bool needs_local_input() const;
    OwnerInputResult sample_local(PackedInput input);
    SessionInputResult receive_input(std::uint8_t peer, std::span<const std::uint8_t> bytes);
    bool service_inputs(); // Owner-only ledger admission, never a simulation tick.
    EpochResult receive_control(std::uint8_t peer, std::span<const std::uint8_t> bytes);
    std::optional<OwnerInputPacket> packet_for(std::uint8_t owner, std::uint8_t peer,
                                             OwnerInputSend lane = OwnerInputSend::Live);
    std::optional<EpochMessage> control_for(std::uint8_t peer) const;
    std::optional<EpochMessage> previous_control_for(std::uint8_t peer) const;
    SessionStep step(bool allow_advance = true);
    const Statistics& statistics() const { return timeline_.statistics(); }
    std::uint32_t frontier() const { return timeline_.frontier(); }
    std::uint64_t epoch() const { return configuration_.simulation.epoch; }
    std::uint8_t local_owner() const { return configuration_.local_owner; }
    std::uint8_t player_count() const { return configuration_.simulation.players; }
    // Do not publish a partially replayed historical frame or scene intent.
    bool presentation_ready() const {
        return phase_==Phase::Running && !timeline_.correcting() && !timeline_.scene_boundary_pending() &&
               statistics().next_frame!=0 && statistics().next_frame==frontier();
    }
    const std::string& error() const { return error_; }
private:
    enum class Phase { Inactive, Running, Boundary, Preparing, Release, Failed };
    bool fail(std::string error);
    SceneSimulation& world_;
    Timeline timeline_;
    EpochGate gate_;
    TimelineConfiguration configuration_{};
    std::uint64_t schema_ = 0;
    BoundaryIntent intent_{};
    Phase phase_ = Phase::Inactive;
    std::string error_;
};
}
