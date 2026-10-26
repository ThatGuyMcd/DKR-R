#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "revision_addresses.hpp"
#include "render_heap_addresses.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {
constexpr std::uint32_t RetailEnd = 0x80400000U, ExpansionEnd = 0x807E0000U;
constexpr unsigned SlotBytes = 20;
bool valid(std::uint32_t at, std::uint32_t bytes) {
    return at >= 0x80000400U && at % 4 == 0 && bytes <= 0x800000U && at <= 0x80800000U - bytes;
}
gpr guest(std::uint32_t at) {
    return static_cast<gpr>(static_cast<std::int32_t>(at));
}
} // namespace

// A code mod can legitimately add a second viewport after the outer level
// loader chose a one-player heap. Keep this scene-local admission inside the
// existing 8 MiB guest RAM. No host allocation, save routing or GPU completion.
extern "C" void dkr_render_heap_reserve(std::uint8_t* rdram, recomp_context*) {
    using namespace dkr::runtime;
    const bool v80 = revision_addresses::CurrentHud == revision_addresses::kUsV80.CurrentHud;
    const auto pool = v80 ? render_heap_addresses::Pool80 : render_heap_addresses::Pool77;
    const auto read = [&](std::uint32_t at) { return std::uint32_t(MEM_W(0, guest(at))); };
    const auto write = [&](std::uint32_t at, std::uint32_t value) { MEM_W(0, guest(at)) = value; };
    const auto maximum = read(pool), used = read(pool + 4), slots = read(pool + 8);
    if (!maximum || maximum > 1600 || !used || used >= maximum || !valid(slots, maximum * SlotBytes))
        return;
    std::array<bool, 1600> seen{};
    const auto slot = [&](unsigned i) { return slots + i * SlotBytes; };
    unsigned tail = 0, visited = 0;
    std::uint32_t prior_end = 0;
    for (;;) {
        if (tail >= maximum || seen[tail] || ++visited > used)
            return;
        seen[tail] = true;
        const auto at = slot(tail), data = read(at), size = read(at + 4);
        if (!valid(data, size) || data < prior_end)
            return;
        prior_end = data + size;
        const auto next = static_cast<std::int16_t>(MEM_H(12, guest(at)));
        if (next == -1)
            break;
        if (next < 0 || unsigned(next) >= maximum)
            return;
        tail = unsigned(next);
    }
    const auto end = prior_end;
    if (visited != used || end < RetailEnd || end >= RetailEnd + 16)
        return;
    const auto extra = ExpansionEnd - end;
    if (MEM_H(8, guest(slot(tail))) == 0) {
        write(slot(tail) + 4, read(slot(tail) + 4) + extra);
    } else {
        if (used + 1 >= maximum)
            return;
        const auto spare = static_cast<std::int16_t>(MEM_H(14, guest(slot(used))));
        if (spare < 0 || unsigned(spare) >= maximum || seen[unsigned(spare)])
            return;
        const auto at = slot(unsigned(spare));
        write(at, end);
        write(at + 4, extra);
        MEM_H(8, guest(at)) = 0;
        MEM_H(10, guest(at)) = static_cast<std::int16_t>(tail);
        MEM_H(12, guest(at)) = -1;
        write(at + 16, 0);
        MEM_H(12, guest(slot(tail))) = spare;
        write(pool + 4, used + 1);
    }
    write(pool + 12, read(pool + 12) + extra);
    static std::atomic<unsigned> reports{0};
    if (reports.fetch_add(1) < 8)
        std::fprintf(
            stderr,
            "[render][heap-headroom] added=%u bytes for mod-expanded viewports; existing 8 MiB guest RAM; 128 KiB upper guard retained\n",
            extra);
}
