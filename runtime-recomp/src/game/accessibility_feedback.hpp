#pragma once

#include "accessibility_observation.hpp"

namespace dkr::runtime::accessibility {

struct FeedbackPreferences {
    bool automatic_speech = false;
    bool announce_position = true;
    std::chrono::milliseconds position_interval{1500};
};

FeedbackPreferences feedback_preferences();
void set_feedback_preferences(FeedbackPreferences preferences);

// Main-thread consumer. Retains only the latest message, never a backlog of
// obsolete race positions. Gaps discard pending speech, but retain delivered
// history for one identified context; speech opt-out resets it immediately.
// Platform adapters own actual speech delivery.
class FeedbackScheduler {
public:
    std::optional<std::string> update(const std::optional<Observation>& observation,
        bool suppressed, FeedbackPreferences preferences,
        Clock::time_point now = Clock::now());
    void reset();
private:
    SessionToken session_ = 0;
    std::uint32_t scene_ = 0;
    StateKind kind_ = StateKind::Unavailable;
    std::string delivered_, pending_;
    int delivered_selection_ = -1;
    RaceStartPrompt delivered_prompt_ = RaceStartPrompt::None;
    Clock::time_point last_delivery_{};
    bool delivered_once_ = false;
};

} // namespace dkr::runtime::accessibility
