#include "party_runtime.hpp"
#include "confirmed_observer.hpp"
#include "online_stability/response_trace.hpp"
#include "netplay/authoritative_state_codec.hpp"
#include "netplay/experimental_checkpoint_hash.hpp"
#include "mods/sdk2_bridge.hpp"
#include "online_stability/live_delay.hpp"
#include "online_stability/input_trace.hpp"
#include "netplay/experimental_performance.hpp"
#include "online_stability/live_inputs.hpp"
#include <algorithm>
#include <future>
#include <set>
#include <stdexcept>
#include <utility>
#include <tuple>

namespace dkr::runtime::spectators {
namespace ex=net::experimental;
namespace {
using Bytes=std::vector<std::uint8_t>;
using Lane=net::TransportTrafficClass;
constexpr auto Tick=std::chrono::nanoseconds(1000000000/30);
enum class Msg:std::uint8_t {Hello=1,Offer,Chunk,Ready,Go,Input,Frames,Scene,Prepare,Prepared,Freeze,Frozen,Swap,Cancel,Need,OfferAck,FrameAck,Progress,LiveInputs,InputReceipt};
struct Write {
    Bytes b;
    Write& n(std::uint64_t v,unsigned count=8){for(unsigned i=0;i<count;++i)b.push_back(std::uint8_t(v>>(8*i)));return *this;}
    Write& at(Cursor c){return n(c.epoch).n(c.frame,4);}
    Write& data(std::span<const std::uint8_t> v){b.insert(b.end(),v.begin(),v.end());return *this;}
};
struct Read {
    std::span<const std::uint8_t> b;std::size_t pos=0;
    std::uint64_t n(unsigned count=8){if(count>8||count>b.size()-pos)throw std::runtime_error("Truncated party runtime message.");std::uint64_t v=0;for(unsigned i=0;i<count;++i)v|=std::uint64_t(b[pos++])<<(8*i);return v;}
    Cursor at(){const auto epoch=n();return {epoch,std::uint32_t(n(4))};}
    Digest hash(){Digest v{};for(auto& c:v)c=std::uint8_t(n(1));return v;}
    std::span<const std::uint8_t> rest(){const auto tail=b.subspan(pos);pos=b.size();return tail;}
    void end(){if(pos!=b.size())throw std::runtime_error("Trailing party runtime message data.");}
};
net::PackedInput packed(PortInput p){return {p.buttons,p.x,p.y,p.accelerator,p.brake,p.driving_flags};}
PortInput portable(net::PackedInput p){return {p.buttons,p.stick_x,p.stick_y,p.accelerator,p.brake,p.driving_flags};}
dkr::sdk2::Command command(std::span<const std::uint8_t> bytes) {
    dkr::sdk2::Command c;if(bytes.empty())return c;
    if(bytes.size()!=64)throw std::runtime_error("Invalid confirmed development command.");
    Read r{bytes};for(auto& w:c.words)w=std::uint32_t(r.n(4));return c;
}
Bytes command_bytes(const dkr::sdk2::Command& c){if(c.empty())return {};Write w;for(auto v:c.words)w.n(v,4);return w.b;}
// Rebase only the driver's relative history cursor. Guest clocks, sidecar
// cursors, SDK commands and irreversible effects keep their actual frame.
class OffsetWorld final:public ex::Simulation {
    ex::OwnedGame& w_;std::uint32_t base_;
public:
    OffsetWorld(ex::OwnedGame& w,std::uint32_t base):w_(w),base_(base){}
    ex::SimulationContract contract()const override{return w_.contract();}
    bool requires_confirmed_tick()const override{return w_.requires_confirmed_tick();}
    std::uint8_t input_dependencies()const override{return w_.input_dependencies();}
    ex::RestoreStep prepare_tick(std::uint64_t e,std::uint32_t f,bool agreed,std::string& s)override{return w_.prepare_tick(e,base_+f,agreed,s);}
    bool capture(std::span<std::uint8_t> b,std::string& s)override{return w_.capture(b,s);}
    bool restore(std::span<const std::uint8_t> b,std::string& s)override{return w_.restore(b,s);}
    ex::RestoreStep prepare_restore(std::uint64_t e,std::uint32_t f,std::string& s)override{return w_.prepare_restore(e,base_+f,s);}
    bool development_command(std::uint64_t e,std::uint32_t f,const dkr::sdk2::Command& c,std::string& s)override{return w_.development_command(e,base_+f,c,s);}
    bool tick(std::uint32_t f,const net::FrameInputs& i,ex::TickOutput& o,std::string& s)override{return w_.tick(base_+f,i,o,s);}
    bool commit(std::uint64_t e,std::uint32_t f,std::span<const std::uint8_t> b,bool boundary,std::string& s)override{return w_.commit(e,base_+f,b,boundary,s);}
};
class WatchingWorld final:public ObserverWorld {
    ex::OwnedGame& w_;bool ready_=false;
public:
    explicit WatchingWorld(ex::OwnedGame& w):w_(w){}
    std::uint64_t schema()const override{return w_.contract().schema;}
    std::size_t state_bytes()const override{return w_.contract().state_bytes;}
    Work retire(Cursor c,std::string& e)override{return retire_owned_observer(&w_,c,e);}
    bool admit(const Baseline& b,std::string& e)override{ready_=false;return admit_owned_observer(&w_,b,e);}
    Work prepare(const ConfirmedFrame& f,std::string& e)override {
        const auto step=w_.prepare_tick(f.cursor.epoch,f.cursor.frame,true,e);
        return step==ex::RestoreStep::Ready?Work::Ready:step==ex::RestoreStep::Pending?Work::Pending:Work::Failed;
    }
    bool tick_and_commit(const ConfirmedFrame& f,std::string& e)override {
        net::FrameInputs inputs{};for(unsigned i=0;i<4;++i)inputs[i]=packed(f.inputs[i]);
        ex::TickOutput output;
        if(!w_.development_command(f.cursor.epoch,f.cursor.frame,command(f.command),e)||
           !w_.tick(f.cursor.frame,inputs,output,e)||output.confirmation_required||output.scene_boundary!=f.scene_boundary||
           !w_.commit(f.cursor.epoch,f.cursor.frame,output.effects,output.scene_boundary,e))return false;
        ready_=!output.scene_boundary;return true;
    }
    bool publish(Cursor c,std::string& e)override{return !ready_||w_.publish_current(c.epoch,c.frame,e);}
    // Watchers do not vote or load ahead of the host's agreed new scene. A new
    // host checkpoint admits the fully constructed scene transaction instead.
    Work prepare_scene(Cursor,std::string&)override{return Work::Ready;}
};
}
struct PartyRuntime::Impl {
    const void* key;ex::OwnedGame& world;ex::PresentationMailbox& box;net::LaunchDescriptor launch;HashState hash;
    dkr::online_stability::LiveDelay live_delay;
    bool delay_network_wait=false;
    unsigned delay_fill=0;
    std::shared_ptr<PartyLink> hub;MemberId local=0,host=0;bool authority=false,started=false,go=false;
    std::uint64_t match=0,controls=1,serial=0,transfer_serial=0,active_transfer=0;Party roster;
    Cursor cursor{91,0};std::uint32_t base=0,authored=0;Clock::time_point next_tick{},progress{},hello_at{},phase_at{},proof_at{};
    std::unique_ptr<OffsetWorld> offset;std::unique_ptr<ex::Driver> driver;
    WatchingWorld watch_world;ObserverPlayback playback;ConfirmedFeed feed;ConfirmedFrameBinding binding;
    std::string text="Preparing party play",callback_error;std::optional<Baseline> held;
    struct Encoded {Baseline raw,wire;};
    std::future<Encoded> encoding;std::future<Baseline> decoding;std::optional<Baseline> encode_pending;
    struct Delivery {CheckpointSender sender;Digest raw_digest{};std::uint32_t actions=0;bool offered=false,accepted=false,admitted=false;Cursor sent{};Clock::time_point progress{},frames_at{};};
    std::map<MemberId,Delivery> deliveries;std::set<MemberId> needing,prepared;std::map<MemberId,Digest> scene_votes,frozen;
    std::map<MemberId,Clock::time_point> requested_at;
    std::optional<CheckpointOffer> receiving;CheckpointReceiver receiver;std::optional<Baseline> encoded;
    std::optional<Cursor> admitted_at;std::map<std::pair<std::uint64_t,std::uint32_t>,ConfirmedFrame> buffered;
    std::map<std::pair<std::uint32_t,unsigned>,std::pair<PortInput,dkr::sdk2::Command>> inputs;
    std::map<std::uint32_t,wire::ControllerSample> local_history;
    std::array<std::uint32_t,4> received_next{};
    std::uint32_t host_received=0;
    std::uint64_t accepted_inputs=0;
    Clock::time_point observed_now{};
    dkr::online_stability::InputTrace input_trace;
    Clock::time_point cpu_report_at{};
    std::map<std::pair<std::uint32_t,unsigned>,Clock::time_point> input_times;
    std::map<std::tuple<MemberId,unsigned,std::uint32_t>,Clock::time_point> repair_times;
    enum class Phase {Admission,Running,SceneVote,ScenePrepare,SceneReady,Freeze,SwapReady};
    Phase phase=Phase::Admission;ex::BoundaryIntent boundary{};Digest phase_digest{};
    std::optional<std::pair<unsigned,MemberId>> swap;std::uint32_t freeze=UINT32_MAX;
    std::uint64_t probe_id=0;Cursor probe_cursor{};std::map<MemberId,Digest> proofs;
    Impl(const void* k,ex::OwnedGame& w,ex::PresentationMailbox& b,net::LaunchDescriptor l,HashState h)
      :key(k),world(w),box(b),launch(l),hash(std::move(h)),live_delay(dkr::online_stability::control(k),l.input_delay_frames,l.rollback_window),hub(party_link(k)),watch_world(w),playback(watch_world,launch_descriptor_hash(l),hash),
       binding([this](ConfirmedFrame f){committed(std::move(f));}),receiver(hash){}
    bool server()const{return authority;}
    const Member* member()const{return roster.find(local);}
    bool watching()const{return member()&&member()->role==Role::Spectator;}
    bool simulating()const{return server()||!watching();}
    unsigned port()const{return member()&&member()->port?*member()->port:4;}
    Cursor next()const{return driver?Cursor{cursor.epoch,base+driver->statistics().next_frame}:playback.view().next;}
    std::uint32_t confirmed()const{return driver?base+driver->statistics().confirmed_frames:playback.view().next.frame;}
    std::set<MemberId> required()const {std::set<MemberId> ids{host};for(const auto& m:roster.snapshot().members)if(m.role==Role::Racer)ids.insert(m.id);return ids;}
    bool all(const std::set<MemberId>& ids)const {for(auto id:required())if(!ids.contains(id))return false;return true;}
    template<class Map>bool all_votes(const Map& votes)const {for(auto id:required())if(!votes.contains(id))return false;return true;}
    void send(MemberId target,Msg type,std::span<const std::uint8_t> body={},Lane lane=Lane::Control) {
        Write w;w.n('D',1).n('P',1).n('R',1).n(2,1).n(unsigned(type),1).n(match).n(++serial).data(body);
        if(!party_send(key,target,w.b,lane)) {
            if(target&&roster.find(target)&&roster.find(target)->role==Role::Spectator&&lane==Lane::Checkpoint)return;
            throw std::runtime_error("Party runtime transport queue is full.");
        }
        if(type==Msg::LiveInputs){dkr::online_stability::LiveInputs packet;if(dkr::online_stability::decode_live_inputs(body,packet))
            input_trace.event(packet.owner,target,6,packet.first+packet.count-1,observed_now);}
    }
    void racers(Msg type,std::span<const std::uint8_t> body={},Lane lane=Lane::Authoritative) {
        for(auto id:required())if(id!=local)send(id,type,body,lane);
    }
    void update_hub() {
        std::scoped_lock lock(hub->mutex);std::string e;
        const auto& latest=hub->party.snapshot();
        // Transport admission may have added another watcher since this
        // owner last polled. Do not overwrite that newer membership snapshot.
        if(latest.generation>=roster.snapshot().generation&&latest.controls==roster.snapshot().controls) {
            if(!roster.replace_verified(latest,e))throw std::runtime_error(e);
        } else if(!hub->party.replace_verified(roster.snapshot(),e))throw std::runtime_error(e);
        hub->status=text;
    }
    Baseline capture(Cursor at) {
        if(world.confirmed_count()!=at.frame)throw std::runtime_error("A speculative world cannot become a spectator baseline.");
        auto bytes=std::make_shared<Bytes>(world.contract().state_bytes);std::string e;
        if(!world.capture(*bytes,e))throw std::runtime_error("Could not capture confirmed party state: "+e);
        return {at,match,world.contract().schema,controls,hash(*bytes),bytes,owned_magic_actions(&world)};
    }
    void encode(Baseline b) {
        held=b;
        if(encoding.valid()){encode_pending=std::move(b);return;}
        feed.clear();if(feed.baseline(b)!=FeedResult::Accepted)throw std::runtime_error("Confirmed party baseline was rejected.");
        encoding=std::async(std::launch::async,[b=std::move(b),hasher=hash]()mutable {
            auto bytes=std::make_shared<Bytes>(net::encode_authoritative_state_wire(*b.state));
            Baseline wire=b;wire.state=bytes;wire.digest=hasher(*bytes);return Encoded{std::move(b),std::move(wire)};
        });
    }
    void committed(ConfirmedFrame f)noexcept {
        try {
            if(!server())return;
            f.cursor.frame+=base;
            if(feed.current()) {
                const auto result=feed.append(f);
                if(result!=FeedResult::Accepted&&result!=FeedResult::Duplicate){feed.clear();for(auto& [id,d]:deliveries){(void)d;needing.insert(id);}}
            }
        }catch(const std::exception& e){callback_error=e.what();}catch(...){callback_error="Confirmed spectator feed failed.";}
    }
    void reset_driver(Cursor at) {
        delay_fill=0;delay_network_wait=false;
        driver.reset();offset.reset();cursor=at;base=at.frame;authored=at.frame;inputs.clear();local_history.clear();
        received_next.fill(base);host_received=base;input_times.clear();repair_times.clear();
        owned_observer_read_only(&world,watching());
        if(simulating()) {
            offset=std::make_unique<OffsetWorld>(world,base);driver=std::make_unique<ex::Driver>(*offset);
            const auto bytes=world.contract().state_bytes;
            const auto budget=std::max<std::size_t>(128U*1024U*1024U,bytes*(std::size_t(launch.rollback_window)+2));
            std::string e;if(!driver->start({at.epoch,launch.player_count,launch.rollback_window,budget},e))throw std::runtime_error(e);
        }
        next_tick=Clock::now();observed_now=progress=next_tick;proof_at=next_tick+std::chrono::milliseconds(100);
        input_trace.reset(cursor.epoch,port(),observed_now,base);
    }
    void ready(Baseline b) {
        if(!server()) {
            const auto restored=capture(b.next);
            if(restored.digest!=b.digest||restored.magic_actions!=b.magic_actions)
                throw std::runtime_error("The admitted party checkpoint did not recapture the exact confirmed state.");
        }
        held=b;admitted_at=b.next;cursor=b.next;controls=b.control_generation;
        if(server()){prepared.insert(local);return;}
        Write w;w.at(b.next).n(controls).data(b.digest).n(b.magic_actions,4);send(host,Msg::Ready,w.b);
    }
    Bytes go_body(Cursor at) {Write w;const auto members=wire::roster(roster.snapshot());w.at(at).n(controls).data(members);return w.b;}
    void release(Cursor at) {
        const auto body=go_body(at);for(const auto& m:roster.snapshot().members)if(m.id!=local)send(m.id,Msg::Go,body);
        activate(at);update_hub();
    }
    void activate(Cursor at) {
        owned_observer_read_only(&world,watching());
        if(!simulating()){driver.reset();offset.reset();cursor=at;}
        if(server())for(auto id:required())deliveries.erase(id);
        if(simulating()) {
            if(world.confirmed_count()!=at.frame)throw std::runtime_error("Party release arrived before exact confirmed admission.");
            reset_driver(at);
        } else if(held&&held->next==at) {
            std::string e;if(!playback.offer(*held,e))throw std::runtime_error(e);
        } else {
            playback.request_resync("Loading the confirmed race");held.reset();admitted_at.reset();
            if(!server())send(host,Msg::Need);
        }
        phase=Phase::Running;freeze=UINT32_MAX;swap.reset();frozen.clear();scene_votes.clear();prepared.clear();go=true;
        text=watching()?"Watching the race":"Experimental rollback";
        {std::scoped_lock lock(hub->mutex);hub->running=true;hub->swap_requests.clear();hub->status=text;}
    }
    bool begin(std::string& error) {
        std::scoped_lock lock(hub->mutex);
        if(!hub->enabled||!hub->admitted||!hub->launch||*hub->launch!=launch||!hash) {error="Party runtime has no authenticated launch.";return false;}
        local=hub->local;host=hub->authority;authority=hub->host;match=launch_descriptor_hash(launch);
        if(!roster.replace_verified(hub->party.snapshot(),error)||!roster.find(local))return false;
        owned_observer_read_only(&world,watching());phase_at=progress=Clock::now();started=true;
        if(server()) {auto b=capture(cursor);ready(b);encode(b);for(const auto& m:roster.snapshot().members)if(m.id!=local)needing.insert(m.id);}
        else send(host,Msg::Hello);
        hello_at=Clock::now()+std::chrono::seconds(2);return true;
    }
    void start_delivery(MemberId id,const Baseline& wire) {
        if(!roster.find(id)||id==local||!held||held->next!=wire.next||held->control_generation!=wire.control_generation)return;
        auto& d=deliveries[id];d={};d.progress=Clock::now();d.sent=wire.next;
        d.raw_digest=held->digest;d.actions=held->magic_actions;
        if(!d.sender.begin(++transfer_serial,std::make_shared<Baseline>(wire)))throw std::runtime_error("Could not stage spectator checkpoint delivery.");
    }
    void transfer(Clock::time_point now) {
        if(encoding.valid()&&encoding.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            auto value=encoding.get();encoded=value.wire;
            if(encode_pending){auto pending=std::move(*encode_pending);encode_pending.reset();encode(std::move(pending));}
        }
        if(encoded&&held&&encoded->next==held->next&&encoded->control_generation==held->control_generation&&feed.covers(encoded->next)) {for(auto id:needing)start_delivery(id,*encoded);needing.clear();}
        unsigned chunks=0;
        for(auto& [id,d]:deliveries) {
            if(!roster.find(id)||id==local)continue;
            if(!d.offered){send(id,Msg::Offer,encode_offer(d.sender.offer()),Lane::Control);d.offered=true;}
            if(d.accepted&&chunks<4)if(const auto chunk=d.sender.next_chunk()) {
                const auto bytes=encode_chunk(*chunk);Write w;w.n('D',1).n('P',1).n('R',1).n(2,1).n(unsigned(Msg::Chunk),1).n(match).n(++serial).data(bytes);
                if(party_send(key,id,w.b,Lane::Checkpoint)){d.sender.sent();d.progress=now;++chunks;}
            }
            if(d.sender.complete()&&d.admitted&&!required().contains(id)&&now-d.frames_at>=std::chrono::milliseconds(50)&&feed.covers(d.sent)) {
                auto frames=feed.after(d.sent,8);
                if(!frames.empty()) {Write w;w.n(d.sender.offer().transfer).data(wire::frames(frames));
                    Write packet;packet.n('D',1).n('P',1).n('R',1).n(2,1).n(unsigned(Msg::Frames),1).n(match).n(++serial).data(w.b);
                    party_send(key,id,packet.b,Lane::Replica);d.frames_at=now;}
            } else if(d.sender.complete()&&feed.current()&&!feed.covers(d.sent))needing.insert(id);
        }
        if(decoding.valid()&&decoding.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            auto b=decoding.get();held=b;
            if(!simulating()) {std::string e;if(!playback.offer(b,e))throw std::runtime_error(e);}
        }
        if(!server()&&held&&simulating()&&!driver&&admitted_at!=held->next) {
            std::string e;const auto work=retire_owned_observer(&world,held->next,e);
            if(work==Work::Failed)throw std::runtime_error(e);
            if(work==Work::Ready){if(!admit_owned_observer(&world,*held,e))throw std::runtime_error(e);ready(*held);}
        }
        if(receiver.active()&&receiver.expired(now)) {receiver.cancel();receiving.reset();send(host,Msg::Need);}
    }
    void receive_input(MemberId source,Read& r,bool relay=true) {
        const auto generation=r.n();wire::ControllerSample sample;std::string e;
        const auto count=r.n(1);if(count!=wire::ControllerSampleBytes||count>r.b.size()-r.pos)throw std::runtime_error("Invalid racing input width.");
        if(!wire::controller(r.b.subspan(r.pos,std::size_t(count)),sample,e))throw std::runtime_error(e);r.pos+=std::size_t(count);
        auto c=command(r.rest());
        if(generation!=controls||sample.cursor.epoch!=cursor.epoch||!driver||sample.port>=launch.player_count||
            sample.lease!=roster.snapshot().controls[sample.port].generation||sample.cursor.frame<base||sample.cursor.frame>=freeze)return;
        if(server()) {
            if(!roster.accepts(source,sample.port,sample.lease,sample.cursor)||!c.empty())return;
            if(std::uint64_t(sample.cursor.frame)>std::uint64_t(next().frame)+48)return;
            const auto id=std::pair{sample.cursor.frame,unsigned(sample.port)};
            const auto existing=inputs.find(id);
            if(existing!=inputs.end()) {
                if(existing->second.first!=sample.input)throw std::runtime_error("A racing member changed immutable input.");
                input_trace.event(sample.port,source,3,sample.cursor.frame,observed_now);
                return;
            }
            if(sample.port==0&&launch.development)c=dkr::sdk2::take(true);
            inputs[id]={sample.input,c};
            Write body;body.n(controls).n(wire::ControllerSampleBytes,1).data(wire::controller(sample)).data(command_bytes(c));
            if(relay)racers(Msg::Input,body.b,Lane::Control);
        } else if(source!=host)return;
        const auto result=driver->receive(cursor.epoch,sample.port,sample.cursor.frame-base,packed(sample.input),c);
        if(result==ex::InputResult::Accepted||result==ex::InputResult::Duplicate) {
            const auto id=std::pair{sample.cursor.frame,unsigned(sample.port)};
            inputs.emplace(id,std::make_pair(sample.input,c));input_times.try_emplace(id,observed_now);
            while(inputs.contains({received_next[sample.port],unsigned(sample.port)}))++received_next[sample.port];
        }
        if(result==ex::InputResult::Accepted){++accepted_inputs;live_delay.arrival(sample.port,sample.cursor.frame,!launch.development);}
        input_trace.event(sample.port,source,result==ex::InputResult::Accepted?2:result==ex::InputResult::Duplicate?3:7,sample.cursor.frame,observed_now);
        if(result==ex::InputResult::Conflict||result==ex::InputResult::Failed)throw std::runtime_error(driver->error());
        if(server()&&result==ex::InputResult::OutOfRange)inputs.erase({sample.cursor.frame,unsigned(sample.port)});
    }
    void message(MemberId source,std::span<const std::uint8_t> bytes) {
        Read r{bytes};if(r.n(1)!='D'||r.n(1)!='P'||r.n(1)!='R'||r.n(1)!=2)return;
        const auto type=Msg(r.n(1));if(r.n()!=match)return;(void)r.n();
        const bool from_host=source==host;
        if(!server()&&!from_host)return;
        if(server()&&!roster.find(source))return;
        switch(type) {
        case Msg::Hello:case Msg::Need:
            r.end();if(server()) {
                if(type==Msg::Need&&required().contains(source)&&phase!=Phase::Admission)return;
                const auto now=Clock::now();auto& previous=requested_at[source];
                if(!previous.time_since_epoch().count()||now-previous>=std::chrono::seconds(2)) {
                    if(type==Msg::Need)if(auto d=deliveries.find(source);d!=deliveries.end()&&encoded&&
                        (encoded->next.epoch<d->second.sent.epoch||(encoded->next.epoch==d->second.sent.epoch&&encoded->next.frame<d->second.sent.frame)))encoded.reset();
                    previous=now;needing.insert(source);text="Preparing a confirmed spectator checkpoint";
                }
            }break;
        case Msg::Offer: {
            if(!from_host||server())return;
            CheckpointOffer offer;std::string e;
            if(!decode_offer(r.rest(),offer,e)||offer.bytes>world.contract().state_bytes+1024U*1024U||
               !receiver.begin(offer,match,world.contract().schema,offer.bytes,Clock::now(),e))throw std::runtime_error(e);
            receiving=offer;active_transfer=offer.transfer;admitted_at.reset();buffered.clear();
            Write ack;ack.n(offer.transfer);send(host,Msg::OfferAck,ack.b);break;
        }
        case Msg::Chunk: {
            if(!from_host||server()||!receiving||decoding.valid())return;
            CheckpointChunk chunk;std::string e;if(!decode_chunk(r.rest(),chunk,e))throw std::runtime_error(e);
            const auto result=receiver.receive(chunk,Clock::now(),e);
            if(result==CheckpointReceiver::Result::Rejected)throw std::runtime_error(e);
            if(result==CheckpointReceiver::Result::Complete) {
                auto wire_state=receiver.take_verified(e);if(!wire_state)throw std::runtime_error(e);
                receiving.reset();
                decoding=std::async(std::launch::async,[b=std::move(*wire_state),limit=world.contract().state_bytes,hasher=hash]()mutable {
                    auto decoded=std::make_shared<Bytes>();std::string failure;
                    if(!net::decode_authoritative_state_wire(*b.state,limit,*decoded,failure)||decoded->size()!=limit)
                        throw std::runtime_error("Spectator checkpoint decode/layout rejected: "+failure);
                    b.state=decoded;b.digest=hasher(*decoded);return b;
                });
            }break;
        }
        case Msg::Ready: {
            if(!server())return;
            const auto at=r.at();const auto generation=r.n();const auto digest=r.hash();const auto actions=r.n(4);r.end();
            const auto delivery=deliveries.find(source);
            if(delivery==deliveries.end()||at!=delivery->second.sender.offer().next||generation!=delivery->second.sender.offer().control_generation||
               digest!=delivery->second.raw_digest||actions!=delivery->second.actions)return;
            delivery->second.admitted=true;
            if(held&&at==held->next&&generation==held->control_generation&&digest==held->digest&&actions==held->magic_actions) {
                prepared.insert(source);if(phase==Phase::SwapReady)frozen[source]=digest;
            }
            break;
        }
        case Msg::Go: {
            if(!from_host||server())return;
            const auto at=r.at();const auto generation=r.n();PartySnapshot state;std::string e;
            if(!wire::roster(r.rest(),state,e)||state.host!=host)throw std::runtime_error(e);
            if(!(state.generation<roster.snapshot().generation&&state.controls==roster.snapshot().controls)&&
               !roster.replace_verified(std::move(state),e))throw std::runtime_error(e);
            if(generation<controls)return;
            if(go&&phase==Phase::Running&&at==cursor&&generation==controls)return;
            controls=generation;update_hub();activate(at);break;
        }
        case Msg::Input:receive_input(source,r);break;
        case Msg::LiveInputs: {
            if(launch.development)return; // SDK commands retain ordered authority delivery.
            dkr::online_stability::LiveInputs packet;
            if(!dkr::online_stability::decode_live_inputs(r.rest(),packet))throw std::runtime_error("Invalid live racing input batch.");
            if(packet.controls!=controls||packet.epoch!=cursor.epoch||!driver||packet.owner>=launch.player_count||
                packet.first<base||std::uint64_t(packet.first)+packet.count>freeze||
                std::uint64_t(packet.first)+packet.count>std::uint64_t(next().frame)+49)return;
            if(server())for(unsigned i=0;i<packet.count;++i)
                if(!roster.accepts(source,packet.owner,packet.lease,{packet.epoch,packet.first+i}))return;
            const auto accepted_before=accepted_inputs;
            for(unsigned i=0;i<packet.count;++i) {
                const auto v=packet.inputs[i];wire::ControllerSample sample{{packet.epoch,packet.first+i},std::uint8_t(packet.owner),packet.lease,{v.buttons,v.x,v.y,v.accelerator,v.brake,v.driving_flags}};
                Write body;body.n(controls).n(wire::ControllerSampleBytes,1).data(wire::controller(sample));Read one{body.b};receive_input(source,one,false);
            }
            if(accepted_inputs!=accepted_before)live_delay.arrival(packet.owner,packet.first+packet.count-1);
            if(server())for(const auto id:required())if(id!=local&&id!=source)
                send(id,Msg::LiveInputs,dkr::online_stability::encode_live_inputs(packet),Lane::Realtime);
            break;
        }
        case Msg::Frames: {
            if(!from_host||simulating())return;
            const auto transfer=r.n();if(transfer!=active_transfer)return;
            std::vector<ConfirmedFrame> frames;std::string e;if(!wire::frames(r.rest(),frames,e))throw std::runtime_error(e);
            for(auto& f:frames)if(buffered.size()<ObserverPlayback::MaximumQueued) {
                const auto id=std::pair{f.cursor.epoch,f.cursor.frame};const auto found=buffered.find(id);
                if(found!=buffered.end()&&found->second!=f)throw std::runtime_error("Conflicting confirmed observer history.");
                buffered.insert_or_assign(id,std::move(f));
            }
            break;
        }
        case Msg::Scene: {
            if(!server()||!required().contains(source))return;
            const auto at=r.at();const auto scene=r.n();const auto digest=r.hash();r.end();
            if(at.epoch!=cursor.epoch||at.frame<confirmed()||!scene)return;
            if(phase==Phase::SceneVote&&(at!=next()||scene!=boundary.scene_hash))throw std::runtime_error("Peer scene continuation differs.");
            scene_votes[source]=digest;break;
        }
        case Msg::Prepare: {
            if(!from_host||server()||!simulating())return;
            const auto at=r.at();const auto scene=r.n();const auto digest=r.hash();r.end();
            if(phase!=Phase::SceneVote||at!=next()||scene!=boundary.scene_hash||digest!=phase_digest)
                throw std::runtime_error("Party scene agreement differs from the owned continuation.");
            phase=Phase::ScenePrepare;phase_at=Clock::now();break;
        }
        case Msg::Prepared: {
            if(!server()||!required().contains(source))return;
            const auto at=r.at();const auto digest=r.hash();r.end();
            if(phase!=Phase::ScenePrepare&&phase!=Phase::SceneReady)return;
            if(at!=(phase==Phase::ScenePrepare?Cursor{cursor.epoch+1,0}:cursor))return;
            frozen[source]=digest;break;
        }
        case Msg::Freeze: {
            if(!from_host||server())return;
            if(phase!=Phase::Running&&phase!=Phase::Freeze)return;
            const auto at=r.at();const auto p=unsigned(r.n(1));const auto incoming=r.n();r.end();
            if(at.epoch!=cursor.epoch||at.frame<next().frame||p>=launch.player_count||!roster.find(incoming))return;
            freeze=at.frame;swap={{p,incoming}};phase=Phase::Freeze;phase_at=Clock::now();frozen.clear();break;
        }
        case Msg::Frozen: {
            if(!server()||!required().contains(source))return;
            const auto at=r.at();const auto digest=r.hash();r.end();
            if(phase==Phase::Freeze&&at==Cursor{cursor.epoch,freeze})frozen[source]=digest;break;
        }
        case Msg::Swap: {
            if(!server())return;
            const auto p=unsigned(r.n(1));const auto incoming=r.n();r.end();const auto* sender=roster.find(source);const auto* replacement=roster.find(incoming);
            if(p>=launch.player_count||!sender||!replacement||replacement->role!=Role::Spectator||
               (source!=incoming&&sender->port!=p))return;
            std::scoped_lock lock(hub->mutex);const auto item=std::pair{p,incoming};
            if(hub->swap_requests.size()<MaximumMembers&&std::find(hub->swap_requests.begin(),hub->swap_requests.end(),item)==hub->swap_requests.end())hub->swap_requests.push_back(item);
            break;
        }
        case Msg::Cancel:
            if(from_host&&!server()) {r.end();swap.reset();freeze=UINT32_MAX;
                if(phase==Phase::Freeze||phase==Phase::SwapReady)phase=Phase::Running;
                text="Player swap cancelled";}
            break;
        case Msg::OfferAck: {
            const auto transfer=r.n();r.end();
            if(server())if(auto d=deliveries.find(source);d!=deliveries.end()&&d->second.sender.offer().transfer==transfer)d->second.accepted=true;
            break;
        }
        case Msg::FrameAck: {
            const auto transfer=r.n();const auto at=r.at();r.end();
            if(server())if(auto d=deliveries.find(source);d!=deliveries.end()&&d->second.sender.offer().transfer==transfer&&d->second.admitted&&
               at.epoch==d->second.sent.epoch&&at.frame>=d->second.sent.frame&&feed.covers(at))d->second.sent=at;
            break;
        }
        case Msg::Progress: {
            const auto generation=r.n();const auto at=r.at();std::array<std::uint32_t,4> have{};
            for(auto& n:have)n=std::uint32_t(r.n(4));r.end();
            if(!server()||!driver||!required().contains(source)||generation!=controls||at.epoch!=cursor.epoch)return;
            const auto* sender=roster.find(source);if(!sender||!sender->port)return;
            const auto owner=*sender->port;
            Write receipt;receipt.n(controls).n(cursor.epoch).n(owner,1).n(roster.snapshot().controls[owner].generation).n(received_next[owner],4);
            send(source,Msg::InputReceipt,receipt.b);
            unsigned sent=0;const auto retry=repair_period(source);
            for(const auto& [id,value]:inputs)if((launch.development||id.second!=owner)&&id.first>=have[id.second]&&id.first<std::uint64_t(have[id.second])+8&&sent<32&&repair_due(source,id.second,id.first,retry)) {
                wire::ControllerSample sample{{cursor.epoch,id.first},std::uint8_t(id.second),roster.snapshot().controls[id.second].generation,value.first};
                Write body;body.n(controls).n(wire::ControllerSampleBytes,1).data(wire::controller(sample)).data(command_bytes(value.second));
                send(source,Msg::Input,body.b,Lane::Control);input_trace.event(id.second,source,4,id.first,observed_now);++sent;
            }
            break;
        }
        case Msg::InputReceipt: {
            const auto generation=r.n(),epoch=r.n();const auto owner=unsigned(r.n(1));const auto lease=r.n();const auto received=std::uint32_t(r.n(4));r.end();
            if(server()||!from_host||generation!=controls||epoch!=cursor.epoch||owner!=port()||owner>=launch.player_count||
                lease!=roster.snapshot().controls[owner].generation||received>authored)return;
            for(auto frame=host_received;frame<received;++frame)if(const auto at=input_times.find({frame,owner});at!=input_times.end()&&observed_now>=at->second)
                input_trace.receipt(std::chrono::duration_cast<std::chrono::milliseconds>(observed_now-at->second));
            host_received=std::max(host_received,received);input_trace.event(owner,host,5,received,observed_now);break;
        }
        default:break;
        }
    }
    void sample(net::PackedInput input,std::uint32_t frame) {
        if(port()>=launch.player_count)return;
        wire::ControllerSample sample{{cursor.epoch,frame},std::uint8_t(port()),roster.snapshot().controls[port()].generation,portable(input)};
        local_history[frame]=sample;
        input_trace.event(port(),local,1,frame,observed_now);
        input_times.try_emplace(std::pair{frame,port()},observed_now);
        if(!launch.development) {
            if(server()){Write one;one.n(controls).n(wire::ControllerSampleBytes,1).data(wire::controller(sample));Read r{one.b};receive_input(local,r,false);}
            else {
                const auto result=driver->receive(cursor.epoch,std::uint8_t(port()),frame-base,input);
                if(result==ex::InputResult::Failed||result==ex::InputResult::Conflict)throw std::runtime_error(driver->error());
                inputs.emplace(std::pair{frame,port()},std::make_pair(portable(input),dkr::sdk2::Command{}));
                while(inputs.contains({received_next[port()],port()}))++received_next[port()];
            }
            dkr::online_stability::LiveInputs live;live.controls=controls;live.epoch=cursor.epoch;live.lease=sample.lease;live.owner=port();live.first=frame;
            while(live.first>base&&frame-live.first<7&&local_history.contains(live.first-1)&&local_history.at(live.first-1).lease==sample.lease)--live.first;
            for(auto at=live.first;at<=frame;++at){const auto v=local_history.at(at).input;live.inputs[live.count++]={v.buttons,v.x,v.y,v.accelerator,v.brake,v.driving_flags};}
            const auto bytes=dkr::online_stability::encode_live_inputs(live);
            if(server())racers(Msg::LiveInputs,bytes,Lane::Realtime);else send(host,Msg::LiveInputs,bytes,Lane::Realtime);
            return; // Exact old holes retain the independent authority repair lane.
        }
        Write body;body.n(controls).n(wire::ControllerSampleBytes,1).data(wire::controller(sample)).data(command_bytes({}));
        if(server()){Read r{body.b};receive_input(local,r);}
        else {
            send(host,Msg::Input,body.b,Lane::Control);
            if(port()!=0||!launch.development) {
                const auto result=driver->receive(cursor.epoch,std::uint8_t(port()),frame-base,input);
                if(result==ex::InputResult::Failed||result==ex::InputResult::Conflict)throw std::runtime_error(driver->error());
            }
        }
    }
    std::chrono::milliseconds repair_period(MemberId destination)const {
        unsigned rtt=0;for(const auto& route:dkr::online_stability::control(key)->identity().routes)
            if(route.id==destination)rtt=std::max(rtt,route.rtt_ms);
        return std::chrono::milliseconds(std::clamp(2U*rtt+100U,100U,3000U));
    }
    bool repair_due(MemberId destination,unsigned owner,std::uint32_t frame,std::chrono::milliseconds period) {
        const auto first=input_times.find({frame,owner});if(first==input_times.end()||observed_now-first->second<period)return false;
        const auto id=std::tuple{destination,owner,frame};auto at=repair_times.find(id);
        if(at!=repair_times.end()&&observed_now-at->second<period)return false;
        repair_times[id]=observed_now;return true;
    }
    Digest signature(const Baseline& b) {Write w;w.data(b.digest).n(b.magic_actions,4);return hash(w.b);}
    void scene_step(Clock::time_point now) {
        std::string e;
        if(phase==Phase::SceneVote&&server()&&all_votes(scene_votes)) {
            for(const auto& [id,d]:scene_votes){(void)id;if(d!=phase_digest)throw std::runtime_error("Racing peers disagreed on a confirmed scene boundary.");}
            Write w;w.at(next()).n(boundary.scene_hash).data(phase_digest);racers(Msg::Prepare,w.b);phase=Phase::ScenePrepare;
        }
        if(phase==Phase::ScenePrepare) {
            if(!box.quiescent())return;
            std::uint64_t proof=0;const auto result=world.prepare_scene(boundary.scene_hash,proof,e);
            if(result==ex::PreparationStep::Failed)throw std::runtime_error(e);
            if(result==ex::PreparationStep::Pending)return;
            ++cursor.epoch;cursor.frame=0;driver.reset();offset.reset();base=authored=0;
            auto b=capture(cursor);phase_digest=signature(b);held=b;phase=Phase::SceneReady;phase_at=now;
            if(server()){frozen[local]=phase_digest;encode(b);for(auto& [id,d]:deliveries){(void)d;if(!required().contains(id))needing.insert(id);}}
            else {Write w;w.at(cursor).data(phase_digest);send(host,Msg::Prepared,w.b);}
        }
        if(phase==Phase::SceneReady&&server()&&all_votes(frozen)) {
            for(const auto& [id,d]:frozen){(void)id;if(d!=phase_digest)throw std::runtime_error("Racing peers constructed different scene baselines.");}
            release(cursor);
        }
    }
    void begin_swap(unsigned p,MemberId incoming,Clock::time_point now) {
        const auto* target=roster.find(incoming);
        if(!server()||phase!=Phase::Running||!driver||p>=launch.player_count||!target||target->role!=Role::Spectator)return;
        // Input authoring is at most 48 ahead of the driver. Freeze beyond all
        // authored input, then admit NO sample at/after that exact cutover.
        const auto head=next().frame;if(head>UINT32_MAX-80)throw std::runtime_error("Party frame cursor exhausted.");
        freeze=head+64;swap={{p,incoming}};phase=Phase::Freeze;phase_at=now;frozen.clear();
        Write w;w.at({cursor.epoch,freeze}).n(p,1).n(incoming);for(const auto& m:roster.snapshot().members)if(m.id!=local)send(m.id,Msg::Freeze,w.b);
        text="Preparing player swap";
    }
    void swap_step(Clock::time_point now) {
        if(!swap)return;
        if(phase==Phase::Freeze&&driver&&next().frame==freeze&&confirmed()==freeze&&!driver->correcting()&&!frozen.contains(local)) {
            auto b=capture(next());held=b;const auto proof=signature(b);frozen[local]=proof;
            if(!server()){Write w;w.at(b.next).data(proof);send(host,Msg::Frozen,w.b);}
        }
        if(phase==Phase::Freeze&&server()&&all_votes(frozen)) {
            const auto proof=frozen.at(local);for(const auto& [id,d]:frozen){(void)id;if(d!=proof)throw std::runtime_error("Player swap stopped because peers disagree on the confirmed race.");}
            std::string e;
            {std::scoped_lock lock(hub->mutex);
                if(!held||hub->party.snapshot().generation!=roster.snapshot().generation) {cancel_swap();return;}
                if(!hub->party.propose(host,std::uint8_t(swap->first),swap->second,held->next,held->digest,e)) {cancel_swap();return;}
                const auto token=hub->party.snapshot().handover->token;
                for(auto id:required())if(!hub->party.acknowledge(id,token,held->next,held->digest,e))throw std::runtime_error(e);
                if(!roster.replace_verified(hub->party.snapshot(),e))throw std::runtime_error(e);
            }
            phase=Phase::SwapReady;phase_at=now;prepared.clear();
            // Existing racers already proved this exact state. The incoming
            // spectator must transactionally admit the same checkpoint first.
            update_hub();needing.insert(swap->second);encode(*held);
        }
        if(phase==Phase::SwapReady&&server()&&prepared.contains(swap->second)) {
            std::string e;const auto pending=*roster.snapshot().handover;
            {std::scoped_lock lock(hub->mutex);
                if(!hub->party.snapshot().handover||hub->party.snapshot().handover->token!=pending.token||hub->party.snapshot().generation!=roster.snapshot().generation) {cancel_swap();return;}
                if(!hub->party.acknowledge(swap->second,pending.token,held->next,held->digest,e)||!hub->party.transfer(host,held->next,e)||
                   !roster.replace_verified(hub->party.snapshot(),e))throw std::runtime_error(e);
            }
            ++controls;held->control_generation=controls;encoded.reset();feed.clear();encode(*held);
            release(held->next);
        }
    }
    void cancel_swap() {
        if(!server()||!swap)return;
        std::string ignored;
        {std::scoped_lock lock(hub->mutex);
            if(hub->party.snapshot().handover)hub->party.cancel(host,hub->party.snapshot().handover->token,ignored);
            if(!roster.replace_verified(hub->party.snapshot(),ignored))throw std::runtime_error(ignored);
        }
        for(const auto& m:roster.snapshot().members)if(m.id!=local)send(m.id,Msg::Cancel);
        swap.reset();freeze=UINT32_MAX;frozen.clear();phase=Phase::Running;text="Player swap cancelled";update_hub();
    }
    bool pulse(Clock::time_point now,net::PackedInput input,std::string& error) {
        observed_now=now;
        if(!started){error="Party runtime is not started.";return false;}
        PartySnapshot latest;std::optional<std::pair<unsigned,MemberId>> request,approval;bool cancel=false;
        {
            std::scoped_lock lock(hub->mutex);
            if(hub->stopping||!hub->enabled){error.clear();return false;}
            latest=hub->party.snapshot();request=std::exchange(hub->requested_swap,{});approval=std::exchange(hub->approved_swap,{});cancel=std::exchange(hub->cancel_swap,false);
        }
        // Transport admission may add/remove watchers, but cannot silently
        // change the four racing leases. Only an agreed runtime Go can do so.
        if(latest.generation>roster.snapshot().generation&&latest.controls==roster.snapshot().controls) {
            const bool lost=swap&&roster.snapshot().handover&&!latest.handover;
            if(!roster.replace_verified(std::move(latest),error))return false;
            if(lost)cancel_swap();
        }
        if(request) {
            Write w;w.n(request->first,1).n(request->second);
            if(server()){Write envelope;envelope.n('D',1).n('P',1).n('R',1).n(2,1).n(unsigned(Msg::Swap),1).n(match).n(++serial).data(w.b);message(local,envelope.b);}
            else send(host,Msg::Swap,w.b);
        }
        if(approval)begin_swap(approval->first,approval->second,now);
        if(cancel&&server())cancel_swap();
        LinkIncoming packet;
        for(unsigned i=0;i<128&&party_receive(key,packet);++i) {
            try {message(packet.source,packet.bytes);}catch(const std::exception& e) {
                // An observer cannot stop required racing peers with malformed
                // playback/seat requests. Required members still fail closed.
                if(server()&&!required().contains(packet.source)){needing.erase(packet.source);deliveries.erase(packet.source);continue;}
                throw std::runtime_error(e.what());
            }
        }
        if(!callback_error.empty())throw std::runtime_error(callback_error);
        transfer(now);
        std::erase_if(deliveries,[&](const auto& entry){return !roster.find(entry.first);});
        std::erase_if(requested_at,[&](const auto& entry){return !roster.find(entry.first);});
        if(server()&&phase==Phase::Admission&&all(prepared))release(cursor);
        if(!server()&&phase==Phase::Admission&&now>=hello_at&&!held&&!receiving&&!decoding.valid()) {send(host,Msg::Hello);hello_at=now+std::chrono::seconds(2);}
        if(!simulating()) {
            if(!receiving&&!decoding.valid()) {
                if(!buffered.empty()&&playback.view().phase!=PlaybackPhase::WaitingForBaseline) {
                    std::vector<ConfirmedFrame> batch;
                    const auto first=playback.view().received;auto expected=first;
                    std::erase_if(buffered,[&](const auto& item){return item.first<std::pair{first.epoch,first.frame};});
                    while(batch.size()<8) {auto f=buffered.find({expected.epoch,expected.frame});if(f==buffered.end())break;
                        const bool boundary=f->second.scene_boundary;batch.push_back(std::move(f->second));buffered.erase(f);++expected.frame;if(boundary)break;}
                    if(!batch.empty()) {const auto result=playback.append(batch,error);if(result!=FeedResult::Accepted&&result!=FeedResult::Duplicate){playback.request_resync(error);buffered.clear();}}
                    Write ack;ack.n(active_transfer).at(playback.view().received);send(host,Msg::FrameAck,ack.b);
                }
                const auto work=playback.pulse(now,error);
                if(work==Work::Failed){playback.request_resync(error);held.reset();admitted_at.reset();send(host,Msg::Need);error.clear();}
                if(held&&admitted_at!=held->next&&playback.view().next==held->next&&playback.view().phase!=PlaybackPhase::Retiring)ready(*held);
                if(playback.needs_baseline()&&now>=hello_at){send(host,Msg::Need);hello_at=now+std::chrono::seconds(2);}
            }
            text=playback.view().status;return true;
        }
        if(phase==Phase::SceneVote||phase==Phase::ScenePrepare||phase==Phase::SceneReady) {
            scene_step(now);
            if(now-phase_at>std::chrono::seconds(120))throw std::runtime_error("Required party scene agreement timed out.");
            return true;
        }
        if(!driver) {
            if(now-phase_at>std::chrono::seconds(120))throw std::runtime_error("Required party admission did not complete within 120 seconds.");
            return true;
        }
        const auto before=next();const auto was_confirmed=confirmed();
        if(!server()&&now>=proof_at) {
            Write proof;proof.n(controls).at({cursor.epoch,confirmed()});for(const auto n:received_next)proof.n(n,4);send(host,Msg::Progress,proof.b);
            // Bounded repair of immutable local samples which may have arrived
            // beyond the host's short future window during a temporary hitch.
            unsigned count=0;const auto retry=repair_period(host);
            for(const auto& [frame,sample]:local_history)if(frame>=host_received&&frame<freeze&&count<8&&repair_due(host,sample.port,frame,retry)) {
                Write body;body.n(controls).n(wire::ControllerSampleBytes,1).data(wire::controller(sample)).data(command_bytes({}));send(host,Msg::Input,body.b,Lane::Control);
                input_trace.event(sample.port,host,4,frame,observed_now);
                ++count;
            }
            proof_at=now+std::chrono::milliseconds(100);
        }
        if(phase==Phase::Running||phase==Phase::Freeze) {
            const bool dynamic=dkr::online_stability::control(key)->identity().dynamic;
            live_delay.local_owner(port());
            live_delay.pulse(cursor.epoch,controls,next().frame,phase!=Phase::Running||driver->scene_boundary_pending(),delay_network_wait,now,driver->missing_input_mask());
            if(dynamic&&!live_delay.capture({input.buttons,input.stick_x,input.stick_y,input.accelerator,input.brake,input.driving_flags},now))throw std::runtime_error("Dynamic input transition queue exceeded its safe bound.");
            delay_fill+=live_delay.take_fill();
            const auto lead=dynamic?live_delay.lead():std::min<unsigned>(launch.input_delay_frames,12);
            // Dynamic controls are sampled at their real tick deadline. Do
            // not publish the next tick early just because a frontier moved.
            // Protective growth still fills immutable future input immediately.
            const bool sample_due=!dynamic||now>=next_tick||delay_fill||lead==dkr::online_stability::MaximumDelay;
            if(sample_due&&port()<launch.player_count)while(authored<freeze&&authored<=next().frame+lead&&authored-next().frame<32) {
                auto value=input;
                if(dynamic){const auto held=delay_fill?live_delay.held():live_delay.take();if(delay_fill)--delay_fill;value={held.buttons,held.x,held.y,held.accelerator,held.brake,held.driving_flags};}
                live_delay.authored(authored);sample(value,authored++);
            }
            // A new spectator may need a newer immutable baseline. Allow the
            // bounded rollback tail to confirm before capturing; never export
            // speculative RAM or wait on that spectator's download speed.
            const bool align=server()&&!needing.empty()&&!encoding.valid()&&(!encoded||!feed.covers(encoded->next));
            const bool advance=now>=next_tick&&next().frame<freeze&&!align;
            const auto step=driver->step(advance);
            delay_network_wait=advance&&(step==ex::Step::PredictionLimit||step==ex::Step::WaitingForConfirmedInput||step==ex::Step::WaitingForLocalInput)&&
                bool(driver->missing_input_mask()&std::uint8_t(~(port()<4?(1U<<port()):0U)));
            if(step==ex::Step::Failed)throw std::runtime_error(driver->error());
            if(step==ex::Step::Advanced)next_tick=std::max(next_tick+Tick,now-Tick*2);
            if(step==ex::Step::ConfirmedBoundary) {
                if(swap)cancel_swap();
                if(!world.confirmed_boundary(boundary,error))throw std::runtime_error(error);
                auto b=capture(next());phase_digest=signature(b);phase=Phase::SceneVote;phase_at=now;frozen.clear();
                if(!box.retire_for_restore(cursor.epoch,next().frame-1))throw std::runtime_error("Scene presentation retirement refused.");
                scene_votes[local]=phase_digest;
                if(!server()){Write w;w.at(next()).n(boundary.scene_hash).data(phase_digest);send(host,Msg::Scene,w.b);}
            }
            if(align&&next().frame==confirmed()&&!driver->correcting()&&!driver->scene_boundary_pending())encode(capture(next()));
        }
        scene_step(now);swap_step(now);
        if(swap&&now-phase_at>std::chrono::seconds(30))cancel_swap();
        if((phase==Phase::SceneVote||phase==Phase::ScenePrepare||phase==Phase::SceneReady)&&now-phase_at>std::chrono::seconds(120))throw std::runtime_error("Required party scene agreement timed out.");
        if(driver) {
            input_trace.pulse(next().frame,confirmed(),received_next,driver->missing_input_mask(),now);
            if(now>=cpu_report_at){cpu_report_at=now+std::chrono::seconds(5);ex::performance::report();}
            const auto after=next();const auto& stats=driver->statistics();
            if(after!=before||confirmed()!=was_confirmed)progress=now;
            if(now-progress>std::chrono::seconds(90))throw std::runtime_error("Racing peers made no confirmed progress for 90 seconds.");
            if(after.frame&&after!=before&&!driver->correcting()&&!driver->scene_boundary_pending()&&
               (phase==Phase::Running||phase==Phase::Freeze)) {
                if(!world.publish_current(after.epoch,after.frame-1,error))throw std::runtime_error(error);
                dkr::online_stability::response::reached(after.epoch,after.frame-1,dkr::online_stability::response::Stage::Publish);
            }
            const auto trim=confirmed()>96?confirmed()-96:0;
            std::erase_if(inputs,[&](const auto& item){return item.first.first<trim;});
            const auto redundant=authored>8?authored-8:base;
            const auto local_trim=server()?redundant:std::min({confirmed(),host_received,redundant});
            std::erase_if(local_history,[&](const auto& item){return item.first<local_trim;});
            std::erase_if(input_times,[&](const auto& item){return item.first.first<std::min(trim,local_trim);});
            std::erase_if(repair_times,[&](const auto& item){return std::get<2>(item.first)<std::min(trim,local_trim);});
            if(local_history.size()>128||inputs.size()>4*160||repair_times.size()>4*4*160)throw std::runtime_error("Racing receipt history exceeded its bounded window.");
            (void)stats;
        }
        if(phase==Phase::Running)text=watching()?"Watching the race":"Experimental rollback";
        {std::scoped_lock lock(hub->mutex);hub->status=text;}
        return true;
    }
};
PartyRuntime::PartyRuntime(const void* k,ex::OwnedGame& w,ex::PresentationMailbox& b,net::LaunchDescriptor l,HashState h)
    :impl_(std::make_unique<Impl>(k,w,b,l,std::move(h))){}
PartyRuntime::~PartyRuntime()=default;
bool PartyRuntime::begin(std::string& error)try{return impl_->begin(error);}catch(const std::exception& e){error=e.what();return false;}
bool PartyRuntime::pulse(Clock::time_point now,net::PackedInput input,std::string& error)try{return impl_->pulse(now,input,error);}catch(const std::exception& e){impl_->input_trace.dump();error=e.what();return false;}
bool PartyRuntime::watching()const{return impl_->watching();}
unsigned PartyRuntime::local_port()const{return impl_->port();}
Cursor PartyRuntime::next()const{return impl_->next();}
std::uint32_t PartyRuntime::confirmed()const{return impl_->confirmed();}
ex::Statistics PartyRuntime::statistics()const{return impl_->driver?impl_->driver->statistics():ex::Statistics{};}
std::string PartyRuntime::status()const{return impl_->text;}
}
