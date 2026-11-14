#pragma once
#include "legacy_asset_io.hpp"
#include "recomp.h"

namespace dkr::mods {
using OriginalPiHandler = void (*)(std::uint8_t*, recomp_context*);
// The thin recomp ABI boundary used by the checked Patch Pipeline calls.
// A production caller must contain virtual-content failures at its session
// boundary; returning a fake success would make DKR block waiting for DMA.
// This function has no UI, global context, scene selection or ROM mutation.
void dispatch_pi_dma(const AssetBus* bus, std::span<std::uint8_t> rdram, recomp_context& context,
                     OriginalPiHandler original, PiCompletion completion);
} // namespace dkr::mods
