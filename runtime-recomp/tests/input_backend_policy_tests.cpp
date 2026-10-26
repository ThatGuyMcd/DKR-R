#include "input_backend_policy.hpp"

#include <cassert>

int main() {
    using namespace dkr::runtime::platform;

    assert(resolve_input_backend(InputBackend::Automatic, true, true) == InputBackend::SDL3Native);
    assert(resolve_input_backend(InputBackend::Automatic, false, true) == InputBackend::SDL2Compatibility);
    assert(resolve_input_backend(InputBackend::SDL3Native, false, true) == InputBackend::SDL3Native);
    assert(resolve_input_backend(InputBackend::SDL3Native, true, false) == InputBackend::SDL2Compatibility);

    assert(input_backend_switch_required(InputBackend::SDL3Native, InputBackend::SDL2Compatibility, false, true));
    assert(!input_backend_switch_required(InputBackend::Automatic, InputBackend::SDL3Native, true, true));
    assert(!input_backend_switch_required(InputBackend::Automatic, InputBackend::SDL2Compatibility, false, true));

    assert(canonical_controller_key("4f32cfa158a8d935") == "4f32cfa158a8d935");
    assert(canonical_controller_key("sdl3-4f32cfa158a8d935") == "4f32cfa158a8d935");
    assert(canonical_controller_key("sdl3-not-a-hash") == "sdl3-not-a-hash");
    return 0;
}
