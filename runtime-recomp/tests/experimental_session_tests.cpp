#include "netplay/experimental_session.hpp"
#include "netplay/rollback_ring.hpp"
#include "netplay/secure_channel.hpp"
#include "netplay/sequence_window.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
namespace {
// A protocol/state-machine model, NOT a native DKR scene-loader adapter.
struct World final : SceneSimulation {
    std::array<std::uint32_t,64> state{};
    struct Effect { std::uint64_t epoch; std::uint32_t frame; std::vector<std::uint8_t> bytes;
                    bool boundary; bool operator==(const Effect&) const = default; };
    std::vector<Effect> effects;
    unsigned tick_calls=0, preparation_calls=0, preparation_wait=0, wait=0, prepared=0;
    bool prepare_fail=false, change_schema=false, throw_prepare=false, schema_on_prepare=false;
    World() { state[2]=123; }
    unsigned scene() const { return state[1]; }
    unsigned end() const { return 12+(scene()%3)*9; }
    std::uint64_t hash() const {
        return state_checksum({reinterpret_cast<const std::uint8_t*>(state.data()),sizeof(state)});
    }
    SimulationContract contract() const override {
        return {change_schema ? 2U:1U,sizeof(state),kRequiredStateDomains,true,true,true};
    }
    bool capture(std::span<std::uint8_t> out,std::string&) override {
        if(out.size()!=sizeof(state)) return false;
        std::memcpy(out.data(),state.data(),out.size()); return true;
    }
    bool restore(std::span<const std::uint8_t> in,std::string&) override {
        if(in.size()!=sizeof(state)) return false;
        std::memcpy(state.data(),in.data(),in.size()); return true;
    }
    bool tick(std::uint32_t frame,const FrameInputs& inputs,TickOutput& out,std::string&) override {
        assert(frame==state[0] && frame<end()); ++tick_calls; out={}; out.effects.reserve(8);
        for(unsigned p=0;p<4;++p) {
            const auto edges=inputs[p].buttons & ~state[3+p];
            state[3+p]=inputs[p].buttons;
            state[7+p]+=std::uint32_t(int(inputs[p].stick_x)*7+inputs[p].stick_y);
            state[11+p]^=state[2]+state[7+p];
            if(edges&1) { state[2]=state[2]*1664525U+1013904223U; out.effects.push_back(std::uint8_t(p)); }
        }
        state[15+frame%49]+=state[2]; ++state[0];
        out.scene_boundary=state[0]==end(); return true;
    }
    bool commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> bytes,
                bool boundary,std::string&) override {
        assert(boundary==(frame+1==end()));
        if(frame) assert(!effects.empty() && effects.back().epoch==epoch && effects.back().frame+1==frame);
        else if(!effects.empty()) assert(effects.back().boundary && effects.back().epoch+1==epoch);
        effects.push_back({epoch,frame,{bytes.begin(),bytes.end()},boundary}); return true;
    }
    bool confirmed_boundary(BoundaryIntent& out,std::string&) override {
        if(state[0]!=end() || effects.empty() || !effects.back().boundary) return false;
        out={hash(),500+scene()+1}; return true;
    }
    PreparationStep prepare_scene(std::uint64_t scene_hash,std::uint64_t& hash,std::string& error) override {
        ++preparation_calls;
        if(throw_prepare) throw std::runtime_error("test preparation exception");
        if(prepare_fail) { error="test preparation failure"; return PreparationStep::Failed; }
        assert(state[0]==end() && scene_hash==500+scene()+1);
        if(wait++<preparation_wait) return PreparationStep::Pending;
        wait=0; ++prepared; state[0]=0; ++state[1];
        if(schema_on_prepare) change_schema=true;
        hash=this->hash(); return PreparationStep::Ready;
    }
};
PackedInput sample(unsigned scene,unsigned frame,unsigned owner) {
    return {std::uint16_t((scene*3+frame*7+owner)%8),std::int8_t(int((frame*17+owner)%161)-80),
            std::int8_t(int((scene+frame)%151)-75)};
}
void distributed(unsigned players,unsigned delay,unsigned input_delay) {
    constexpr unsigned scenes=5;
    std::array<std::unique_ptr<World>,4> worlds;
    std::array<std::unique_ptr<Session>,4> peers;
    std::array<unsigned,4> starts{};
    std::array<std::array<secure::Key,4>,4> keys;
    std::array<std::array<std::uint64_t,4>,4> sequences{};
    std::array<std::array<ReceiveSequenceWindow,4>,4> replay;
    std::string error;
    for(unsigned p=0;p<players;++p) {
        worlds[p]=std::make_unique<World>(); worlds[p]->preparation_wait=p*7;
        peers[p]=std::make_unique<Session>(*worlds[p]);
        assert(peers[p]->start({{100,std::uint8_t(players),6},std::uint8_t(p),std::uint8_t(input_delay)},error));
        assert(!peers[p]->presentation_ready()); // No completed initial output.
        for(unsigned q=p+1;q<players;++q) keys[p][q]=keys[q][p]=secure::generate_key();
    }
    struct Packet { unsigned source,destination,at; bool control; std::vector<std::uint8_t> bytes; };
    std::deque<Packet> packets;
    unsigned sends=0,parked=0,pending=0,tampered=0,time=0;
    const auto send=[&](unsigned p,unsigned q,bool control,std::vector<std::uint8_t> plain) {
        auto bytes=secure::seal(plain,keys[p][q],1000+p,++sequences[p][q],42);
        assert(!bytes.empty()); if(++sends%5==0) return;
        packets.push_back({p,q,time+delay+sends%4,control,bytes});
        if(sends%7==0) packets.push_front({p,q,time+delay+1+sends%4,control,bytes});
        if(sends%11==0) { bytes.back()^=1; packets.push_back({p,q,time+2,control,bytes}); }
    };
    for(;time<6000;++time) {
        for(auto it=packets.begin();it!=packets.end();) {
            if(it->at>time) { ++it; continue; }
            std::uint64_t sender=0,sequence=0; std::vector<std::uint8_t> plain;
            if(!secure::open(it->bytes,keys[it->source][it->destination],42,sender,sequence,plain)) ++tampered;
            else {
                assert(sender==1000+it->source);
                if(replay[it->destination][it->source].accept(sequence)) {
                    auto& peer=*peers[it->destination];
                    if(it->control) {
                        const auto result=peer.receive_control(it->source,plain);
                        if(result==EpochResult::Conflict || result==EpochResult::Rejected)
                            std::cerr<<"control rejected: t="<<time<<" epoch="<<peer.epoch()<<" "<<peer.error()<<'\n';
                        assert(result!=EpochResult::Conflict && result!=EpochResult::Rejected);
                    } else {
                        const auto result=peer.receive_input(it->source,plain);
                        if(result==SessionInputResult::Rejected || result==SessionInputResult::Conflict)
                            std::cerr<<"input rejected: t="<<time<<" epoch="<<peer.epoch()<<" "<<peer.error()<<'\n';
                        assert(result!=SessionInputResult::Rejected && result!=SessionInputResult::Conflict);
                        pending+=result==SessionInputResult::Pending;
                    }
                }
            }
            it=packets.erase(it);
        }
        for(unsigned p=0;p<players;++p) {
            auto& peer=*peers[p]; auto& world=*worlds[p];
            if(starts[p]<scenes) {
                if(peer.needs_local_input())
                    assert(peer.sample_local(sample(world.scene(),peer.frontier()+input_delay,p))==OwnerInputResult::Accepted);
                for(unsigned budget=0;budget<8;++budget) {
                    const auto ticks=world.tick_calls,preparations=world.preparation_calls;
                    const auto result=peer.step();
                    assert(world.tick_calls-ticks + world.preparation_calls-preparations<=1);
                    if(result==SessionStep::Failed) std::cerr<<peer.error()<<'\n';
                    assert(result!=SessionStep::Failed);
                    if(peer.presentation_ready()) {
                        assert(peer.statistics().next_frame!=0 && peer.statistics().next_frame==peer.frontier());
                        assert(world.state[0]<world.end()); // No reversible unload intent is drawn.
                    }
                    if(result==SessionStep::PreparingScene || result==SessionStep::WaitingForPeers ||
                       result==SessionStep::WaitingForConfirmation || result==SessionStep::SceneStarted)
                        assert(!peer.presentation_ready());
                    if(result==SessionStep::SceneStarted) ++starts[p];
                    if(result==SessionStep::PredictionLimit) ++parked;
                    if(result!=SessionStep::Replayed) break;
                }
            }
            // Still service previous-release repair after starting the next
            // scene, including when this peer has finished the last round.
            for(unsigned q=0;q<players;++q) {
                if(q==p || (p&&q)) continue;
                for(unsigned owner=0;owner<players;++owner) for(auto lane:{OwnerInputSend::Live,OwnerInputSend::Repair}) {
                    if(lane==OwnerInputSend::Repair && time%4) continue;
                    const auto packet=peer.packet_for(owner,q,lane);
                    if(packet) send(p,q,false,encode_owner_inputs(*packet));
                }
                for(const auto message:{peer.control_for(q),peer.previous_control_for(q)})
                    if(message) send(p,q,true,encode_epoch_message(*message));
            }
        }
        if(std::all_of(starts.begin(),starts.begin()+players,[](unsigned n){return n==scenes;})) break;
    }
    assert(time<6000 && tampered);
    World reference;
    for(unsigned scene=0;scene<scenes;++scene) {
        const auto end=reference.end();
        for(unsigned frame=0;frame<end;++frame) {
            FrameInputs inputs{};
            if(frame>=input_delay) for(unsigned p=0;p<players;++p) inputs[p]=sample(scene,frame,p);
            TickOutput out;
            assert(reference.tick(frame,inputs,out,error));
            assert(reference.commit(100+scene,frame,out.effects,out.scene_boundary,error));
        }
        BoundaryIntent intent; std::uint64_t hash=0;
        assert(reference.confirmed_boundary(intent,error));
        assert(reference.prepare_scene(intent.scene_hash,hash,error)==PreparationStep::Ready);
    }
    for(unsigned p=0;p<players;++p) {
        assert(worlds[p]->state==reference.state && worlds[p]->effects==reference.effects);
        assert(worlds[p]->prepared==scenes && peers[p]->epoch()==100+scenes);
    }
    if(delay>=12) assert(parked);
    std::cout<<"Experimental coordinator MODEL: peers="<<players<<" delay="<<delay<<" input_delay="<<input_delay
             <<" scenes=5 ticks="<<time<<" future_deferred="<<pending<<" parked="<<parked
             <<" authenticated loss=20% exact=1; not live DKR netplay.\n";
}
void preparation_failure(unsigned failure) {
    World world; Session session(world); std::string error;
    assert(session.start({{10,2,6},0,0},error));
    assert(!session.start({{11,2,6},0,0},error));
    OwnerInputPacket input; input.epoch=10; input.players=2; input.owner=1; input.count=1;
    while(world.state[0]<world.end()) {
        input.first_frame=world.state[0];
        assert(session.receive_input(1,encode_owner_inputs(input))==SessionInputResult::Accepted);
        assert(session.sample_local({})==OwnerInputResult::Accepted);
        const auto result=session.step();
        assert(result==(world.state[0]==world.end() ? SessionStep::WaitingForPeers : SessionStep::Advanced));
    }
    assert(session.step()==SessionStep::WaitingForPeers);
    BoundaryIntent intent; assert(world.confirmed_boundary(intent,error));
    EpochMessage vote{EpochMessageKind::Boundary,2,10,0,world.end(),intent.boundary_hash,1,0,0};
    assert(session.receive_control(1,encode_epoch_message(vote))==EpochResult::Accepted);
    assert(session.step()==SessionStep::WaitingForPeers);
    world.prepare_fail=failure==0;
    world.throw_prepare=failure==1;
    world.schema_on_prepare=failure==2;
    assert(session.step()==SessionStep::Failed && !session.error().empty());
    assert(session.step()==SessionStep::Failed && !session.needs_local_input());
    assert(!session.start({{11,2,6},0,0},error));
    assert(!session.control_for(1) && world.prepared==(failure==2 ? 1U:0U));
}
void wrong_scene_cannot_prepare() {
    World world; Session client(world); std::string error;
    assert(client.start({{20,2,6},1,0},error));
    OwnerInputPacket input; input.epoch=20; input.players=2; input.owner=0; input.count=1;
    while(world.state[0]<world.end()) {
        input.first_frame=world.state[0];
        assert(client.receive_input(0,encode_owner_inputs(input))==SessionInputResult::Accepted);
        assert(client.sample_local({})==OwnerInputResult::Accepted);
        const auto result=client.step();
        assert(result==(world.state[0]==world.end() ? SessionStep::WaitingForPeers : SessionStep::Advanced));
    }
    BoundaryIntent intent; assert(world.confirmed_boundary(intent,error));
    EpochMessage prepare{EpochMessageKind::Prepare,2,20,21,world.end(),intent.boundary_hash,1,intent.scene_hash+1,0};
    assert(client.receive_control(0,encode_epoch_message(prepare))==EpochResult::Conflict);
    assert(client.step()==SessionStep::Failed && world.preparation_calls==0 && !client.needs_local_input());
}
}
int main() {
    for(unsigned failure=0;failure<3;++failure) preparation_failure(failure);
    wrong_scene_cannot_prepare();
    for(unsigned players:{2,3,4}) for(unsigned delay:{0,6,12,30}) for(unsigned input_delay:{0,1,5})
        distributed(players,delay,input_delay);
}
