#include "legacy_asset_io.hpp"

namespace dkr::mods {
namespace {
std::size_t guest_range(std::span<std::uint8_t> rdram, std::uint32_t address, std::size_t length) {
    const auto segment = address & 0xe0000000U;
    const auto physical = std::size_t(address & 0x1fffffffU);
    if ((segment != 0 && segment != 0x80000000U && segment != 0xa0000000U) || rdram.size() % 4 ||
        physical > rdram.size() || length > rdram.size() - physical)
        throw Error("Virtual PI DMA references an invalid guest range.");
    return physical;
}
std::uint32_t word(std::span<std::uint8_t> rdram, std::uint32_t address) {
    const auto physical = guest_range(rdram, address, 4);
    if (physical % 4)
        throw Error("Virtual PI DMA references an unaligned guest word.");
    return std::uint32_t(rdram[physical ^ 3]) << 24 | std::uint32_t(rdram[(physical + 1) ^ 3]) << 16 |
           std::uint32_t(rdram[(physical + 2) ^ 3]) << 8 | rdram[(physical + 3) ^ 3];
}
} // namespace
bool intercept_virtual_pi_dma(const AssetBus& bus, std::span<std::uint8_t> rdram, PiDmaCall call,
                              PiCompletion completion) {
    if (!AssetBus::is_virtual(call.device_address))
        return false;
    if (call.direction != 0)
        throw Error("Legacy content is read-only; virtual PI writes are forbidden.");
    if (!completion.enqueue)
        throw Error("Virtual PI DMA has no completion dispatcher.");
    const auto stack_start = guest_range(rdram, call.stack_pointer, 0x1c);
    const auto destination = word(rdram, call.stack_pointer + 0x10);
    const auto length = word(rdram, call.stack_pointer + 0x14);
    const auto queue = word(rdram, call.stack_pointer + 0x18);
    // Only tagged immutable assets use this bridge. Both compressed record
    // sources and object-map scratch destinations can be byte-aligned.
    // copy_to_guest maps every byte lane and validates the entire range.
    // Real cartridge DMA retains its untouched upstream hardware checks.
    // Both actual DKR call sites use KSEG0 queues/buffers. The runtime queue
    // dispatcher consumes these as guest pointers, not normalized physical
    // offsets; accepting arbitrary aliases here would bypass its pointer ABI.
    if ((queue & 0xe0000000U) != 0x80000000U || queue % 4)
        throw Error("Virtual PI DMA has an invalid completion queue.");
    const auto queue_start = guest_range(rdram, queue, 0x18);
    // These are immutable queue configuration fields, not validCount/first,
    // which the runtime scheduler may update concurrently.
    const auto count = word(rdram, queue + 0x10), messages = word(rdram, queue + 0x14);
    if (!count || count > 0x100000 || (messages & 0xe0000000U) != 0x80000000U || messages % 4)
        throw Error("Virtual PI DMA has an invalid completion buffer.");
    const auto message_bytes = std::size_t(count) * 4;
    const auto message_start = guest_range(rdram, messages, message_bytes);
    const auto destination_start = guest_range(rdram, destination, length);
    const auto overlaps = [&](std::size_t start, std::size_t size) {
        return length && destination_start < start + size && start < destination_start + length;
    };
    if (overlaps(queue_start, 0x18) || overlaps(message_start, message_bytes) || overlaps(stack_start + 0x10, 12))
        throw Error("Virtual PI DMA would overwrite its own arguments or completion queue.");
    const auto read = bus.resolve(call.device_address, length);
    if (!read)
        throw Error("Virtual PI DMA did not resolve to a content bank.");
    read->copy_to_guest(rdram, destination);
    // No bus lock survives resolve(). A completion may immediately wake a
    // guest thread that performs another asset lookup without deadlocking.
    completion.enqueue(completion.state, queue);
    return true;
}
} // namespace dkr::mods
