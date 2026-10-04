#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dkr::runtime::netplay::experimental {

// The existing wire journal, encoded into a small stack header. Prepending to
// the ring-owned vector preserves its capacity across corrections/ring reuse;
// it does not replace that vector with five separately allocated envelopes.
// This deliberately changes neither the field order nor the commit decoder.
template<std::size_t Words, std::size_t Proofs = 0>
constexpr auto effect_envelope_header(std::array<char, 4> tag,
        std::array<std::uint32_t, Words> words,
        std::array<std::uint64_t, Proofs> proofs = {}) noexcept {
    std::array<std::uint8_t, 4 + Words * 4 + Proofs * 8> header{};
    std::size_t cursor = 0;
    for (const auto value : tag) header[cursor++] = std::uint8_t(value);
    for (const auto value : words)
        for (unsigned byte = 0; byte < 4; ++byte)
            header[cursor++] = std::uint8_t(value >> (byte * 8));
    for (const auto value : proofs)
        for (unsigned byte = 0; byte < 8; ++byte)
            header[cursor++] = std::uint8_t(value >> (byte * 8));
    return header;
}

template<std::size_t Words, std::size_t Proofs = 0>
void prepend_effect_envelope(std::vector<std::uint8_t>& journal,
        std::array<char, 4> tag, std::array<std::uint32_t, Words> words,
        std::array<std::uint64_t, Proofs> proofs = {}) {
    const auto header = effect_envelope_header(tag, words, proofs);
    journal.insert(journal.begin(), header.begin(), header.end());
}

// Fixed endian/layout contracts also run during compilation, without starting
// a test or game process. All existing commit offsets remain unchanged.
static_assert(effect_envelope_header({'D','K','G','A'},
    std::array<std::uint32_t,4>{0x12345678U,32000U,1066U,8U}) ==
    std::array<std::uint8_t,20>{'D','K','G','A',0x78,0x56,0x34,0x12,
        0x00,0x7D,0,0,0x2A,0x04,0,0,8,0,0,0});
static_assert(effect_envelope_header({'D','K','G','V'},
    std::array<std::uint32_t,9>{},std::array<std::uint64_t,2>{}).size()==56);
static_assert(effect_envelope_header({'D','K','G','V'},
    std::array<std::uint32_t,9>{},
    std::array<std::uint64_t,2>{0x0123456789ABCDEFULL,0})[40]==0xEF);
static_assert(effect_envelope_header({'D','K','G','V'},
    std::array<std::uint32_t,9>{},
    std::array<std::uint64_t,2>{0x0123456789ABCDEFULL,0})[47]==0x01);

}
