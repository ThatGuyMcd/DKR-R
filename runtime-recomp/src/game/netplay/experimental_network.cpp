#include "experimental_network.hpp"
#include "monocypher.h"
#include <algorithm>
#include <exception>

namespace dkr::runtime::netplay::experimental {
namespace {
constexpr std::array<std::uint8_t,5> kPrefix{'D','K','X','N',1};
constexpr std::size_t kMaximumWireBytes = 44 + 6 + 68;
bool nonzero(const secure::Key& key) {
    return std::any_of(key.begin(),key.end(),[](auto byte){return byte!=0;});
}
secure::Key network_key(const secure::Key& admitted,std::uint64_t match,const secure::Key& incarnation) {
    // Domain-separated from the existing session's encryption key. Resetting
    // experiment sequences can never reuse its stable-mode AEAD key/nonce.
    std::array<std::uint8_t,56> context{'D','K','R','-','R','O','L','L','B','A','C','K','-','V','1',0};
    for(unsigned b=0;b<8;++b) context[16+b]=std::uint8_t(match>>(8*b));
    std::copy(incarnation.begin(),incarnation.end(),context.begin()+24);
    secure::Key result{};
    crypto_blake2b_keyed(result.data(),result.size(),admitted.data(),admitted.size(),context.data(),context.size());
    return result;
}
}
Network::~Network() {
    for(auto& key:keys_) crypto_wipe(key.data(),key.size());
    for(auto& peer:configuration_.peers) crypto_wipe(peer.admitted_key.data(),peer.admitted_key.size());
}
bool Network::fail(std::string message) { error_=std::move(message); return false; }
bool Network::start(NetworkConfiguration configuration,std::string& error) {
    const unsigned players=session_.player_count(),local=session_.local_owner();
    if(started_ || !session_.epoch() || players<2 || players>4 || local>=players ||
       !configuration.match_id || !configuration.local_sender_id || !nonzero(configuration.incarnation) || !transport_.is_open()) {
        error="Invalid or already started experimental transport."; return false;
    }
    for(unsigned peer=0;peer<players;++peer) {
        if(peer==local || (local && peer)) continue;
        const auto& binding=configuration.peers[peer];
        if(!binding.address || binding.address.size>binding.address.storage.size() ||
           !binding.sender_id || binding.sender_id==configuration.local_sender_id || !nonzero(binding.admitted_key)) {
            error="Missing authenticated experimental peer binding."; return false;
        }
        for(unsigned previous=0;previous<peer;++previous) {
            if(previous==local || (local && previous)) continue;
            const auto& other=configuration.peers[previous];
            if(binding.address==other.address || binding.sender_id==other.sender_id || binding.admitted_key==other.admitted_key) {
                error="Experimental peer routes, identities and keys must be distinct."; return false;
            }
        }
    }
    configuration_=configuration;
    for(unsigned peer=0;peer<players;++peer) {
        if(peer==local || (local && peer)) continue;
        keys_[peer]=network_key(configuration.peers[peer].admitted_key,configuration.match_id,configuration.incarnation);
    }
    // Do not retain a second secret copy after deriving the experimental key.
    for(auto& peer:configuration_.peers) crypto_wipe(peer.admitted_key.data(),peer.admitted_key.size());
    started_=true; epoch_=session_.epoch(); error.clear(); return true;
}
bool Network::queue(unsigned peer,unsigned lane,unsigned kind,std::vector<std::uint8_t> payload,
                    TransportTrafficClass traffic, Clock::time_point now) {
    auto& pending=pending_[peer][lane];
    if(payload.empty()) return true;
    if(payload.size()>68) return fail("Experimental wire bound exceeded.");
    std::vector<std::uint8_t> plain(kPrefix.begin(),kPrefix.end());
    plain.push_back(std::uint8_t(kind)); plain.insert(plain.end(),payload.begin(),payload.end());
    if(!pending.bytes.empty()) {
        if(traffic!=TransportTrafficClass::Realtime || pending.plain==plain) return true;
        // ONLY an unsent live packet may be superseded. Final input stays in
        // owner history for redundant live batches and the independent repair
        // lane. Retire its nonce permanently; never edit/reseal that nonce.
        // Reliable scene/repair packets retain their exact bytes for retry.
        ++statistics_.superseded_live;
    } else {
        const auto interval=traffic==TransportTrafficClass::Control ? std::chrono::milliseconds(100) :
            traffic==TransportTrafficClass::Authoritative ? std::chrono::milliseconds(50) : std::chrono::milliseconds(33);
        if(pending.sent && pending.last_sent_plain==plain && now-pending.sent_at<interval) {
            ++statistics_.suppressed_resends; return true;
        }
    }
    if(sequences_[peer]==UINT64_MAX) return fail("Experimental nonce exhausted.");
    pending.bytes=secure::seal(plain,keys_[peer],configuration_.local_sender_id,
                              ++sequences_[peer],configuration_.match_id);
    pending.plain=std::move(plain);
    pending.traffic=traffic; return true;
}
void Network::receive(const PeerAddress& source,std::span<const std::uint8_t> bytes) {
    ++statistics_.received;
    const unsigned players=session_.player_count(),local=session_.local_owner();
    unsigned peer=players;
    for(unsigned p=0;p<players;++p) if(p!=local && (!local || !p) && configuration_.peers[p].address==source) { peer=p; break; }
    if(peer==players || bytes.size()<50 || bytes.size()>kMaximumWireBytes) { ++statistics_.rejected; return; }
    std::uint64_t sender=0,sequence=0; std::vector<std::uint8_t> plain;
    if(!secure::open(bytes,keys_[peer],configuration_.match_id,sender,sequence,plain) ||
       sender!=configuration_.peers[peer].sender_id || plain.size()<6 ||
       !std::equal(kPrefix.begin(),kPrefix.end(),plain.begin()) || plain[5]>1) { ++statistics_.rejected; return; }
    if(!replay_[peer].accept(sequence)) { ++statistics_.duplicate; return; }
    const auto payload=std::span(plain).subspan(6);
    if(plain[5]==0) {
        const auto result=session_.receive_input(std::uint8_t(peer),payload);
        if(result==SessionInputResult::Rejected) ++statistics_.rejected;
        if(result==SessionInputResult::Conflict) fail("Conflicting authenticated experimental owner input.");
    } else {
        const auto result=session_.receive_control(std::uint8_t(peer),payload);
        if(result==EpochResult::Rejected) ++statistics_.rejected;
        if(result==EpochResult::Conflict) fail("Conflicting authenticated experimental scene control.");
    }
}
bool Network::service(Clock::time_point now) {
    if(!started_ || !error_.empty()) return false;
    try {
        if(clock_started_ && now<last_service_) return fail("Experimental transport clock moved backwards.");
        clock_started_=true; last_service_=now;
        if(!session_.error().empty()) return fail(session_.error());
        transport_.service();
        if(!transport_.is_open()) return fail("Experimental transport closed; speculative gameplay remains halted.");
        for(unsigned n=0;n<32;++n) {
            PeerAddress source; std::vector<std::uint8_t> bytes; std::string error;
            if(!transport_.receive(source,bytes,error)) {
                if(!error.empty()) return fail(error);
                break;
            }
            receive(source,bytes);
            if(!error_.empty() || !session_.error().empty()) return fail(error_.empty()?session_.error():error_);
        }
        // Retired input must not consume a new epoch's network budget. Old
        // Release/ACK repair is reconstructed through previous_control_for().
        if(epoch_!=session_.epoch()) { pending_={}; epoch_=session_.epoch(); }
        const unsigned players=session_.player_count(),local=session_.local_owner();
        for(unsigned peer=0;peer<players;++peer) {
            if(peer==local || (local && peer)) continue;
            const auto current=session_.control_for(std::uint8_t(peer));
            const auto previous=session_.previous_control_for(std::uint8_t(peer));
            if(current && !queue(peer,0,1,encode_epoch_message(*current),TransportTrafficClass::Control,now)) return false;
            if(previous && !queue(peer,1,1,encode_epoch_message(*previous),TransportTrafficClass::Control,now)) return false;
            for(unsigned owner=0;owner<players;++owner) for(unsigned repair=0;repair<2;++repair) {
                const unsigned lane=2+owner*2+repair;
                // Reliable repair keeps its exact predecessor under
                // backpressure. Live input can refresh without losing the
                // owner's immutable repair history.
                if(!pending_[peer][lane].bytes.empty() && repair) continue;
                const auto packet=session_.packet_for(std::uint8_t(owner),std::uint8_t(peer),
                    repair ? OwnerInputSend::Repair : OwnerInputSend::Live);
                // The live lane already carries cumulative actual-input ACKs.
                // Do not send a second ACK-only repair when there is no hole.
                if(packet && (!repair || packet->count) && !queue(peer,lane,0,encode_owner_inputs(*packet),
                    repair ? TransportTrafficClass::Authoritative : TransportTrafficClass::Realtime,now)) return false;
            }
        }
        // Round-robin both dimensions; control goes first on the first pass,
        // but queued input/repair cannot be starved by continuous control.
        unsigned attempts=0;
        for(unsigned round=0;round<kLanes;++round) {
            const unsigned lane=(next_lane_+round)%kLanes;
            for(unsigned route=0;route<players;++route) {
                const unsigned peer=(next_route_+route)%players;
                auto& packet=pending_[peer][lane];
                if(packet.bytes.empty()) continue;
                // A hot UI poll must not hammer a closed/congested SDK queue.
                // This does not pace incoming packets or a different route.
                if(packet.attempted && now-packet.attempted_at<std::chrono::milliseconds(5)) continue;
                if(++attempts>32) break;
                packet.attempted=true; packet.attempted_at=now;
                std::string error;
                const auto result=transport_.send_status(configuration_.peers[peer].address,packet.bytes,packet.traffic,error);
                if(result==DatagramSendStatus::Sent) {
                    ++statistics_.sent; packet.bytes.clear(); packet.last_sent_plain=std::move(packet.plain);
                    packet.sent=true; packet.sent_at=now; packet.attempted=false;
                }
                else if(result==DatagramSendStatus::WouldBlock) ++statistics_.backpressure;
                else ++statistics_.send_errors; // retain exact packet; route recovery owns the retry
            }
            if(attempts>=32) break;
        }
        next_route_=(next_route_+1)%players; next_lane_=(next_lane_+1)%kLanes;
        statistics_.pending_packets=0;
        for(const auto& route:pending_) for(const auto& packet:route) statistics_.pending_packets+=!packet.bytes.empty();
        return true;
    } catch(const std::exception& exception) {
        return fail(std::string("Experimental transport exception: ")+exception.what());
    } catch(...) { return fail("Unexpected experimental transport failure."); }
}
}
