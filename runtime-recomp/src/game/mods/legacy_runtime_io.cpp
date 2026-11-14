#include "legacy_runtime_io.hpp"

namespace dkr::mods {
void dispatch_pi_dma(const AssetBus* bus, std::span<std::uint8_t> rdram, recomp_context& context,
                     OriginalPiHandler original, PiCompletion completion) {
    const auto device = static_cast<std::uint32_t>(context.r7);
    if (!AssetBus::is_virtual(device)) {
        // Do not inspect/normalise guest arguments or touch scheduler state
        // on the stock path. It retains the runtime's original PI semantics.
        if (!original)
            throw Error("The stock PI handler is unavailable.");
        original(rdram.data(), &context);
        return;
    }
    if (!bus)
        throw Error("Virtual PI DMA has no active legacy-content bus.");
    intercept_virtual_pi_dma(*bus, rdram,
                             {static_cast<std::uint32_t>(context.r6), device, static_cast<std::uint32_t>(context.r29)},
                             completion);
    context.r2 = 0;
}
} // namespace dkr::mods
