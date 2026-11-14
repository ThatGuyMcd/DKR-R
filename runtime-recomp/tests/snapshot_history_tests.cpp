#include "netplay/rollback_state_store.hpp"
#include "online_stability/snapshot_history.hpp"
#include <cassert>
#include <iostream>
#include <random>

using History = dkr::online_stability::SnapshotHistory<dkr::runtime::netplay::RollbackStateStore>;
int main() {
    constexpr std::size_t bytes = 8199;
    constexpr unsigned window = 14;
    History history(bytes, window, bytes * (window + 1) + 65536);
    assert(history.direct());
    const auto allocation = history.allocated_bytes();
    std::vector<std::uint8_t> image(bytes), restored(bytes);
    std::mt19937 random(77);
    for (unsigned frame = 0; frame < 2000; ++frame) {
        history.retire_before(frame > window ? frame - window : 0);
        auto output = history.staging(frame);
        assert(output.size() == bytes);
        for (auto& value : image)
            value = std::uint8_t(random());
        std::copy(image.begin(), image.end(), output.begin());
        assert(history.save(frame, output, frame + 123));
        assert(history.allocated_bytes() == allocation);
        std::uint64_t hash = 0;
        assert(history.load(frame, restored, &hash));
        assert(restored == image && hash == frame + 123);
        assert(history.staging(frame).empty());
        if (frame % 29 == 20) {
            const auto from = frame - 7;
            std::vector<std::uint8_t> before(bytes);
            assert(history.load(from, before));
            history.discard_after(from);
            assert(!history.load(frame, restored));
            assert(history.load(from, restored) && before == restored);
            // Replay writes every intermediate state, preserving the restored image.
            for (unsigned at = from + 1; at <= frame; ++at) {
                auto target = history.staging(at);
                assert(target.size() == bytes);
                std::fill(target.begin(), target.end(), std::uint8_t(at));
                assert(history.save(at, target, at));
            }
            assert(history.load(from, restored) && before == restored);
        }
    }
    // A failed capture cannot overwrite the last intact retained checkpoint.
    history.retire_before(1999);
    auto abandoned = history.staging(2000);
    assert(!abandoned.empty());
    abandoned[0] = 99;
    assert(!history.load(2000, restored));
    assert(history.load(1999, restored));
    history.discard_after(1999);
    assert(history.staging(2000).size() == bytes);
    assert(!history.save(2000, {}, 0));
    History full(13, 2, 10000);
    for (unsigned frame = 0; frame <= 2; ++frame)
        assert(full.save(frame, std::vector<std::uint8_t>(13, std::uint8_t(frame)), frame));
    assert(full.staging(3).empty());
    assert(!full.save(3, std::vector<std::uint8_t>(13), 0));
    assert(full.load(0, std::span(restored).first(13)));
    // Grow without moving or losing speculative images, then shrink while
    // those images still need correction. Only retired buffers may be freed.
    History resizing(13, 2, 10000);
    std::array<std::uint8_t, 13> small{};
    for (unsigned frame = 0; frame < 3; ++frame) {
        small.fill(std::uint8_t(frame));
        assert(resizing.save(frame, small, frame + 10));
    }
    const auto old_bytes = resizing.allocated_bytes();
    assert(!resizing.reserve_window(20, old_bytes) && resizing.allocated_bytes() == old_bytes);
    assert(resizing.reserve_window(20, 10000));
    for (unsigned frame = 3; frame <= 20; ++frame) {
        small.fill(std::uint8_t(frame));
        assert(resizing.save(frame, small, frame + 10));
    }
    const auto expanded = resizing.allocated_bytes();
    assert(expanded > old_bytes);
    assert(resizing.reserve_window(2, 10000) && resizing.allocated_bytes() == expanded);
    for (unsigned frame = 0; frame <= 20; ++frame) {
        std::uint64_t sum = 0;
        assert(resizing.load(frame, small, &sum));
        assert(small[0] == frame && sum == frame + 10);
    }
    resizing.discard_after(12);
    assert(resizing.load(12, small) && small[0] == 12);
    resizing.retire_before(12);
    assert(resizing.allocated_bytes() > old_bytes);
    for (unsigned frame = 13; frame <= 20; ++frame) {
        small.fill(std::uint8_t(frame));
        assert(resizing.save(frame, small, frame + 10));
    }
    resizing.retire_before(20);
    assert(resizing.allocated_bytes() == old_bytes);
    assert(!resizing.reserve_window(1, 10000) && !resizing.reserve_window(21, 10000));
    assert(resizing.load(20, small) && small[0] == 20);
    assert(resizing.staging(21).size() == 13);
    assert(!resizing.reserve_window(20, 10000));
    assert(resizing.save(21, std::vector<std::uint8_t>(13, 21), 31));
    // Preserve the paged implementation when a larger world cannot fit all full images.
    History paged(bytes, window, bytes * 3);
    assert(!paged.direct());
    for (unsigned frame = 0; frame < 40; ++frame) {
        image[frame % bytes] = std::uint8_t(frame);
        assert(paged.staging(frame).empty());
        assert(paged.save(frame, image, frame));
        assert(paged.load(frame, restored) && restored == image);
        paged.retire_before(frame);
        paged.trim_spares(128 * 1024);
    }
    std::cout << "snapshot history: exact bytes, bounded reuse, rollback, failed capture and paged fallback passed\n";
}
