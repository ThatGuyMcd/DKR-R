#pragma once
// Implemented by the checked Patch Pipeline copies. Never reset a scheduler
// while any native guest still owns its RDRAM or an asset-read lease.
namespace ultramodern {
void cancel_native_guest_threads();
void reset_native_guest_queue();
} // namespace ultramodern
