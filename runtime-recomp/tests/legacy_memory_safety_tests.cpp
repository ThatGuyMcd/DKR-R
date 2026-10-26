#include "../src/game/mods/legacy_memory_safety.h"
#include <vector>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <string>
static unsigned checks;
static void check(bool v) {
    ++checks;
    if (!v)
        throw std::runtime_error("Memory safety check " + std::to_string(checks));
}
int main() {
    try {
        for (unsigned rev : {77U, 80U}) {
            std::vector<uint8_t> memory(8U * 1024 * 1024);
            dkr_memory_view g{memory.data(), memory.size(), rev};
            const auto d = dkr_memory_descriptor(g), count = dkr_memory_count_address(g), p = dkr_memory_pools(g);
            dkr_memory_write(g, count + 4, 2, 4);
            check(dkr_memory_queue(g) == d);
            for (unsigned i = 0; i < 256; ++i)
                check(dkr_memory_enqueue(g, 0x80300000 + i * 16) == 0);
            check(dkr_memory_read(g, count, 4) == 256);
            const auto full = memory;
            check(dkr_memory_enqueue(g, 0x80301000) == DKR_MEMORY_ERROR && memory == full);
            check(dkr_memory_enqueue(g, 0x80300000) == 0 && memory == full);
            dkr_memory_write(g, count, 257, 4);
            check(dkr_memory_queue(g) == DKR_MEMORY_ERROR);
            dkr_memory_write(g, count, 0, 4);
            check(!dkr_memory_install(g, 0x807ff000, 4096));
            check(dkr_memory_install(g, 0x80200000, 4096));
            for (unsigned i = 0; i < 4096; ++i)
                check(dkr_memory_enqueue(g, 0x80300000 + i * 16) == 0);
            check(dkr_memory_read(g, d + 20, 4) == 4096 && dkr_memory_read(g, count, 4) == 4096);
            const auto large = memory;
            check(dkr_memory_enqueue(g, 0x80310000) == DKR_MEMORY_ERROR && memory == large);
            dkr_memory_write(g, count, 0, 4);
            const auto a = dkr_memory_acquire(g, 928), b = dkr_memory_acquire(g, 3488), c = dkr_memory_acquire(g, 7712);
            check(a && b == a + 7712 && c == b + 7712 && !dkr_memory_acquire(g, 928));
            auto saved = memory;
            check(dkr_memory_enqueue(g, a) == 0);
            // Queuing cannot return the reserve to another renderer borrower yet.
            check(!dkr_memory_acquire(g, 928));
            check(dkr_memory_release(g, a) == 1 && dkr_memory_acquire(g, 928) == a);
            memory = saved;
            g.ram = memory.data(); // checkpoint owns reserve busy bits
            check(!dkr_memory_acquire(g, 928));
            check(dkr_memory_release(g, b) == 1 && dkr_memory_release(g, b) == DKR_MEMORY_ERROR);
            check(dkr_memory_release(g, a + 16) == DKR_MEMORY_ERROR);
            check(dkr_memory_release(g, 0x80300000) == 0);
            check(dkr_memory_acquire(g, 7713) == 0);
            for (unsigned n = 0; n < 100000; ++n) {
                check(dkr_memory_acquire(g, 928) == b);
                check(dkr_memory_release(g, b) == 1);
            }
            // A valid split pool: unused descriptors are NOT active linked-list nodes.
            const unsigned base = 0x80140000, max = 1600, end = 0x80400000, start = base + max * 20;
            dkr_memory_write(g, p, max, 4);
            dkr_memory_write(g, p + 4, 2, 4);
            dkr_memory_write(g, p + 8, base, 4);
            dkr_memory_write(g, p + 12, end - base, 4);
            dkr_memory_write(g, p + 64, 0, 4);
            for (unsigned i = 0; i < max; ++i)
                dkr_memory_write(g, base + 20 * i + 14, i, 2);
            const auto slot = [&](unsigned index, unsigned data, unsigned bytes, unsigned flags, unsigned prev,
                                  unsigned next) {
                const unsigned s = base + 20 * index;
                dkr_memory_write(g, s, data, 4);
                dkr_memory_write(g, s + 4, bytes, 4);
                dkr_memory_write(g, s + 8, flags, 2);
                dkr_memory_write(g, s + 10, prev, 2);
                dkr_memory_write(g, s + 12, next, 2);
            };
            slot(0, start, 1024, 1, 65535, 1);
            slot(1, start + 1024, end - start - 1024, 0, 0, 65535);
            check(dkr_memory_pool(g, 0) == max && dkr_memory_mutation(g, 0, 0));
            auto metrics = dkr_memory_measure(g);
            check(metrics.valid && metrics.free_bytes == end - start - 1024 &&
                  metrics.largest_free == metrics.free_bytes);
            dkr_memory_write(g, base + 12, max, 2);
            check(!dkr_memory_mutation(g, 0, 0) && !dkr_memory_measure(g).valid);
            dkr_memory_write(g, base + 12, 0, 2);
            check(!dkr_memory_measure(g).valid);
            dkr_memory_write(g, base + 12, 1, 2);
            dkr_memory_write(g, base + 20 + 4, 0x7fffffff, 4);
            check(!dkr_memory_mutation(g, 0, 0));
            dkr_memory_write(g, p + 4, max, 4);
            check(dkr_memory_pool(g, 0) == DKR_MEMORY_ERROR);
            check(dkr_memory_pool(g, 4) == DKR_MEMORY_ERROR);
        }
        std::cout << checks << " guest allocator/queue/reserve checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
