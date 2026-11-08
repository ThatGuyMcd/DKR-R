"""Repair authenticated party lobby serialization, promotion and lifetimes."""
def once(text,before,after):
 if text.count(before)!=1:raise ValueError('Lobby-link repair anchor changed: '+before[:140])
 return text.replace(before,after,1)
def region(text,start,end,change):
 a=text.index(start);b=text.index(end,a);return text[:a]+change(text[a:b])+text[b:]
def direct(text):
 # The promoted key must keep ONE sequence/nonce domain, even while the
 # provisional record still exists during Accepted packet construction.
 pending='''    for (PendingRecord& pending : pending_joins_) {
        if (pending.active && pending.address == address && pending.key == key) {
            return pending.next_sequence;
        }
    }
'''
 promoted='''    {auto party=dkr::runtime::spectators::party_link(this);std::scoped_lock party_lock(party->mutex);
     if(auto* p=party->peer(address);p&&p->key==key)return p->sequence;}
'''
 text=once(text,pending+promoted,'    // Promotion retains one monotonically increasing authenticated nonce.\n'+promoted+pending)
 text=once(text,'auto room=lobby_payload();room.generation=party->party.snapshot().generation;room.players={};room.countdown_active=false;',
 '''auto room=lobby_payload();room.generation=party->party.snapshot().generation;room.players={};room.countdown_active=false;
                // The codec validates every slot, including vacant ports.
                for(unsigned p=0;p<room.players.size();++p)room.players[p].slot=std::uint8_t(p);''')
 text=once(text,'if(valid_launch_descriptor(launch)) {',
  'if(valid_launch_descriptor(launch)&&!protocol::encode_start({1,launch,launch_descriptor_hash(launch)}).empty()) {')
 text=once(text,'party->launch=launch;party->seed=session_save_;party->running=party->stopping=party->returned=false;party->advertised={};',
  'party->launch=launch;party->seed=session_save_;party->running=party->stopping=party->returned=party->launch_sent=false;party->advertised={};')
 text=once(text,'queue(0,watch::LinkKind::Room,protocol::encode_lobby_state(room));',
 '''const auto room_bytes=protocol::encode_lobby_state(room);
                if(room_bytes.empty()) {
                    std::fprintf(stderr,"[lobby][wire-error] room serialization failed; no empty settings packet sent\\n");
                    stop("The lobby settings could not be published. Please recreate the lobby.");
                    party->launch.reset();
                } else queue(0,watch::LinkKind::Room,room_bytes);''')
 text=once(text,'if(party->launch&&!party->stopping)queue(0,watch::LinkKind::Launch,protocol::encode_start({2,*party->launch,launch_descriptor_hash(*party->launch)}));',
 '''if(party->launch&&!party->stopping){
                    // StartPayload's committed stage is 1, never 2.
                    const auto bytes=protocol::encode_start({1,*party->launch,launch_descriptor_hash(*party->launch)});
                    if(bytes.empty()) {
                        std::fprintf(stderr,"[lobby][wire-error] committed launch serialization failed\\n");
                        stop("The lobby start message could not be published. Please recreate the lobby.");party->launch.reset();
                    } else {
                        queue(0,watch::LinkKind::Launch,bytes);
                        if(!party->launch_sent){party->launch_sent=true;std::fprintf(stderr,"[lobby][launch] committed stage=1 racers=%u\\n",unsigned(party->launch->player_count));}
                    }
                }''')
 # An admitted client must have current roster/rules before its Ready request
 # is sent. Keep the request pending briefly rather than silently losing it.
 text=once(text,'if(party->ready_requested){queue(party->authority,watch::LinkKind::Ready,{std::uint8_t(party->ready_value)});party->ready_requested=false;}',
 '''if(party->ready_requested){
                if(party->party.find(party->local)&&room_view_.generation==party->party.snapshot().generation&&
                   room_view_.rules.synchronization==SynchronizationMode::ExperimentalRollback) {
                    queue(party->authority,watch::LinkKind::Ready,{std::uint8_t(party->ready_value)});party->ready_requested=false;
                } else party->status="Waiting for the host's current lobby settings...";
            }''')
 text=once(text,'const bool all=std::all_of(members.begin(),members.end(),[&](const auto& m){return m.role==watch::Role::Spectator||party->ready[m.id];});',
 '''const bool ready_only=std::all_of(members.begin(),members.end(),[&](const auto& m){return m.role==watch::Role::Spectator||party->ready[m.id];});
                const bool all=std::all_of(members.begin(),members.end(),[&](const auto& m){
                    if(m.role==watch::Role::Spectator)return true;
                    if(!party->ready[m.id])return false;
                    if(m.id==sender_id_)return true;
                    const auto* peer=party->peer(m.id);
                    return peer&&peer->rtt_count&&now-peer->heard<std::chrono::seconds(5);
                });''')
 text=once(text,'} else party->status=reason.empty()?"Every racer must be ready before starting.":reason;',
  '} else party->status=reason.empty()?(ready_only?"Waiting for a completed two-way racer connection. Please try Start again in a moment.":"Every racer must be ready before starting."):reason;')
 # Failure is terminal. Delayed heartbeat/Accepted/roster packets cannot
 # revive Failed, and the service must not emit repeated timeout failures.
 text=once(text,'if (unsigned(packet.header.type)==dkr::runtime::spectators::PartyMessageType) {',
 '''if (unsigned(packet.header.type)==dkr::runtime::spectators::PartyMessageType) {
    if(state_==ConnectionState::Failed||state_==ConnectionState::Offline)continue;''')
 text=once(text,'    if(party->enabled) {\n#include ',
  '    if(party->enabled) {\n        if(state_==ConnectionState::Failed||state_==ConnectionState::Offline)return;\n#include ')
 text=once(text,'stable_hash(std::string_view(reinterpret_cast<const char*>(accepted.synchronized_save.data()),512))!=accepted.online_save_hash)continue;',
 '''stable_hash(std::string_view(reinterpret_cast<const char*>(accepted.synchronized_save.data()),512))!=accepted.online_save_hash||
               !accepted.online_save_generation||!accepted.online_save_hash)continue;
            if(party->enabled&&party->admitted){
                if(accepted.online_save_generation<session_save_generation_)continue;
                if(accepted.online_save_generation==session_save_generation_){
                    if(accepted.online_save_hash!=manifest_.session_save_hash)fail_locked("The host supplied conflicting lobby save identities. Leave and rejoin.");
                    continue; // An acceptance retry cannot reset a live client's port or phase.
                }
                if(party->launch&&!party->stopping){fail_locked("The host changed the lobby save during a running match.");continue;}
            }''')
 text=once(text,'party->status="Joined the lobby";state_=ConnectionState::Lobby;local_slot_=255;',
 '''party->status="Joined the lobby";state_=ConnectionState::Lobby;local_slot_=255;
            std::fprintf(stderr,"[lobby][admission] party accepted; awaiting authenticated roster and settings\\n");''')
 text=once(text,'if(protocol::decode_lobby_state(message.body,room,reason)) {\n                    apply_lobby_payload(room);',
 '''if(protocol::decode_lobby_state(message.body,room,reason)&&room.generation==party->party.snapshot().generation&&room.generation>=room_view_.generation) {
                    const bool changed=room_view_.generation!=room.generation||room_view_.rules!=room.rules;
                    apply_lobby_payload(room);
                    // Party generation names membership, not Ready or automatic
                    // input-delay updates. Apply validated current metadata too.
                    room_view_.rules=room.rules;room_view_.phase=room.phase;input_delay_=room.input_delay_frames;
                    if(changed)std::fprintf(stderr,"[lobby][room] synchronized generation=%llu mode=Rollback delay=%u window=%u\\n",
                        static_cast<unsigned long long>(room.generation),unsigned(input_delay_),unsigned(room.rules.rollback_window));''')
 text=once(text,'if(protocol::decode_start(message.body,start,reason)&&start.descriptor.match_id==match_id_&&',
  'if(protocol::decode_start(message.body,start,reason)&&start.stage==1&&start.descriptor.match_id==match_id_&&')
 text=once(text,'status_="Preparing the online game";',
  'status_="Preparing the online game";\n                    std::fprintf(stderr,"[lobby][launch] accepted committed stage=1 racers=%u\\n",unsigned(start.descriptor.player_count));')
 return text
def context(text):
 return once(text,'    if(!OlPartyActive())return;\n    view.room.players={};',
 '''    // A terminal transport failure must not be projected back to Lobby
    // just because the retained party is marked stopping.
    if(view.state==ConnectionState::Failed||view.state==ConnectionState::Offline){
        g_ol_party.link.enabled=g_ol_party.link.admitted=false;
        g_ol_rollback_check.active=g_ol_rollback_check.complete=false;
        view.connection_test_active=false;view.connection_test_remaining_ms=0;view.connection_test_result_generation=0;
        return;
    }
    if(!OlPartyActive())return;
    view.room.players={};''')
def party_link(text):
 text=once(text,'std::mutex links_mutex;\nstd::unordered_map<const void*,std::shared_ptr<PartyLink>> links;',
 '''// DirectSession can be destroyed after translation-unit statics. The
// registry itself has process lifetime; normal session teardown still wipes
// keys and erases every retained PartyLink after the owner worker joins.
struct LinkRegistry {std::mutex mutex;std::unordered_map<const void*,std::shared_ptr<PartyLink>> links;};
LinkRegistry& link_registry(){static auto* value=new LinkRegistry;return *value;}''')
 text=once(text,'std::shared_ptr<PartyLink> party_link(const void* session){std::scoped_lock lock(links_mutex);auto& result=links[session];if(!result)result=std::make_shared<PartyLink>();return result;}',
  'std::shared_ptr<PartyLink> party_link(const void* session){auto& r=link_registry();std::scoped_lock lock(r.mutex);auto& result=r.links[session];if(!result)result=std::make_shared<PartyLink>();return result;}')
 return once(text,'void forget_party_link(const void* session){std::scoped_lock lock(links_mutex);links.erase(session);}',
  'void forget_party_link(const void* session){auto& r=link_registry();std::scoped_lock lock(r.mutex);r.links.erase(session);}')
def check(text):
 text=once(text,'std::mutex registry_mutex;\nstd::map<const void*,std::shared_ptr<State>> states;',
 '''struct CheckRegistry {std::mutex mutex;std::map<const void*,std::shared_ptr<State>> states;};
CheckRegistry& check_registry(){static auto* value=new CheckRegistry;return *value;}''')
 text=text.replace('std::scoped_lock lock(registry_mutex);','auto& registry=check_registry();std::scoped_lock lock(registry.mutex);')
 import re
 # Only container uses, not CheckRegistry's field declaration.
 text=re.sub(r'(?<![\w.])states(?=\[|\.)','registry.states',text)
 return text
def adapter(text):
 text=once(text,'std::mutex removal_mutex;\nstd::map<const void*,Removal> removals;',
 '''struct RemovalRegistry {std::mutex mutex;std::map<const void*,Removal> removals;};
RemovalRegistry& removal_registry(){static auto* value=new RemovalRegistry;return *value;}''')
 text=text.replace('std::scoped_lock lock(removal_mutex);','auto& registry=removal_registry();std::scoped_lock lock(registry.mutex);')
 import re
 return re.sub(r'(?<![\w.])removals(?=\[|\.)','registry.removals',text)
