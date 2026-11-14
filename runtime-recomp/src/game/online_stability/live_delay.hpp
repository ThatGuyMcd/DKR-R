#pragma once

#include "control.hpp"
#include "live_prediction.hpp"
#include <array>
#include <functional>
#include <optional>
#include <set>

namespace dkr::online_stability {
inline constexpr unsigned MaximumDelay = 9;
struct Input {
    std::uint16_t buttons = 0;
    std::int8_t x = 0, y = 0;
    std::uint8_t accelerator = 0, brake = 0, driving_flags = 0;
    bool operator==(const Input&) const = default;
};
// Retain digital transitions while immutable future input drains. Analogue
// values remain live even when an older press/release still needs authoring.
class InputQueue final {
  public:
    bool capture(Input value, Clock::time_point now = Clock::now());
    Input take(Clock::time_point now = Clock::now());
    Input held() const {
        return held_;
    }
    std::size_t queued() const {
        return changes_.size();
    }
    unsigned age_ms() const {
        return age_ms_;
    }
    Clock::time_point captured_at() const {
        return held_at_;
    }
    std::uint64_t serial() const {
        return serial_;
    }
    void reset();

  private:
    struct Change {
        Input value;
        Clock::time_point at;
    };
    std::deque<Change> changes_;
    unsigned age_ms_ = 0;
    Input observed_{}, held_{};
    Clock::time_point held_at_{};
    Clock::time_point observed_at_{};
    std::uint64_t serial_ = 0;
};
enum class DelayKind : std::uint8_t { Hello = 1, Report, Propose, Ack, Commit, Abort, Target };
struct DelayMessage {
    DelayKind kind = DelayKind::Hello;
    std::uint64_t room = 0, epoch = 0, controls = 0, sequence = 0;
    std::uint32_t frame = 0, effective = 0;
    unsigned delay = 0, rtt_ms = 0, late_frames = 0, network_waits = 0, samples = 0;
    bool enabled = false;
    unsigned blocked_ms = 0, arrival_age_ms = 0;
    Member target = 0;
    unsigned owner = 255;
    std::array<unsigned, 4> owner_late{}, owner_samples{}, owner_waits{};
    unsigned work_us = 0, replay_us = 0, interval_us = 0;
    bool operator==(const DelayMessage&) const = default;
};
Bytes encode(const DelayMessage& message);
bool decode(std::span<const std::uint8_t> bytes, DelayMessage& message);
class DelayPolicy final {
  public:
    void reset(unsigned current, Clock::time_point now);
    std::optional<unsigned> evaluate(unsigned current, unsigned window, unsigned rtt_ms, unsigned late_frames,
                                     unsigned network_waits, bool fresh, bool measured, Clock::time_point now,
                                     bool replay_pressure = false, bool replay_busy = false,
                                     unsigned blocked_ms = UINT32_MAX);

  private:
    Clock::time_point changed_{}, stable_{}, recovery_until_{};
};
// A future schedule changes only local sample lead, never a launch descriptor,
// logical frame number, published sample, guest clock or checkpoint.
class LiveDelay final {
  public:
    struct HostClock {
        std::uint32_t frame = 0;
        unsigned rtt_ms = 0;
        Clock::time_point received{};
    };
    LiveDelay(std::shared_ptr<Control> control, unsigned fixed, unsigned window);
    void pulse(std::uint64_t epoch, std::uint64_t controls, std::uint32_t frame, bool paused, bool network_wait,
               Clock::time_point now = Clock::now(), std::uint8_t missing_owners = 0, bool confirmed_gate = false);
    bool capture(Input input, Clock::time_point now = Clock::now());
    Input take() {
        return inputs_.take(pulse_at_);
    }
    Input held() const {
        return inputs_.held();
    }
    unsigned lead() const {
        return current_;
    }
    unsigned host_lead() const {
        return host_lead_.value_or(fixed_);
    }
    std::optional<HostClock> take_host_clock() {
        auto value = host_clock_;
        host_clock_.reset();
        return value;
    }
    unsigned take_fill() {
        const auto n = fill_;
        fill_ = 0;
        return n;
    }
    void local_owner(unsigned owner) {
        local_owner_ = owner;
    }
    void arrival(unsigned owner, std::uint32_t frame, bool repair = false);
    void work(std::uint64_t microseconds, bool replay, std::uint32_t frame, bool executed);
    void authored(std::uint32_t frame);
    bool negotiated() const {
        return negotiated_;
    }
    bool dynamic() const {
        return dynamic_;
    }
    const unsigned& prediction_window() const {
        return window_;
    }

  private:
    struct Report {
        DelayMessage message;
        Clock::time_point at{};
    };
    struct Proposal {
        DelayMessage message;
        Clock::time_point retry{};
    };
    void send(Member target, DelayMessage message);
    void receive(const Identity&, Member source, const DelayMessage&, Clock::time_point now);
    void apply(unsigned delay, Clock::time_point now);
    std::shared_ptr<Control> control_;
    unsigned fixed_ = 0, current_ = 0, window_ = 0, fill_ = 0, late_ = 0, waits_ = 0, samples_ = 0, local_owner_ = 4;
    std::uint64_t room_ = 0, epoch_ = 0, controls_ = 0, serial_ = 0, retired_ = 0, report_serial_ = 0;
    std::uint32_t frame_ = 0, waiting_frame_ = UINT32_MAX;
    std::uint32_t clock_frontier_ = 0;
    bool paused_ = true, negotiated_ = false, host_ready_ = false, dynamic_ = false;
    Clock::time_point report_at_{}, hello_at_{}, decision_at_{}, sample_at_{}, moved_at_{};
    Clock::time_point pulse_at_{}, arrival_at_{}, wait_started_{}, applied_at_{};
    Clock::time_point diagnostic_at_{};
    std::array<Clock::time_point, 4> owner_arrivals_{};
    std::chrono::nanoseconds blocked_time_{};
    std::chrono::nanoseconds wait_time_{};
    bool was_waiting_ = false;
    std::map<Member, Clock::time_point> capable_;
    std::map<Member, Report> reports_;
    InputQueue inputs_;
    std::array<unsigned, 4> owner_late_{}, owner_samples_{}, owner_waits_{};
    std::array<std::array<unsigned, 49>, 4> arrival_histogram_{};
    std::uint64_t work_us_ = 0, replay_us_ = 0, traced_serial_ = 0;
    Clock::time_point measured_at_{};
    std::map<Member, DelayPolicy> policies_;
    std::map<Member, Proposal> targets_;
    std::optional<unsigned> host_lead_;
    std::optional<HostClock> host_clock_;
};
// The legacy Timeline's single sample-lead query uses this optional owner-local
// binding. Ordinary/manual tests and other threads keep their supplied value.
unsigned sample_lead(unsigned fallback);
std::uint64_t sample_limit(std::uint32_t frontier, unsigned fallback, bool live_lead);
class DelayScope final {
  public:
    explicit DelayScope(LiveDelay& delay);
    ~DelayScope();
    DelayScope(const DelayScope&) = delete;
    DelayScope& operator=(const DelayScope&) = delete;

  private:
    LiveDelay* previous_;
    PredictionScope prediction_;
};
void input_arrival(unsigned owner, std::uint32_t frame);
void input_authored(std::uint32_t frame);
void input_work(std::uint64_t microseconds, bool replay, std::uint32_t frame, bool executed);
class WorkScope final {
  public:
    WorkScope(const std::uint32_t& next, const std::uint32_t& replayed);
    ~WorkScope();
    WorkScope(const WorkScope&) = delete;
    WorkScope& operator=(const WorkScope&) = delete;

  private:
    LiveDelay* delay_;
    const std::uint32_t& next_;
    const std::uint32_t& replayed_;
    std::uint32_t before_, replays_;
    Clock::time_point start_;
};
void batch_arrival(unsigned owner, std::uint32_t newest, bool repair);
class ArrivalScope final {
  public:
    ArrivalScope();
    ~ArrivalScope();
    ArrivalScope(const ArrivalScope&) = delete;
    ArrivalScope& operator=(const ArrivalScope&) = delete;

  private:
    bool previous_;
};
} // namespace dkr::online_stability
