#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dkr::runtime::platform {

enum class InputBackend : std::uint8_t {
    Automatic = 0,
    SDL2Compatibility = 1,
    SDL3Native = 2,
};

constexpr InputBackend sanitise_input_backend(InputBackend backend) {
    return backend == InputBackend::SDL2Compatibility || backend == InputBackend::SDL3Native ? backend
                                                                                             : InputBackend::Automatic;
}

constexpr InputBackend resolve_input_backend(InputBackend requested, bool steam_deck, bool sdl3_host_available) {
    requested = sanitise_input_backend(requested);
    if (!sdl3_host_available) {
        return InputBackend::SDL2Compatibility;
    }
    if (requested == InputBackend::SDL3Native || (requested == InputBackend::Automatic && steam_deck)) {
        return InputBackend::SDL3Native;
    }
    return InputBackend::SDL2Compatibility;
}

constexpr bool input_backend_switch_required(InputBackend requested, InputBackend active, bool steam_deck,
                                             bool sdl3_host_available) {
    return resolve_input_backend(requested, steam_deck, sdl3_host_available) != active;
}

inline std::string canonical_controller_key(std::string_view key) {
    constexpr std::string_view kSdl3Prefix = "sdl3-";
    constexpr std::size_t kHashCharacters = 16U;
    if (key.size() == kSdl3Prefix.size() + kHashCharacters && key.starts_with(kSdl3Prefix) &&
        std::all_of(key.begin() + static_cast<std::ptrdiff_t>(kSdl3Prefix.size()), key.end(),
                    [](unsigned char character) { return std::isxdigit(character) != 0; })) {
        key.remove_prefix(kSdl3Prefix.size());
    }
    return std::string(key);
}

} // namespace dkr::runtime::platform
