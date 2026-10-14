#include "accessibility_feedback.hpp"

#include <algorithm>
#include <mutex>

namespace dkr::runtime::accessibility {
namespace {
std::mutex preferences_mutex;
FeedbackPreferences preferences_value;
std::chrono::milliseconds position_interval(FeedbackPreferences preferences) {
    return std::clamp(preferences.position_interval,
        std::chrono::milliseconds{500}, std::chrono::milliseconds{10000});
}
}

FeedbackPreferences feedback_preferences() {
    std::scoped_lock lock(preferences_mutex);
    return preferences_value;
}

void set_feedback_preferences(FeedbackPreferences preferences) {
    preferences.position_interval = position_interval(preferences);
    std::scoped_lock lock(preferences_mutex);
    preferences_value = preferences;
}

void FeedbackScheduler::reset() {
    session_ = 0;
    scene_ = 0;
    kind_ = StateKind::Unavailable;
    delivered_.clear();
    delivered_selection_ = -1;
    delivered_prompt_ = RaceStartPrompt::None;
    pending_.clear();
    delivered_once_ = false;
}

std::optional<std::string> FeedbackScheduler::update(
    const std::optional<Observation>& observation, bool suppressed,
    FeedbackPreferences preferences, Clock::time_point now) {
    if (!preferences.automatic_speech) {
        reset();
        return std::nullopt;
    }
    const bool unavailable = !observation || observation->kind == StateKind::Unavailable;
    if (unavailable) {
        // An unavailable sample may still identify a new session/scene. Its
        // unknown kind alone is not evidence that the last delivered context ended.
        if (observation && (observation->session != session_ || observation->scene != scene_)) reset();
        pending_.clear();
        return std::nullopt;
    }
    const auto& current = *observation;
    // A latest-only mailbox can skip the menus between two race scenes.
    // Pending messages and prompt deduplication belong to this whole context.
    if (current.session != session_ || current.kind != kind_ || current.scene != scene_) {
        reset();
        session_ = current.session;
        scene_ = current.scene;
        kind_ = current.kind;
    }
    if (suppressed) {
        // Keep only what was actually delivered in this observed context;
        // pending speech cannot survive a focus/visibility interruption.
        pending_.clear();
        return std::nullopt;
    }

    const auto prompt = current.kind == StateKind::Race && !current.paused
        ? current.race_start_prompt : RaceStartPrompt::None;
    if (prompt != RaceStartPrompt::None) {
        // Qualified authored prompts are time-sensitive. Do not hold GO behind
        // position throttling, or repeat a prompt after a brief visibility gap.
        if (prompt == delivered_prompt_) return std::nullopt;
        delivered_prompt_ = prompt;
        delivered_ = prompt == RaceStartPrompt::GetReady ? "Get Ready!" : "GO!";
        pending_.clear();
        last_delivery_ = now;
        delivered_once_ = true;
        return delivered_;
    }

    if (current.kind == StateKind::Race) {
        if (current.paused) pending_ = "Race paused.";
        // Startup speech comes only from qualified Get Ready/GO artwork above,
        // never from the separate cached countdown when that art is unavailable.
        else if (preferences.announce_position && current.racer)
            pending_ = "Position " + std::to_string(current.racer->position) + ".";
        else pending_.clear();
    } else {
        pending_ = describe_observation(current);
    }

    // Identical labels can belong to different visible menu choices (for
    // example blank Save cards). A changed selection still needs feedback.
    const bool same_selection = current.kind == StateKind::Race ||
        current.selection == delivered_selection_;
    if (pending_.empty() || (pending_ == delivered_ && same_selection)) return std::nullopt;
    const auto interval = current.kind == StateKind::Race
        ? position_interval(preferences)
        : std::chrono::milliseconds{150};
    if (delivered_once_ && now - last_delivery_ < interval) return std::nullopt;
    delivered_ = pending_;
    delivered_selection_ = current.selection;
    last_delivery_ = now;
    delivered_once_ = true;
    return delivered_;
}

} // namespace dkr::runtime::accessibility
