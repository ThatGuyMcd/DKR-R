#include "probe_audio.h"
#include "recomp.h"
#include "netplay/experimental_rollback.hpp"
#include "netplay/replay_qualification.hpp"
#include "netplay/runtime_state.hpp"
#include "netplay/experimental_pcm.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#if DKR_PROBE_HAS_AUDIO
namespace {
using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
uint32_t word(std::span<const uint8_t> bytes,unsigned offset) {
    uint32_t value=0; std::memcpy(&value,bytes.data()+offset,4); return value;
}
void append(std::vector<uint8_t>& out,uint64_t value,unsigned count) {
    for(unsigned b=0;b<count;++b) out.push_back(uint8_t(value>>(b*8)));
}
uint64_t decode(std::span<const uint8_t> in,unsigned offset,unsigned count) {
    uint64_t value=0; for(unsigned b=0;b<count;++b) value|=uint64_t(in[offset+b])<<(b*8); return value;
}
struct AudioState {
    uint32_t rate=0,remainder=0,next_frame=0;
    dkr_probe_native_state native{};
    std::array<uint8_t,4096> dmem{};
    std::array<dkr_probe_audio_guard,8> guards{};
};
static_assert(sizeof(AudioState)==12+sizeof(dkr_probe_native_state)+4096+8*sizeof(dkr_probe_audio_guard));
class RetailAudio final : public Simulation {
    RuntimeState state_;
    recomp_context context_{};
    std::vector<uint8_t> ram_=std::vector<uint8_t>(kRollbackMemoryBytes);
    AudioState audio_{};
    unsigned rate_=0;
    dkr_probe_native_state initial_native_{};
    std::vector<std::vector<uint8_t>> confirmed_;
public:
    explicit RetailAudio(std::span<const uint8_t> fixture) {
        assert(state_.register_context(&context_)); unsigned revision=0;
        assert(state_.restore(ram_.data(),fixture,revision) && revision==DKR_PROBE_REVISION);
        const unsigned globals=word(ram_,DKR_PROBE_REVISION==77 ? 0xE3780:0xE3D10);
        assert(globals>=0x80000000U && globals<0x80FFFFB4U);
        rate_=word(ram_,globals-0x80000000U+0x44);
        assert(rate_>=8000 && rate_<=48000);
        // Private scratch is separate from all retail <=8MiB heaps. No live
        // launcher/patch thread/stack exists in this standalone process.
        context_={}; repair_float_register_pointer(context_);
        context_.r29=std::int32_t(0x80FF0000U);
        dkr_probe_offline_services(1); dkr_probe_canonical_presentation(1);
        initial_native_=audio_.native=dkr_probe_native_capture();
        audio_.rate=rate_; dkr_probe_audio_rsp_initialize();
        dkr_probe_audio_dmem_capture(audio_.dmem.data());
        dkr_probe_audio_guards_initialize(); dkr_probe_audio_guards_capture(audio_.guards.data());
    }
    SimulationContract contract() const override {
        // Complete ONLY for the isolated audio CPU/DSP closure, not gameplay.
        return {0x415544494F525350ULL+DKR_PROBE_REVISION+256,state_.snapshot_size()+sizeof(audio_),
                kRequiredStateDomains,true,true,true};
    }
    bool capture(std::span<uint8_t> out,std::string&) override {
        if(out.size()!=contract().state_bytes || !state_.capture(ram_.data(),0,out.first(state_.snapshot_size()))) return false;
        std::memcpy(out.data()+state_.snapshot_size(),&audio_,sizeof(audio_)); return true;
    }
    bool restore(std::span<const uint8_t> in,std::string&) override {
        if(in.size()!=contract().state_bytes) return false;
        AudioState staged{}; std::memcpy(&staged,in.data()+state_.snapshot_size(),sizeof(staged));
        if(staged.rate!=rate_ || staged.remainder>=480 || staged.next_frame>UINT32_MAX-1 ||
           staged.next_frame<confirmed_.size() ||
           std::memcmp(&staged.native,&initial_native_,sizeof(initial_native_))) return false;
        for(unsigned i=0;i<staged.guards.size();++i) {
            const auto& guard=staged.guards[i];
            if(guard.immediate_events>256 || guard.logged_empty>1 || guard.logged_corrupt>1 ||
               (guard.queue && ((guard.queue&3) || guard.queue<0x80000000U || guard.queue>0x80FFFFECU))) return false;
            if(!guard.queue && (guard.immediate_events || guard.logged_empty || guard.logged_corrupt)) return false;
            for(unsigned j=0;j<i;++j) if(guard.queue && guard.queue==staged.guards[j].queue) return false;
        }
        unsigned frame=0;
        if(!state_.restore(ram_.data(),in.first(state_.snapshot_size()),frame)) return false;
        audio_=staged; return true;
    }
    bool tick(uint32_t frame,const FrameInputs& inputs,TickOutput& out,std::string& error) override {
        if(frame!=audio_.next_frame || frame==UINT32_MAX) return false;
        // Rational sample cadence, 30 logical ticks/sec, 16-sample RSP blocks.
        // No wall-clock DAC fullness/native VI thread controls game replay.
        const unsigned total=audio_.remainder+rate_,samples=(total/480)*16;
        dkr_probe_native_restore(audio_.native); dkr_probe_canonical_presentation(1);
        dkr_probe_audio_configure(1); dkr_probe_audio_dmem_restore(audio_.dmem.data());
        dkr_probe_audio_guards_restore(audio_.guards.data());
        dkr_probe_audio_arguments(samples,inputs[1].buttons!=0);
        const auto result=dkr_probe_run(dkr_probe_audio_tick,ram_.data(),ram_.size(),&context_,5000000);
        dkr_probe_audio_configure(0); audio_.native=dkr_probe_native_capture();
        dkr_probe_audio_dmem_capture(audio_.dmem.data());
        dkr_probe_audio_guards_capture(audio_.guards.data());
        if(!result.completed || std::memcmp(&audio_.native,&initial_native_,sizeof(initial_native_))) {
            error=result.blocked ? result.blocked:"unbalanced private audio native state";
            std::cerr<<"Retail audio fenced: "<<error<<" address="<<std::hex<<result.bad_address<<std::dec<<'\n';
            for(unsigned s=0;s<result.stack_depth;++s) std::cerr<<"  "<<result.stack[s]<<'\n';
            std::cerr<<"  callback="<<std::hex<<context_.r25<<" a0="<<context_.r4<<" a1="<<context_.r5<<std::dec<<'\n';
            return false;
        }
        audio_.remainder=total%480; ++audio_.next_frame;
        out={}; out.effects={'D','K','P','C'};
        append(out.effects,102,8); append(out.effects,frame,4); append(out.effects,rate_,4); append(out.effects,samples,4);
        const unsigned offset=DKR_PROBE_AUDIO_OUTPUT-0x80000000U;
        const auto pcm_start=out.effects.size();out.effects.resize(pcm_start+samples*4);
        if(!pcm_s16le_from_guest(ram_,offset,std::span(out.effects).subspan(pcm_start)))return false;
        return true;
    }
    bool commit(uint64_t epoch,uint32_t frame,std::span<const uint8_t> effects,bool boundary,std::string&) override {
        static constexpr std::array<uint8_t,4> magic={'D','K','P','C'};
        if(epoch!=102 || frame!=confirmed_.size() || frame>=audio_.next_frame || boundary || effects.size()<24 ||
           !std::equal(magic.begin(),magic.end(),effects.begin()) || decode(effects,4,8)!=epoch ||
           decode(effects,12,4)!=frame || decode(effects,16,4)!=rate_) return false;
        const auto samples=decode(effects,20,4);
        const auto expected=(uint64_t(frame+1)*rate_/480-uint64_t(frame)*rate_/480)*16;
        if(!samples || samples!=expected || samples>2048 || samples%16 || effects.size()!=24+samples*4) return false;
        // Private recording sink, never SDL output. Exactly-once driver
        // confirmation is checked; no PCM is emitted during tick/restore.
        confirmed_.emplace_back(effects.begin(),effects.end()); return true;
    }
    const auto& confirmed() const { return confirmed_; }
    unsigned rate() const { return rate_; }
};
FrameInputs audio_sequence(unsigned frame) {
    FrameInputs input{}; input[1].buttons=frame%4==0 ? 0x8000:0; return input;
}
}
#endif
bool dkr_probe_retail_audio_check(std::span<const uint8_t> fixture) {
#if DKR_PROBE_HAS_AUDIO
    std::string error; RetailAudio qualification(fixture); std::array<FrameInputs,12> inputs{};
    for(unsigned f=0;f<inputs.size();++f) inputs[f]=audio_sequence(f);
    const auto qualified=qualify_replay(qualification,inputs);
    if(!qualified.passed() || qualified.replayed_ticks!=78 || !qualification.confirmed().empty()) {
        std::cerr<<"Retail audio replay qualification failed: "<<qualified.detail<<'\n'; return false;
    }
    constexpr unsigned frames=150;
    RetailAudio reference(fixture),corrected(fixture); Driver driver(corrected);
    if(!driver.start({102,2,6},error)) return false;
    for(unsigned f=0;f<frames;++f) {
        TickOutput out;
        if(!reference.tick(f,audio_sequence(f),out,error) || !reference.commit(102,f,out.effects,false,error)) return false;
    }
    for(unsigned time=0;time<frames+4;++time) {
        if(time<frames && driver.receive(102,0,time,{})!=InputResult::Accepted) return false;
        if(time>=4 && driver.receive(102,1,time-4,audio_sequence(time-4)[1])!=InputResult::Accepted) return false;
        for(unsigned budget=0;budget<6;++budget) {
            const auto step=driver.step(driver.statistics().next_frame<(std::min)(time+1,frames));
            if(step==Step::Failed) { std::cerr<<driver.error()<<'\n'; return false; }
            if(step!=Step::Replayed) break;
        }
    }
    if(driver.statistics().confirmed_frames!=frames || !driver.statistics().rollbacks ||
       corrected.confirmed()!=reference.confirmed()) return false;
    std::vector<uint8_t> expected(reference.contract().state_bytes),actual(expected.size());
    if(!reference.capture(expected,error) || !corrected.capture(actual,error) || actual!=expected) return false;
    uint64_t samples=0; bool audible=false;
    for(const auto& pcm:corrected.confirmed()) {
        samples+=decode(pcm,20,4);
        audible=audible || std::any_of(pcm.begin()+24,pcm.end(),[](uint8_t byte){return byte!=0;});
    }
    if(!audible || samples!=(uint64_t(frames)*corrected.rate()/480)*16) return false;
    // Invalid participants must refuse BEFORE changing RAM or output delivery.
    const auto audio_offset=actual.size()-sizeof(AudioState);
    for(const auto offset:{size_t{0},audio_offset+offsetof(AudioState,rate),
                           audio_offset+offsetof(AudioState,remainder)}) {
        auto corrupt=actual;
        if(offset==audio_offset+offsetof(AudioState,remainder)) {
            uint32_t invalid=480; std::memcpy(corrupt.data()+offset,&invalid,4);
        } else corrupt[offset]^=1;
        if(corrected.restore(corrupt,error)) return false;
        std::vector<uint8_t> after(actual.size());
        if(!corrected.capture(after,error) || after!=actual || corrected.confirmed()!=reference.confirmed()) return false;
    }
    auto behind=actual;
    uint32_t old_frame=frames-1; std::memcpy(behind.data()+audio_offset+offsetof(AudioState,next_frame),&old_frame,4);
    if(corrected.restore(behind,error) || corrected.commit(102,frames-1,corrected.confirmed().back(),false,error) ||
       corrected.confirmed()!=reference.confirmed()) return false;
    std::cout<<"REAL retail audio CPU/DSP: frames=150 suffix_replays=78 corrected_pcm=1 audible=1 samples="
             <<samples<<" rate="<<corrected.rate()<<" confirmed_exactly_once=1; no live audio worker/admission.\n";
    return true;
#else
    (void)fixture;
    std::cerr<<"Generate this private payload with --audio-services first.\n";
    return false;
#endif
}
