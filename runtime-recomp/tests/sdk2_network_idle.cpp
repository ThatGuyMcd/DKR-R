// The experimental network forwards SDK tools traffic to the broker in
// mods/sdk2_broker.inl, which is part of the game. Host tests that link the
// network without the game get an SDK with no tools traffic. The signatures
// follow mods/sdk2_bridge.hpp, which needs the game's include paths; a
// mismatch fails the link.
#include <cstdint>
#include <span>
#include <vector>

namespace dkr::sdk2 {
void network_epoch(std::uint64_t, unsigned, unsigned) {}
std::vector<std::uint8_t> network_packet(unsigned) { return {}; }
bool network_receive(unsigned, std::span<const std::uint8_t>) { return true; }
} // namespace dkr::sdk2
