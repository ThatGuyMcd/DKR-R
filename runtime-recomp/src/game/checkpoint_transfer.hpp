#pragma once
#include "confirmed_feed.hpp"
#include "party_codec.hpp"
#include <chrono>
#include <functional>
#include <optional>

namespace dkr::runtime::spectators {
using Digest = std::array<std::uint8_t, 32>;
using HashState = std::function<Digest(std::span<const std::uint8_t>)>;
struct CheckpointOffer {
    std::uint64_t transfer = 0, match = 0, schema = 0, control_generation = 0;
    Cursor next;
    std::uint32_t bytes = 0;
    Digest digest{};
    std::uint32_t magic_actions = 0;
    bool operator==(const CheckpointOffer&) const = default;
};
struct CheckpointChunk {
    std::uint64_t transfer = 0;
    std::uint32_t offset = 0;
    std::vector<std::uint8_t> bytes;
};
std::vector<std::uint8_t> encode_offer(const CheckpointOffer& offer);
bool decode_offer(std::span<const std::uint8_t> bytes, CheckpointOffer& out, std::string& error);
std::vector<std::uint8_t> encode_chunk(const CheckpointChunk& chunk);
bool decode_chunk(std::span<const std::uint8_t> bytes, CheckpointChunk& out, std::string& error);
bool valid_offer(const CheckpointOffer& offer);

// Only the authenticated host may feed this receiver. One bounded allocation
// per observer, not one allocation per datagram. Duplicate chunks must agree
// byte-for-byte. An invalid transfer cannot replace the last verified state.
class CheckpointReceiver final {
  public:
    using Clock = std::chrono::steady_clock;
    enum class Result { Accepted, Duplicate, Complete, Stale, Rejected };
    explicit CheckpointReceiver(HashState hash) : hash_(std::move(hash)) {}
    bool begin(CheckpointOffer offer, std::uint64_t expected_match, std::uint64_t expected_schema,
               std::size_t expected_state_bytes, Clock::time_point now, std::string& error);
    Result receive(const CheckpointChunk& chunk, Clock::time_point now, std::string& error);
    std::optional<Baseline> take_verified(std::string& error);
    bool expired(Clock::time_point now) const;
    bool active() const {
        return offer_.has_value();
    }
    std::size_t received_bytes() const {
        return received_;
    }
    void cancel();

  private:
    HashState hash_;
    std::optional<CheckpointOffer> offer_;
    std::shared_ptr<std::vector<std::uint8_t>> state_;
    std::vector<bool> present_;
    Clock::time_point started_{}, progress_{};
    std::uint64_t retired_ = 0;
    std::size_t received_ = 0;
};
// Off-owner delivery can retain this immutable reference without reading live
// guest RAM. Fair scheduling decides when next_chunk() is submitted; it does
// not sleep or enqueue the whole checkpoint ahead of gameplay inputs.
class CheckpointSender final {
  public:
    bool begin(std::uint64_t transfer, std::shared_ptr<const Baseline> baseline);
    std::optional<CheckpointChunk> next_chunk() const;
    void sent();
    const CheckpointOffer& offer() const {
        return offer_;
    }
    bool complete() const {
        return state_ && offset_ == state_->size();
    }
    void clear() {
        state_.reset();
        offset_ = 0;
        offer_ = {};
    }

  private:
    CheckpointOffer offer_{};
    std::shared_ptr<const std::vector<std::uint8_t>> state_;
    std::size_t offset_ = 0;
};
} // namespace dkr::runtime::spectators
