#include "rollback_simulation_state.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace dkr::runtime::netplay {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{'D', 'K', 'R', 'R', 'B', 'S', 'I', 'M'};
constexpr std::uint32_t kEnvelopeVersion = 1U;

struct EnvelopeHeader {
    std::array<std::uint8_t, 8> magic{};
    std::uint32_t version = 0U;
    std::uint32_t frame = 0U;
    std::uint32_t payload_bytes = 0U;
    std::uint32_t reserved = 0U;
    std::uint64_t checksum = 0U;
};

static_assert(sizeof(EnvelopeHeader) == kRollbackSimulationHeaderBytes);

bool decode_header(std::span<const std::uint8_t> source, EnvelopeHeader& header, std::string& error) {
    if (source.size() != kRollbackSimulationStateBytes) {
        error = "The rollback simulation envelope has an invalid size.";
        return false;
    }
    std::memcpy(&header, source.data(), sizeof(header));
    if (header.magic != kMagic || header.version != kEnvelopeVersion || header.payload_bytes == 0U ||
        header.payload_bytes > kMaximumAuthoritativeStateBytes) {
        error = "The rollback simulation envelope header is invalid.";
        return false;
    }
    return true;
}

} // namespace

bool capture_rollback_simulation_state(const std::uint8_t* rdram, std::size_t rdram_size, std::uint32_t frame,
                                       std::span<std::uint8_t> destination, std::uint64_t& checksum,
                                       std::string& error) {
    if (destination.size() != kRollbackSimulationStateBytes) {
        error = "The rollback simulation destination has an invalid size.";
        return false;
    }

    std::vector<std::uint8_t> authored;
    if (!capture_authoritative_state(rdram, rdram_size, frame, authored, error)) {
        return false;
    }
    if (authored.empty() || authored.size() > kMaximumAuthoritativeStateBytes) {
        error = "The authored rollback state exceeds its fixed envelope.";
        return false;
    }

    checksum = authoritative_state_checksum(authored);
    const EnvelopeHeader header{kMagic, kEnvelopeVersion, frame, static_cast<std::uint32_t>(authored.size()),
                                0U,     checksum};
    std::fill(destination.begin(), destination.end(), 0U);
    std::memcpy(destination.data(), &header, sizeof(header));
    std::copy(authored.begin(), authored.end(), destination.begin() + static_cast<std::ptrdiff_t>(sizeof(header)));
    error.clear();
    return true;
}

bool restore_rollback_simulation_state(std::uint8_t* rdram, std::size_t rdram_size,
                                       std::span<const std::uint8_t> source, std::uint32_t expected_frame,
                                       std::uint64_t expected_checksum, std::string& error) {
    EnvelopeHeader header{};
    if (!decode_header(source, header, error))
        return false;
    if (header.frame != expected_frame || header.checksum != expected_checksum) {
        error = "The rollback simulation envelope does not match its token.";
        return false;
    }

    const auto payload = source.subspan(sizeof(header), header.payload_bytes);
    if (authoritative_state_checksum(payload) != header.checksum) {
        error = "The rollback simulation envelope failed its checksum.";
        return false;
    }
    return apply_authoritative_state(rdram, rdram_size, payload, expected_frame, error);
}

} // namespace dkr::runtime::netplay
