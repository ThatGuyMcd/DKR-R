#include "netplay/experimental_timeline.hpp"
#include "online_stability/live_delay.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

namespace ex=dkr::runtime::netplay::experimental;
namespace stability=dkr::online_stability;
namespace {
struct State {std::uint32_t ticks=0,edges=0,last=0,checksum=0;};
class World final:public ex::Simulation {
public:
    State state{},committed{};
    ex::SimulationContract contract()const override{return {412,sizeof(State),ex::kRequiredStateDomains,true,true,true};}
    bool capture(std::span<std::uint8_t> out,std::string&)override{std::memcpy(out.data(),&state,sizeof(state));return true;}
    bool restore(std::span<const std::uint8_t> in,std::string&)override{std::memcpy(&state,in.data(),sizeof(state));return true;}
    bool tick(std::uint32_t,const dkr::runtime::netplay::FrameInputs& input,ex::TickOutput& output,std::string&)override {
        ++state.ticks;const auto buttons=input[0].buttons;
        if((buttons&0x8000)&&!(state.last&0x8000))++state.edges;
        state.last=buttons;state.checksum+=buttons+input[1].buttons;
        output.effects.resize(sizeof(state));std::memcpy(output.effects.data(),&state,sizeof(state));return true;
    }
    bool commit(std::uint64_t,std::uint32_t,std::span<const std::uint8_t> effect,bool,std::string&)override {
        assert(effect.size()==sizeof(committed));std::memcpy(&committed,effect.data(),sizeof(committed));return true;
    }
};
}
int main() {
    World world;ex::Timeline timeline(world);std::string error;
    assert(timeline.start({{91,2,2,1024*1024},0,4},error));
    auto bridge=std::make_shared<stability::Control>();bridge->configure({8,1,1,true,true,true,{}});
    stability::LiveDelay delay(bridge,4,2);delay.local_owner(0);
    unsigned authored=4,transmitted=0,remote=0,physical_edges=0,previous=0,minimum=4;
    const auto start=stability::Clock::time_point(std::chrono::seconds(100));
    for(unsigned tick=0;tick<1800;++tick) {
        const auto now=start+std::chrono::nanoseconds(1000000000ULL*tick/30);
        delay.pulse(91,1,timeline.frontier(),timeline.correcting(),false,now);
        const std::uint16_t buttons=(tick/11)%2?0x8000:0;
        if(buttons&&!previous)++physical_edges;previous=buttons;
        assert(delay.capture({buttons,0,0},now));
        stability::DelayScope scope(delay);
        assert(stability::sample_lead(99)==delay.lead());
        for(unsigned fill=delay.take_fill();fill&&timeline.needs_local_input(true);--fill) {
            const auto held=delay.held();assert(timeline.sample_local({held.buttons,held.x,held.y},true)==ex::OwnerInputResult::Accepted);++authored;
        }
        if(timeline.needs_local_input(true)) {
            const auto sample=delay.take();const auto result=timeline.sample_local({sample.buttons,sample.x,sample.y},true);
            if(result!=ex::OwnerInputResult::Accepted)std::cerr<<"sample rejected tick="<<tick<<" frontier="<<timeline.frontier()<<" authored="<<authored<<" result="<<unsigned(result)<<" error="<<timeline.error()<<'\n';
            assert(result==ex::OwnerInputResult::Accepted);++authored;
        }
        // Authenticated remote input, with valid receipts for every immutable
        // local sample. Future coverage keeps this test about sample scheduling.
        const auto sent=timeline.packet_for(0,1);
        if(sent&&sent->count)transmitted=std::max(transmitted,sent->first_frame+sent->count);
        assert(transmitted==authored);
        while(remote<=timeline.frontier()+10) {
            ex::OwnerInputPacket packet;packet.epoch=91;packet.players=2;packet.owner=1;packet.count=1;packet.first_frame=remote++;
            packet.received_next[0]=authored;packet.inputs[0]={};
            const auto result=timeline.receive_authenticated(1,ex::encode_owner_inputs(packet));
            assert(result==ex::OwnerInputResult::Accepted||result==ex::OwnerInputResult::Duplicate);
        }
        assert(timeline.service_inputs());
        const auto result=timeline.step(true);assert(result==ex::TimelineStep::Advanced);
        minimum=std::min(minimum,delay.lead());
    }
    assert(minimum==0&&delay.lead()==0);
    assert(authored-timeline.frontier()==0); // Fails if the compiled Timeline stays frozen at launch delay.
    assert(world.committed.edges==physical_edges);
    assert(stability::sample_lead(99)==99); // Owner-local binding was retired.
    std::cout<<"Actual owned Timeline: 1800 confirmed ticks, delay 4 -> 0, "<<physical_edges<<" button edges preserved\n";
}
