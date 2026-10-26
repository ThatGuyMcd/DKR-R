#pragma once
#include "legacy_asset_bus.hpp"

namespace dkr::mods {
// The o32 osPiStartDma boundary used by both DKR revisions. The production
// bridge supplies these registers and enqueues a normal Pi-source completion;
// it must not call osRecvMesg, invoke a guest function or hold a network lock.
struct PiDmaCall {
    std::uint32_t direction = 0, device_address = 0, stack_pointer = 0;
};
struct PiCompletion {
    void* state = nullptr;
    void (*enqueue)(void*, std::uint32_t queue) = nullptr;
};
// false = original cartridge call, with no guest access and no completion.
// true = bytes copied, exactly one completion dispatched; bridge returns v0=0.
// Error = invalid virtual read; bridge MUST stop the pending scene, not fall
// through to librecomp (which masks these addresses into cartridge offsets).
bool intercept_virtual_pi_dma(const AssetBus& bus, std::span<std::uint8_t> rdram, PiDmaCall call,
                              PiCompletion completion);
} // namespace dkr::mods
