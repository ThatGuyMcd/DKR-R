#include "netplay/experimental_timeline.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>

using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
namespace {
// Protocol/timeline test model, NOT admission of the actual game adapter.
struct World final : Simulation {
    std::array<std::uint32_t,64> state{};
    std::vector<std::vector<std::uint8_t>> effects;
    unsigned end = 200, tick_calls = 0;
    bool valid = true;
    SimulationContract contract() const override {
        return {valid ? 1U : 0U,sizeof(state),kRequiredStateDomains,true,true,true};
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
        assert(frame==state[0] && frame<end); ++tick_calls;
        out={}; out.effects.reserve(4);
        for(unsigned p=0;p<4;++p) {
            const auto edges=inputs[p].buttons & ~state[1+p];
            state[1+p]=inputs[p].buttons;
            state[5+p]+=std::uint32_t(int(inputs[p].stick_x)*7+inputs[p].stick_y);
            if(edges&1) { state[9]=state[9]*1664525U+1013904223U; out.effects.push_back(std::uint8_t(p)); }
            state[10+p]^=state[9]+inputs[(p+1)%4].buttons;
        }
        state[14+frame%50]+=state[9]; ++state[0];
        out.scene_boundary=state[0]==end; return true;
    }
    bool commit(std::uint64_t,std::uint32_t frame,std::span<const std::uint8_t> bytes,bool boundary,std::string&) override {
        assert(frame==effects.size() && boundary==(frame+1==end));
        effects.emplace_back(bytes.begin(),bytes.end()); return true;
    }
};
PackedInput sample(unsigned frame,unsigned owner) {
    return {std::uint16_t((frame*7+owner*3)%8),std::int8_t(int((frame*17+owner)%161)-80),std::int8_t(int(frame%151)-75)};
}
void confirmation_without_another_sample() {
    World world; world.end=1;
    Timeline timeline(world); std::string error;
    assert(timeline.start({{9,2,6},0,0},error));
    assert(timeline.step()==TimelineStep::WaitingForLocalInput && world.tick_calls==0);
    assert(timeline.sample_local({})==OwnerInputResult::Accepted);
    assert(timeline.sample_local({1,0,0})==OwnerInputResult::Rejected);
    assert(timeline.step()==TimelineStep::Advanced && world.tick_calls==1);
    assert(!timeline.needs_local_input());
    assert(timeline.step()==TimelineStep::AwaitingBoundaryConfirmation && world.tick_calls==1);
    OwnerInputPacket p; p.epoch=9; p.players=2; p.owner=1; p.count=1;
    assert(timeline.receive_authenticated(1,encode_owner_inputs(p))==OwnerInputResult::Accepted);
    assert(timeline.step()==TimelineStep::ConfirmedBoundary && world.effects.size()==1);
    assert(timeline.step()==TimelineStep::ConfirmedBoundary && world.tick_calls==1 && world.effects.size()==1);
    world.valid=false;
    assert(!timeline.start({{10,2,6},0,0},error));
    assert(timeline.frontier()==1 && timeline.statistics().confirmed_frames==1);
}
void corrected_boundary() {
    World world; world.end=1;
    Timeline timeline(world); std::string error;
    assert(timeline.start({{10,2,6},0,0},error));
    assert(timeline.sample_local({})==OwnerInputResult::Accepted);
    assert(timeline.step()==TimelineStep::Advanced);
    OwnerInputPacket p; p.epoch=10; p.players=2; p.owner=1; p.count=1; p.inputs[0]={1,40,0};
    assert(timeline.receive_authenticated(1,encode_owner_inputs(p))==OwnerInputResult::Accepted);
    // No next local sample exists. The old sampled tick must still rewind and
    // replay, then confirm its corrected effects exactly once.
    assert(timeline.step()==TimelineStep::ConfirmedBoundary);
    assert(timeline.statistics().rollbacks==1 && timeline.statistics().replayed_frames==1);
    assert(world.effects==std::vector<std::vector<std::uint8_t>>{{1}});
}
void distributed(unsigned players,unsigned delay,unsigned window,unsigned input_delay) {
    std::array<std::unique_ptr<World>,4> worlds;
    std::array<std::unique_ptr<Timeline>,4> peers;
    std::array<bool,4> done{};
    std::string error;
    for(unsigned p=0;p<players;++p) {
        worlds[p]=std::make_unique<World>(); peers[p]=std::make_unique<Timeline>(*worlds[p]);
        assert(peers[p]->start({{17,std::uint8_t(players),std::uint8_t(window)},std::uint8_t(p),std::uint8_t(input_delay)},error));
    }
    struct Envelope { unsigned from,to,when; std::vector<std::uint8_t> bytes; };
    std::deque<Envelope> queue;
    unsigned sends=0,time=0,parked=0;
    for(;time<6000;++time) {
        for(auto it=queue.begin();it!=queue.end();) {
            if(it->when>time) { ++it; continue; }
            const auto result=peers[it->to]->receive_authenticated(std::uint8_t(it->from),it->bytes);
            if(result!=OwnerInputResult::Accepted && result!=OwnerInputResult::Duplicate) {
                std::cerr<<"timeline receive failed p="<<players<<" delay="<<delay<<" t="<<time
                         <<" from="<<it->from<<" to="<<it->to<<" result="<<int(result)<<'\n';
            }
            assert(result==OwnerInputResult::Accepted || result==OwnerInputResult::Duplicate);
            it=queue.erase(it);
        }
        for(unsigned p=0;p<players;++p) {
            if(!done[p]) {
                if(peers[p]->needs_local_input()) {
                    const auto frame=peers[p]->frontier()+input_delay;
                    assert(peers[p]->sample_local(sample(frame,p))==OwnerInputResult::Accepted);
                }
                for(unsigned budget=0;budget<8;++budget) {
                    const auto before=worlds[p]->tick_calls;
                    const auto step=peers[p]->step();
                    assert(worlds[p]->tick_calls-before<=1); // A single call never drains a replay loop.
                    if(step==TimelineStep::Failed) std::cerr<<peers[p]->error()<<'\n';
                    assert(step!=TimelineStep::Failed);
                    if(step==TimelineStep::ConfirmedBoundary) done[p]=true;
                    if(step==TimelineStep::PredictionLimit) ++parked;
                    if(step!=TimelineStep::Replayed) break;
                }
            }
            // Continue network/ACK service at scene/prediction barriers, including
            // a peer which has already confirmed its own final scene tick.
            for(unsigned to=0;to<players;++to) {
                if(p==to || (p&&to)) continue;
                for(unsigned owner=0;owner<players;++owner) for(auto lane:{OwnerInputSend::Live,OwnerInputSend::Repair}) {
                    if(lane==OwnerInputSend::Repair && time%4) continue;
                    const auto packet=peers[p]->packet_for(std::uint8_t(owner),std::uint8_t(to),lane);
                    if(!packet) continue;
                    if(++sends%5==0) continue;
                    auto bytes=encode_owner_inputs(*packet);
                    queue.push_back({p,to,time+delay+sends%4,bytes});
                    if(sends%7==0) queue.push_front({p,to,time+delay+sends%4+1,bytes});
                }
            }
        }
        if(std::all_of(done.begin(),done.begin()+players,[](bool value){return value;})) break;
    }
    assert(time<6000);
    World reference;
    for(unsigned f=0;f<reference.end;++f) {
        FrameInputs inputs{};
        for(unsigned p=0;p<players;++p) if(f>=input_delay) inputs[p]=sample(f,p);
        TickOutput out;
        assert(reference.tick(f,inputs,out,error) && reference.commit(17,f,out.effects,out.scene_boundary,error));
    }
    unsigned rollbacks=0;
    for(unsigned p=0;p<players;++p) {
        assert(worlds[p]->state==reference.state && worlds[p]->effects==reference.effects);
        assert(peers[p]->statistics().confirmed_frames==reference.end);
        rollbacks+=peers[p]->statistics().rollbacks;
    }
    // An agreed local delay can absorb a short/jitter-free link entirely.
    // Such a run should not be forced to perform unnecessary corrections.
    if(delay>input_delay) assert(rollbacks>0);
    std::cout<<"timeline peers="<<players<<" delay="<<delay<<" window="<<window<<" local-delay="<<input_delay
             <<" rollbacks="<<rollbacks<<" parked="<<parked<<" exact=1\n";
}
}
int main() {
    confirmation_without_another_sample(); corrected_boundary();
    for(unsigned players:{2,3,4}) for(unsigned delay:{0,3,12,30}) for(unsigned input_delay:{0,1,5})
        distributed(players,delay,delay<12?6:20,input_delay);
    std::cout<<"Experimental distributed timeline checks passed; no full-game admission.\n";
}
