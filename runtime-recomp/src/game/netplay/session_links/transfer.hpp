#pragma once
#include "policy.hpp"
#include <algorithm>

namespace dkr::runtime::netplay::links {
// Waiting-room transfers are bounded independently of racing input queues.
// No file paths, private host key or account credentials enter this bundle.
inline constexpr std::size_t MaximumTransferBytes = 1024 * 1024, TransferChunk = 640;
struct TransferBuffer {
    std::vector<std::uint8_t> bytes;
    std::vector<bool> received;
    Id serial = 0;
    bool accept(Id token, std::size_t total, std::size_t offset, std::span<const std::uint8_t> chunk) {
        if (!token || !total || total > MaximumTransferBytes || offset % TransferChunk || offset >= total ||
            chunk.size() != std::min(TransferChunk, total - offset))
            return false;
        if (serial != token) {
            serial = token;
            bytes.assign(total, 0);
            received.assign((total + TransferChunk - 1) / TransferChunk, false);
        }
        if (bytes.size() != total)
            return false;
        const auto slot = offset / TransferChunk;
        if (received[slot])
            return std::equal(chunk.begin(), chunk.end(), bytes.begin() + offset);
        std::copy(chunk.begin(), chunk.end(), bytes.begin() + offset);
        received[slot] = true;
        return true;
    }
    bool complete() const {
        return !received.empty() && std::all_of(received.begin(), received.end(), [](bool v) { return v; });
    }
};
} // namespace dkr::runtime::netplay::links
