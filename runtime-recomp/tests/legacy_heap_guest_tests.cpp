// Execute retail allocator bookkeeping above/below the original 4 MiB bound.
#include "legacy_character_menu.hpp"
#include "legacy_heap_policy.hpp"
#include "recomp.h"
#include <algorithm>
#include <iostream>
using namespace dkr::mods;
extern "C" void mempool_init_main(std::uint8_t*, recomp_context*);
extern "C" void mempool_alloc(std::uint8_t*, recomp_context*);
extern "C" void mempool_free_addr(std::uint8_t*, recomp_context*);
namespace {
Bytes memory(8 * MiB);
bool custom = false;
unsigned checks = 0;
void check(bool b, const char* why) {
    ++checks;
    if (!b)
        throw Error(why);
}
recomp_context context() {
    recomp_context c{};
    c.r29 = std::int32_t(0x800ff000U);
    return c;
}
} // namespace
extern "C" void dkr_legacy_heap_capacity(std::uint8_t*, recomp_context* c) {
    c->r15 = std::int32_t(legacy_heap_end(unsigned(c->r15), memory.size(), custom));
}
extern "C" void interrupts_disable(std::uint8_t*, recomp_context* c) {
    c->r2 = 0;
}
extern "C" void interrupts_enable(std::uint8_t*, recomp_context*) {}
extern "C" void stubbed_printf(std::uint8_t*, recomp_context*) {}
extern "C" void do_break(std::uint32_t) {
    throw Error("Native allocator break");
}
int main() {
    try {
        for (bool enabled : {false, true}) {
            custom = enabled;
            std::fill(memory.begin(), memory.end(), 0);
            auto c = context();
            mempool_init_main(memory.data(), &c);
            std::vector<std::pair<unsigned, unsigned>> regions;
            unsigned total = 0;
            for (unsigned i = 0; i < 64; ++i) {
                c = context();
                c.r4 = 256 * 1024;
                c.r5 = 0x7f7f7fff;
                mempool_alloc(memory.data(), &c);
                const unsigned p = unsigned(c.r2);
                if (!p)
                    break;
                const unsigned offset = p & 0x1fffffffU;
                check(p >= 0x80100000U && offset + 256 * 1024 <= (custom ? 8 * MiB : 4 * MiB),
                      "Allocator escaped admitted RDRAM");
                check(p % 16 == 0, "Allocator lost alignment");
                for (auto [old, size] : regions)
                    check(p >= old + size || p + 256 * 1024 <= old, "Native allocations overlap");
                regions.emplace_back(p, 256 * 1024);
                total += 256 * 1024;
                std::fill(memory.begin() + offset, memory.begin() + offset + 256 * 1024, std::uint8_t(i + 1));
            }
            check(total > (custom ? 6 * MiB : 2 * MiB) && total < (custom ? 7 * MiB : 3 * MiB),
                  "Unexpected usable heap capacity");
            for (unsigned i = 0; i < regions.size(); ++i) {
                auto [p, size] = regions[i];
                unsigned offset = p & 0x1fffffffU;
                check(std::all_of(memory.begin() + offset, memory.begin() + offset + size,
                                  [&](auto b) { return b == i + 1; }),
                      "Allocator corrupted a live allocation");
            }
            for (auto [p, size] : regions) {
                c = context();
                c.r4 = std::int32_t(p);
                mempool_free_addr(memory.data(), &c);
            }
            c = context();
            c.r4 = total;
            c.r5 = 0x7f7f7fff;
            mempool_alloc(memory.data(), &c);
            check(unsigned(c.r2) == regions.front().first, "Released heap failed to coalesce and reload");
        }
        std::cout << checks << " native heap boundary/ownership checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
