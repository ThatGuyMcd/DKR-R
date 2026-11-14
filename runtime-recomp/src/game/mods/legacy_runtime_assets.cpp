#include "legacy_runtime_assets.hpp"

namespace dkr::mods {
namespace {
gpr guest_pointer(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}
void validate_destination(std::span<std::uint8_t> memory, std::uint32_t address, std::size_t length, unsigned section,
                          std::size_t source_offset) {
    const auto segment = address & 0xe0000000U;
    const auto offset = std::size_t(address & 0x1fffffffU);
    // Retail track_spawn_objects places its compressed input at
    // allocation + decoded_size - packed_size + 0x20. That BYTE address need
    // not be 8-byte aligned (e.g. Big Donut / Moon Station). This mounted
    // route is a checked software copy, not a hardware PI transfer.
    if ((segment != 0x80000000U && segment != 0xa0000000U) || memory.size() % 4 || offset > memory.size() ||
        length > memory.size() - offset)
        throw Error("Custom asset destination is outside guest memory (section=" + std::to_string(section) +
                    ", source_offset=" + std::to_string(source_offset) + ", destination=" + std::to_string(address) +
                    ", length=" + std::to_string(length) + ", RDRAM=" + std::to_string(memory.size()) + ").");
}
} // namespace
bool dispatch_asset_api(AssetOperation operation, std::shared_ptr<const AssetBus::Mount> mount,
                        std::span<std::uint8_t> rdram, recomp_context& ctx, const GuestAssetCalls& calls) {
    if (!mount)
        return false;
    const auto& directory = *mount->directory();
    const auto section = static_cast<std::uint32_t>(ctx.r4);
    const auto size = directory.section_size(section); // reject invalid sections before guest writes
    if (operation == AssetOperation::Size) {
        ctx.r2 = size;
        return true;
    }
    if (operation == AssetOperation::Address) {
        ctx.r2 = guest_pointer(mount->address(section, static_cast<std::uint32_t>(ctx.r5)));
        return true;
    }
    if (operation == AssetOperation::CompressedTable)
        throw Error("Whole-section compressed loading has no certified custom-track caller.");
    if (operation != AssetOperation::TableLoad && operation != AssetOperation::PartialLoad &&
        operation != AssetOperation::LoadAtAddress)
        throw Error("Unknown custom asset operation.");
    const auto offset = operation == AssetOperation::PartialLoad ? static_cast<std::uint32_t>(ctx.r6) : 0U;
    const auto length = operation == AssetOperation::PartialLoad ? static_cast<std::uint32_t>(ctx.r7) : size;
    // Track Lab compressed records can have odd lengths. Concatenating them
    // deliberately preserves their exact bytes, so the next record's offset
    // need not satisfy hardware PI alignment. Tagged assets use a bounded
    // software copy; do not round the source or alter its lookup table.
    if (offset > size || length > size - offset)
        throw Error("Custom asset read is outside its immutable section (section=" + std::to_string(section) +
                    ", source_offset=" + std::to_string(offset) + ", length=" + std::to_string(length) +
                    ", section_size=" + std::to_string(size) + ").");
    if (!length) {
        ctx.r2 = 0;
        return true;
    }
    if (!calls.copy)
        throw Error("The revision's serialized guest DMA entry is unavailable.");
    const auto address = mount->address(section, offset);
    auto call = ctx;
    std::uint32_t destination = static_cast<std::uint32_t>(ctx.r5);
    const bool owns_allocation = operation == AssetOperation::TableLoad;
    if (owns_allocation) {
        if (!calls.allocate || !calls.release)
            throw Error("Guest asset heap callbacks are unavailable.");
        call.r4 = length;
        call.r5 = 0x7f7f7fff; // retail COLOUR_TAG_GREY
        calls.allocate(rdram.data(), &call);
        destination = static_cast<std::uint32_t>(call.r2);
        if (!destination) {
            ctx.r2 = 0;
            return true;
        }
    }
    validate_destination(rdram, destination, length, section, offset);
    call = ctx;
    call.r4 = guest_pointer(address);
    call.r5 = guest_pointer(destination);
    call.r6 = length;
    try {
        calls.copy(rdram.data(), &call);
    } catch (const Error&) {
        if (owns_allocation) {
            auto release = ctx;
            release.r4 = guest_pointer(destination);
            calls.release(rdram.data(), &release);
        }
        throw;
    }
    ctx.r2 = owns_allocation ? guest_pointer(destination) : length;
    return true;
}
} // namespace dkr::mods
