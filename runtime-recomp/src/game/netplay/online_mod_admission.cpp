#include "direct_session.hpp"
#include "mods/online_mod_manifest.hpp"
#include <algorithm>

namespace dkr::runtime::netplay {
namespace {
constexpr std::size_t MaxModPacket=8192+142,MaxModQueue=128*1024;
bool waiting(ConnectionState state){return state==ConnectionState::Hosting || state==ConnectionState::Lobby;}
bool joining(ConnectionState state){return state==ConnectionState::Connecting || state==ConnectionState::AwaitingApproval;}
}
bool DirectSession::configure_mod_admission(std::string digest,std::uint32_t size) {
    std::scoped_lock lock(mutex_);
    if(state_!=ConnectionState::Offline || !valid_mod_manifest_hash(digest) ||
       (digest.empty()?size!=0:(!size || size>dkr::mods::online::MaxManifestBytes)))return false;
    manifest_.mod_manifest_hash=std::move(digest);mod_manifest_bytes_=size;mod_offer_.reset();
    mod_inbound_.clear();mod_inbound_bytes_=0;return true;
}
void DirectSession::send_mod_offer_locked(PendingRecord& pending) {
    if(!pending.mods_approved || pending.mods_verified || manifest_.mod_manifest_hash.empty() || !mod_manifest_bytes_)return;
    send_with_key(pending.address,pending.key,protocol::MessageType::ModOffer,
        protocol::encode_mod_offer({manifest_.mod_manifest_hash,mod_manifest_bytes_}));
    pending.last_acknowledgement=std::chrono::steady_clock::now();
}
bool DirectSession::take_mod_packet(ModPacket& packet) {
    std::scoped_lock lock(mutex_);if(mod_inbound_.empty())return false;
    mod_inbound_bytes_-=mod_inbound_.front().bytes.size();packet=std::move(mod_inbound_.front());mod_inbound_.pop_front();return true;
}
std::size_t DirectSession::mod_chunk_budget()const {
    std::scoped_lock lock(mutex_);
    if(!transport_ || transport_->maximum_plaintext_datagram_bytes()<=174)return 0;
    return (std::min)(std::size_t(8192),transport_->maximum_plaintext_datagram_bytes()-174);
}
bool DirectSession::send_mod_packet(std::uint64_t target,std::span<const std::uint8_t> bytes,bool data,std::string& error) {
    std::scoped_lock lock(mutex_);
    if(!transport_ || bytes.empty() || bytes.size()>MaxModPacket ||
       bytes.size()+32>transport_->maximum_plaintext_datagram_bytes()) {error="Invalid online mod packet budget.";return false;}
    const auto traffic=data?TransportTrafficClass::Checkpoint:TransportTrafficClass::Control;
    // One request/response is in flight per recipient. Do not grow the general
    // reliable queue behind a slow import worker or stalled WebRTC channel.
    PeerAddress address{};const secure::Key* key=nullptr;
    if(is_host_ && waiting(state_)) {
        auto* pending=pending_by_sender(target);
        if(pending && pending->mods_approved && !pending->mods_verified){address=pending->address;key=&pending->key;}
    } else if(!is_host_ && joining(state_) && mod_offer_ && (target==0 || target==host_sender_id_)) {address=host_address_;key=&key_;}
    if(!key) {error="Mod transfers are restricted to approved provisional lobby admission.";return false;}
    const auto queued=[&](const auto& queue){return std::any_of(queue.begin(),queue.end(),[&](const auto& packet){return packet.destination==address && (packet.type==protocol::MessageType::ModControl || packet.type==protocol::MessageType::ModData);});};
    if(!transport_->traffic_ready(address,traffic) || queued(normal_priority_outbound_) || queued(bulk_outbound_)) {error.clear();return false;}
    const bool sent=send_with_key(address,*key,data?protocol::MessageType::ModData:protocol::MessageType::ModControl,bytes);
    if(sent){worker_wake_=true;state_changed_.notify_all();}error.clear();return sent;
}
void DirectSession::clear_pending_mod_route_locked(PendingRecord& pending) {
    if(pending.mod_route_retained && transport_)transport_->release_peer_route(pending.address);
    if(pending.active) {
        for(auto* queue:{&normal_priority_outbound_,&bulk_outbound_})
            std::erase_if(*queue,[&](const auto& p){return p.destination==pending.address &&
                (p.type==protocol::MessageType::ModOffer || p.type==protocol::MessageType::ModControl || p.type==protocol::MessageType::ModData);});
        std::erase_if(mod_inbound_,[&](const auto& p){return p.sender==pending.sender_id;});
        mod_inbound_bytes_=0;for(const auto& p:mod_inbound_)mod_inbound_bytes_+=p.bytes.size();
    }
    pending.mod_route_retained=false;
}
bool DirectSession::complete_local_mod_preparation(std::string_view digest,std::string& error) {
    std::scoped_lock lock(mutex_);
    if(is_host_ || !joining(state_) || !mod_offer_ || mod_offer_->manifest_hash!=digest) {
        error="The prepared mods do not belong to the current host offer.";return false;
    }
    manifest_.mod_manifest_hash=std::string(digest);room_view_.manifest.mod_manifest_hash=std::string(digest);error.clear();return true;
}
bool DirectSession::complete_mod_admission(std::uint64_t request,std::string_view digest,std::string& error) {
    std::scoped_lock lock(mutex_);auto* pending=pending_by_request(request);
    if(!is_host_ || !waiting(state_) || !pending || !pending->mods_approved || manifest_.mod_manifest_hash.empty() ||
       digest!=manifest_.mod_manifest_hash) {error="The mod preparation proof does not belong to this approved request.";return false;}
    pending->mods_verified=true;pending->manifest.mod_manifest_hash=std::string(digest);
    return approve_join_locked(request,error);
}
bool DirectSession::handle_mod_packet_locked(std::uint64_t sender,const protocol::Datagram& packet) {
    const auto type=packet.header.type;
    if(type!=protocol::MessageType::ModOffer && type!=protocol::MessageType::ModControl && type!=protocol::MessageType::ModData)return false;
    if(!is_host_ && joining(state_) && sender==host_sender_id_) {
        if(type==protocol::MessageType::ModOffer) {
            protocol::ModOfferPayload offer;
            if(!protocol::decode_mod_offer(packet.payload,offer))return true;
            if(mod_offer_ && *mod_offer_!=offer){fail_locked("The host changed its mods during admission. Rejoin to review the new selection.");return true;}
            mod_offer_=std::move(offer);state_=ConnectionState::AwaitingApproval;
            last_admission_response_=std::chrono::steady_clock::now();status_="Host mods require review and consent before joining.";return true;
        }
        if(!mod_offer_)return true;
        last_admission_response_=std::chrono::steady_clock::now();
    } else if(is_host_ && waiting(state_)) {
        const auto* pending=pending_by_sender(sender);
        if(type==protocol::MessageType::ModOffer || !pending || !pending->mods_approved || pending->mods_verified)return true;
    } else return true;
    // Drop/retry rather than grow a queue or terminate unrelated lobby peers.
    if(packet.payload.empty() || packet.payload.size()>MaxModPacket || packet.payload.size()>MaxModQueue-mod_inbound_bytes_ || mod_inbound_.size()>=32)return true;
    const auto* pending=is_host_?pending_by_sender(sender):nullptr;
    mod_inbound_bytes_+=packet.payload.size();
    mod_inbound_.push_back({sender,type==protocol::MessageType::ModData,packet.payload,pending?pending->sender_id:0});return true;
}
}
