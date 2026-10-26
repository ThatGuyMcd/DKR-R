#include "netplay/authoritative_state_codec.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main() {
    using namespace dkr::runtime::netplay;
    std::string error;
    std::vector<std::uint8_t> decoded;

    std::vector<std::uint8_t> repeated(4096U, 0U);
    for (std::size_t index = 0U; index < repeated.size(); index += 257U) {
        repeated[index] = static_cast<std::uint8_t>(index);
    }
    const auto compact = encode_authoritative_state_wire(repeated);
    assert(compact.size() < repeated.size() / 4U);
    assert(decode_authoritative_state_wire(compact, 8192U, decoded, error));
    assert(decoded == repeated);

    const std::vector<std::uint8_t> raw{0U, 0U, 0U, 8U, 1U, 2U, 3U, 4U};
    const auto raw_wire = encode_authoritative_state_wire(raw);
    assert(raw_wire == raw);
    assert(decode_authoritative_state_wire(raw_wire, 32U, decoded, error));
    assert(decoded == raw);

    std::vector<std::uint8_t> keyframe(4096U, 0x5AU);
    std::vector<std::uint8_t> temporal = keyframe;
    for (std::size_t index = 17U; index < temporal.size(); index += 521U) {
        temporal[index] ^= static_cast<std::uint8_t>(index);
    }
    constexpr std::uint32_t keyframe_frame = 120U;
    const auto delta = encode_authoritative_state_delta_wire(temporal, keyframe, keyframe_frame);
    assert(!delta.empty());
    assert(delta.size() < temporal.size() / 8U);
    assert(authoritative_state_wire_is_delta(delta));
    assert(decode_authoritative_state_delta_wire(delta, keyframe, keyframe_frame, 8192U, decoded, error));
    assert(decoded == temporal);
    assert(!decode_authoritative_state_delta_wire(delta, keyframe, keyframe_frame + 1U, 8192U, decoded, error));
    assert(!error.empty());

    auto corrupt_delta = delta;
    corrupt_delta.pop_back();
    assert(!decode_authoritative_state_delta_wire(corrupt_delta, keyframe, keyframe_frame, 8192U, decoded, error));
    assert(!error.empty());

    auto corrupt = compact;
    corrupt.pop_back();
    assert(!decode_authoritative_state_wire(corrupt, 8192U, decoded, error));
    assert(!error.empty());

    auto oversized = compact;
    oversized[4] = 0x7FU;
    assert(!decode_authoritative_state_wire(oversized, 8192U, decoded, error));
    return 0;
}
