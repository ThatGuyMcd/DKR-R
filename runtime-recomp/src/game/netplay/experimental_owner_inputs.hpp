#pragma once
#include "netplay_types.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dkr::runtime::netplay::experimental {

// Experimental-only lane. Unlike the stable future-sample refresh protocol,
// every published owner sample is final. Transport encryption/authentication
// belongs to SecureChannel; this codec is its bounded plaintext payload only.
inline constexpr unsigned kOwnerInputBatch = 8;
inline constexpr unsigned kOwnerInputMaximumBytes = 52+4*kOwnerInputBatch;
struct OwnerInputPacket {
    std::uint64_t epoch = 0;
    std::uint32_t first_frame = 0;
    std::array<std::uint32_t, 4> received_next{};
    // Selective actual-input receipts relative to received_next, never
    // predictions or simulation ACKs. Bit zero names the first missing frame.
    std::array<std::uint32_t, 4> received_bits{};
    std::array<PackedInput, kOwnerInputBatch> inputs{};
    std::uint8_t players = 0;
    std::uint8_t owner = 0;
    std::uint8_t count = 0;
    bool operator==(const OwnerInputPacket&) const = default;
};
std::vector<std::uint8_t> encode_owner_inputs(const OwnerInputPacket& packet);
std::optional<OwnerInputPacket> decode_owner_inputs(std::span<const std::uint8_t> bytes);

enum class OwnerInputResult { Accepted, Duplicate, StaleEpoch, Rejected, Backpressure, Conflict };
enum class OwnerInputSend { Live, Repair };
struct FinalOwnerInput { std::uint8_t owner; std::uint32_t frame; PackedInput input; };

// All methods run on the simulation owner. Receive callbacks enqueue bounded
// authenticated messages; they never mutate this history or the game directly.
// Host forwards final owner samples; clients accept relays only from slot 0.
// ACK means received actual input, NOT simulated/confirmed input.
class OwnerInputHistory final {
public:
    bool begin(std::uint64_t epoch, std::uint8_t players, std::uint8_t local);
    bool set_simulation_cursor(std::uint32_t cursor);
    OwnerInputResult publish(std::uint32_t frame, PackedInput input);
    OwnerInputResult receive_authenticated(std::uint8_t peer, const OwnerInputPacket& packet,
                                          std::vector<FinalOwnerInput>& newly_received);
    std::optional<OwnerInputPacket> packet_for(std::uint8_t owner, std::uint8_t peer,
                                             OwnerInputSend send = OwnerInputSend::Live);
    std::optional<PackedInput> actual(std::uint8_t owner, std::uint32_t frame) const;
    std::uint32_t received_next(std::uint8_t owner) const { return owner < players_ ? next_[owner] : 0; }
    bool failed() const { return conflict_; }
private:
    static constexpr unsigned kHistory = 128;
    static constexpr unsigned kFuture = 48;
    struct Sample { std::uint32_t frame = 0; PackedInput input{}; bool valid = false; };
    bool within_window(std::uint8_t owner, std::uint32_t frame) const;
    bool may_replace(std::uint8_t owner, std::uint32_t frame) const;
    void insert(std::uint8_t owner, std::uint32_t frame, PackedInput input);
    std::array<std::array<Sample, kHistory>, 4> samples_{};
    std::array<std::uint32_t, 4> next_{};
    std::array<std::uint32_t, 4> newest_next_{};
    std::array<std::uint32_t, 4> received_bits_{};
    std::array<std::array<std::uint32_t, 4>, 4> ack_next_{}; // peer, owner
    std::array<std::array<std::uint32_t, 4>, 4> ack_bits_{};
    std::array<std::array<std::uint32_t, 4>, 4> sent_next_{};
    std::uint64_t epoch_ = 0;
    std::uint32_t cursor_ = 0;
    std::uint8_t players_ = 0, local_ = 0;
    bool conflict_ = false;
};
}
