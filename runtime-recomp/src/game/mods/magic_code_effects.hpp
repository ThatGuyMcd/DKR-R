#pragma once
#include "magic_code_world.hpp"
namespace dkr::mods::code {
inline constexpr std::size_t MaxEffectJournal = 8 + 128 * (8 + 512);
Bytes encode_effects(const LinkedSet&, const std::vector<Effect>&);
std::vector<Effect> decode_effects(const LinkedSet&, View);
} // namespace dkr::mods::code
