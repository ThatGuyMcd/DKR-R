#pragma once
#include <algorithm>
#include <regex>
#include <string>
#include <string_view>

namespace dkr::profile {
inline constexpr std::string_view picture_origin =
    "https://dkr-mod-library-api.ross-macgregorbars.workers.dev/v1/launcher/profile-picture/";
inline bool custom_avatar_url(std::string_view value) {
    if (!value.starts_with(picture_origin) || !value.ends_with(".png"))
        return false;
    const auto hash = value.substr(picture_origin.size(), value.size() - picture_origin.size() - 4);
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(),
                                            [](char c) { return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'); });
}
inline bool avatar_url(std::string_view value) {
    if (value.size() > 180)
        return false;
    if (custom_avatar_url(value))
        return true;
    static const std::regex pattern(
        R"(^https://cdn\.discordapp\.com/(avatars/[0-9]{17,22}/(a_)?[0-9a-f]{32}|embed/avatars/[0-5])\.png\?size=128$)");
    return std::regex_match(value.begin(), value.end(), pattern);
}

inline bool shared_avatar(std::string_view value) {
    return value.empty() || avatar_url(value);
}

// The existing online name field holds 24 UTF-8 bytes, plus its terminator.
inline std::string display_name(std::string_view input) {
    std::string out;
    for (std::size_t at = 0; at < input.size();) {
        const auto lead = static_cast<unsigned char>(input[at]);
        const std::size_t count = lead < 0x80                    ? 1
                                  : lead >= 0xC2 && lead <= 0xDF ? 2
                                  : lead >= 0xE0 && lead <= 0xEF ? 3
                                  : lead >= 0xF0 && lead <= 0xF4 ? 4
                                                                 : 0;
        if (!count || at + count > input.size())
            break;
        bool valid = true;
        for (std::size_t n = 1; n < count; ++n)
            valid &= (static_cast<unsigned char>(input[at + n]) & 0xC0) == 0x80;
        if (count == 3)
            valid &= !(lead == 0xE0 && static_cast<unsigned char>(input[at + 1]) < 0xA0) &&
                     !(lead == 0xED && static_cast<unsigned char>(input[at + 1]) >= 0xA0);
        if (count == 4)
            valid &= !(lead == 0xF0 && static_cast<unsigned char>(input[at + 1]) < 0x90) &&
                     !(lead == 0xF4 && static_cast<unsigned char>(input[at + 1]) >= 0x90);
        if (!valid || out.size() + count > 24)
            break;
        if (lead >= 32 && lead != 127)
            out.append(input.substr(at, count));
        at += count;
    }
    const auto first = out.find_first_not_of(' '), last = out.find_last_not_of(' ');
    return first == out.npos ? std::string{} : out.substr(first, last - first + 1);
}
} // namespace dkr::profile
