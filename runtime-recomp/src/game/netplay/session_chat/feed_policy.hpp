#pragma once
#include "chat.hpp"
#include <algorithm>
#include <set>

namespace dkr::chat {
// Passive race chat has no idle window. Muted or expired messages cannot
// keep it visible; opening the editor is independent of the feed preference.
inline std::vector<const Message*> recent_feed(const View& view, const std::set<Id>& muted, Clock::time_point now) {
    std::vector<const Message*> rows;
    for (auto m = view.messages.rbegin(); m != view.messages.rend() && rows.size() < 3; ++m)
        if (!muted.contains(m->author) && now >= m->received && now - m->received < std::chrono::seconds(8))
            rows.push_back(&*m);
    std::reverse(rows.begin(), rows.end());
    return rows;
}
} // namespace dkr::chat
