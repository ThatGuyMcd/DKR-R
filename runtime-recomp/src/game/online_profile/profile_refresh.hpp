#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace dkr::profile {
// Publishing a verified friend link is metadata work. It must also run while
// hosting or visiting a lobby; otherwise other members can retain an expired
// friend capability and discard both the picture and the Add Friend action.
struct ProfileRefresh {
    using Clock = std::chrono::steady_clock;
    std::string account, key, capability;
    Clock::time_point next{};
    std::uint64_t mutation = 0;
    unsigned failures = 0;
    bool pending = false;

    bool due(std::string_view identity, std::string_view public_key, Clock::time_point now,
             std::string_view friend_code = {}) {
        if (identity.empty() || public_key.empty()) return false;
        if (account != identity || key != public_key || capability != friend_code) {
            *this = {};
            account = identity;
            key = public_key;
            capability = friend_code;
        }
        return !pending && now >= next;
    }
    void begin(std::uint64_t before) { mutation = before; pending = true; }
    void settle(std::string_view identity, std::uint64_t after, std::string_view action,
                std::string_view saved_account, std::string_view saved_key, bool busy,
                Clock::time_point now) {
        if (account != identity) { *this = {}; return; }
        if (!pending || busy) return;
        pending = false;
        if (after != mutation && action == "launcher.profile" && saved_account == account && saved_key == key) {
            failures = 0;
            next = now + std::chrono::hours(12);
        } else {
            next = now + std::chrono::minutes(1U << std::min(failures, 5U));
            failures = std::min(failures + 1U, 6U);
        }
    }
};
} // namespace dkr::profile
