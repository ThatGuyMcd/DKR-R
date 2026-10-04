#pragma once
#include <cstdint>
#include <span>

namespace dkr::runtime::netplay::experimental {
// Local checkpoint integrity only, NOT authentication or permission to restore
// a network-supplied image. The stable mode's checksum/wire format is untouched.
std::uint64_t checkpoint_hash(std::span<const std::uint8_t> bytes);
}
