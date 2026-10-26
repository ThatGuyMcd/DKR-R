#pragma once
#include "game_payload.hpp"

namespace dkr::runtime::legacy {
class ModWorld;
// Native entrypoints are bound only on the private simulation owner's stack.
// No host address enters guest arguments, checkpoints or network messages.
int draw_owned_vehicle_menu(ModWorld&, std::uint8_t*, recomp_context*, RecompiledEntrypoint frame,
                            RecompiledEntrypoint text_width) noexcept;
} // namespace dkr::runtime::legacy
