"""Restore the party countdown and acknowledged start without header ABI edits."""
from pathlib import Path
import hashlib
AUTH=Path(__file__).resolve().parent
def once(t,a,b):
 if t.count(a)!=1:raise ValueError('Start repair anchor changed: '+a[:150])
 return t.replace(a,b,1)
def region(t,a,b,next):
 start=t.index(a);end=t.index(b,start);return t[:start]+next+t[end:]
def snippet(name):return (AUTH/'native'/name).read_text()
def contract():
 h=hashlib.sha256()
 for p in sorted([AUTH/'transforms.py',*(AUTH/'native').iterdir()],key=lambda p:p.name):
  if p.is_file():h.update(p.name.encode());h.update(p.read_bytes().replace(b'\r\n',b'\n'))
 return h.hexdigest()
def direct(t):
 t='#include "start_gate.hpp"\n'+t
 t=once(t,'    dkr::runtime::spectators::forget_party_link(this);','    dkr::runtime::party_start::forget(this);\n    dkr::runtime::spectators::forget_party_link(this);')
 # Also clear on explicit disconnect/rehost, not just object destruction.
 t=once(t,'     party->reset_connection();}','     party->reset_connection();dkr::runtime::party_start::forget(this);}')
 t=region(t,'            if(party->start_requested) {','            if(party->stop_requested) {',snippet('host_start.inl')+'\n')
 t=once(t,'if(!party->launch&&room_view_.rules.automatic_input_delay)',
  'if(!party->launch&&!launch_countdown_active_&&launch_stage_==LaunchStage::Idle&&room_view_.rules.automatic_input_delay)')
 t=once(t,'auto room=lobby_payload();room.generation=party->party.snapshot().generation;room.players={};room.countdown_active=false;',
  'auto room=lobby_payload();room.generation=party->party.snapshot().generation;room.players={};room.rules=room_view_.rules;')
 t=once(t,'        case watch::LinkKind::Ready:', '''        case watch::LinkKind::Start: {
            auto gate=dkr::runtime::party_start::gate(this);
            const auto receipt=dkr::runtime::party_start::receipt(message.body,*gate);
            if(!party->participant(packet_sender)||!gate->launch||gate->roster!=party->party.snapshot().generation)break;
            if(receipt==1&&gate->phase==dkr::runtime::party_start::Phase::Countdown)gate->prepared.insert(packet_sender);
            if(receipt==2&&gate->phase==dkr::runtime::party_start::Phase::Committing)gate->committed.insert(packet_sender);
            break;
        }
        case watch::LinkKind::Ready:''')
 t=region(t,'                protocol::StartPayload start;std::string reason;','            } else if(message.kind==watch::LinkKind::Runtime)',snippet('client_start.inl')+'\n')
 t=once(t,'            else if(message.kind==watch::LinkKind::Stop) {', '''            else if(message.kind==watch::LinkKind::Start) {
                auto gate=dkr::runtime::party_start::gate(this);
                if(dkr::runtime::party_start::receipt(message.body,*gate)==3){
                    gate->retired=gate->hash;gate->launch.reset();gate->phase=dkr::runtime::party_start::Phase::Cancelled;
                    reset_launch_transaction_locked();launch_descriptor_.reset();launch_requested_=false;
                    // A committed client may already be cold-booting. Its
                    // runtime sees stopping and retires; waiting clients stay.
                    party->stopping=bool(party->launch);party->launch.reset();party->running=false;party->ready.clear();
                    party->status=status_="The host cancelled the synchronized start. Racers must ready again.";
                    state_=ConnectionState::Lobby;std::fprintf(stderr,"[lobby][start-cancel] host cancelled\\n");
                }
            }
            else if(message.kind==watch::LinkKind::Stop) {''')
 t=once(t,'        } else {\n            if(party->admitted&&now-last_admission_response_', '''        } else {
            {auto gate=dkr::runtime::party_start::gate(this);
             if(gate->phase==dkr::runtime::party_start::Phase::Countdown&&now>=gate->deadline){
                gate->retired=gate->hash;gate->launch.reset();gate->phase=dkr::runtime::party_start::Phase::Cancelled;
                reset_launch_transaction_locked();party->status=status_="The host did not complete the synchronized start. Leave and rejoin the lobby.";
             }}
            if(party->admitted&&now-last_admission_response_''')
 t=once(t,'            status_=party->status;\n        };','            reset_launch_transaction_locked();dkr::runtime::party_start::forget(this);\n            status_=party->status;\n        };')
 t=once(t,'                if(reason.size()>240)reason.resize(240);queue(0,watch::LinkKind::Stop,{reason.begin(),reason.end()});stop(std::move(reason));', '''                if(reason.size()>240)reason.resize(240);
                if(gate->phase==StartPhase::Countdown||gate->phase==StartPhase::Committing||
                   (gate->phase==StartPhase::Released&&launch_requested_&&!party->running))cancel_start(std::move(reason));
                else {queue(0,watch::LinkKind::Stop,{reason.begin(),reason.end()});stop(std::move(reason));}''')
 t=once(t,'            else if(message.kind==watch::LinkKind::Stop) {\n                party->stopping=true;', '''            else if(message.kind==watch::LinkKind::Stop) {
                {auto gate=dkr::runtime::party_start::gate(this);gate->retired=gate->hash;gate->launch.reset();gate->phase=dkr::runtime::party_start::Phase::Cancelled;}
                reset_launch_transaction_locked();
                party->stopping=true;''')
 return t
def runtime(t):
 # The final identity pins the launch handshake and checkpoint admission code.
 # Mixed playtests fail compatibility admission, not an opaque frame-zero wait.
 t=once(t,';const auto h=digest(', ';base+="'+contract()+'";const auto h=digest(')
 t=once(t,'    Shared shared;requested_stop=false;shared.confirmed_save=seed;',
  '    if(initial.stopping){party::party_returned(&lobby,{});ui::report_online_game_end(initial.status,lobby.active());error.clear();return true;}\n    std::fprintf(stderr,"[party][boot] role=%s racers=%u seed=verified\\n",initial.host?"host":initial.party.controls[0].member==initial.local?"racer-p1":"member",unsigned(launch.player_count));\n    Shared shared;requested_stop=false;shared.confirmed_save=seed;')
 t=once(t,'            if(!world)throw std::runtime_error(message);\n            if(initial.host)',
  '            if(!world)throw std::runtime_error(message);\n            std::fprintf(stderr,"[party][boot] owned world ready; bytes=%zu\\n",world->contract().state_bytes);\n            if(initial.host)')
 t=once(t,'            if(!runtime.begin(message))throw std::runtime_error(message);',
  '            if(!runtime.begin(message))throw std::runtime_error(message);\n            std::fprintf(stderr,"[party][boot] checkpoint admission started\\n");')
 t=once(t,'        }catch(const std::exception& e){shared.fail(e.what());if(!initial.host&&party::party_watching(&lobby))',
  '        }catch(const std::exception& e){std::fprintf(stderr,"[party][failure] %s\\n",e.what());shared.fail(e.what());if(!initial.host&&party::party_watching(&lobby))')
 return t
def party_runtime(t):
 t='#include <cstdio>\n'+t
 t=once(t,'    Phase phase=Phase::Admission;ex::BoundaryIntent boundary{};Digest phase_digest{};',
  '    Phase phase=Phase::Admission;ex::BoundaryIntent boundary{};Digest phase_digest{};\n    Clock::time_point admission_log{},ready_retry{},release_retry{},release_until{};\n    Bytes release_packet;std::optional<CheckpointOffer> last_offer;')
 # No guest capture, hashing, allocations or compression while retaining the
 # transport's PartyLink lock. This keeps one consistent session->party order.
 t=region(t,'        std::scoped_lock lock(hub->mutex);\n        if(!hub->enabled','        owned_observer_read_only(&world,watching());phase_at=progress=Clock::now();started=true;', '''        {
            std::scoped_lock lock(hub->mutex);
            if(!hub->enabled||!hub->admitted||!hub->launch||*hub->launch!=launch||!hash) {error="Party runtime has no authenticated launch.";return false;}
            local=hub->local;host=hub->authority;authority=hub->host;match=launch_descriptor_hash(launch);
            if(!roster.replace_verified(hub->party.snapshot(),error)||!roster.find(local))return false;
        }
''')
 t=once(t,'        if(server()) {auto b=capture(cursor);ready(b);encode(b);for(const auto& m:roster.snapshot().members)if(m.id!=local)needing.insert(m.id);}',
  '        std::fprintf(stderr,"[party][admission] begin authority=%u watching=%u\\n",unsigned(server()),unsigned(watching()));\n        if(server()) {auto b=capture(cursor);ready(b);encode(b);for(const auto& m:roster.snapshot().members)if(m.id!=local)needing.insert(m.id);}')
 t=once(t,'        auto bytes=std::make_shared<Bytes>(world.contract().state_bytes);std::string e;',
  '        if(phase==Phase::Admission)std::fprintf(stderr,"[party][checkpoint] capture frame=%u bytes=%zu\\n",at.frame,world.contract().state_bytes);\n        auto bytes=std::make_shared<Bytes>(world.contract().state_bytes);std::string e;')
 t=once(t,'            auto value=encoding.get();encoded=value.wire;',
  '            auto value=encoding.get();encoded=value.wire;\n            if(phase==Phase::Admission)std::fprintf(stderr,"[party][checkpoint] encoded bytes=%zu\\n",encoded->state->size());')
 t=once(t,'            if(!d.offered){send(id,Msg::Offer,encode_offer(d.sender.offer()),Lane::Control);d.offered=true;}', '''            if(!d.offered||(!d.accepted&&now-d.progress>=std::chrono::seconds(1))){
                send(id,Msg::Offer,encode_offer(d.sender.offer()),Lane::Control);d.offered=true;d.progress=now;
                if(phase==Phase::Admission)std::fprintf(stderr,"[party][checkpoint] offer bytes=%u\\n",d.sender.offer().bytes);
            }''')
 t=once(t,'''            if(!decode_offer(r.rest(),offer,e)||offer.bytes>world.contract().state_bytes+1024U*1024U||
               !receiver.begin(offer,match,world.contract().schema,offer.bytes,Clock::now(),e))throw std::runtime_error(e);
            receiving=offer;active_transfer=offer.transfer;admitted_at.reset();buffered.clear();''', '''            if(!decode_offer(r.rest(),offer,e))throw std::runtime_error(e);
            if(offer.bytes>world.contract().state_bytes+1024U*1024U||offer.match!=match||offer.schema!=world.contract().schema)
                throw std::runtime_error("Initial checkpoint does not match the admitted game layout.");
            // Retried offers must not reset a transfer already decoded or
            // admitted at this cursor. Keep the acknowledgement idempotent.
            if(offer.transfer==active_transfer&&(held||decoding.valid())){
                if(!last_offer||offer!=*last_offer)throw std::runtime_error("Conflicting checkpoint offer retry.");
                Write ack;ack.n(offer.transfer);send(host,Msg::OfferAck,ack.b);break;
            }
            if(!receiver.begin(offer,match,world.contract().schema,offer.bytes,Clock::now(),e))throw std::runtime_error(e);
            last_offer=offer;receiving=offer;active_transfer=offer.transfer;admitted_at.reset();buffered.clear();
            if(phase==Phase::Admission)std::fprintf(stderr,"[party][checkpoint] offer accepted bytes=%u\\n",offer.bytes);''')
 t=once(t,'            auto b=decoding.get();held=b;',
  '            auto b=decoding.get();held=b;\n            if(phase==Phase::Admission)std::fprintf(stderr,"[party][checkpoint] decoded bytes=%zu\\n",b.state->size());')
 t=once(t,'            std::string e;const auto work=retire_owned_observer(&world,held->next,e);\n            if(work==Work::Failed)throw std::runtime_error(e);\n            if(work==Work::Ready){if(!admit_owned_observer(&world,*held,e))throw std::runtime_error(e);ready(*held);}', '''            std::string e;
            if(phase==Phase::Admission&&!go&&held->next==Cursor{91,0}&&world.confirmed_count()==0&&held->magic_actions==0){
                // A fresh racer is a zero-frame owned launch, not a future
                // observer restore. Use the existing canonical admission gate.
                if(box.quiescent()){
                    if(!world.admit_initial(*held->state,e))throw std::runtime_error("Initial racing checkpoint admission failed: "+e);
                    ready(*held);
                }
            }else{
                const auto work=retire_owned_observer(&world,held->next,e);
                if(work==Work::Failed)throw std::runtime_error(e);
                if(work==Work::Ready){if(!admit_owned_observer(&world,*held,e))throw std::runtime_error(e);ready(*held);}
            }''')
 t=once(t,'        Write w;w.at(b.next).n(controls).data(b.digest).n(b.magic_actions,4);send(host,Msg::Ready,w.b);',
  '        Write w;w.at(b.next).n(controls).data(b.digest).n(b.magic_actions,4);send(host,Msg::Ready,w.b);ready_retry=Clock::now()+std::chrono::milliseconds(500);\n        if(phase==Phase::Admission)std::fprintf(stderr,"[party][checkpoint] canonical admission verified; Ready sent\\n");')
 t=once(t,'            delivery->second.admitted=true;',
  '            delivery->second.admitted=true;\n            if(phase==Phase::Admission)std::fprintf(stderr,"[party][checkpoint] racer admission acknowledged\\n");')
 t=once(t,'        const auto body=go_body(at);for(const auto& m:roster.snapshot().members)if(m.id!=local)send(m.id,Msg::Go,body);', '''        const auto body=go_body(at);release_packet=body;release_retry=Clock::now()+std::chrono::milliseconds(500);release_until=Clock::now()+std::chrono::seconds(5);
        for(const auto& m:roster.snapshot().members)if(m.id!=local)send(m.id,Msg::Go,body);
        std::fprintf(stderr,"[party][release] confirmed start epoch=%llu frame=%u\\n",static_cast<unsigned long long>(at.epoch),at.frame);''')
 t=once(t,'            const auto at=r.at();const auto generation=r.n();PartySnapshot state;std::string e;',
  '            const auto at=r.at();const auto generation=r.n();PartySnapshot state;std::string e;\n            if(at.epoch<cursor.epoch||(go&&at.epoch==cursor.epoch&&at.frame<=cursor.frame&&generation<=controls))return;')
 t=once(t,'        if(!callback_error.empty())throw std::runtime_error(callback_error);', '''        if(phase==Phase::Admission&&now>=admission_log){
            admission_log=now+std::chrono::seconds(5);
            std::fprintf(stderr,"[party][admission-progress] authority=%u prepared=%zu required=%zu receiving=%zu decoding=%u encoded=%u deliveries=%zu\\n",
                unsigned(server()),prepared.size(),required().size(),receiver.received_bytes(),unsigned(decoding.valid()),unsigned(bool(encoded)),deliveries.size());
        }
        if(server()&&go&&now<release_until&&now>=release_retry&&!release_packet.empty()){
            for(const auto& m:roster.snapshot().members)if(m.id!=local)send(m.id,Msg::Go,release_packet);
            release_retry=now+std::chrono::milliseconds(500);
        }
        if(!server()&&phase==Phase::Admission&&held&&admitted_at==held->next&&now>=ready_retry){
            Write w;w.at(held->next).n(held->control_generation).data(held->digest).n(held->magic_actions,4);send(host,Msg::Ready,w.b);
            ready_retry=now+std::chrono::milliseconds(500);
        }
        if(!callback_error.empty())throw std::runtime_error(callback_error);''')
 return t
