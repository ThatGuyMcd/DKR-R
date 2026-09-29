#pragma once
#include <android/native_window.h>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <dlfcn.h>
#include "surface_registry.hpp"

namespace dkr::runtime::android_surface {
using Registry = SurfaceRegistry<ANativeWindow, ANativeWindow_acquire, ANativeWindow_release>;
inline Registry registry;
inline std::atomic<int> target_rate{60};
inline void request_rate(ANativeWindow* native, int rate) {
    // API 30+ only, dynamically resolved for API 28/29 compatibility. This is
    // a display-rate hint, not a second frame limiter or forced display mode.
    using SetRate = int (*)(ANativeWindow*, float, int8_t);
    static auto setRate = reinterpret_cast<SetRate>(dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate"));
    if (setRate && native) setRate(native, static_cast<float>(rate), 0 /* DEFAULT compatibility */);
}
// Android callbacks publish availability; the Vulkan owner alone rebuilds GPU
// objects. Each reader owns a native-window reference across callback races.
inline void publish(ANativeWindow* next) {
    registry.publish(next);
}
struct Lease : Registry::Lease {
    Lease() : Registry::Lease(registry) {}
};
inline bool matches(std::uint64_t expected) {
    return registry.matches(expected);
}
}
