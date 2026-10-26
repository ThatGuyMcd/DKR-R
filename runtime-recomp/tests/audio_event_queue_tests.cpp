#include "recomp.h"
#include "audio_event_queue.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace {
constexpr uint32_t player = 0x80004000, queue = player + 0x48, destination = player + 0x38;
constexpr uint32_t pool = 0x80005000, globals = 0x80006000;
struct World {
    std::vector<uint8_t> ram = std::vector<uint8_t>(8 * 1024 * 1024);
    dkr_audio_queue_guard guard{};
    unsigned status = 0;
    World() {
        assert(dkr_audio_register_queue(&guard, queue, pool, 120));
        auto* rdram = ram.data();
        dkr_audio_rebuild_pool(rdram, &guard);
        MEM_W(0, dkr_audio_address(globals)) = player;
        MEM_W(0x5C, dkr_audio_address(player)) = 16667;
    }
    uint32_t read(uint32_t at) {
        auto* rdram = ram.data();
        return MEM_W(0, dkr_audio_address(at));
    }
    void write(uint32_t at, uint32_t value) {
        auto* rdram = ram.data();
        MEM_W(0, dkr_audio_address(at)) = value;
    }
    unsigned type() {
        auto* rdram = ram.data();
        return MEM_HU(0, dkr_audio_address(destination));
    }
    void post(uint32_t delta, unsigned type, uint32_t data) {
        auto* rdram = ram.data();
        const uint32_t item = read(queue), next = read(item);
        assert(item);
        write(queue, next);
        if (next)
            write(next + 4, queue);
        uint32_t previous = queue + 8, following = read(queue + 8);
        while (following && delta >= read(following + 8)) {
            delta -= read(following + 8);
            previous = following;
            following = read(following);
        }
        write(item, following);
        write(item + 4, previous);
        write(item + 8, delta);
        for (unsigned b = 12; b < 28; b += 4)
            write(item + b, 0);
        MEM_H(12, dkr_audio_address(item)) = type;
        write(item + 16, data);
        write(previous, item);
        if (following) {
            write(following + 4, item);
            write(following + 8, read(following + 8) - delta);
        }
    }
    uint32_t next() {
        return dkr_audio_queue_next(ram.data(), &guard, destination, globals, nullptr, nullptr, &status);
    }
    void conserved() {
        assert(!read(queue + 4) && !read(queue + 12)); // Native ALLink sentinels, not tails.
        std::array<bool, 120> seen{};
        unsigned count = 0;
        for (unsigned offset : {0U, 8U}) {
            uint32_t item = read(queue + offset), previous = queue + offset;
            while (item) {
                assert(dkr_audio_pool_item(&guard, item));
                const unsigned index = (item - pool) / 28;
                assert(!seen[index] && read(item + 4) == previous);
                seen[index] = true;
                ++count;
                previous = item;
                item = read(item);
            }
        }
        assert(count == 120);
    }
};
void resume(void* user, uint8_t*, uint32_t owner) {
    assert(owner == player);
    static_cast<World*>(user)->post(0, 0, 0x12345678); // Native sequence repost analogue.
}
} // namespace

int main() {
    World world;
    // Warm-checkpoint adoption: every node must be present exactly once.
    world.guard.items = world.guard.item_count = 0;
    dkr_audio_adopt_sequence_pool(world.ram.data(), &world.guard, destination, globals);
    assert(world.guard.items == pool && world.guard.item_count == 120);
    assert(dkr_audio_guard_valid(&world.guard));
    auto invalid_guard = world.guard;
    invalid_guard.immediate_events = 8192;
    assert(!dkr_audio_guard_valid(&invalid_guard));
    invalid_guard = world.guard;
    invalid_guard.items = queue;
    assert(!dkr_audio_guard_valid(&invalid_guard));
    // Actual fixed pool reused by thousands of event handlers. A pending API
    // heartbeat stays queued throughout; capacity is not an event-count limit.
    world.post(16667, 9, 0);
    world.guard.items = world.guard.item_count = 0;
    dkr_audio_adopt_sequence_pool(world.ram.data(), &world.guard, destination, globals);
    assert(world.guard.items == pool && world.guard.item_count == 120);
    for (unsigned count = 0; count < 4096; ++count) {
        world.post(0, 2, count);
        assert(world.next() == 0 && world.status == DKR_AUDIO_QUEUE_OK);
        assert(world.type() == 2 && world.read(destination + 4) == count);
        world.conserved();
    }
    assert(world.next() == 16667 && world.type() == 9);
    world.conserved();
    // Even a budget-exceeding stream yields without losing the popped event
    // or the note-off/STOP/heartbeat waiting behind it.
    world.post(16667, 9, 0);
    world.post(1000, 16, 0);
    for (unsigned count = 0; count < DKR_AUDIO_IMMEDIATE_BUDGET - 1; ++count) {
        world.post(0, 21, count);
        assert(world.next() == 0);
        world.conserved();
    }
    world.post(0, 21, 8191);
    const auto checkpoint = world.ram;
    const auto guard = world.guard;
    assert(world.next() == 16667 && world.status == DKR_AUDIO_QUEUE_YIELDED && world.type() == 21);
    const auto expected = world.ram;
    const auto expected_guard = world.guard;
    world.ram = checkpoint;
    world.guard = guard;
    assert(world.next() == 16667 && world.ram == expected);
    assert(world.guard.immediate_events == expected_guard.immediate_events);
    world.conserved();
    assert(world.next() == 1000 && world.type() == 16);
    assert(world.next() == 15667 && world.type() == 9);
    world.conserved();
    // Idle/stopped native player regains its heartbeat without an arbitrary
    // restart. Repeating recovery never shrinks the event pool.
    for (unsigned count = 0; count < 64; ++count) {
        assert(world.next() == 16667 && world.status == DKR_AUDIO_QUEUE_REPAIRED && world.type() == 9);
        world.conserved();
    }
    world.write(player + 0x2C, 1);
    world.write(player + 0x18, 0x80007000);
    assert(dkr_audio_queue_next(world.ram.data(), &world.guard, destination, globals, resume, &world, &world.status) ==
           0);
    assert(world.status == DKR_AUDIO_QUEUE_REPAIRED && world.type() == 0);
    world.conserved();
    assert(world.next() == 16667 && world.type() == 9);
    // Native STOPPING completion survives a missing final STOP event.
    world.write(player + 0x2C, 2);
    assert(world.next() == 0 && world.type() == 16);
    world.conserved();
    world.write(player + 0x2C, 0);
    assert(world.next() == 16667 && world.type() == 9);
    // Malformed known queue: never follow the bad link. Rebuild only the
    // registered pool and request native voice release, not stream replay.
    world.write(player + 0x2C, 1);
    world.post(10, 5, 0);
    world.write(world.read(queue + 8), 0x80FFFFFC);
    assert(world.next() == 0 && world.type() == 16 && world.read(player + 0x2C) == 2);
    world.conserved();
    world.write(player + 0x2C, 0);
    assert(world.next() == 16667 && world.type() == 9);
    // No owner: corruption is fenced without any RAM writes or lost nodes.
    world.write(globals, 0);
    world.post(0, 2, 0);
    world.write(world.read(queue + 8) + 4, 0x80007700);
    const auto before = world.ram;
    assert(world.next() == 16667 && world.status == DKR_AUDIO_QUEUE_INVALID && world.ram == before);
    for (uint32_t alias : {queue, queue + 8, pool, pool + 4}) {
        assert(dkr_audio_queue_next(world.ram.data(), &world.guard, alias, globals, nullptr, nullptr, &world.status) ==
                   16667 &&
               world.status == DKR_AUDIO_QUEUE_INVALID && world.ram == before);
    }
    dkr_audio_queue_guard invalid{};
    assert(!dkr_audio_register_queue(&invalid, queue, queue, 120));
    assert(!dkr_audio_register_queue(&invalid, queue, pool, 513));
    assert(!dkr_audio_register_queue(&invalid, queue, 0x807FFFF0, 120));
    std::puts(
        "audio queue: dense bursts, bounded preservation, pool recovery, stop/restart and checkpoint checks passed");
}
