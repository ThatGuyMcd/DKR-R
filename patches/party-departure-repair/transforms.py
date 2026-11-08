"""Explicit party departures; preserve accepted startup, checkpoint and game code."""
from pathlib import Path
import hashlib
AUTH=Path(__file__).resolve().parent
PRIOR_IDENTITY='2a82142dc00619b29b80f5f0b6485c3ffe8c0a33e89a7a6c48d0a6ac8eeb4a66'
def once(t,a,b):
 if t.count(a)!=1:raise ValueError('Party departure anchor changed: '+a[:160])
 return t.replace(a,b,1)
def snippet(name):return (AUTH/'native'/name).read_text()
def contract():
 h=hashlib.sha256()
 for p in sorted([AUTH/'transforms.py',*(AUTH/'native').iterdir()],key=lambda p:p.name):
  if p.is_file():h.update(p.name.encode());h.update(p.read_bytes().replace(b'\r\n',b'\n'))
 return h.hexdigest()
def direct(t):
 t='#include "departure.hpp"\n'+t
 t=once(t,'void DirectSession::disconnect(std::string_view reason) {\n    std::scoped_lock lock(mutex_);',
  'void DirectSession::disconnect(std::string_view reason) {\n    std::scoped_lock lock(mutex_);\n'+snippet('close_notice.inl'))
 t=once(t,'    PeerAddress departed{};\n    while(transport_->take_peer_departure(departed)) {',
  snippet('retire_member.inl')+'\n    PeerAddress departed{};\n    while(transport_->take_peer_departure(departed)) {')
 old='''        {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
         if(party->enabled&&is_host_)if(auto* member=party->peer(departed)) {
            const auto id=member->id;if(party->participant(id)&&party->launch){party->stop_requested=true;party->status="A racer disconnected.";}
            std::string ignored;party->party.remove(id,ignored);party->ready.erase(id);crypto_wipe(member->key.data(),member->key.size());
            std::erase_if(party->peers,[&](const auto& p){return p.id==id;});continue;
         }}'''
 t=once(t,old,'''        {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
         if(party->enabled){
            if(is_host_){if(const auto* member=party->peer(departed))if(retire_party_member(member->id,"transport closed"))continue;}
            else if(departed==host_address_){fail_locked("The host disconnected. The lobby has closed.");return;}
         }}''')
 anchor='        if(dkr::runtime::rollback_check::diagnostic_type(packet.header.type)) {'
 t=once(t,anchor,'''        // Only the authenticated host's matching receipt is consumed during
        // local teardown. Delayed launch/Room packets cannot re-arm the game.
        if(dkr::runtime::party_departure::waiting(this)){
            dkr::runtime::spectators::LinkMessage reply;
            if(packet_sender==host_sender_id_&&source==host_address_&&
               unsigned(packet.header.type)==dkr::runtime::spectators::PartyMessageType&&
               dkr::runtime::spectators::link_decode(packet.payload,reply)&&
               reply.kind==dkr::runtime::spectators::LinkKind::Leave&&
               dkr::runtime::party_departure::notice(reply.body,2,sender_id_))
                dkr::runtime::party_departure::acknowledge(this,sender_id_);
            continue;
        }
        // The generic Disconnect handler only knows the old four-port peer
        // table. An admitted party member has its own authenticated identity.
        if(packet.header.type==protocol::MessageType::Disconnect){
            auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
            if(party->enabled){
                if(is_host_)retire_party_member(packet_sender,"disconnect notice");
                else if(packet_sender==host_sender_id_&&source==host_address_){fail_locked("The host disconnected. The lobby has closed.");return;}
                continue;
            }
        }
'''+anchor)
 old='''        case watch::LinkKind::Leave: {
            if(party->participant(packet_sender)&&party->launch){party->stop_requested=true;party->status="A racer left the lobby.";}
            std::string ignored;party->party.remove(packet_sender,ignored);party->ready.erase(packet_sender);
            transport_->release_peer_route(member->address);crypto_wipe(member->key.data(),member->key.size());
            std::erase_if(party->peers,[&](const auto& p){return p.id==packet_sender;});break;
        }'''
 t=once(t,old,'''        case watch::LinkKind::Returned:
            if(party->launch&&party->participant(packet_sender)){
                party->stopping=party->stop_requested=true;party->running=false;
                party->status=(identity?identity->name:std::string("A racer"))+" returned to the lobby. The match has ended.";
                std::fprintf(stderr,"[lobby][departure] racer returned; match retirement requested\\n");
            }
            break;
        case watch::LinkKind::Leave: {
            const bool receipt=dkr::runtime::party_departure::notice(message.body,1,packet_sender);
            if(!message.body.empty()&&!receipt)break;
            if(receipt){
                const auto bytes=watch::link_encode(watch::LinkKind::Leave,dkr::runtime::party_departure::notice(2,packet_sender));
                send_with_key(member->address,member->key,static_cast<protocol::MessageType>(watch::PartyMessageType),bytes);
                flush_outbound_locked();
            }
            retire_party_member(packet_sender,"leave notice");break;
        }''')
 old='''            for(auto it=party->peers.begin();it!=party->peers.end();) {
                if(now-it->heard<std::chrono::seconds(30)){++it;continue;}
                const auto id=it->id;
                if(party->participant(id)&&party->launch)party->stop_requested=true;
                std::string ignored;party->party.remove(id,ignored);party->ready.erase(id);
                transport_->release_peer_route(it->address);crypto_wipe(it->key.data(),it->key.size());it=party->peers.erase(it);
            }'''
 t=once(t,old,'''            std::vector<std::uint64_t> silent_members;
            for(const auto& peer:party->peers)
                if(now-peer.heard>=dkr::runtime::party_departure::SilenceLimit)silent_members.push_back(peer.id);
            for(auto id:silent_members)retire_party_member(id,"authenticated traffic silent for 10 seconds");''')
 t=once(t,'now-last_admission_response_>std::chrono::seconds(30)',
  'now-last_admission_response_>dkr::runtime::party_departure::SilenceLimit')
 t=once(t,'    if(dkr::runtime::rollback_check::engaged(this))',
  '    if(dkr::runtime::party_departure::waiting(this)){flush_outbound_locked();return;}\n    if(dkr::runtime::rollback_check::engaged(this))')
 return t
def runtime(t):
 t=once(t,';const auto h=digest(', ';base+="'+contract()+'";const auto h=digest(')
 return once(t,'            if(lifecycle==ui::LifecycleRequest::Exit||lifecycle==ui::LifecycleRequest::Restart||requested_stop)lobby.disconnect(initial.host?',
  '            std::fprintf(stderr,"[party][exit] role=%s requested=%u lifecycle=%d\\n",initial.host?"host":"client",unsigned(requested_stop.load()),int(lifecycle));\n            if(lifecycle==ui::LifecycleRequest::Exit||lifecycle==ui::LifecycleRequest::Restart||requested_stop)lobby.disconnect(initial.host?')
def party_runtime(t):
 return once(t,'        std::scoped_lock lock(hub->mutex);std::string e;\n        const auto& latest=hub->party.snapshot();',
  '        std::scoped_lock lock(hub->mutex);std::string e;\n        if(hub->stopping||!hub->enabled)return; // A retiring owner cannot republish its old racing roster.\n        const auto& latest=hub->party.snapshot();')
