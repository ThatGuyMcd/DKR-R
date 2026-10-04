#include "probe_save.h"
#include "recomp.h"
#include "netplay/experimental_eeprom.hpp"
#include "netplay/experimental_rollback.hpp"
#include "netplay/replay_qualification.hpp"
#include "netplay/runtime_state.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
class RetailSave final : public Simulation {
    RuntimeState state_;
    recomp_context context_{};
    std::vector<uint8_t> ram_=std::vector<uint8_t>(kRollbackMemoryBytes);
    Eeprom save_;
    dkr_probe_native_state native_{};
    unsigned confirmations_=0;
public:
    explicit RetailSave(std::span<const uint8_t> fixture) {
        assert(state_.register_context(&context_)); unsigned revision=0;
        assert(state_.restore(ram_.data(),fixture,revision) && revision==DKR_PROBE_REVISION);
        std::array<uint8_t,512> blank; blank.fill(0xFF);
        // A raw EEPROM with uninitialized slots; the ACTUAL retail reader
        // recognizes it as fresh and repairs it through journalled writes.
        assert(save_.start(101,blank));
        dkr_probe_offline_services(1); native_=dkr_probe_native_capture();
    }
    ~RetailSave() { dkr_probe_bind_eeprom(nullptr); }
    SimulationContract contract() const override {
        // Only this isolated retail SAVE closure, not full DKR gameplay.
        return {0x5341564552455431ULL,state_.snapshot_size()+sizeof(native_)+Eeprom::kCheckpointBytes,
            kRequiredStateDomains,true,true,true};
    }
    bool capture(std::span<uint8_t> out,std::string&) override {
        if(out.size()!=contract().state_bytes || !save_.capture(out.last(Eeprom::kCheckpointBytes)) ||
           !state_.capture(ram_.data(),0,out.first(state_.snapshot_size()))) return false;
        std::memcpy(out.data()+state_.snapshot_size(),&native_,sizeof(native_)); return true;
    }
    bool restore(std::span<const uint8_t> in,std::string&) override {
        if(in.size()!=contract().state_bytes) return false;
        dkr_probe_native_state next{}; std::memcpy(&next,in.data()+state_.snapshot_size(),sizeof(next));
        if(next.vehicle_audio_scope || next.nature_audio_scope || next.requested_table!=UINT32_MAX ||
           next.load_section!=UINT32_MAX || next.load_destination || next.load_offset || next.load_size ||
           next.clock_enabled || next.clock_base_hi || next.clock_base_lo || next.clock_count_hi ||
           next.clock_count_lo || next.clock_offset_hi || next.clock_offset_lo || next.pending_scene_site) return false;
        // Stage native save restoration BEFORE guest RAM. An invalid or
        // behind-confirmation save cursor cannot partially rewind the world.
        auto staged=save_;
        if(!staged.restore(in.last(Eeprom::kCheckpointBytes))) return false;
        unsigned tag=0;
        if(!state_.restore(ram_.data(),in.first(state_.snapshot_size()),tag)) return false;
        save_=std::move(staged); native_=next; return true;
    }
    bool tick(uint32_t frame,const FrameInputs& inputs,TickOutput& out,std::string& error) override {
        if(!save_.begin_frame(frame)) return false;
        dkr_probe_native_restore(native_); dkr_probe_bind_eeprom(&save_);
        dkr_probe_reset_poll_configure(1); dkr_probe_effects_begin();
        dkr_probe_save_arguments(frame%3,inputs[1].buttons,inputs[1].buttons!=0);
        const auto result=dkr_probe_run(dkr_probe_save_tick,ram_.data(),ram_.size(),&context_,5000000);
        dkr_probe_bind_eeprom(nullptr); dkr_probe_reset_poll_configure(0);
        native_=dkr_probe_native_capture();
        if(!result.completed) {
            error=result.blocked ? result.blocked:"retail save failed";
            std::cerr<<"Retail save fenced: "<<error<<" address="<<std::hex<<result.bad_address<<std::dec<<'\n';
            for(unsigned i=0;i<result.stack_depth;++i) std::cerr<<"  "<<result.stack[i]<<'\n';
            return false;
        }
        out={}; return save_.end_frame(out.effects);
    }
    bool commit(uint64_t epoch,uint32_t frame,std::span<const uint8_t> effects,bool boundary,std::string&) override {
        if(boundary || !save_.commit(epoch,frame,effects)) return false;
        ++confirmations_; return true;
    }
    auto confirmed() const { return save_.confirmed_image(); }
    unsigned confirmations() const { return confirmations_; }
};
FrameInputs sequence(unsigned frame) {
    FrameInputs input{};
    input[1].buttons=frame%4==0 ? 0:static_cast<uint16_t>(0x1200+frame*13);
    return input;
}
}
bool dkr_probe_retail_save_check(std::span<const uint8_t> fixture) {
    std::string error;
    RetailSave qualification(fixture); std::array<FrameInputs,12> inputs{};
    for(unsigned f=0;f<inputs.size();++f) inputs[f]=sequence(f);
    const auto result=qualify_replay(qualification,inputs);
    if(!result.passed() || result.replayed_ticks!=78 || qualification.confirmations()) {
        std::cerr<<"Retail save replay qualification failed: "<<result.detail<<'\n'; return false;
    }
    RetailSave reference(fixture),corrected(fixture); Driver driver(corrected);
    if(!driver.start({101,2,6},error)) return false;
    constexpr unsigned frames=150;
    for(unsigned f=0;f<frames;++f) {
        TickOutput out;
        if(!reference.tick(f,sequence(f),out,error) || !reference.commit(101,f,out.effects,false,error)) return false;
    }
    for(unsigned time=0;time<frames+4;++time) {
        if(time<frames && driver.receive(101,0,time,{})!=InputResult::Accepted) return false;
        if(time>=4 && driver.receive(101,1,time-4,sequence(time-4)[1])!=InputResult::Accepted) return false;
        for(unsigned budget=0;budget<6;++budget) {
            const auto step=driver.step(driver.statistics().next_frame<(std::min)(time+1,frames));
            if(step==Step::Failed) { std::cerr<<driver.error()<<'\n'; return false; }
            if(step!=Step::Replayed) break;
        }
    }
    if(driver.statistics().confirmed_frames!=frames || !driver.statistics().rollbacks || corrected.confirmations()!=frames) return false;
    std::vector<uint8_t> expected(reference.contract().state_bytes),actual(expected.size());
    if(!reference.capture(expected,error) || !corrected.capture(actual,error) || actual!=expected ||
       !std::equal(reference.confirmed().begin(),reference.confirmed().end(),corrected.confirmed().begin())) return false;
    // Corrupt the SAVE participant, the guest header or a native participant;
    // all must refuse before any world byte or irreversible save changes.
    const auto native_offset=expected.size()-Eeprom::kCheckpointBytes-sizeof(dkr_probe_native_state);
    for(auto offset:{size_t{0},expected.size()-Eeprom::kCheckpointBytes,
                    native_offset+offsetof(dkr_probe_native_state,clock_enabled)}) {
        auto corrupt=actual; corrupt[offset]^=1;
        if(corrected.restore(corrupt,error)) return false;
        std::vector<uint8_t> after(actual.size());
        if(!corrected.capture(after,error) || after!=actual ||
           !std::equal(reference.confirmed().begin(),reference.confirmed().end(),corrected.confirmed().begin())) return false;
    }
    // Independently verify RETAIL checksums of each written Adventure slot.
    // Do not assume that matching peers implies a valid game save format.
    const auto image=corrected.confirmed();
    for(unsigned slot=0;slot<3;++slot) {
        const auto bytes=image.subspan(slot*40,40); unsigned checksum=5;
        for(unsigned b=2;b<40;++b) checksum+=bytes[b];
        if(((unsigned(bytes[0])<<8)|bytes[1])!=checksum) return false;
    }
    std::cout<<"REAL retail Adventure save routines: frames=150 suffix_replays=78 corrected_writes=1 all_three_slots=1 confirmed_exact=1; no disk/Pak/live admission.\n";
    return true;
}
