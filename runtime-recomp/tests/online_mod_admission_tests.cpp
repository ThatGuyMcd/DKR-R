#include "direct_session.hpp"
#include "online_mod_sync_link.hpp"
#include "mods/online_mod_manifest.hpp"
#include <stdexcept>
#include <iostream>
namespace dkr::runtime::netplay {
namespace {
unsigned checks=0;
void check(bool value){if(!value)throw std::runtime_error("Mod admission assertion "+std::to_string(checks));++checks;}
class ModTestTransport final:public SessionTransport {
public:
    unsigned retained=0,released=0;
    bool open(std::uint16_t,std::string&)override{return true;}
    void close()override{}
    bool is_open()const override{return true;}
    std::uint16_t local_port()const override{return 0;}
    void retain_peer_route(const PeerAddress&)override{++retained;}
    void release_peer_route(const PeerAddress&)override{++released;}
    bool receive(PeerAddress&,std::vector<std::uint8_t>&,std::string&)override{return false;}
    DatagramSendStatus send_status(const PeerAddress&,std::span<const std::uint8_t>,TransportTrafficClass,std::string&)override{return DatagramSendStatus::Sent;}
};
}
struct DirectSessionTestAccess {
    static void stop(DirectSession& s){s.worker_stop_.store(true);s.state_changed_.notify_all();s.network_worker_.join();}
    static void run(Revision revision,SynchronizationMode mode) {
        DirectSession host;stop(host);std::string error;
        CompatibilityManifest m;m.release_version="test";m.build_fingerprint="test";
        m.architecture="test";m.floating_point_mode="test";
        m.revision=revision;
        check(host.enable_owned_backend(true));
        host.configure_manifest(m);const std::string digest(64,'a'),wrong(64,'b');
        check(!host.configure_mod_admission(digest,0));check(!host.configure_mod_admission("bad",32));
        check(host.configure_mod_admission(digest,dkr::mods::online::MaxManifestBytes));
        check(!host.configure_mod_admission(digest,dkr::mods::online::MaxManifestBytes+1));
        const auto large_offer=protocol::encode_mod_offer({digest,dkr::mods::online::MaxManifestBytes});
        protocol::ModOfferPayload decoded_offer;
        check(!large_offer.empty() && protocol::decode_mod_offer(large_offer,decoded_offer));
        check(decoded_offer.manifest_bytes==dkr::mods::online::MaxManifestBytes);
        check(protocol::encode_mod_offer({digest,dkr::mods::online::MaxManifestBytes+1}).empty());
        check(host.configure_mod_admission(digest,128));
        host.session_save_=std::vector<std::uint8_t>(512,7);host.session_save_generation_=1;
        host.manifest_.session_save_hash=stable_hash(std::string_view(reinterpret_cast<const char*>(host.session_save_.data()),512));
        auto route=std::make_unique<ModTestTransport>();auto* counters=route.get();host.transport_=std::move(route);
        Rules rules;rules.maximum_players=4;rules.synchronization=mode;
        if(mode==SynchronizationMode::Lockstep)rules.rollback_window=0;
        check(host.lobby_.create("room","modtest","Mods",Visibility::Private,"100","Host",host.manifest_,rules,error));
        host.state_=ConnectionState::Hosting;host.is_host_=true;host.sender_id_=100;host.match_id_=101;host.room_view_=host.lobby_.room();
        check(host.admission_incompatibility(m).empty());
        const auto original_candidate=m;
        check(session_content_sync_incompatibility(host.manifest_,m,true).empty());
        check(!session_content_sync_incompatibility(host.manifest_,m,false).empty());
        check(m==original_candidate); // eligibility must never claim downloaded/verified mods
        auto invalid_mod=m;invalid_mod.mod_manifest_hash="bad";
        check(!session_content_sync_incompatibility(host.manifest_,invalid_mod,true).empty());
        auto mismatch=m;mismatch.magic_codes_hash=99;check(!host.admission_incompatibility(mismatch).empty());
        mismatch=m;mismatch.revision=revision==Revision::UsV77?Revision::UsV80:Revision::UsV77;
        check(!host.admission_incompatibility(mismatch).empty());
        check(!host.configure_mod_admission(wrong,128)); // frozen while hosting
        for(unsigned i=0;i<3;++i) {
            auto& p=host.pending_joins_[i];p.active=true;p.sender_id=201+i;
            p.address.size=1;p.address.storage[0]=i+1;p.key=secure::generate_key();p.manifest=m;p.display_name="Client";
            check(host.approve_join(p.sender_id,error));check(p.mods_approved && !p.mods_verified);
        }
        check(host.occupied_players()==1 && counters->retained==3);
        // Bounded independent bursts; control remains ahead of bulk packets.
        std::vector<std::uint8_t> mod_bytes(826,7);
        for(auto sender:{201U,202U,203U}) {
            for(unsigned packet=0;packet<8;++packet) {
                mod_bytes[0]=static_cast<std::uint8_t>(packet); // Distinct offsets, not coalesced retries.
                check(host.send_mod_packet(sender,mod_bytes,true,error));
            }
            check(!host.send_mod_packet(sender,mod_bytes,true,error) && error.empty());
            check(host.send_mod_packet(sender,mod_bytes,false,error));
            check(!host.send_mod_packet(sender,mod_bytes,false,error) && error.empty());
        }
        check(host.bulk_outbound_.size()==24);
        host.bulk_outbound_.clear();host.normal_priority_outbound_.clear();
        check(!host.complete_mod_admission(201,wrong,error));check(host.occupied_players()==1);
        protocol::Datagram data;data.header.type=protocol::MessageType::ModControl;data.payload={1,2,3};
        check(host.handle_mod_packet_locked(999,data));check(host.mod_inbound_.empty());
        for(unsigned i=0;i<100;++i)check(host.handle_mod_packet_locked(201,data));
        check(host.pending_by_sender(201)->last_seen.time_since_epoch().count()!=0);
        check(host.mod_inbound_.size()==32 && host.state_==ConnectionState::Hosting);
        DirectSession::ModPacket taken;check(host.take_mod_packet(taken)&&taken.sender==201&&!taken.data);
        check(host.complete_mod_admission(201,digest,error));check(host.occupied_players()==2);
        check(counters->retained==3); // retain exactly once across preparation -> admission
        check(host.complete_mod_admission(203,digest,error));check(host.occupied_players()==3);
        check(host.reject_join(202,false,error));check(counters->released==1 && host.occupied_players()==3);
        check(host.complete_mod_admission(202,digest,error)==false);
        host.state_=ConnectionState::Running;check(host.handle_mod_packet_locked(203,data));
        std::vector<std::uint8_t> bytes{1};check(!host.send_mod_packet(203,bytes,true,error));
        DirectSession client;stop(client);client.state_=ConnectionState::Connecting;client.host_sender_id_=100;
        {
            DirectSession rejected;stop(rejected);rejected.state_=ConnectionState::Connecting;
            protocol::Datagram rejection;rejection.header.type=protocol::MessageType::HelloAck;
            const std::string reason="Magic Codes do not match.|magic:2:0";
            rejection.payload=protocol::encode_hello_ack({false,0,reason,{},host.manifest_});
            rejected.handle_client_packet(rejection);
            DirectModSyncLink rejected_link(rejected);const auto rejected_view=rejected_link.view();
            check(rejected_view.failed && rejected_view.failure_reason==reason);
            check(rejected.view().compatibility_sync_offer==host.manifest_);
        }
        check(!client.complete_local_mod_preparation(digest,error));
        protocol::Datagram offer;offer.header.type=protocol::MessageType::ModOffer;
        offer.payload=protocol::encode_mod_offer({digest,128});
        check(client.handle_mod_packet_locked(999,offer)&&!client.mod_offer_);
        check(client.handle_mod_packet_locked(100,offer)&&client.mod_offer_);
        check(client.state_==ConnectionState::AwaitingApproval && client.manifest_.mod_manifest_hash.empty());
        check(client.complete_local_mod_preparation(digest,error));
        check(client.manifest_.mod_manifest_hash==digest);
        check(client.handle_mod_packet_locked(100,offer)&&client.state_==ConnectionState::AwaitingApproval);
        offer.payload=protocol::encode_mod_offer({wrong,128});client.handle_mod_packet_locked(100,offer);
        check(client.state_==ConnectionState::Failed);
        protocol::ModOfferPayload decoded;check(protocol::decode_mod_offer(protocol::encode_mod_offer({digest,128}),decoded));
        check(decoded.manifest_hash==digest && decoded.manifest_bytes==128);
        auto truncated=protocol::encode_mod_offer({digest,128});truncated.pop_back();
        check(!protocol::decode_mod_offer(truncated,decoded));
        auto before=host.manifest_;auto after=before;after.mod_manifest_hash=wrong;
        check(manifest_hash(before)!=manifest_hash(after));check(!incompatibility_reason(before,after).empty());
        client.disconnect();check(!client.mod_offer_&&client.mod_inbound_.empty());
    }
};
}
int main(){try{
    using namespace dkr::runtime::netplay;
    for(auto revision:{Revision::UsV77,Revision::UsV80})
        for(auto mode:{SynchronizationMode::Rollback,SynchronizationMode::Lockstep,SynchronizationMode::ExperimentalRollback})
            DirectSessionTestAccess::run(revision,mode);
    std::cout<<checks<<" mod admission/roster/consent/queue/lifecycle checks passed across two revisions and three backends. No game window opened.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
