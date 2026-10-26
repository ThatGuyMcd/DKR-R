#pragma once
#include <cstdint>

namespace dkr::mods::online {
// v3 binds the prepared runtime-resource identity into the Verified proof.
// Included in lobby identity so older peers are rejected before transfer.
// v4 adds explicitly consented executable packages and frozen code config.
inline constexpr std::uint8_t WireVersion = 4;
} // namespace dkr::mods::online
