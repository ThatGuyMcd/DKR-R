#include "authoritative_state_codec.hpp"
#include "../performance_trace.hpp"

#include <algorithm>
#include <array>
#include <optional>

namespace dkr::runtime::netplay {
namespace {

constexpr std::array<std::uint8_t, 4U> kMagic{'D', 'K', 'R', 'Z'};
constexpr std::array<std::uint8_t, 4U> kDeltaMagic{'D', 'K', 'R', 'D'};
constexpr std::size_t kHeaderBytes = 8U;
constexpr std::size_t kDeltaHeaderBytes = 12U;
constexpr std::size_t kMaximumLiteral = 128U;
constexpr std::size_t kMaximumRun = 130U;

void put_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

std::uint32_t take_u32(std::span<const std::uint8_t> bytes,
                       std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::size_t repeated_length(std::span<const std::uint8_t> state,
                            std::size_t first) {
    std::size_t length = 1U;
    while (first + length < state.size() && length < kMaximumRun &&
           state[first + length] == state[first]) {
        ++length;
    }
    return length;
}

std::vector<std::uint8_t> encode_rle(
    std::span<const std::uint8_t> bytes,
    std::span<const std::uint8_t, 4U> magic,
    std::optional<std::uint32_t> base_frame = std::nullopt) {
    std::vector<std::uint8_t> encoded;
    encoded.reserve(bytes.size());
    encoded.insert(encoded.end(), magic.begin(), magic.end());
    put_u32(encoded, static_cast<std::uint32_t>(bytes.size()));
    if (base_frame) put_u32(encoded, *base_frame);

    std::size_t cursor = 0U;
    while (cursor < bytes.size()) {
        const std::size_t run = repeated_length(bytes, cursor);
        if (run >= 3U) {
            encoded.push_back(static_cast<std::uint8_t>(
                0x80U | static_cast<std::uint8_t>(run - 3U)));
            encoded.push_back(bytes[cursor]);
            cursor += run;
            continue;
        }

        const std::size_t literal_first = cursor;
        cursor += run;
        while (cursor < bytes.size() &&
               cursor - literal_first < kMaximumLiteral) {
            const std::size_t next_run = repeated_length(bytes, cursor);
            if (next_run >= 3U) break;
            cursor += (std::min)(next_run,
                kMaximumLiteral - (cursor - literal_first));
        }
        const std::size_t literal_length = cursor - literal_first;
        encoded.push_back(static_cast<std::uint8_t>(literal_length - 1U));
        encoded.insert(encoded.end(),
                       bytes.begin() + static_cast<std::ptrdiff_t>(literal_first),
                       bytes.begin() + static_cast<std::ptrdiff_t>(cursor));
    }
    return encoded;
}

bool decode_rle(std::span<const std::uint8_t> wire,
                std::size_t header_bytes,
                std::size_t decoded_size,
                std::vector<std::uint8_t>& decoded,
                std::string& error) {
    decoded.clear();
    decoded.reserve(decoded_size);
    std::size_t cursor = header_bytes;
    while (cursor < wire.size() && decoded.size() < decoded_size) {
        const std::uint8_t command = wire[cursor++];
        if ((command & 0x80U) != 0U) {
            const std::size_t length = (command & 0x7FU) + 3U;
            if (cursor >= wire.size() ||
                decoded.size() + length > decoded_size) {
                error = "The compressed authoritative run is invalid.";
                decoded.clear();
                return false;
            }
            decoded.insert(decoded.end(), length, wire[cursor++]);
        } else {
            const std::size_t length = command + 1U;
            if (cursor + length > wire.size() ||
                decoded.size() + length > decoded_size) {
                error = "The compressed authoritative literal is invalid.";
                decoded.clear();
                return false;
            }
            decoded.insert(decoded.end(), wire.begin() +
                           static_cast<std::ptrdiff_t>(cursor),
                           wire.begin() + static_cast<std::ptrdiff_t>(cursor + length));
            cursor += length;
        }
    }
    if (cursor != wire.size() || decoded.size() != decoded_size) {
        error = "The compressed authoritative state is truncated or trailing.";
        decoded.clear();
        return false;
    }
    error.clear();
    return true;
}

} // namespace

std::vector<std::uint8_t> encode_authoritative_state_wire(
    std::span<const std::uint8_t> state) {
    if (state.empty() || state.size() > 0xFFFFFFFFULL) {
        return std::vector<std::uint8_t>(state.begin(), state.end());
    }

    std::vector<std::uint8_t> encoded = encode_rle(state, kMagic);

    if (encoded.size() >= state.size()) {
        return std::vector<std::uint8_t>(state.begin(), state.end());
    }
    return encoded;
}

std::vector<std::uint8_t> encode_authoritative_state_delta_wire(
    std::span<const std::uint8_t> state,
    std::span<const std::uint8_t> keyframe,
    std::uint32_t keyframe_frame) {
    if (state.empty() || state.size() != keyframe.size() ||
        state.size() > 0xFFFFFFFFULL) {
        return {};
    }
    thread_local std::vector<std::uint8_t> delta;
    delta.resize(state.size());
    for (std::size_t index = 0U; index < state.size(); ++index) {
        delta[index] = state[index] ^ keyframe[index];
    }
    std::vector<std::uint8_t> encoded = encode_rle(
        delta, kDeltaMagic, keyframe_frame);
    if (encoded.size() >= state.size()) return {};
    return encoded;
}

bool authoritative_state_wire_is_delta(std::span<const std::uint8_t> wire) {
    return wire.size() >= kDeltaHeaderBytes &&
           std::equal(kDeltaMagic.begin(), kDeltaMagic.end(), wire.begin());
}

bool decode_authoritative_state_delta_wire(
    std::span<const std::uint8_t> wire,
    std::span<const std::uint8_t> keyframe,
    std::uint32_t keyframe_frame,
    std::size_t maximum_decoded_size,
    std::vector<std::uint8_t>& state,
    std::string& error) {
    performance_trace::Scope profile(performance_trace::Region::OnlineStateDecode, wire.size());
    state.clear();
    if (!authoritative_state_wire_is_delta(wire)) {
        error = "The authoritative replica is not a temporal delta.";
        return false;
    }
    const std::size_t decoded_size = take_u32(wire, 4U);
    const std::uint32_t declared_base_frame = take_u32(wire, 8U);
    if (decoded_size == 0U || decoded_size > maximum_decoded_size ||
        decoded_size != keyframe.size() ||
        declared_base_frame != keyframe_frame) {
        error = "The authoritative replica does not match its keyframe.";
        return false;
    }
    thread_local std::vector<std::uint8_t> delta;
    if (!decode_rle(wire, kDeltaHeaderBytes, decoded_size, delta, error)) {
        return false;
    }
    state.resize(decoded_size);
    for (std::size_t index = 0U; index < decoded_size; ++index) {
        state[index] = delta[index] ^ keyframe[index];
    }
    error.clear();
    return true;
}

bool decode_authoritative_state_wire(
    std::span<const std::uint8_t> wire,
    std::size_t maximum_decoded_size,
    std::vector<std::uint8_t>& state,
    std::string& error) {
    performance_trace::Scope profile(performance_trace::Region::OnlineStateDecode, wire.size());
    state.clear();
    if (wire.size() < kHeaderBytes ||
        !std::equal(kMagic.begin(), kMagic.end(), wire.begin())) {
        if (wire.empty() || wire.size() > maximum_decoded_size) {
            error = "The authoritative state is outside decoded limits.";
            return false;
        }
        state.assign(wire.begin(), wire.end());
        error.clear();
        return true;
    }

    const std::size_t decoded_size = take_u32(wire, 4U);
    if (decoded_size == 0U || decoded_size > maximum_decoded_size) {
        error = "The compressed authoritative state declares an invalid size.";
        return false;
    }
    return decode_rle(wire, kHeaderBytes, decoded_size, state, error);
}

} // namespace dkr::runtime::netplay
