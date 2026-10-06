#include "experimental_owner_inputs.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace dkr::runtime::netplay::experimental {
namespace {
constexpr unsigned kHeader = 52;
bool valid(const OwnerInputPacket& p) {
    if (!p.epoch || p.players < 2 || p.players > 4 || p.owner >= p.players || p.count > kOwnerInputBatch ||
        (!p.count && p.first_frame) || std::uint64_t(p.first_frame) + p.count > UINT32_MAX) return false;
    for (unsigned owner = p.players; owner < 4; ++owner)
        if (p.received_next[owner] || p.received_bits[owner]) return false;
    for(unsigned owner=0;owner<p.players;++owner) {
        if(p.received_bits[owner]&1U)return false; // Contiguous cursor must have consumed bit zero.
        for(unsigned bit=1;bit<32;++bit)if((p.received_bits[owner]&(1U<<bit)) &&
            std::uint64_t(p.received_next[owner])+bit>=UINT32_MAX)return false;
    }
    return true;
}
void put(std::vector<std::uint8_t>& bytes, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) bytes.push_back(std::uint8_t(value >> (8 * i)));
}
std::uint64_t get(std::span<const std::uint8_t> bytes, unsigned at, unsigned width) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= std::uint64_t(bytes[at + i]) << (8 * i);
    return value;
}
}
std::vector<std::uint8_t> encode_owner_inputs(const OwnerInputPacket& p) {
    if (!valid(p)) return {};
    std::vector<std::uint8_t> result{'D','K','R','X',2,p.players,p.owner,p.count};
    result.reserve(kHeader + p.count * 4);
    put(result, p.epoch, 8); put(result, p.first_frame, 4);
    for (auto ack : p.received_next) put(result, ack, 4);
    for (auto bits : p.received_bits) put(result,bits,4);
    for (unsigned i = 0; i < p.count; ++i) {
        put(result, p.inputs[i].buttons, 2);
        result.push_back(std::bit_cast<std::uint8_t>(p.inputs[i].stick_x));
        result.push_back(std::bit_cast<std::uint8_t>(p.inputs[i].stick_y));
    }
    return result;
}
std::optional<OwnerInputPacket> decode_owner_inputs(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeader || bytes[0] != 'D' || bytes[1] != 'K' || bytes[2] != 'R' ||
        bytes[3] != 'X' || bytes[4] != 2 || bytes[7] > kOwnerInputBatch ||
        bytes.size() != kHeader + bytes[7] * 4U) return {};
    OwnerInputPacket p;
    p.players = bytes[5]; p.owner = bytes[6]; p.count = bytes[7];
    p.epoch = get(bytes, 8, 8); p.first_frame = std::uint32_t(get(bytes, 16, 4));
    for (unsigned i = 0; i < 4; ++i) p.received_next[i] = std::uint32_t(get(bytes, 20 + i * 4, 4));
    for (unsigned i = 0; i < 4; ++i) p.received_bits[i] = std::uint32_t(get(bytes,36+i*4,4));
    for (unsigned i = 0; i < p.count; ++i) {
        p.inputs[i] = {std::uint16_t(get(bytes, kHeader + i * 4, 2)),
            std::bit_cast<std::int8_t>(bytes[kHeader + i * 4 + 2]),
            std::bit_cast<std::int8_t>(bytes[kHeader + i * 4 + 3])};
    }
    return valid(p) ? std::optional(p) : std::nullopt;
}
bool OwnerInputHistory::begin(std::uint64_t epoch, std::uint8_t players, std::uint8_t local) {
    if (!epoch || epoch <= epoch_ || players < 2 || players > 4 || local >= players) return false;
    samples_ = {}; next_ = {}; newest_next_ = {}; received_bits_ = {};
    ack_next_ = {}; ack_bits_ = {}; sent_next_ = {};
    epoch_ = epoch; players_ = players; local_ = local; cursor_ = 0; conflict_ = false;
    return true;
}
bool OwnerInputHistory::set_simulation_cursor(std::uint32_t cursor) {
    // Caller passes its non-rewinding presentation frontier, not the driver's
    // temporary correction cursor. No clock or remote packet advances it.
    if (!epoch_ || conflict_ || cursor < cursor_ || cursor > std::uint64_t(cursor_) + kFuture) return false;
    cursor_ = cursor; return true;
}
std::optional<PackedInput> OwnerInputHistory::actual(std::uint8_t owner, std::uint32_t frame) const {
    if (owner >= players_) return {};
    const auto& sample = samples_[owner][frame % kHistory];
    return sample.valid && sample.frame == frame ? std::optional(sample.input) : std::nullopt;
}
bool OwnerInputHistory::within_window(std::uint8_t owner, std::uint32_t frame) const {
    return frame != UINT32_MAX && std::uint64_t(frame) <= std::uint64_t(cursor_) + kFuture &&
        (frame >= next_[owner] || next_[owner] - frame <= kHistory);
}
bool OwnerInputHistory::may_replace(std::uint8_t owner, std::uint32_t frame) const {
    const auto& old = samples_[owner][frame % kHistory];
    if (!old.valid || old.frame == frame) return true;
    if (frame <= old.frame || frame - old.frame < kHistory || old.frame >= cursor_) return false;
    // A client never forwards another owner's stream. Its already-simulated
    // remote history needs no outbound ACK; imposing one would fill the ring
    // after 128 frames in every three/four-player session.
    if (local_ != 0 && owner != local_) return true;
    // Never lose an unacknowledged sample merely to make space. Clients need
    // the host's ACK; host relays must be acknowledged by every other client.
    for (unsigned peer = 0; peer < players_; ++peer) {
        if (peer == local_ || peer == owner || (local_ != 0 && peer != 0)) continue;
        if (ack_next_[peer][owner] <= old.frame) return false;
    }
    return true;
}
void OwnerInputHistory::insert(std::uint8_t owner, std::uint32_t frame, PackedInput input) {
    samples_[owner][frame % kHistory] = {frame, input, true};
    newest_next_[owner]=(std::max)(newest_next_[owner],frame+1);
    while (actual(owner, next_[owner])) ++next_[owner];
    // Receipt maps change only when actual input arrives, not on every hot
    // network poll for every owner/peer/lane. Rebuild the one changed map so
    // holes beyond the bitmap correctly enter it when its base advances.
    received_bits_[owner]=0;
    if(next_[owner]<newest_next_[owner]) for(unsigned bit=1;bit<32;++bit)
        if(std::uint64_t(next_[owner])+bit<UINT32_MAX && actual(owner,next_[owner]+bit))
            received_bits_[owner]|=1U<<bit;
}
OwnerInputResult OwnerInputHistory::publish(std::uint32_t frame, PackedInput input) {
    if (!epoch_ || conflict_) return OwnerInputResult::Rejected;
    if (const auto prior = actual(local_, frame)) {
        if (*prior == input) return OwnerInputResult::Duplicate;
        conflict_ = true; return OwnerInputResult::Conflict;
    }
    if (frame != next_[local_] || !within_window(local_, frame)) return OwnerInputResult::Rejected;
    if (!may_replace(local_, frame)) return OwnerInputResult::Backpressure;
    insert(local_, frame, input); return OwnerInputResult::Accepted;
}
OwnerInputResult OwnerInputHistory::receive_authenticated(std::uint8_t peer, const OwnerInputPacket& p,
                                                        std::vector<FinalOwnerInput>& received) {
    received.clear();
    if (!epoch_ || conflict_ || peer >= players_ || peer == local_ || !valid(p) || p.players != players_)
        return OwnerInputResult::Rejected;
    if (p.epoch != epoch_) return OwnerInputResult::StaleEpoch;
    if ((local_ != 0 && peer != 0) || p.owner == local_ ||
        (p.owner != peer && !(local_ != 0 && peer == 0))) return OwnerInputResult::Rejected;
    // Validate the whole packet before applying ACKs or input. ACKs for lanes
    // this peer learned elsewhere are irrelevant; ACKs for transmitted lanes
    // cannot claim bytes we have never sent to this peer.
    for (unsigned owner = 0; owner < players_; ++owner) {
        if (sent_next_[peer][owner] && p.received_next[owner] > sent_next_[peer][owner])
            return OwnerInputResult::Rejected;
        if(sent_next_[peer][owner]) for(unsigned bit=0;bit<32;++bit) if(p.received_bits[owner]&(1U<<bit)) {
            const auto frame=std::uint64_t(p.received_next[owner])+bit;
            if(frame>=sent_next_[peer][owner])return OwnerInputResult::Rejected;
        }
    }
    for (unsigned i = 0; i < p.count; ++i) {
        const auto frame = p.first_frame + i;
        if (!within_window(p.owner, frame)) return OwnerInputResult::Rejected;
        if (const auto prior = actual(p.owner, frame); prior && *prior != p.inputs[i]) {
            conflict_ = true; return OwnerInputResult::Conflict;
        }
        if (!may_replace(p.owner, frame)) return OwnerInputResult::Backpressure;
    }
    // Reserve before mutating state so an allocation failure cannot produce a
    // partly delivered input batch. Upstream hands these actuals to Driver.
    received.reserve(p.count);
    for (unsigned i = 0; i < p.count; ++i) {
        const auto frame = p.first_frame + i;
        if (!actual(p.owner, frame)) {
            insert(p.owner, frame, p.inputs[i]); received.push_back({p.owner, frame, p.inputs[i]});
        }
    }
    for (unsigned owner = 0; owner < players_; ++owner) if (sent_next_[peer][owner]) {
        auto& base=ack_next_[peer][owner];auto& bits=ack_bits_[peer][owner];
        const auto incoming=p.received_next[owner];
        if(incoming>base) {const auto shift=incoming-base;bits=shift>=32 ? 0:bits>>shift;base=incoming;}
        const auto shift=base-incoming;
        if(shift<32)bits|=p.received_bits[owner]>>shift;
    }
    return received.empty() ? OwnerInputResult::Duplicate : OwnerInputResult::Accepted;
}
std::optional<OwnerInputPacket> OwnerInputHistory::packet_for(std::uint8_t owner, std::uint8_t peer, OwnerInputSend send) {
    if (!epoch_ || conflict_ || owner >= players_ || peer >= players_ || peer == local_ || owner == peer ||
        (local_ != 0 && (owner != local_ || peer != 0))) return {};
    OwnerInputPacket p;
    p.epoch = epoch_; p.players = players_; p.owner = owner; p.received_next = next_;
    p.received_bits=received_bits_;
    // Never make latency-first input stop-and-wait. Oldest-only batches cap
    // throughput at batch_size / RTT and build debt even without packet loss.
    // Live data carries the newest contiguous samples with redundancy. The
    // independently scheduled repair lane starts at the oldest unacked hole.
    const auto ack = ack_next_[peer][owner];
    const auto end = newest_next_[owner];
    // A gap in a client's earlier packet must not hold all newer actual input
    // hostage at the host relay. Forward the newest complete run; repair still
    // uses cumulative ACKs and retains the missing prefix until every peer has it.
    auto live=end;
    while(live>ack && end-live<kOwnerInputBatch && actual(owner,live-1)) --live;
    auto first=send==OwnerInputSend::Repair ? ack : live;
    if(send==OwnerInputSend::Repair)
        while(first<end && first-ack<32 && (ack_bits_[peer][owner]&(1U<<(first-ack))))++first;
    if (first < end) {
        p.first_frame = first;
        for (unsigned i = 0; i < kOwnerInputBatch && std::uint64_t(first) + i < end; ++i) {
            const auto sample = actual(owner, first + i);
            if (!sample) break; // Relay cannot repair bytes it has not received.
            if(send==OwnerInputSend::Repair && i && first+i-ack<32 &&
               (ack_bits_[peer][owner]&(1U<<(first+i-ack))))break;
            p.inputs[p.count++] = *sample;
        }
        if(p.count)sent_next_[peer][owner] = (std::max)(sent_next_[peer][owner], first + p.count);
        else p.first_frame=0;
    }
    return p;
}
}
