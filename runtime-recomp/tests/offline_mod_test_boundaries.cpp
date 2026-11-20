#include "recomp.h"
#include <cstddef>
#include <cstdint>

// Offline adapter tests can exercise the frozen/owned no-op gates.
bool dkr_test_frozen_resources = false;
bool dkr_test_owned_bootstrap = false;
bool dkr_experimental_bootstrap_active() {
    return dkr_test_owned_bootstrap;
}
// These tests have no live online owner.
// Actual frozen routing/checkpoint coverage lives in NativeModWorldTests.
namespace dkr::runtime::legacy {
bool frozen_online_resources() {
    return dkr_test_frozen_resources;
}
bool frozen_course_hook(const char*, std::uint8_t*, recomp_context*) {
    return false;
}
bool frozen_music_hook(const char*, std::uint8_t*, recomp_context*) {
    return false;
}
bool frozen_music_render(float*, std::size_t, unsigned) {
    return false;
}
} // namespace dkr::runtime::legacy

// custom_tracks_hooks.cpp asks whether a private replay fixture owns the boot.
// These offline tests never arm one.
bool dkr_private_replay_boot(std::uint8_t*, recomp_context*) {
    return false;
}
