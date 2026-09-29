#pragma once
#include <atomic>
namespace dkr::runtime::render_power {
// Dummy GPU submissions are a driver workaround, not required game work.
// Retain an explicit opt-in for hardware that downclocks too aggressively.
inline std::atomic<bool> gpu_keep_awake{false};
}
