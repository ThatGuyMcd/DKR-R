#pragma once
#include "online_mod_manifest.hpp"
#include <algorithm>

namespace dkr::mods::online {
// Extension values preserve the accepted manifest/ABI layouts. Old builds
// reject the new explicit "car" tags, rather than misreading a car as code,
// a ROM patch or a course. No machine-local path is sent to another player.
inline constexpr Kind CarContent = static_cast<Kind>(3);
inline constexpr PayloadKind CarPayload = static_cast<PayloadKind>(3);
inline constexpr unsigned CarAdapterVersion = 2;
inline std::size_t car_count(const Manifest& m) {
    return std::count_if(m.content.begin(), m.content.end(), [](const auto& c) { return c.kind == CarContent; });
}
} // namespace dkr::mods::online
