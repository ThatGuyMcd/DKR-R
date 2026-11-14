#pragma once
#include "magic_code_world.hpp"
#include "../netplay/experimental_guest_memory.h"

namespace dkr::mods::code {
// Capture only native callback arguments from the reviewed online preview
// stack. The VM never receives a permission region for this scratch memory.
// Nested package calls use their own VM stack, not this native-entry snapshot.
inline std::optional<World::ArgumentStack> owned_preview_arguments(std::span<const std::uint8_t> ram, bool online,
                                                                   const Registers& registers) {
    const auto stack = std::uint32_t(registers.integer[29]);
    if (!online || stack < DKR_OWNED_PREVIEW_STACK_BEGIN || stack > DKR_OWNED_PREVIEW_STACK_TOP)
        return {};
    static_assert(std::tuple_size_v<World::ArgumentStack> == DKR_OWNED_O32_ARGUMENT_WORDS);
    const auto offset = std::size_t(stack - 0x80000000U);
    if ((stack & 3U) || ram.size() % 4 || offset > ram.size() || sizeof(World::ArgumentStack) > ram.size() - offset)
        throw Error("Custom Magic Code: online preview arguments are outside their live native stack.");
    World::ArgumentStack words{};
    for (unsigned i = 0; i < words.size(); ++i)
        for (unsigned byte = 0; byte < 4; ++byte)
            words[i] = words[i] << 8 | ram[(offset + i * 4 + byte) ^ 3U];
    return words;
}
} // namespace dkr::mods::code
