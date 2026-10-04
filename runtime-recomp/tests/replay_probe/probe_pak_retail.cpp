#include "probe_pak.h"
#include "probe_input.h"
#include "recomp.h"
#include "netplay/experimental_pak.hpp"
#include "netplay/experimental_rollback.hpp"
#include "netplay/replay_qualification.hpp"
#include "netplay/runtime_state.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
namespace {
using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
class RetailPak final : public Simulation {
    RuntimeState state_;recomp_context context_{};std::vector<uint8_t> ram_=std::vector<uint8_t>(kRollbackMemoryBytes);
    Paks paks_;dkr_probe_native_state native_{};dkr_probe_input_state input_{};
    std::vector<std::vector<uint8_t>> commits_;
    unsigned confirmed_motors_=0;
public:
    explicit RetailPak(std::span<const uint8_t> fixture) {
        assert(state_.register_context(&context_));unsigned revision=0;
        assert(state_.restore(ram_.data(),fixture,revision) && revision==DKR_PROBE_REVISION);
        assert(paks_.start(108,15,Paks::blank_images()));
        dkr_probe_offline_services(1);dkr_probe_input_initialize();input_=dkr_probe_input_capture();native_=dkr_probe_native_capture();
    }
    ~RetailPak(){dkr_probe_bind_paks(nullptr);dkr_probe_input_configure(0);}
    SimulationContract contract() const override {return {0x50414B5245544149ULL,state_.snapshot_size()+sizeof(native_)+sizeof(input_)+Paks::kCheckpointBytes,kRequiredStateDomains,true,true,true};}
    bool capture(std::span<uint8_t> out,std::string&) override {
        if(out.size()!=contract().state_bytes || !paks_.capture(out.last(Paks::kCheckpointBytes)) || !state_.capture(ram_.data(),0,out.first(state_.snapshot_size())))return false;
        memcpy(out.data()+state_.snapshot_size(),&native_,sizeof(native_));
        memcpy(out.data()+state_.snapshot_size()+sizeof(native_),&input_,sizeof(input_));return true;
    }
    bool restore(std::span<const uint8_t> in,std::string&) override {
        if(in.size()!=contract().state_bytes)return false;
        dkr_probe_native_state native;dkr_probe_input_state input;
        memcpy(&native,in.data()+state_.snapshot_size(),sizeof(native));memcpy(&input,in.data()+state_.snapshot_size()+sizeof(native),sizeof(input));
        if(native.vehicle_audio_scope || native.nature_audio_scope || native.requested_table!=UINT32_MAX || native.load_section!=UINT32_MAX ||
           native.load_destination || native.load_offset || native.load_size || native.clock_enabled || native.clock_base_hi || native.clock_base_lo ||
           native.clock_count_hi || native.clock_count_lo || native.clock_offset_hi || native.clock_offset_lo || native.pending_scene_site ||
           input.requested!=1 || input.ready || input.delivered || input.motors>15)return false;
        auto staged=paks_;if(!staged.restore(in.last(Paks::kCheckpointBytes)))return false;
        unsigned tag=0;if(!state_.restore(ram_.data(),in.first(state_.snapshot_size()),tag))return false;
        paks_=std::move(staged);native_=native;input_=input;return true;
    }
    bool tick(uint32_t frame,const FrameInputs& inputs,TickOutput& out,std::string& error) override {
        if(!paks_.begin_frame(frame))return false;
        dkr_probe_native_restore(native_);dkr_probe_input_restore(input_);dkr_probe_input_configure(1);dkr_probe_bind_paks(&paks_);
        dkr_probe_retail_pak_arguments(frame,inputs[1].buttons);
        const auto result=dkr_probe_run(dkr_probe_retail_pak_tick,ram_.data(),ram_.size(),&context_,5000000);
        native_=dkr_probe_native_capture();input_=dkr_probe_input_capture();
        std::array<dkr_probe_motor_event,128> motors{};const auto count=dkr_probe_motor_events(motors.data());
        dkr_probe_bind_paks(nullptr);dkr_probe_input_configure(0);
        if(!result.completed) {
            error=result.blocked?result.blocked:"actual retail Pak failure";
            std::cerr<<"Retail Pak fenced: "<<error<<" address="<<std::hex<<result.bad_address<<std::dec<<'\n';
            for(unsigned i=0;i<result.stack_depth;++i)std::cerr<<"  "<<result.stack[i]<<'\n';return false;
        }
        if(input_.requested!=1 || input_.ready || input_.delivered || input_.motors>15) {
            error="retail Pak tick left an unowned input state";
            std::cerr<<error<<" frame="<<frame<<" requested="<<input_.requested<<" ready="<<input_.ready
                     <<" delivered="<<input_.delivered<<" motors="<<input_.motors<<'\n';return false;
        }
        out={};std::vector<uint8_t> pak;
        if(!paks_.end_frame(pak)){error="retail Pak end-frame failed";return false;}
        out.effects={'D','K','P','R'};
        const auto put=[&](unsigned v){for(unsigned b=0;b<4;++b)out.effects.push_back(uint8_t(v>>(8*b)));};
        put(unsigned(pak.size()));put(count);out.effects.insert(out.effects.end(),pak.begin(),pak.end());
        for(unsigned m=0;m<count;++m){put(motors[m].channel);put(motors[m].enabled);}return true;
    }
    bool commit(uint64_t epoch,uint32_t frame,std::span<const uint8_t> effects,bool boundary,std::string&) override {
        if(boundary || frame!=commits_.size() || effects.size()<12 || !std::equal(effects.begin(),effects.begin()+4,"DKPR"))return false;
        const auto read=[&](size_t off){unsigned v=0;for(unsigned b=0;b<4;++b)v|=unsigned(effects[off+b])<<(8*b);return v;};
        const auto bytes=read(4),count=read(8);
        if(bytes>49152 || count>128 || effects.size()!=12+bytes+count*8)return false;
        auto motors=confirmed_motors_;
        for(unsigned m=0;m<count;++m){const auto channel=read(12+bytes+m*8),enabled=read(16+bytes+m*8);
            if(channel>3 || enabled>1)return false;motors=(motors&~(1U<<channel))|(enabled<<channel);
        }
        auto staged=paks_;if(!staged.commit(epoch,frame,effects.subspan(12,bytes)))return false;
        commits_.emplace_back(effects.begin(),effects.end());paks_=std::move(staged);confirmed_motors_=motors;return true;
    }
    const auto& commits() const {return commits_;}
    auto images() const {return paks_.confirmed_images();}
    unsigned motors() const {return confirmed_motors_;}
};
FrameInputs inputs(unsigned frame) {FrameInputs value{};value[1].buttons=uint16_t(frame*11+1);return value;}
}
bool dkr_probe_retail_pak_check(std::span<const uint8_t> fixture) {
    RetailPak qualification(fixture);std::array<FrameInputs,12> sequence;
    for(unsigned f=0;f<12;++f)sequence[f]=inputs(f);
    const auto result=qualify_replay(qualification,sequence);
    if(!result.passed() || result.replayed_ticks!=78 || !qualification.commits().empty()){std::cerr<<"Retail Pak qualification failed: "<<result.detail<<'\n';return false;}
    RetailPak reference(fixture),corrected(fixture);Driver driver(corrected);std::string error;
    if(!driver.start({108,2,6},error)){std::cerr<<"Retail Pak driver start: "<<error<<'\n';return false;}
    for(unsigned f=0;f<150;++f){TickOutput out;if(!reference.tick(f,inputs(f),out,error) || !reference.commit(108,f,out.effects,false,error)) {
        std::cerr<<"Retail Pak reference frame="<<f<<": "<<error<<'\n';return false;
    }}
    for(unsigned time=0;time<154;++time) {
        if(time<150 && driver.receive(108,0,time,inputs(time)[0])!=InputResult::Accepted)return false;
        if(time>=4 && driver.receive(108,1,time-4,inputs(time-4)[1])!=InputResult::Accepted)return false;
        for(unsigned budget=0;driver.statistics().next_frame<std::min(time+1,150U) && budget<20;++budget)if(driver.step()==Step::Failed) {
            std::cerr<<"Retail Pak correction time="<<time<<": "<<driver.error()<<'\n';return false;
        }
    }
    for(unsigned b=0;driver.statistics().confirmed_frames<150 && b<100;++b)if(driver.step(false)==Step::Failed) {
        std::cerr<<"Retail Pak final correction: "<<driver.error()<<'\n';return false;
    }
    std::vector<uint8_t> a(reference.contract().state_bytes),b(a.size());
    if(driver.statistics().confirmed_frames!=150 || !reference.capture(a,error) || !corrected.capture(b,error) || a!=b ||
       reference.commits()!=corrected.commits() || reference.motors()!=corrected.motors() ||
       !std::equal(reference.images().begin(),reference.images().end(),corrected.images().begin())) {
        const auto mismatch=std::mismatch(a.begin(),a.end(),b.begin());
        std::cerr<<"Retail Pak final mismatch: confirmed="<<driver.statistics().confirmed_frames<<" next="<<driver.statistics().next_frame
                 <<" state-byte="<<std::distance(a.begin(),mismatch.first)<<" effects-equal="<<(reference.commits()==corrected.commits())
                 <<" images-equal="<<std::equal(reference.images().begin(),reference.images().end(),corrected.images().begin())<<" error="<<error<<'\n';return false;
    }
    for(size_t offset:{a.size()-Paks::kCheckpointBytes,a.size()-Paks::kCheckpointBytes+32,a.size()-1}) {
        auto bad=a;bad[offset]^=128;if(corrected.restore(bad,error))return false;
        std::vector<uint8_t> after(a.size());if(!corrected.capture(after,error) || after!=a)return false;
    }
    std::cout<<"ACTUAL RETAIL Pak file write/find/size/read: revision="<<DKR_PROBE_REVISION<<" 150 frames across four ports, 78 suffix replays, delayed owner correction, byte-exact RAM/state and confirmed image/journal equality. No physical Pak/filesystem.\n";return true;
}
