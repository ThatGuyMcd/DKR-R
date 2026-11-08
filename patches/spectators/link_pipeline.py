"""Authenticated party admission in isolated first-party build copies."""
from pathlib import Path
AUTH=Path(__file__).resolve().parent

def once(text,old,new):
 if text.count(old)!=1:raise ValueError('Party link anchor changed: '+old[:160])
 return text.replace(old,new,1)
def transport(text):
 text=once(text,'if(departed_peers_.size()<4U)','if(departed_peers_.size()<16U)')
 text=once(text,'pinned_routes_.size() < 4U','pinned_routes_.size() < 16U')
 return once(text,'(peers_.size() >= 8U &&','(peers_.size() >= 16U &&')

def direct(text):
 text='#include "party_link.hpp"\n#include "party_codec.hpp"\n#include "monocypher.h"\n'+text
 text=once(text,'    transport_closer_.stop();','    transport_closer_.stop();\n    dkr::runtime::spectators::forget_party_link(this);')
 text=once(text,'''    state_ = ConnectionState::Hosting;
    worker_wake_ = true;''','''    state_ = ConnectionState::Hosting;
    {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
     if(party->requested&&method_==ConnectionMethod::QuickJoin&&rules.synchronization==SynchronizationMode::ExperimentalRollback) {
        party->enabled=party->host=party->admitted=true;party->local=party->authority=sender_id_;party->room_id=match_id_;
        if(!party->party.create(sender_id_,local_name_,error))return false;
        party->seed=session_save_;party->save_hash=manifest_.session_save_hash;party->save_generation=session_save_generation_;
        party->ready[sender_id_]=false;party->maximum_racers=rules.maximum_players;
     }}
    worker_wake_ = true;''')
 text=once(text,'    const auto payload = protocol::encode_hello(hello);','''    auto payload = protocol::encode_hello(hello);
    {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
     if(party->requested){const std::array<std::uint8_t,8> marker{'D','K','R','P','T','Y',1,std::uint8_t(party->join_watch)};
        payload.insert(payload.begin(),marker.begin(),marker.end());}}
''')
 text=once(text,'    if (lobby_locked_ || (state_ != ConnectionState::Lobby && state_ != ConnectionState::Hosting)) {',
  '    if (lobby_locked_ || ((state_ != ConnectionState::Lobby && state_ != ConnectionState::Hosting)&&!party_join)) {')
 anchor='''    protocol::HelloPayload hello{};
    std::string error;
    if (!protocol::decode_hello(plain, hello, error)) {'''
 text=once(text,anchor,'''    auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
    bool party_join=false,watch_join=false;
    const std::array<std::uint8_t,7> marker{'D','K','R','P','T','Y',1};
    if(plain.size()>=8&&std::equal(marker.begin(),marker.end(),plain.begin())&&plain[7]<=1) {
        party_join=true;watch_join=plain[7]!=0;plain.erase(plain.begin(),plain.begin()+8);
    }
    // The host owns this policy. A racer advertises support for parties, not
    // permission to enable them. A non-spectator lobby uses normal admission.
    if(party_join&&!party->enabled&&!watch_join)party_join=false;
    if(party_join!=party->enabled || (party_join&&party->launch&&!watch_join)) {
        send_with_key(source,peer_key,protocol::MessageType::HelloAck,
            protocol::encode_hello_ack({false,0,party->enabled?"Join using Racer or Watch Race. A running race accepts spectators only.":"This host has disabled spectators. Join as a racer."}));return;
    }
'''+anchor)
 anchor='''    ++join_packets_accepted_;
    if (PeerRecord* existing = peer_by_sender(packet_sender)) {'''
 text=once(text,anchor,'''    ++join_packets_accepted_;
    if(party_join) {
        if(auto* existing=party->peer(packet_sender)) {
            if(existing->address!=source||existing->key!=peer_key)return;
            const auto body=protocol::encode_hello_ack({true,0,"Joined the lobby",session_save_,{},session_save_generation_,manifest_.session_save_hash});
            send_with_key(existing->address,existing->key,static_cast<protocol::MessageType>(dkr::runtime::spectators::PartyMessageType),
                dkr::runtime::spectators::link_encode(dkr::runtime::spectators::LinkKind::Accepted,body));party->advertised={};return;
        }
    }
    if (PeerRecord* existing = peer_by_sender(packet_sender)) {''')
 text=once(text,'    pending->last_seen = std::chrono::steady_clock::now();','''    pending->last_seen = std::chrono::steady_clock::now();
    if(party_join){
        const auto role=party->pending_watch.find(packet_sender);
        if(role!=party->pending_watch.end()&&role->second!=watch_join)return;
        party->pending_watch[packet_sender]=watch_join;
    }''')
 old='''    if (!is_host_ || (state_ != ConnectionState::Hosting &&
                      state_ != ConnectionState::Lobby)) {
        error = "Only the host can approve racers while waiting in the lobby.";'''
 new='''    if (!is_host_ || ((state_ != ConnectionState::Hosting &&
                      state_ != ConnectionState::Lobby)&&!dkr::runtime::spectators::party_enabled(this))) {
        error = "Only the host can approve racers while waiting in the lobby.";'''
 text=once(text,old,new)
 text=once(text,'            if (occupied_players()+reserved>=room_view_.rules.maximum_players) {',
  '            if (!dkr::runtime::spectators::party_enabled(this)&&occupied_players()+reserved>=room_view_.rules.maximum_players) {')
 anchor='    const std::string peer_id = std::to_string(pending->sender_id);'
 text=once(text,anchor,(AUTH/'native/link_approve.inl').read_text()+'\n'+anchor)
 anchor="    // Rejections sent before a pending record is allocated use the host's"
 text=once(text,anchor,'''    {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
     if(auto* p=party->peer(address);p&&p->key==key)return p->sequence;}
'''+anchor)
 anchor='''            else {
                auto* pending=pending_by_sender(packet_sender);
                if(!pending || !pending->mods_approved || !(pending->address==source))continue;
                receive_key=&pending->key;provisional_mod_peer=true;
            }'''
 text=once(text,anchor,'''            else {
                auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
                if(auto* member=party->peer(packet_sender);member&&member->address==source)receive_key=&member->key;
                else {auto* pending=pending_by_sender(packet_sender);
                    if(!pending || !pending->mods_approved || !(pending->address==source))continue;
                    receive_key=&pending->key;provisional_mod_peer=true;}
            }''')
 anchor='        handle_packet(source, packet_sender, packet);'
 text=once(text,'        if (!receive_windows[receive_sequence_lane(packet.header.type)]',
 '''        if(unsigned(packet.header.type)==dkr::runtime::spectators::PartyMessageType&&packet.header.frame>=5)continue;
        const auto sequence_lane=unsigned(packet.header.type)==dkr::runtime::spectators::PartyMessageType?packet.header.frame:receive_sequence_lane(packet.header.type);
        if (!receive_windows[sequence_lane]''')
 text=once(text,anchor,(AUTH/'native/link_receive.inl').read_text()+'\n'+anchor)
 anchor='    constexpr auto control_retry = std::chrono::milliseconds(250);'
 text=once(text,anchor,(AUTH/'native/link_service.inl').read_text()+'\n'+anchor)
 anchor='    while(transport_->take_peer_departure(departed)) {'
 text=once(text,anchor,anchor+'''
        {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
         if(party->enabled&&is_host_)if(auto* member=party->peer(departed)) {
            const auto id=member->id;if(party->participant(id)&&party->launch){party->stop_requested=true;party->status="A racer disconnected.";}
            std::string ignored;party->party.remove(id,ignored);party->ready.erase(id);crypto_wipe(member->key.data(),member->key.size());
            std::erase_if(party->peers,[&](const auto& p){return p.id==id;});continue;
         }}
''')
 for method,body in (
  ('set_ready(bool ready, std::string& error)','dkr::runtime::spectators::party_ready(this,ready);error.clear();return true;'),
  ('request_start(std::string& error)','if(!is_host_){error="Only the host can start.";return false;}dkr::runtime::spectators::party_start(this);error.clear();return true;')):
  anchor='bool DirectSession::'+method+' {\n    std::scoped_lock lock(mutex_);'
  text=once(text,anchor,anchor+'\n    if(dkr::runtime::spectators::party_enabled(this)){'+body+'}')
 anchor='void DirectSession::fail_runtime_start(std::string reason) {\n    std::scoped_lock lock(mutex_);'
 text=once(text,anchor,anchor+'\n    if(dkr::runtime::spectators::party_enabled(this)){dkr::runtime::spectators::party_stop(this,std::move(reason));return;}')
 anchor='    status_ = message;\n    if (is_host_) broadcast(protocol::MessageType::Disconnect, failure_payload_);'
 text=once(text,anchor,'''    status_ = message;
    {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
     if(party->enabled){party->stopping=true;party->running=false;party->status=message;
        const auto body=dkr::runtime::spectators::link_encode(dkr::runtime::spectators::LinkKind::Leave);
        if(is_host_)for(const auto& peer:party->peers)send_with_key(peer.address,peer.key,static_cast<protocol::MessageType>(dkr::runtime::spectators::PartyMessageType),body);
        else if(host_address_)send_with_key(host_address_,key_,static_cast<protocol::MessageType>(dkr::runtime::spectators::PartyMessageType),body);
     }}
    if (is_host_) broadcast(protocol::MessageType::Disconnect, failure_payload_);''')
 anchor='    result.mod_offer=mod_offer_;'
 text=once(text,anchor,'''    {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
     if(party->enabled){result.room=room_view_;result.room.players={};result.room.generation=party->party.snapshot().generation;
        result.room.phase=party->launch&&!party->stopping?RoomPhase::Running:RoomPhase::Waiting;
        for(const auto& m:party->party.snapshot().members)if(m.port){auto& p=result.room.players[*m.port];
            p.occupied=p.compatible=true;p.host=m.host;p.slot=*m.port;p.peer_id=std::to_string(m.id);p.display_name=m.name;
            p.ready=party->ready.contains(m.id)&&party->ready.at(m.id);p.loaded=party->running;result.online_save_ready[*m.port]=true;}
        if(!party->status.empty())result.status=party->status;
        result.network_rtt_ms=party->rtt_ms;
        if(is_host_)for(const auto& peer:party->peers)if(const auto* m=party->party.find(peer.id);m&&m->role==dkr::runtime::spectators::Role::Racer&&peer.rtt_count)
            for(unsigned i=0;i<peer.rtt_count;++i)result.network_rtt_ms=std::max(result.network_rtt_ms,peer.rtts[i]);
     }}
'''+anchor)
 anchor='    if (transport_->is_open() && state_ != ConnectionState::Offline) {'
 text=once(text,anchor,'''    {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
     if(party->enabled&&transport_->is_open()) {
        const auto bytes=dkr::runtime::spectators::link_encode(dkr::runtime::spectators::LinkKind::Leave);
        if(is_host_)for(const auto& p:party->peers)send_with_key(p.address,p.key,static_cast<protocol::MessageType>(dkr::runtime::spectators::PartyMessageType),bytes);
        else if(host_address_)send_with_key(host_address_,key_,static_cast<protocol::MessageType>(dkr::runtime::spectators::PartyMessageType),bytes);
        flush_outbound_locked();
     }
     party->reset_connection();}
'''+anchor)
 return text

def protocol(text):
 old='''    return type >= static_cast<std::uint8_t>(MessageType::Hello) &&
           type <=
               static_cast<std::uint8_t>(MessageType::ModData);'''
 return once(text,old,'''    return type==250 || (type >= static_cast<std::uint8_t>(MessageType::Hello) &&
           type <= static_cast<std::uint8_t>(MessageType::ModData));''')

def mods(text):
 text='#include "party_link.hpp"\n'+text
 text=text.replace('is_host_ && waiting(state_)','is_host_ && (waiting(state_)||dkr::runtime::spectators::party_enabled(this))')
 text=once(text,'void DirectSession::clear_pending_mod_route_locked(PendingRecord& pending) {','''void DirectSession::clear_pending_mod_route_locked(PendingRecord& pending) {
    {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);party->pending_watch.erase(pending.sender_id);}''')
 return once(text,'!is_host_ || !waiting(state_) || !pending','!is_host_ || (!waiting(state_)&&!dkr::runtime::spectators::party_enabled(this)) || !pending')
