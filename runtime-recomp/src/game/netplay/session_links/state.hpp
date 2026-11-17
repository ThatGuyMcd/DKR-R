#pragma once
#include "policy.hpp"
#include "transfer.hpp"
#include <algorithm>
#include <deque>
#include <set>

namespace dkr::runtime::netplay::links {
// Version 2 transfers the authenticated architecture inventory. Older local
// playtests retain the ordinary host route and cannot enter this handover.
inline constexpr std::uint8_t ProtocolVersion = 2;
enum class Kind : std::uint8_t {
    Hello = 1,
    Introduce,
    Ping,
    Pong,
    Report,
    Offer,
    Accept,
    Prepare,
    Prepared,
    Commit,
    Acknowledge,
    Release,
    Cancel,
    Registration,
    SaveChunk,
    SaveReceipt,
    Activated,
    Finished,
    Inventory
};
struct Packet {
    Kind kind = Kind::Hello;
    Id room = 0, generation = 0;
    std::vector<std::uint8_t> body;
};
inline std::vector<std::uint8_t> encode(const Packet& value) {
    if (!value.room || !value.generation || value.body.size() > 11500)
        return {};
    std::vector<std::uint8_t> result{'D', 'K', 'R', 'X', 1, 7, std::uint8_t(value.kind)};
    for (const auto v : {value.room, value.generation})
        for (unsigned i = 0; i < 8; ++i)
            result.push_back(std::uint8_t(v >> (i * 8)));
    result.insert(result.end(), value.body.begin(), value.body.end());
    return result;
}
inline bool decode(std::span<const std::uint8_t> bytes, Packet& value) {
    constexpr std::array<std::uint8_t, 6> magic{'D', 'K', 'R', 'X', 1, 7};
    if (bytes.size() < 23 || bytes.size() > 11523 || !std::equal(magic.begin(), magic.end(), bytes.begin()) ||
        bytes[6] < 1 || bytes[6] > unsigned(Kind::Inventory))
        return false;
    value = {};
    value.kind = Kind(bytes[6]);
    for (unsigned i = 0; i < 8; ++i) {
        value.room |= Id(bytes[7 + i]) << (8 * i);
        value.generation |= Id(bytes[15 + i]) << (8 * i);
    }
    value.body.assign(bytes.begin() + 23, bytes.end());
    return value.room && value.generation;
}
struct Write {
    std::vector<std::uint8_t> bytes;
    Write& number(Id value, unsigned size = 8) {
        for (unsigned i = 0; i < size; ++i)
            bytes.push_back(std::uint8_t(value >> (8 * i)));
        return *this;
    }
    Write& data(std::span<const std::uint8_t> value) {
        bytes.insert(bytes.end(), value.begin(), value.end());
        return *this;
    }
    Write& text(std::string_view value) {
        number(value.size(), 2);
        bytes.insert(bytes.end(), value.begin(), value.end());
        return *this;
    }
};
struct Read {
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;
    bool good = true;
    Id number(unsigned size = 8) {
        if (size > 8 || at > bytes.size() || size > bytes.size() - at) {
            good = false;
            return 0;
        }
        Id value = 0;
        for (unsigned i = 0; i < size; ++i)
            value |= Id(bytes[at++]) << (8 * i);
        return value;
    }
    std::vector<std::uint8_t> data(std::size_t size) {
        if (at > bytes.size() || size > bytes.size() - at) {
            good = false;
            return {};
        }
        std::vector<std::uint8_t> result(bytes.begin() + at, bytes.begin() + at + size);
        at += size;
        return result;
    }
    std::string text(std::size_t maximum = 128) {
        const auto size = number(2);
        if (size > maximum) {
            good = false;
            return {};
        }
        const auto value = data(size);
        return {value.begin(), value.end()};
    }
    bool done() const {
        return good && at == bytes.size();
    }
};
struct Incoming {
    Id source;
    Packet packet;
};
struct Introduction {
    Id remote = 0;
    secure::Key key{};
    std::string endpoint;
    Clock::time_point issued{};
};
struct State {
    Scope scope;
    std::set<Id> capable;
    std::map<Pair, secure::Key> keys;
    std::map<Id, Introduction> introduced;
    std::set<Id> live_routes;
    std::map<Pair, Sample> samples;
    std::map<Id, Capacity> capacity;
    std::map<Id, std::string> architectures;
    std::map<Id, std::map<Id, Clock::time_point>> pings;
    std::map<Id, std::deque<unsigned>> rtts;
    std::map<Id, Clock::time_point> last_probe;
    std::map<Id, std::deque<bool>> probe_results;
    std::deque<Incoming> incoming;
    Handover handover;
    Id save_owner = 0, request = 0, last_applied = 0, serial = 0;
    std::optional<bool> consent;
    std::optional<bool> local_consent;
    bool cancel = false;
    bool registration_started = false, registration_ready = false, prepared_state = false, released = false,
         finished = false;
    secure::KeyPair successor_keys{};
    secure::Key successor_capability{}, successor_public{};
    std::string successor_endpoint, successor_invite;
    std::set<Id> activated;
    std::vector<std::uint8_t> prepared_bytes, save_paks, save_records;
    TransferBuffer assembly;
    TransferBuffer inventory_assembly;
    std::vector<std::uint8_t> inventory_bytes;
    Digest inventory_hash{};
    std::map<Pair, secure::Key> pending_keys;
    bool inventory_ready = false;
    std::map<Id, std::size_t> transfer_offsets;
    std::size_t expected_transfer = 0;
    Clock::time_point finished_until{};
    bool directory_required = false, directory_public = false, directory_accepted = false, directory_committed = false;
    bool mod_requested = false, mod_ready = false;
    std::string directory_code;
    Clock::time_point directory_send{};
    Clock::time_point transfer_send{};
    Clock::time_point hello{}, introduce{}, probe{}, report{};
    std::string status;
    bool receive(Id source, std::span<const std::uint8_t> bytes) {
        Packet packet;
        if (incoming.size() >= 96 || !decode(bytes, packet))
            return false;
        incoming.push_back({source, std::move(packet)});
        return true;
    }
    void reset() {
        *this = State{};
    }
};
inline bool busy(const State& state) {
    const auto phase = state.handover.phase();
    return phase == Phase::Offered || phase == Phase::Preparing || phase == Phase::Committing ||
           (phase == Phase::Committed && !state.finished);
}
} // namespace dkr::runtime::netplay::links
