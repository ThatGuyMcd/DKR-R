#pragma once
#include <atomic>

namespace dkr::runtime::vehicles::lighting {
// Main-thread service for the existing fresh-profile owned-game diagnostic.
// Normal startup never installs or calls this callback. No background owner,
// network trust decision or gameplay path is changed by the diagnostic.
inline void (*owned_check_pump)() = nullptr;
inline bool owned_car_check = false;
inline bool owned_road_wheel_check = false;
inline std::atomic<unsigned> owned_car_picker{0}; // 0 inactive, 1 needs selection, 2 selected
inline void pump_owned_check() {
    if (owned_check_pump)
        owned_check_pump();
}
} // namespace dkr::runtime::vehicles::lighting
