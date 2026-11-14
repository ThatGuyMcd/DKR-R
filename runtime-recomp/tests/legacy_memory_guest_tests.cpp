// Headless failure-injection harness for ACTUAL fresh Patch Pipeline output.
// No renderer, audio device, network session, launcher or user profile.
extern "C" {
#include "recomp.h"
void mempool_init_main(uint8_t*, recomp_context*);
void mempool_alloc(uint8_t*, recomp_context*);
void mempool_alloc_safe(uint8_t*, recomp_context*);
void mempool_free(uint8_t*, recomp_context*);
void mempool_free_queue_clear(uint8_t*, recomp_context*);
void mempool_free_timer(uint8_t*, recomp_context*);
void transition_begin(uint8_t*, recomp_context*);
void transition_update(uint8_t*, recomp_context*);
void transition_end(uint8_t*, recomp_context*);
}
#include "../src/game/mods/legacy_memory_safety.h"
#include <vector>
#include <stdexcept>
#include <iostream>
#include <bit>
#include <chrono>
#include <string>
#include <algorithm>
static std::vector<uint8_t> memory(8U * 1024U * 1024U);
static dkr_memory_view g{memory.data(), memory.size(), DKR_MEMORY_TEST_REVISION};
static unsigned checks = 0, fallbacks = 0, slots = 1600;
static bool wide_registers = false;
static void check(bool v) {
    ++checks;
    if (!v)
        throw std::runtime_error("Guest memory assertion " + std::to_string(checks));
}
static recomp_context context() {
    recomp_context c{};
    c.r29 = (int32_t)0x807ff000U;
    c.r31 = 0x12345678;
    c.f_odd = &c.f0.u32h;
    return c;
}
extern "C" void interrupts_disable(uint8_t*, recomp_context* c) {
    c->r2 = 1;
}
extern "C" void interrupts_enable(uint8_t*, recomp_context*) {}
extern "C" void dkr_legacy_heap_capacity(uint8_t*, recomp_context* c) {
    c->r15 = (int32_t)0x80800000U;
}
extern "C" void stack_pointer(uint8_t*, recomp_context*) {
    throw std::runtime_error("Unexpected retail diagnostic");
}
extern "C" void dump_memory_to_cpak(uint8_t*, recomp_context*) {
    throw std::runtime_error("Unexpected retail diagnostic");
}
extern "C" void sins_f(uint8_t*, recomp_context* c) {
    c->f0.fl = 0;
}
extern "C" void coss_f(uint8_t*, recomp_context* c) {
    c->f0.fl = 1;
}
extern "C" void switch_error(const char*, uint32_t, uint32_t) {
    throw std::runtime_error("Invalid test jump table");
}
extern "C" void do_break(uint32_t) {
    throw std::runtime_error("Unexpected guest break");
}
extern "C" uint32_t dkr_legacy_memory_service(uint8_t*, recomp_context*, unsigned op, unsigned rev, uint32_t a,
                                              uint32_t b) {
    check(rev == DKR_MEMORY_TEST_REVISION);
    uint32_t v = 0;
    switch (op) {
    case 0:
        for (unsigned i = 0; i < 32; i += 4)
            dkr_memory_write(g, dkr_memory_descriptor(g) + i, 0, 4);
        dkr_memory_write(g, dkr_memory_count_address(g), 0, 4);
        return slots;
    case 1:
        v = dkr_memory_pool(g, 0);
        if (v != DKR_MEMORY_ERROR)
            v = dkr_memory_reserve_bytes(v);
        break;
    case 2:
        v = !a || dkr_memory_install(g, a, slots) ? 0 : DKR_MEMORY_ERROR;
        break;
    case 3:
        v = dkr_memory_queue(g);
        break;
    case 4:
        v = dkr_memory_enqueue(g, a);
        break;
    case 5:
        v = dkr_memory_acquire(g, a);
        break;
    case 6:
        v = dkr_memory_release(g, a);
        break;
    case 7:
        v = dkr_memory_pool(g, a);
        break;
    case 8:
        v = dkr_memory_mutation(g, a, b) ? 0 : DKR_MEMORY_ERROR;
        break;
    case 9:
        ++fallbacks;
        return 0;
    default:
        v = DKR_MEMORY_ERROR;
        break;
    }
    if (v == DKR_MEMORY_ERROR)
        throw std::runtime_error("Controlled guest memory stop op=" + std::to_string(op) + " a=" + std::to_string(a) +
                                 " b=" + std::to_string(b));
    return v;
}
static void run(void (*fn)(uint8_t*, recomp_context*), uint32_t a = 0, uint32_t b = 0) {
    auto c = context();
    c.mips3_float_mode = wide_registers;
    c.f_odd = wide_registers ? &c.f1.u32l : &c.f0.u32h;
    c.r4 = (int32_t)a;
    c.r5 = (int32_t)b;
    c.r16 = 16;
    c.r17 = 17;
    c.r18 = 18;
    c.r19 = 19;
    c.r20 = 20;
    c.r21 = 21;
    c.f20.u32l = 0x11223344;
    c.f_odd[40] = 0x55667788;
    fn(memory.data(), &c);
    check((uint32_t)c.r29 == 0x807ff000U && c.r31 == 0x12345678);
    check(c.r16 == 16 && c.r17 == 17 && c.r18 == 18 && c.r19 == 19 && c.r20 == 20 && c.r21 == 21);
    check(c.f20.u32l == 0x11223344 && c.f_odd[40] == 0x55667788);
}
static uint32_t alloc(unsigned bytes) {
    auto c = context();
    c.f_odd = &c.f0.u32h;
    c.r4 = bytes;
    c.r5 = 0x11223344;
    mempool_alloc(memory.data(), &c);
    return (uint32_t)c.r2;
}
int main() {
    try {
        const auto began = std::chrono::steady_clock::now();
        for (unsigned capacity : {1600U, 4096U, 8192U}) {
            slots = capacity;
            std::fill(memory.begin(), memory.end(), 0);
            run(mempool_init_main);
            const auto initial = dkr_memory_measure(g);
            check(initial.valid && initial.max_slots == capacity);
            const auto count = dkr_memory_count_address(g), desc = dkr_memory_descriptor(g), q = dkr_memory_queue(g);
            check(q != desc && dkr_memory_capacity(g) == capacity);
            // Bulk unload must not overwrite the old 256-entry boundary or free early.
            std::vector<uint32_t> blocks;
            for (unsigned i = 0; i < 500; ++i) {
                auto p = alloc(32);
                check(p != 0);
                blocks.push_back(p);
            }
            for (auto p : blocks)
                run(mempool_free, p);
            check(dkr_memory_read(g, count, 4) == 500);
            run(mempool_free_queue_clear);
            check(dkr_memory_read(g, count, 4) == 500);
            run(mempool_free_queue_clear);
            check(!dkr_memory_read(g, count, 4));
            auto after = dkr_memory_measure(g);
            check(after.valid && after.free_bytes == initial.free_bytes && after.used_slots == initial.used_slots);
            const auto request = 0x807f0000U;
            const auto type = DKR_MEMORY_TEST_REVISION == 77 ? 0x8012a730U : 0x8012acf0U;
            const auto vertices = DKR_MEMORY_TEST_REVISION == 77 ? 0x800e31c0U : 0x800e3750U;
            // Exhaust ordinary free bytes. Reserved geometry must still work.
            const auto last = alloc(after.largest_free);
            check(last != 0 && !alloc(16));
            for (unsigned mode : {1U, 2U, 3U, 4U, 5U})
                for (unsigned flags : {0U, 128U, 192U}) {
                    dkr_memory_write(g, request, mode | flags, 1);
                    dkr_memory_write(g, request + 1, 0x113355, 3);
                    dkr_memory_write(g, request + 4, 4, 2);
                    dkr_memory_write(g, request + 6, 3, 2);
                    run(transition_begin, request);
                    check(dkr_memory_read(g, type, 4) == mode && dkr_memory_read(g, vertices, 4));
                    for (unsigned i = 0; i < 20; ++i) {
                        run(transition_update, 1);
                        run(mempool_free_queue_clear);
                    }
                    check(dkr_memory_read(g, vertices, 4) == 0 && dkr_memory_read(g, desc + 16, 4) == 0);
                }
            // Reserve exhaustion AND ordinary exhaustion -> fade instead of NULL write.
            check(dkr_memory_acquire(g, 928) && dkr_memory_acquire(g, 928) && dkr_memory_acquire(g, 928));
            for (bool wide : {false, true})
                for (unsigned mode : {1U, 2U, 3U, 4U, 5U}) {
                    wide_registers = wide;
                    dkr_memory_write(g, request, mode | 128U, 1);
                    dkr_memory_write(g, request + 4, 4, 2);
                    dkr_memory_write(g, request + 6, 3, 2);
                    const auto before = fallbacks;
                    run(transition_begin, request);
                    check(fallbacks == before + 1 && !dkr_memory_read(g, type, 4) && !dkr_memory_read(g, vertices, 4));
                    for (unsigned i = 0; i < 20; ++i)
                        run(transition_update, 1);
                }
            wide_registers = false;
            bool stopped = false;
            try {
                run(mempool_alloc_safe, 32);
            } catch (const std::runtime_error& e) {
                stopped = std::string(e.what()).find("op=10") != std::string::npos;
            }
            check(stopped);
            run(mempool_free_timer, 0);
            run(mempool_free, last);
            // Thousands of alternating scene-like allocations, delayed releases and
            // transitions must return to EXACTLY the same pool byte/slot ownership.
            const auto reserve = dkr_memory_read(g, desc + 12, 4);
            for (unsigned i = 0; i < 3; ++i)
                check(dkr_memory_release(g, reserve + i * 7712) == 1);
            run(mempool_free_timer, 2);
            for (unsigned race = 0; race < 5000; ++race) {
                const auto a = alloc(256 + (race % 8) * 16), b = alloc(512);
                check(a && b);
                run(mempool_free, a);
                run(mempool_free, b);
                run(mempool_free_queue_clear);
                run(mempool_free_queue_clear);
            }
            after = dkr_memory_measure(g);
            check(after.valid && after.free_bytes == initial.free_bytes && after.used_slots == initial.used_slots);
        }
        std::cout << checks << " actual guest allocator/transition checks, revision " << DKR_MEMORY_TEST_REVISION
                  << ", " << std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count()
                  << "s passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
