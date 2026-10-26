#include "observer_playback.hpp"
#include <algorithm>
#include <exception>

namespace dkr::runtime::spectators {
namespace {
constexpr auto Tick = std::chrono::nanoseconds(1000000000 / 30);
}
ObserverPlayback::ObserverPlayback(ObserverWorld& world, std::uint64_t match, HashState hash)
    : world_(world), match_(match), hash_(std::move(hash)) {}
bool ObserverPlayback::offer(Baseline b, std::string& error) {
    if (view_.phase == PlaybackPhase::Stopped || !match_ || !hash_ || b.match != match_ ||
        b.schema != world_.schema() || !b.state || b.state->size() != world_.state_bytes() || b.state->empty() ||
        b.state->size() > ConfirmedFeed::MaximumState || !b.next.epoch || b.next.frame == UINT32_MAX ||
        !b.control_generation || hash_(*b.state) != b.digest) {
        error = "Spectator baseline does not match the verified game state.";
        return false;
    }
    if (view_.next.epoch &&
        (b.next.epoch < view_.next.epoch || (b.next.epoch == view_.next.epoch && b.next.frame < view_.next.frame))) {
        error = "Spectator baseline is older than playback.";
        return false;
    }
    if (pending_ && b.next == pending_->next) {
        if (b.digest != pending_->digest || b.magic_actions != pending_->magic_actions) {
            error = "Conflicting spectator baseline.";
            return false;
        }
        error.clear();
        return true;
    }
    if (pending_ && (b.next.epoch < pending_->next.epoch ||
                     (b.next.epoch == pending_->next.epoch && b.next.frame < pending_->next.frame))) {
        error = "Spectator baseline offer is older than the pending checkpoint.";
        return false;
    }
    // Full integrity validation precedes any publication retirement or RAM
    // mutation. Retire/admit themselves remain on the guest owner thread.
    pending_ = std::move(b);
    queue_.clear();
    recent_.clear();
    queued_bytes_ = 0;
    prepared_ = scene_prepared_ = false;
    view_.received = pending_->next;
    view_.queued = 0;
    view_.phase = PlaybackPhase::Retiring;
    view_.status = "Loading the confirmed race";
    error.clear();
    return true;
}
FeedResult ObserverPlayback::append(std::span<const ConfirmedFrame> frames, std::string& error) {
    if (frames.empty() || frames.size() > 30 || view_.phase == PlaybackPhase::WaitingForBaseline ||
        view_.phase == PlaybackPhase::Failed || view_.phase == PlaybackPhase::Stopped) {
        error = "Spectator playback is not ready for input history.";
        return FeedResult::Invalid;
    }
    // Validate the whole batch first; a malformed tail must not partly enter
    // the queue or advance its acknowledged cursor.
    Cursor expected = view_.received;
    std::size_t cost = 0, accepted = 0;
    bool boundary = !queue_.empty() && queue_.back().scene_boundary;
    for (const auto& f : frames) {
        if (!f.cursor.epoch || f.cursor.frame == UINT32_MAX || f.command.size() > ConfirmedFeed::MaximumCommand) {
            error = "Invalid confirmed spectator frame.";
            return FeedResult::Invalid;
        }
        if (f.cursor.epoch < expected.epoch || (f.cursor.epoch == expected.epoch && f.cursor.frame < expected.frame)) {
            const auto same = [&](const auto& old) { return old.cursor == f.cursor; };
            auto old = std::find_if(queue_.begin(), queue_.end(), same);
            if (old != queue_.end()) {
                if (*old != f) {
                    error = "Conflicting confirmed spectator input.";
                    return FeedResult::Invalid;
                }
            } else {
                auto retired = std::find_if(recent_.begin(), recent_.end(), same);
                if (retired == recent_.end()) {
                    error = "Spectator input is outside the replay window.";
                    return FeedResult::Stale;
                }
                if (*retired != f) {
                    error = "Conflicting retired spectator input.";
                    return FeedResult::Invalid;
                }
            }
            continue;
        }
        if (boundary || f.cursor != expected) {
            error = "Spectator history has a gap; a new checkpoint is required.";
            return FeedResult::Gap;
        }
        ++expected.frame;
        ++accepted;
        cost += 48 + f.command.size();
        boundary = f.scene_boundary;
    }
    if (accepted > MaximumQueued - queue_.size() || cost > ConfirmedFeed::MaximumTail - queued_bytes_) {
        error = "Spectator fell behind; a new checkpoint is required.";
        return FeedResult::Capacity;
    }
    for (const auto& f : frames)
        if (f.cursor.epoch == view_.received.epoch && f.cursor.frame >= view_.received.frame) {
            queue_.push_back(f);
            queued_bytes_ += 48 + f.command.size();
        }
    view_.received = expected;
    view_.queued = queue_.size();
    error.clear();
    return accepted ? FeedResult::Accepted : FeedResult::Duplicate;
}
Work ObserverPlayback::fail(std::string message, std::string& error) {
    view_.phase = PlaybackPhase::Failed;
    view_.status = std::move(message);
    error = view_.status;
    return Work::Failed;
}
Work ObserverPlayback::pulse(Clock::time_point now, std::string& error) try {
    if (clock_started_ && now < last_poll_)
        return fail("Spectator pacing clock moved backwards.", error);
    clock_started_ = true;
    last_poll_ = now;
    error.clear();
    if (view_.phase == PlaybackPhase::Failed) {
        error = view_.status;
        return Work::Failed;
    }
    if (view_.phase == PlaybackPhase::Stopped || view_.phase == PlaybackPhase::WaitingForBaseline)
        return Work::Pending;
    if (view_.phase == PlaybackPhase::Retiring) {
        if (!pending_)
            return fail("Spectator baseline transaction was lost.", error);
        const auto ready = world_.retire(pending_->next, error);
        if (ready == Work::Pending)
            return ready;
        if (ready != Work::Ready || !world_.admit(*pending_, error))
            return fail(error.empty() ? "Spectator state admission failed." : error, error);
        view_.next = pending_->next;
        pending_.reset();
        view_.phase = PlaybackPhase::CatchingUp;
        view_.status = "Catching up";
        next_tick_ = now;
        return Work::Ready;
    }
    if (view_.phase == PlaybackPhase::SceneBoundary) {
        if (!scene_prepared_) {
            const auto work = world_.prepare_scene({view_.next.epoch, view_.next.frame - 1}, error);
            if (work == Work::Failed)
                return fail(error.empty() ? "Spectator scene preparation failed." : error, error);
            if (work == Work::Pending)
                return work;
            scene_prepared_ = true;
        }
        request_resync("Waiting for the next scene checkpoint");
        return Work::Ready;
    }
    if (queue_.empty()) {
        view_.phase = PlaybackPhase::Watching;
        view_.status = "Watching";
        next_tick_ = now;
        return Work::Pending;
    }
    // Catch up without an unbounded inner loop. UI and incoming traffic get a
    // turn between every tick. Near the head, keep a small confirmed delay.
    const bool catchup = queue_.size() > 8;
    if (!catchup && now < next_tick_)
        return Work::Pending;
    const auto& frame = queue_.front();
    if (frame.cursor != view_.next)
        return fail("Spectator playback lost its contiguous confirmed cursor.", error);
    if (!prepared_) {
        const auto work = world_.prepare(frame, error);
        if (work == Work::Failed)
            return fail(error.empty() ? "Spectator tick preparation failed." : error, error);
        if (work == Work::Pending)
            return work;
        prepared_ = true;
    }
    if (!world_.tick_and_commit(frame, error))
        return fail(error.empty() ? "Confirmed spectator playback failed." : error, error);
    const auto completed = frame.cursor;
    const bool boundary = frame.scene_boundary;
    queued_bytes_ -= 48 + frame.command.size();
    recent_.push_back(frame);
    if (recent_.size() > 120)
        recent_.pop_front();
    queue_.pop_front();
    ++view_.next.frame;
    ++view_.simulated;
    view_.queued = queue_.size();
    prepared_ = false;
    view_.phase = boundary  ? PlaybackPhase::SceneBoundary
                  : catchup ? PlaybackPhase::CatchingUp
                            : PlaybackPhase::Watching;
    view_.status = boundary ? "Changing scene" : catchup ? "Catching up" : "Watching";
    // Rendering intermediate fast-forward ticks wastes GPU time and can fill
    // immutable mailbox leases. Publish only near the confirmed live head.
    if (!boundary && !catchup && !world_.publish(completed, error))
        return fail(error.empty() ? "Spectator presentation failed." : error, error);
    next_tick_ = catchup ? now : std::max(next_tick_ + Tick, now - Tick);
    return Work::Ready;
} catch (const std::exception& e) {
    return fail(std::string("Spectator playback stopped: ") + e.what(), error);
} catch (...) {
    return fail("Spectator playback stopped unexpectedly.", error);
}
void ObserverPlayback::request_resync(std::string reason) {
    if (view_.phase == PlaybackPhase::Stopped)
        return;
    pending_.reset();
    queue_.clear();
    recent_.clear();
    queued_bytes_ = 0;
    prepared_ = scene_prepared_ = false;
    view_.queued = 0;
    view_.phase = PlaybackPhase::WaitingForBaseline;
    view_.status = std::move(reason);
    ++view_.resyncs;
}
void ObserverPlayback::stop() {
    pending_.reset();
    queue_.clear();
    recent_.clear();
    queued_bytes_ = 0;
    view_.queued = 0;
    view_.phase = PlaybackPhase::Stopped;
    view_.status = "Stopped watching";
}
} // namespace dkr::runtime::spectators
