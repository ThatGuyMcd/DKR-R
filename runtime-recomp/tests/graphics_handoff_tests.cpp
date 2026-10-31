#include "producer_snapshot.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace dkr::runtime::graphics_handoff;
namespace {
unsigned checks = 0;
void check(bool ok, const char* why) {
    ++checks;
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main() {
    try {
        Mailbox<0x10000> q;
        std::vector<std::uint8_t> live(0x10000, 0x11);
        // Model the actual two-queue ordering: completed valid list -> game
        // scheduler message -> asset reuse -> RSP submission -> host render queue.
        // The old submission-only capture sees the logged invalid indices.
        constexpr unsigned payload = 0x3000;
        live[(payload + 1) ^ 3U] = 1;
        live[(payload + 2) ^ 3U] = 2;
        live[(payload + 3) ^ 3U] = 3;
        check(q.capture(live.data(), 0x80001000, 0x80001100), "Producer capture failed");
        live[(payload + 1) ^ 3U] = 64;
        live[(payload + 2) ^ 3U] = 65;
        auto old = q.acquire();
        std::memcpy(old.get(), live.data(), live.size());
        check(old[(payload + 1) ^ 3U] == 64 && old[(payload + 2) ^ 3U] == 65,
              "Before control did not reproduce late snapshot");
        auto held = q.take(live.data(), 0x80001000, 32);
        check(held && held[(payload + 1) ^ 3U] == 1 && held[(payload + 2) ^ 3U] == 2,
              "Scheduler delay exposed reused payload");
        check(held.get() != old.get(), "Concurrent owners share memory");
        check(q.metrics().transferred == 1 && q.metrics().fallback == 0, "Protected path performed a second capture");
        check(!q.take(live.data(), 0x80001000, 32), "Consumed image reused");
        check(!q.capture(nullptr, 0x1000, 0x1100), "Null producer accepted");
        check(!q.capture(live.data(), 0x80001001, 0x80001100), "Unaligned list accepted");
        check(!q.capture(live.data(), 0xA0001000, 0xA0001100), "Unadmitted segment accepted");
        check(!q.capture(live.data(), 0x80001100, 0x80001000), "Reversed list accepted");
        check(!q.capture(live.data(), 0x1000, 0x1000), "Empty list accepted");
        check(!q.capture(live.data(), 0x1000, 0x10008), "Out-of-range end accepted");
        q.reset();
        live[0x3000] = 0x22;
        check(q.capture(live.data(), 0x1000, 0x1100), "Physical root rejected");
        check(!q.take(live.data(), 0x80001000, 31), "Wrong size matched");
        check(!q.take(live.data(), 0x1000, 32), "Mismatch left a stale candidate");
        check(!q.capture(live.data(), 0x1000, 0x1100), "Mismatch did not disable ambiguous matching");
        q.reset();
        q.capture(live.data(), 0x1000, 0x1100);
        std::vector<std::uint8_t> other(live.size());
        check(!q.take(other.data(), 0x1000, 32), "Different runtime owner matched");
        q.reset();
        q.capture(live.data(), 0x1000, 0x1100);
        q.capture(live.data(), 0x2000, 0x2100);
        check(!q.take(live.data(), 0x2000, 32), "Out-of-order consume searched a later address");
        check(!q.take(live.data(), 0x1000, 32), "Out-of-order consume retained stale work");
        q.reset();
        // Duplicate roots are different submissions; keep FIFO image ownership.
        live[0x3000] = 0x33;
        q.capture(live.data(), 0x1000, 0x1100);
        live[0x3000] = 0x44;
        q.capture(live.data(), 0x1000, 0x1100);
        auto a = q.take(live.data(), 0x1000, 32), b = q.take(live.data(), 0x1000, 32);
        check(a && b && a[0x3000] == 0x33 && b[0x3000] == 0x44, "Recycled root mixed frame order");
        check(held[(payload + 1) ^ 3U] == 1, "Later frames mutated active lease");
        q.capture(live.data(), 0x1000, 0x1100);
        auto m = q.reset();
        check(m.transferred == 2, "Transfer accounting changed");
        check(!q.take(live.data(), 0x1000, 32), "Session reset retained old image");
        check(held[(payload + 1) ^ 3U] == 1, "Session reset freed active renderer image");
        // More than four pending entries disables early capture for this session.
        // Even when two recycled roots collide, the latest image cannot match an
        // older in-flight task. Session reset is the only safe way to rearm it.
        for (unsigned i = 0; i < 5; ++i)
            q.capture(live.data(), 0x1000 + (i & 1) * 0x100, 0x1100 + (i & 1) * 0x100);
        check(q.metrics().overflow == 1, "Pending metadata not bounded");
        check(!q.take(live.data(), 0x1000, 32), "Overflow handed new image to old task");
        check(!q.take(live.data(), 0x1400, 32), "Overflow mismatch left stale latest image");
        check(!q.capture(live.data(), 0x1000, 0x1100), "Overflow rearmed before session reset");
        q.reset();
        q.capture(live.data(), 0x1000, 0x1100);
        {
            auto suppressed = q.take(live.data(), 0x1000, 32);
            check(bool(suppressed), "Suppressed frame was not retired");
        }
        live[0x3000] = 0x55;
        q.capture(live.data(), 0x1000, 0x1100);
        auto after_suppressed = q.take(live.data(), 0x1000, 32);
        check(after_suppressed && after_suppressed[0x3000] == 0x55, "Suppressed image matched a later task");
        q.reset();
        const bool across_reset = q.capture(live.data(), 0x1000, 0x1100, [&](auto* dst, const auto* src, auto bytes) {
            std::memcpy(dst, src, bytes);
            q.reset();
        });
        check(!across_reset, "In-flight capture published across session reset");
        check(!q.take(live.data(), 0x1000, 32), "Old capture entered a new session");
        {
            ProductionMailbox full;
            std::vector<std::uint8_t> ram(0x800000, 0xA5);
            ram.back() = 0x12;
            unsigned copies = 0;
            std::size_t copied_bytes = 0;
            check(full.capture(ram.data(), 0x807FFF00, 0x80800000,
                               [&](auto* dst, const auto* src, auto bytes) {
                                   ++copies;
                                   copied_bytes += bytes;
                                   std::memcpy(dst, src, bytes);
                               }),
                  "Exact 8 MiB boundary rejected");
            ram.back() = 0x34;
            auto image = full.take(ram.data(), 0x807FFF00, 32);
            check(image && image[0x7FFFFF] == 0x12, "Upper RAM was not captured");
            check(ram.back() == 0x34 && copies == 1 && copied_bytes == 0x800000,
                  "Snapshot changed live RAM or copied more than once");
        }
        // Concurrent producer/consumer transfer with a semaphore-like turn gate.
        // Every iteration mutates live memory after capture, then holds the lease
        // across a subsequent submission. No game, ROM, RSP or GPU is executed.
        q.reset();
        std::atomic<unsigned> turn{0};
        std::atomic<bool> failed{false};
        constexpr unsigned rounds = 8000;
        std::thread producer([&] {
            for (unsigned i = 0; i < rounds; ++i) {
                while (turn.load(std::memory_order_acquire) != 0)
                    std::this_thread::yield();
                live[0x3000] = std::uint8_t(i);
                q.capture(live.data(), 0x1000 + (i & 1) * 0x100, 0x1100 + (i & 1) * 0x100);
                live[0x3000] = std::uint8_t(i ^ 0x80);
                turn.store(1, std::memory_order_release);
            }
        });
        std::thread consumer([&] {
            Mailbox<0x10000>::Snapshot previous{};
            unsigned previous_id = 0;
            for (unsigned i = 0; i < rounds; ++i) {
                while (turn.load(std::memory_order_acquire) != 1)
                    std::this_thread::yield();
                auto image = q.take(live.data(), 0x1000 + (i & 1) * 0x100, 32);
                if (!image || image[0x3000] != std::uint8_t(i))
                    failed = true;
                if (previous && previous[0x3000] != std::uint8_t(previous_id))
                    failed = true;
                previous = std::move(image);
                previous_id = i;
                turn.store(0, std::memory_order_release);
            }
        });
        producer.join();
        consumer.join();
        check(!failed, "Concurrent handoff changed an owned image");
        check(q.metrics().captured == rounds && q.metrics().transferred == rounds && !q.metrics().fallback,
              "Concurrent transfer lost work");
        std::cout << checks << " graphics handoff checks; " << rounds << " concurrent transfers; synthetic RAM only\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
