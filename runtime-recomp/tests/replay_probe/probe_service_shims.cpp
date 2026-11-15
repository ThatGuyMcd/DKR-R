// The probe links no custom-music decoder or mod package reader, so the
// preparation scope that the owned driver opens around course loads is an
// inert marker here. It carries no game state: no stock-content run decodes music.
#include "resident_music.hpp"

namespace dkr::mods::online::resident_music {
PreparationScope::PreparationScope(bool enabled, bool audible) noexcept
    : enabled_(enabled), silent_(enabled && !audible) {}
PreparationScope::~PreparationScope() = default;
} // namespace dkr::mods::online::resident_music

// Development-tool transport of the SDK2 broker. The probe never opens a
// development session, which is exactly the broker's inactive behaviour:
// epochs are ignored, no packet is produced and every receive is rejected.
#include "mods/sdk2_bridge.hpp"

namespace dkr::sdk2 {
void network_epoch(std::uint64_t, unsigned, unsigned) {}
std::vector<std::uint8_t> network_packet(unsigned) {
    return {};
}
bool network_receive(unsigned, std::span<const std::uint8_t>) {
    return false;
}
} // namespace dkr::sdk2
