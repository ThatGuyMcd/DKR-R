#pragma once
#include "legacy_runtime_io.hpp"

namespace dkr::mods {
enum class AssetOperation : unsigned { TableLoad, PartialLoad, Address, Size, LoadAtAddress, CompressedTable };
struct GuestAssetCalls {
    OriginalPiHandler allocate = nullptr, release = nullptr, copy = nullptr;
};
// Called at the verified retail function entry, before it changes registers or
// the guest stack. A null mount leaves every instruction of the stock path in
// place. A mounted route borrows one immutable owner for the entire operation.
// copy must be the revision's public dmacopy wrapper (preserving v80's mutex),
// routed through the checked PI bridge. No synchronous fake message enqueue.
bool dispatch_asset_api(AssetOperation operation, std::shared_ptr<const AssetBus::Mount> mount,
                        std::span<std::uint8_t> rdram, recomp_context& context, const GuestAssetCalls& calls);
} // namespace dkr::mods
