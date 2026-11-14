#pragma once
#include <cstdint>

namespace dkr::crash_reports {
// Called at normal startup boundaries, never from the fatal handler. Publishes
// immutable module/build identities for crash-time address attribution.
void refresh_native_context(bool log_binary = false);
void print_native_frame(std::uintptr_t pc, unsigned frame);
// Linux uses only pre-published data and async-signal-safe writes here.
void fatal_signal(int signal, void* information, void* context);
} // namespace dkr::crash_reports
