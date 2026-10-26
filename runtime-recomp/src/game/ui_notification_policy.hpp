#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <utility>

namespace dkr::runtime::ui_notifications {

enum class Kind {
    FriendOnline,
    LobbyInvite,
};

struct Toast {
    Kind kind = Kind::FriendOnline;
    std::string title;
    std::string message;
    std::chrono::steady_clock::time_point started{};
    std::chrono::duration<float> duration{7.0F};
    std::chrono::duration<float> fade_duration{1.5F};
};

class Queue {
  public:
    explicit Queue(std::size_t capacity = 8U) : capacity_(std::max<std::size_t>(capacity, 1U)) {}

    bool push(Kind kind, std::string title, std::string message, std::chrono::steady_clock::time_point now) {
        if (message.empty())
            return false;
        discard_expired(now);
        if (toasts_.size() >= capacity_) {
            // Never evict an actionable invitation for background presence.
            // Leave the displayed toast intact; coalesce queued presence bursts.
            const auto presence = std::find_if(std::next(toasts_.begin()), toasts_.end(),
                                               [](const Toast& toast) { return toast.kind == Kind::FriendOnline; });
            if (presence == toasts_.end())
                return false;
            if (kind == Kind::FriendOnline) {
                presence->title = "FRIENDS ONLINE";
                presence->message = "Several friends are online. Open FRIENDS to see who is available.";
                return true;
            }
            toasts_.erase(presence);
        }
        toasts_.push_back({kind, std::move(title), std::move(message), now});
        return true;
    }

    const Toast* current(std::chrono::steady_clock::time_point now) {
        discard_expired(now);
        return toasts_.empty() ? nullptr : &toasts_.front();
    }

    std::size_t size(std::chrono::steady_clock::time_point now) {
        discard_expired(now);
        return toasts_.size();
    }

    static float alpha(const Toast& toast, std::chrono::steady_clock::time_point now) {
        const auto elapsed = now - toast.started;
        if (elapsed <= toast.duration - toast.fade_duration)
            return 1.0F;
        const float remaining = std::chrono::duration<float>(toast.duration - elapsed).count();
        return std::clamp(remaining / std::max(toast.fade_duration.count(), 0.001F), 0.0F, 1.0F);
    }

  private:
    void discard_expired(std::chrono::steady_clock::time_point now) {
        while (!toasts_.empty() && now - toasts_.front().started >= toasts_.front().duration) {
            toasts_.pop_front();
            if (!toasts_.empty())
                toasts_.front().started = now;
        }
    }

    std::size_t capacity_ = 8U;
    std::deque<Toast> toasts_;
};

// A friend route can be replaced while WebRTC renegotiates. Treating the
// retiring route's close callback as a real offline event creates a false
// offline -> online edge and repeatedly queues the same notification. Keep a
// small second line of defence at the presentation boundary: a friend must
// have been continuously observed offline before a later online edge is
// eligible for a toast.
class FriendOnlineTransitionGate {
  public:
    explicit FriendOnlineTransitionGate(std::chrono::steady_clock::duration minimum_offline = std::chrono::seconds{10})
        : minimum_offline_(minimum_offline) {}

    bool update(bool online, std::chrono::steady_clock::time_point now) {
        if (!seeded_) {
            seeded_ = true;
            online_ = online;
            if (!online)
                offline_since_ = now;
            return false;
        }

        if (!online) {
            if (online_)
                offline_since_ = now;
            online_ = false;
            return false;
        }

        if (online_)
            return false;
        const bool notify = now - offline_since_ >= minimum_offline_;
        online_ = true;
        return notify;
    }

  private:
    std::chrono::steady_clock::duration minimum_offline_{};
    std::chrono::steady_clock::time_point offline_since_{};
    bool seeded_ = false;
    bool online_ = false;
};

} // namespace dkr::runtime::ui_notifications
