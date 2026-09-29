#pragma once

#include "netplay_types.hpp"
#include "rollback_state_store.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::netplay::experimental {

// This driver is deliberately independent of DirectSession's committed-input
// ledger. A prediction is NEVER a confirmed input, including on the host.
// All methods run on one simulation owner; authenticated network input must be
// delivered to that owner, not applied from a receive callback.
enum class StateDomain : std::uint32_t {
    Globals = 1U << 0U,
    Actors = 1U << 1U,
    Allocation = 1U << 2U,
    Collision = 1U << 3U,
    Random = 1U << 4U,
    InputEdges = 1U << 5U,
    CameraAndWater = 1U << 6U,
    NativeParticipants = 1U << 7U,
};
inline constexpr std::uint32_t kRequiredStateDomains = 0xFFU;

struct SimulationContract {
    // Local checkpoint schema, NOT the portable correction-packet schema.
    std::uint64_t schema = 0U;
    std::size_t state_bytes = 0U;
    std::uint32_t state_domains = 0U;
    bool quiescent_tick_boundary = false;
    bool transactional_restore = false;
    bool deferred_external_effects = false;
};

struct TickOutput {
    // Adapter-owned encoding. Audio/rumble/save/scene effects are delivered
    // only through commit(), once their complete input frame is confirmed.
    std::vector<std::uint8_t> effects;
    // A scene change is a reversible intent until commit, never a load/free
    // performed while speculation is running. Do not simulate beyond it.
    bool scene_boundary = false;
};

class Simulation {
public:
    virtual ~Simulation() = default;
    virtual SimulationContract contract() const = 0;
    virtual bool capture(std::span<std::uint8_t> state, std::string& error) = 0;
    // Validate all participants before mutating any of them. Failure leaves
    // the entire world unchanged; native stacks/OS queues are not checkpoints.
    virtual bool restore(std::span<const std::uint8_t> state,
                         std::string& error) = 0;
    virtual bool tick(std::uint32_t frame, const FrameInputs& inputs,
                      TickOutput& output, std::string& error) = 0;
    virtual bool commit(std::uint64_t epoch, std::uint32_t frame,
                        std::span<const std::uint8_t> effects,
                        bool scene_boundary, std::string& error) = 0;
};

struct Configuration {
    std::uint64_t epoch = 0U;
    std::uint8_t players = 2U;
    std::uint8_t prediction_window = 6U;
    std::size_t checkpoint_budget_bytes = 128U * 1024U * 1024U;
};

enum class Step {
    Advanced,
    Replayed,
    PredictionLimit,
    AwaitingBoundaryConfirmation,
    ConfirmedBoundary,
    Failed,
};
enum class InputResult { Accepted, Duplicate, StaleEpoch, OutOfRange, Conflict, Failed };

struct Statistics {
    std::uint32_t next_frame = 0U;
    std::uint32_t confirmed_frames = 0U;
    std::uint32_t rollbacks = 0U;
    std::uint32_t replayed_frames = 0U;
    std::uint32_t largest_rollback = 0U;
    std::uint32_t predicted_frames = 0U;
    std::size_t checkpoint_bytes = 0U;
};

class Driver final {
public:
    explicit Driver(Simulation& simulation);
    // start() is a new scene/epoch only. No implicit mid-race conversion of
    // stable-mode state, and no reinterpretation of its committed predictions.
    bool start(Configuration configuration, std::string& error);
    InputResult receive(std::uint64_t epoch, std::uint8_t owner,
                        std::uint32_t frame, PackedInput input);
    // At most one simulated tick per call, including correction replay. The
    // caller controls a wall-clock/replay budget and can always service UI.
    Step step();
    const Statistics& statistics() const { return statistics_; }
    const std::string& error() const { return error_; }
    bool active() const { return active_ && error_.empty(); }
    bool correcting() const;
    static bool valid_contract(const SimulationContract& contract);

private:
    static constexpr std::size_t kHistory = 128U;
    static constexpr std::uint32_t kFutureInputs = 48U;
    static constexpr std::size_t kMaximumEffectBytes = 64U * 1024U;
    struct Frame {
        std::uint32_t number = 0U;
        bool valid = false;
        bool simulated = false;
        std::uint8_t actual_mask = 0U;
        FrameInputs actual{};
        FrameInputs used{};
        TickOutput output{};
    };
    Frame* find(std::uint32_t frame);
    Frame& entry(std::uint32_t frame);
    FrameInputs predict(const Frame& frame);
    bool save(std::uint32_t frame);
    bool restore(std::uint32_t frame);
    bool confirm();
    bool fail(std::string message);
    Simulation& simulation_;
    Configuration configuration_{};
    SimulationContract contract_{};
    std::array<Frame, kHistory> frames_{};
    FrameInputs confirmed_inputs_{};
    std::unique_ptr<RollbackStateStore> checkpoints_;
    std::vector<std::uint8_t> scratch_;
    Statistics statistics_{};
    std::uint32_t dirty_frame_ = UINT32_MAX;
    std::uint32_t replay_goal_ = 0U;
    std::uint32_t boundary_frame_ = UINT32_MAX;
    bool active_ = false;
    bool committed_boundary_ = false;
    std::string error_;
};

} // namespace dkr::runtime::netplay::experimental
