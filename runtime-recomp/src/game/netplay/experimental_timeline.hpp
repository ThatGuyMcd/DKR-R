#pragma once
#include "experimental_owner_inputs.hpp"
#include "experimental_rollback.hpp"

namespace dkr::runtime::netplay::experimental {

enum class TimelineStep { Advanced, Replayed, WaitingForLocalInput, PredictionLimit,
                          AwaitingBoundaryConfirmation, ConfirmedBoundary, Failed, WaitingForPresentation,
                          WaitingForConfirmedInput };
struct TimelineConfiguration {
    Configuration simulation;
    std::uint8_t local_owner = 0;
    std::uint8_t input_delay = 0;
};

// Experimental-only bridge, NOT attached to the stable DirectSession ledger.
// The caller authenticates peers and serializes these calls on the simulation
// owner. Network callbacks must not call this object or mutate guest RAM.
class Timeline final {
public:
    explicit Timeline(Simulation& simulation) : driver_(simulation) {}
    bool start(TimelineConfiguration configuration, std::string& error);
    // Sample exactly once per new local authored frame. During a correction
    // the presentation frontier does not rewind. The paced live clock may
    // publish a bounded lead, never overwrite a final sample on the wire.
    bool needs_local_input(bool live_lead=false) const;
    OwnerInputResult sample_local(PackedInput input,bool live_lead=false);
    OwnerInputResult receive_authenticated(std::uint8_t peer, std::span<const std::uint8_t> bytes);
    // Admit received immutable inputs into the driver's ledger BEFORE querying
    // whether to sample. Does not capture, restore, tick or commit the world.
    bool service_inputs();
    std::optional<OwnerInputPacket> packet_for(std::uint8_t owner, std::uint8_t peer,
                                             OwnerInputSend lane = OwnerInputSend::Live);
    // One tick maximum; no sleep, network poll, physical poll or frame-draining
    // loop. A caller can keep servicing its UI at a prediction/scene barrier.
    // Pacing may forbid a NEW tick, never correction or confirmation.
    TimelineStep step(bool allow_advance = true);
    const Statistics& statistics() const { return driver_.statistics(); }
    const std::string& error() const { return error_.empty() ? driver_.error() : error_; }
    std::uint32_t frontier() const { return frontier_; }
    bool correcting() const { return driver_.correcting(); }
    std::uint8_t missing_input_mask() const {return driver_.missing_input_mask();}
    bool scene_boundary_pending() const { return driver_.scene_boundary_pending(); }
private:
    struct Pending { std::uint32_t frame = 0; PackedInput input{}; bool valid = false; };
    bool stage(std::uint8_t owner, std::uint32_t frame, PackedInput input);
    bool feed();
    Driver driver_;
    OwnerInputHistory inputs_;
    std::array<std::array<Pending,128>,4> pending_{};
    std::array<unsigned,4> pending_counts_{};
    TimelineConfiguration configuration_{};
    std::uint32_t frontier_ = 0;
    bool started_ = false;
    std::string error_;
};
}
