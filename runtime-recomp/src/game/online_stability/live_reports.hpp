#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

namespace dkr::online_stability {
// DPR v2 progress/receipt reports are repeated monotonic snapshots. They carry
// no final input or command and can replace an UNSENT older report only for
// the same match, epoch, control generation and owner lease.
inline bool report_supersedes(std::span<const std::uint8_t> old, std::span<const std::uint8_t> next) {
    if (old.size() != next.size() || (next.size() != 50 && next.size() != 57))
        return false;
    if (!std::equal(next.begin(), next.begin() + 4, "DPR\2") || old[4] != next[4] ||
        !std::equal(old.begin(), old.begin() + 13, next.begin()))
        return false;
    const auto read = [](auto bytes, unsigned at) {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= std::uint32_t(bytes[at + i]) << (8 * i);
        return value;
    };
    if (next[4] == 18 && next.size() == 57) {
        if (!std::equal(old.begin() + 21, old.begin() + 37, next.begin() + 21))
            return false;
        for (unsigned at = 37; at < 57; at += 4)
            if (read(next, at) < read(old, at))
                return false;
        return true;
    }
    return next[4] == 20 && next.size() == 50 && std::equal(old.begin() + 21, old.begin() + 46, next.begin() + 21) &&
           read(next, 46) >= read(old, 46);
}
} // namespace dkr::online_stability
