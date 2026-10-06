#pragma once
#include "direct_session.hpp"
#include "../mods/online_mod_sync.hpp"

namespace dkr::runtime::netplay {
// One adapter for all connection routes/backends. This cannot admit a player
// or touch a save before DirectSession's provisional-mod gate completes.
class DirectModSyncLink final:public dkr::mods::online::SyncLink {
public:
    explicit DirectModSyncLink(DirectSession& session):session_(session){}
    dkr::mods::online::SyncNetworkView view()const override {
        const auto state=session_.view();dkr::mods::online::SyncNetworkView result;
        result.host=state.host;result.hosting=state.state==ConnectionState::Hosting;
        result.joining=state.state==ConnectionState::Connecting || state.state==ConnectionState::AwaitingApproval;
        result.lobby=state.state==ConnectionState::Lobby;result.failed=state.state==ConnectionState::Failed;
        if(result.failed)result.failure_reason=state.status;
        result.game_active=state.state==ConnectionState::Loading || state.state==ConnectionState::Running;
        result.session_active=result.hosting || result.lobby || result.game_active;
        if(state.mod_offer)result.offer=dkr::mods::online::SyncOffer{state.mod_offer->manifest_hash,state.mod_offer->manifest_bytes};
        for(const auto& request:state.pending_joins)if(request.preparing_mods)result.preparing_requests.push_back(request.request_id);
        return result;
    }
    bool receive(dkr::mods::online::SyncPacket& result)override {
        DirectSession::ModPacket packet;if(!session_.take_mod_packet(packet))return false;
        result={packet.sender,packet.request_id,std::move(packet.bytes)};return true;
    }
    std::size_t chunk_budget()const override{return session_.mod_chunk_budget();}
    bool send(std::uint64_t target,dkr::mods::View bytes,bool data,std::string& error)override{return session_.send_mod_packet(target,bytes,data,error);}
    bool verify_local(std::string_view digest,std::string& error)override{return session_.complete_local_mod_preparation(digest,error);}
    bool admit(std::uint64_t request,std::string_view digest,std::string& error)override{return session_.complete_mod_admission(request,digest,error);}
    void reject(std::uint64_t request,std::string_view)override{std::string error;session_.reject_join(request,false,error);}
    void leave(std::string_view reason)override{session_.disconnect(std::string(reason));}
private:
    DirectSession& session_;
};
}
