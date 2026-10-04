#include "probe_input.h"
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
static_assert(sizeof(dkr_probe_input_state)==32);
void append(std::vector<uint8_t>& out,unsigned value) {
    for(unsigned b=0;b<4;++b) out.push_back(uint8_t(value>>(b*8)));
}
unsigned word(std::span<const uint8_t> in,unsigned offset) {
    unsigned value=0; for(unsigned b=0;b<4;++b) value|=unsigned(in[offset+b])<<(b*8); return value;
}
class RetailInput final : public Simulation {
    RuntimeState world_;
    recomp_context context_{};
    std::vector<uint8_t> ram_=std::vector<uint8_t>(kRollbackMemoryBytes);
    Eeprom save_;
    dkr_probe_native_state native_{},initial_native_{};
    dkr_probe_input_state input_{};
    std::vector<std::vector<uint8_t>> commits_;
public:
    explicit RetailInput(std::span<const uint8_t> fixture) {
        assert(world_.register_context(&context_)); unsigned revision=0;
        assert(world_.restore(ram_.data(),fixture,revision) && revision==DKR_PROBE_REVISION);
        std::array<uint8_t,512> blank; blank.fill(255); assert(save_.start(104,blank));
        dkr_probe_offline_services(1); initial_native_=native_=dkr_probe_native_capture();
        dkr_probe_input_initialize(); input_=dkr_probe_input_capture();
    }
    ~RetailInput() { dkr_probe_input_configure(0); dkr_probe_bind_eeprom(nullptr); }
    SimulationContract contract() const override {
        return {0x494E5055544F574EULL,world_.snapshot_size()+sizeof(native_)+sizeof(input_)+Eeprom::kCheckpointBytes,
            kRequiredStateDomains,true,true,true}; // ONLY this private retail input/save closure.
    }
    bool valid(const dkr_probe_input_state& s) const {
        return s.requested==1 && !s.ready && !s.delivered && s.motors<=15;
    }
    bool capture(std::span<uint8_t> out,std::string&) override {
        if(out.size()!=contract().state_bytes || !valid(input_) ||
           !save_.capture(out.last(Eeprom::kCheckpointBytes)) ||
           !world_.capture(ram_.data(),0,out.first(world_.snapshot_size()))) return false;
        memcpy(out.data()+world_.snapshot_size(),&native_,sizeof(native_));
        memcpy(out.data()+world_.snapshot_size()+sizeof(native_),&input_,sizeof(input_)); return true;
    }
    bool restore(std::span<const uint8_t> in,std::string&) override {
        if(in.size()!=contract().state_bytes) return false;
        dkr_probe_native_state n{}; dkr_probe_input_state i{};
        memcpy(&n,in.data()+world_.snapshot_size(),sizeof(n));
        memcpy(&i,in.data()+world_.snapshot_size()+sizeof(n),sizeof(i));
        if(memcmp(&n,&initial_native_,sizeof(n)) || !valid(i)) return false;
        auto save=save_;
        if(!save.restore(in.last(Eeprom::kCheckpointBytes))) return false;
        unsigned tag=0; if(!world_.restore(ram_.data(),in.first(world_.snapshot_size()),tag)) return false;
        save_=std::move(save); native_=n; input_=i; return true;
    }
    bool tick(uint32_t frame,const FrameInputs& inputs,TickOutput& out,std::string& error) override {
        if(!save_.begin_frame(frame)) return false;
        std::array<dkr_probe_pad,4> pads{};
        for(unsigned p=0;p<4;++p) pads[p]={inputs[p].buttons,inputs[p].stick_x,inputs[p].stick_y};
        // Unit policy exercises the RETAIL save flag path, not an emulated
        // file-select/adventure sequence. Every slot is read; writes vary with
        // immutable remote input and logical frame and are therefore replayable.
        const unsigned flags=8 | ((inputs[1].buttons&0x8000) ? 64|((frame%3)<<10):0);
        dkr_probe_native_restore(native_); dkr_probe_reset_poll_configure(1);
        dkr_probe_bind_eeprom(&save_); dkr_probe_input_restore(input_);
        dkr_probe_input_configure(1); dkr_probe_input_test_arguments(pads.data(),flags);
        const auto result=dkr_probe_run(dkr_probe_input_tick,ram_.data(),ram_.size(),&context_,5000000);
        input_=dkr_probe_input_capture(); native_=dkr_probe_native_capture();
        dkr_probe_input_configure(0); dkr_probe_bind_eeprom(nullptr); dkr_probe_reset_poll_configure(0);
        if(!result.completed) {
            error=result.blocked ? result.blocked:"retail input failed";
            std::cerr<<"Retail input fenced: "<<error<<" addr="<<std::hex<<result.bad_address<<std::dec<<'\n';
            for(unsigned i=0;i<result.stack_depth;++i) std::cerr<<"  "<<result.stack[i]<<'\n'; return false;
        }
        if(!valid(input_) || memcmp(&native_,&initial_native_,sizeof(native_))) return false;
        std::vector<uint8_t> save; if(!save_.end_frame(save)) return false;
        std::array<dkr_probe_motor_event,128> motors{}; const auto count=dkr_probe_motor_events(motors.data());
        out={}; out.effects={'D','K','I','J'}; append(out.effects,frame); append(out.effects,save.size()); append(out.effects,count);
        out.effects.insert(out.effects.end(),save.begin(),save.end());
        for(unsigned m=0;m<count;++m) { append(out.effects,motors[m].channel); append(out.effects,motors[m].enabled); }
        return true;
    }
    bool commit(uint64_t epoch,uint32_t frame,std::span<const uint8_t> effects,bool boundary,std::string&) override {
        if(boundary || frame!=commits_.size() || effects.size()<16 || !std::equal(effects.begin(),effects.begin()+4,"DKIJ") || word(effects,4)!=frame) return false;
        const auto bytes=word(effects,8),motors=word(effects,12);
        if(bytes>65536 || motors>128 || effects.size()!=16+bytes+motors*8) return false;
        for(unsigned m=0;m<motors;++m) if(word(effects,16+bytes+m*8)>3 || word(effects,20+bytes+m*8)>1) return false;
        auto staged=save_; if(!staged.commit(epoch,frame,effects.subspan(16,bytes))) return false;
        commits_.emplace_back(effects.begin(),effects.end()); save_=std::move(staged); return true;
    }
    const auto& commits() const { return commits_; }
    auto save() const { return save_.confirmed_image(); }
};
FrameInputs sequence(unsigned frame) {
    FrameInputs inputs{};
    for(unsigned p=0;p<4;++p) inputs[p]={uint16_t((frame%3 ? 0x8000:0)|(frame%7==1 ? 0x2000:0)),int8_t((frame+p)%5*23-46),int8_t(frame%3*30-30)};
    return inputs;
}
const char* import_name=nullptr;
void import_entry(uint8_t* ram,recomp_context* ctx) { dkr_probe_native(import_name,ram,ctx); }
bool service_checks() {
    std::vector<uint8_t> ram(kRollbackMemoryBytes); recomp_context ctx{}; repair_float_register_pointer(ctx);
    const unsigned q=DKR_PROBE_REVISION==77 ? 0x1210E0:0x121660;
    const auto set=[&](unsigned offset,unsigned value){memcpy(ram.data()+offset,&value,4);};
    const auto get=[&](unsigned offset){unsigned value=0;memcpy(&value,ram.data()+offset,4);return value;};
    set(q+16,1);set(q+20,0x80000000U+q+24);
    dkr_probe_offline_services(1); dkr_probe_input_initialize(); dkr_probe_input_configure(1);
    const auto call=[&](const char* name){import_name=name;return dkr_probe_run(import_entry,ram.data(),ram.size(),&ctx,10000);};
    // Return values and guest ABI writes, including the native byte-swizzle,
    // must match the reviewed connected-controller/rumble runtime branch.
    ctx.r4=int32_t(0x80000000U+q);ctx.r5=int32_t(0x80FA0000U);ctx.r6=2;
    assert(call("osMotorInit_recomp").completed && ctx.r2==0);
    assert(get(0xFA0000)==8 && get(0xFA0004)==0x80000000U+q && get(0xFA0008)==2 && ram[0xFA0066]==255);
    ctx.r4=ctx.r5;
    assert(call("osMotorStart_recomp").completed && ctx.r2==0 && dkr_probe_input_capture().motors==4);
    const auto snap=dkr_probe_input_capture();
    assert(call("osMotorStop_recomp").completed && ctx.r2==0 && !dkr_probe_input_capture().motors);
    dkr_probe_input_restore(snap);
    assert(call("osMotorStop_recomp").completed && ctx.r2==0 && !dkr_probe_input_capture().motors);
    std::array<dkr_probe_motor_event,128> events{};
    assert(dkr_probe_motor_events(events.data())==1 && events[0].channel==2 && !events[0].enabled);
    for(unsigned bad=0;bad<6;++bad) {
        ctx={};repair_float_register_pointer(ctx);ctx.r4=int32_t(0x80FA0000U);ctx.r5=1;ctx.r6=0;
        import_name="__osMotorAccess_recomp";
        if(bad==0) ctx.r5=2;
        if(bad==1) set(0xFA0008,4);
        if(bad==2) ctx.r4=int32_t(0x81000000U);
        if(bad==3) {import_name="osMotorInit_recomp";ctx.r5=int32_t(0x80FA0000U);ctx.r6=4;}
        if(bad==4) {import_name="osContGetReadData_recomp";ctx.r4=int32_t(0x80FA0000U);}
        if(bad==5) {import_name="osRecvMesg_recomp";ctx.r4=int32_t(0x80000000U+q);ctx.r5=0;}
        const auto before=ram;const auto owned=dkr_probe_input_capture();
        assert(!dkr_probe_run(import_entry,ram.data(),ram.size(),&ctx,10000).completed && ram==before);
        const auto after=dkr_probe_input_capture(); assert(!memcmp(&owned,&after,sizeof(owned)));
        set(0xFA0008,2);
    }
    // The 129th output is refused before a motor bit or guest byte changes.
    dkr_probe_input_restore(snap);ctx.r4=int32_t(0x80FA0000U);
    for(unsigned i=0;i<128;++i) assert(call("osMotorStart_recomp").completed && ctx.r2==0);
    const auto owned=dkr_probe_input_capture(),before=owned;
    assert(!call("osMotorStop_recomp").completed);
    const auto after=dkr_probe_input_capture();assert(!memcmp(&before,&after,sizeof(before)));
    assert(dkr_probe_motor_events(events.data())==128);
    dkr_probe_input_configure(0);
    assert(!call("osMotorStart_recomp").completed); // No implicit live/native fallback.
    return true;
}
}
bool dkr_probe_retail_input_check(std::span<const uint8_t> fixture) {
    if(!service_checks()) return false;
    RetailInput qualification(fixture); std::array<FrameInputs,12> inputs{};
    for(unsigned f=0;f<12;++f) inputs[f]=sequence(f);
    const auto qualified=qualify_replay(qualification,inputs);
    if(!qualified.passed() || qualified.replayed_ticks!=78 || !qualification.commits().empty()) {
        std::cerr<<"Retail input qualification failed: "<<qualified.detail<<'\n'; return false;
    }
    RetailInput reference(fixture),corrected(fixture); Driver driver(corrected); std::string error;
    if(!driver.start({104,4,6},error)) return false;
    constexpr unsigned frames=150;
    for(unsigned f=0;f<frames;++f) { TickOutput out; if(!reference.tick(f,sequence(f),out,error) || !reference.commit(104,f,out.effects,false,error)) return false; }
    for(unsigned time=0;time<frames+4;++time) {
        if(time<frames && driver.receive(104,0,time,sequence(time)[0])!=InputResult::Accepted) return false;
        if(time>=4) for(unsigned p=1;p<4;++p) if(driver.receive(104,p,time-4,sequence(time-4)[p])!=InputResult::Accepted) return false;
        for(unsigned budget=0;budget<6;++budget) {
            const auto step=driver.step(driver.statistics().next_frame<(std::min)(time+1,frames));
            if(step==Step::Failed) { std::cerr<<driver.error()<<'\n'; return false; }
            if(step!=Step::Replayed) break;
        }
    }
    std::vector<uint8_t> expected(reference.contract().state_bytes),actual(expected.size());
    if(driver.statistics().confirmed_frames!=frames || !driver.statistics().rollbacks || !reference.capture(expected,error) ||
       !corrected.capture(actual,error) || expected!=actual || reference.commits()!=corrected.commits() ||
       !std::equal(reference.save().begin(),reference.save().end(),corrected.save().begin())) return false;
    const auto input_offset=actual.size()-Eeprom::kCheckpointBytes-sizeof(dkr_probe_input_state);
    for(auto offset:{size_t{0},input_offset+offsetof(dkr_probe_input_state,ready),input_offset+offsetof(dkr_probe_input_state,motors),actual.size()-Eeprom::kCheckpointBytes}) {
        auto bad=actual; bad[offset]^=128;
        if(corrected.restore(bad,error)) return false;
        std::vector<uint8_t> after(actual.size()); if(!corrected.capture(after,error) || after!=actual) return false;
    }
    if(corrected.commit(104,frames-1,corrected.commits().back(),false,error)) return false;
    unsigned motor_events=0;
    for(const auto& journal:corrected.commits()) motor_events+=word(journal,12);
    if(!motor_events) return false;
    std::cout<<"REAL retail input/save/rumble: frames="<<frames<<" suffix_replays=78 players=4 corrected_state=1 motor_intents="<<motor_events<<" confirmed_exactly_once=1; no live SI/device/Pak ownership.\n";
    return true;
}
