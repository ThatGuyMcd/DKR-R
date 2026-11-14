#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::netplay {

// The encrypted nonce/sequence is global to a peer, while Quick Join routes
// traffic over independent SCTP streams. In particular, Player 1's frame
// ledger is reliable but deliberately unordered so a repaired old commit does
// not wait behind a newer packet. A small global-sequence replay window can
// therefore mistake a legitimate delayed ledger packet for an ancient replay
// after activity on the input/replica streams. Keep a bounded 64K horizon: it
// covers sustained latency spikes while still accepting every authenticated
// sequence at most once. At five lanes this costs 40 KiB per connected peer.
inline constexpr std::size_t kReceiveSequenceWindowBits = 65536U;

struct ReceiveSequenceWindow {
    std::uint64_t highest = 0U;
    std::bitset<kReceiveSequenceWindowBits> seen{};

    bool accept(std::uint64_t sequence) {
        if (sequence == 0U)
            return false;
        if (sequence > highest) {
            const std::uint64_t advance = sequence - highest;
            if (advance >= kReceiveSequenceWindowBits) {
                seen.reset();
            } else {
                seen <<= static_cast<std::size_t>(advance);
            }
            seen.set(0U);
            highest = sequence;
            return true;
        }
        const std::uint64_t age = highest - sequence;
        if (age >= kReceiveSequenceWindowBits)
            return false;
        const auto index = static_cast<std::size_t>(age);
        if (seen.test(index))
            return false;
        seen.set(index);
        return true;
    }
};

} // namespace dkr::runtime::netplay
