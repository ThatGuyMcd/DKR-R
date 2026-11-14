#include "netplay/experimental_checkpoint_hash.hpp"
#include "netplay/rollback_state_store.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using dkr::runtime::netplay::RollbackStateStore;
using dkr::runtime::netplay::experimental::checkpoint_hash;
using Clock = std::chrono::steady_clock;
constexpr std::size_t kStateBytes = 8U * 1024U * 1024U;
constexpr std::uint32_t kWarmup = 4U;
constexpr std::uint32_t kSamples = 32U;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<std::uint8_t> initial_state() {
    std::vector<std::uint8_t> state(kStateBytes);
    for (std::size_t index = 0; index < state.size(); ++index) {
        state[index] = static_cast<std::uint8_t>((index * 37U + index / 4096U) & 0xFFU);
    }
    return state;
}

// Deterministic synthetic sparse writes, including both ends of the image.
// These are storage workloads, not captured gameplay or simulated DKR frames.
void advance_state(std::vector<std::uint8_t>& state, std::uint32_t frame) {
    for (std::size_t page = 0; page < 32U; ++page) {
        const std::size_t offset = (std::size_t(frame) * 131071U + page * 977U) % state.size();
        state[offset] ^= static_cast<std::uint8_t>(frame + page + 1U);
    }
    state.front() ^= static_cast<std::uint8_t>(frame + 1U);
    state.back() ^= static_cast<std::uint8_t>(frame + 7U);
}

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double percentile(std::vector<double> samples, double fraction) {
    require(!samples.empty(), "No timing samples recorded");
    for (double sample : samples)
        require(std::isfinite(sample) && sample >= 0, "Invalid timing sample");
    std::sort(samples.begin(), samples.end());
    return samples[static_cast<std::size_t>(std::ceil(fraction * samples.size())) - 1U];
}
} // namespace

int main() {
    try {
        require(checkpoint_hash({}) == UINT64_C(0x2D06800538D394C2), "Checkpoint hash reference mismatch");
        auto state = initial_state();
        std::vector<std::uint8_t> restored(kStateBytes);
        // Exercise the stable production policy, including checkpoint-group eviction.
        RollbackStateStore store(kStateBytes, 12U, 4U);
        std::vector<double> hash_times, store_times, restore_times;
        std::vector<std::uint64_t> fingerprints;
        for (std::uint32_t frame = 0; frame < kWarmup + kSamples; ++frame) {
            advance_state(state, frame);
            auto start = Clock::now();
            const auto fingerprint = checkpoint_hash(state);
            const double hash_ms = elapsed_ms(start);
            start = Clock::now();
            const bool saved = store.save(frame, state, fingerprint);
            const double store_ms = elapsed_ms(start);
            require(saved, "Checkpoint save failed");
            std::uint64_t restored_fingerprint = 0;
            start = Clock::now();
            const bool loaded = store.load(frame, restored, &restored_fingerprint);
            const double restore_ms = elapsed_ms(start);
            require(loaded, "Checkpoint restore failed");
            // All correctness work is outside measured stages. Consuming both
            // bytes and hashes also prevents dead-code elimination under LTO.
            require(restored == state, "Checkpoint roundtrip changed state bytes");
            require(restored_fingerprint == fingerprint && checkpoint_hash(restored) == fingerprint,
                    "Checkpoint roundtrip changed fingerprint");
            fingerprints.push_back(fingerprint);
            if (frame >= kWarmup) {
                hash_times.push_back(hash_ms);
                store_times.push_back(store_ms);
                restore_times.push_back(restore_ms);
            }
        }
        require(fingerprints.back() == UINT64_C(0xEFAD86F4DC2A2C07), "Synthetic state reference fingerprint mismatch");
        require(!store.contains(0) && !store.load(0, restored), "Old checkpoint was not evicted");
        // Independently regenerate every image: retained historical deltas must
        // reconstruct correctly, not merely the most recently saved image.
        auto expected = initial_state();
        std::size_t retained = 0;
        for (std::uint32_t frame = 0; frame < kWarmup + kSamples; ++frame) {
            advance_state(expected, frame);
            if (!store.contains(frame))
                continue;
            ++retained;
            std::uint64_t fingerprint = 0;
            require(store.load(frame, restored, &fingerprint), "Retained checkpoint unavailable");
            require(restored == expected && fingerprint == fingerprints[frame], "Historical checkpoint mismatch");
        }
        require(retained >= 8U && retained <= 12U, "Checkpoint retention window is incorrect");
        constexpr std::uint32_t rewind_frame = kWarmup + kSamples - 3U;
        require(store.load(rewind_frame, state), "Rewind checkpoint unavailable");
        store.discard_after(rewind_frame);
        require(!store.contains(rewind_frame + 1U), "Rewind retained future state");
        state.back() ^= 0xA5U;
        const auto branch_hash = checkpoint_hash(state);
        require(store.save(rewind_frame + 1U, state, branch_hash), "Branched checkpoint save failed");
        std::uint64_t restored_hash = 0;
        require(store.load(rewind_frame + 1U, restored, &restored_hash), "Branched checkpoint restore failed");
        require(restored == state && restored_hash == branch_hash, "Branched checkpoint mismatch");

        std::cout << "Synthetic checkpoint workload: 8388608 bytes, 4 warmup iterations, 32 measured iterations\n"
                  << "Checkpoint fingerprint: " << std::hex << fingerprints.back() << std::dec << '\n'
                  << std::fixed << std::setprecision(6) << "DKR_HARNESS {\"metrics\":{"
                  << "\"checkpoint.hash_median_ms\":" << percentile(hash_times, 0.5)
                  << ",\"checkpoint.hash_p95_ms\":" << percentile(hash_times, 0.95)
                  << ",\"checkpoint.store_median_ms\":" << percentile(store_times, 0.5)
                  << ",\"checkpoint.store_p95_ms\":" << percentile(store_times, 0.95)
                  << ",\"checkpoint.restore_median_ms\":" << percentile(restore_times, 0.5)
                  << ",\"checkpoint.restore_p95_ms\":" << percentile(restore_times, 0.95)
                  << "}}\nCheckpoint benchmark passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Checkpoint benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
