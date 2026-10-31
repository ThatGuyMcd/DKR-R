#pragma once
#include "profile_policy.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace dkr::profile {
inline bool refresh_idle_name(std::string_view previous, std::string_view current, bool hosting, bool member,
                              bool joining) {
    return previous != current && !hosting && !member && !joining;
}

inline bool complete_display_name(std::string_view input) {
    if (input.size() > 24 || display_name(input) != input)
        return false;
    std::size_t characters = 0;
    for (const unsigned char c : input)
        if ((c & 0xC0) != 0x80)
            ++characters;
    return characters >= 2;
}

// Responses belong to both the account and the requested name. Other account
// mutations, failed requests and account switches cannot commit this save.
struct DisplayNameSave {
    enum class Result { Waiting, Saved, Failed, AccountChanged };
    std::string account, name;
    std::uint64_t mutation = 0;
    bool pending = false;
    void begin(std::string identity, std::string requested, std::uint64_t before) {
        account = std::move(identity);
        name = std::move(requested);
        mutation = before;
        pending = true;
    }
    Result settle(std::string_view identity, std::uint64_t now, std::string_view action,
                  std::string_view result_account, std::string_view result_name, bool busy) {
        if (!pending)
            return Result::Waiting;
        if (identity != account) {
            pending = false;
            return Result::AccountChanged;
        }
        if (now != mutation && action == "account.profile" && result_account == account && result_name == name) {
            pending = false;
            return Result::Saved;
        }
        if (!busy) {
            pending = false;
            return Result::Failed;
        }
        return Result::Waiting;
    }
};
} // namespace dkr::profile
