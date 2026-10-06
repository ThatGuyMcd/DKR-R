#include "funcs.h"
#include "probe_bridge.h"
#include "probe_driver.hpp"
#include "probe_save.h"
#include "probe_audio.h"
#include "probe_input.h"
#include "probe_pak.h"
#include "probe_magic.h"
#include "netplay/runtime_state.hpp"
#include "netplay/experimental_eeprom.hpp"
#include "rom_revision.hpp"
#define XXH_INLINE_ALL
#include "xxHash/xxhash.h"
#include <cassert>
#include <array>
#include <cfenv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <cstdlib>
#include <limits>
#include "steering_wheel_policy.hpp"

#if DKR_PROBE_HAS_SYMBOL_ISOLATION
extern "C" int dkr_probe_check_stable_link_canaries(void);
extern "C" unsigned dkr_probe_stable_link_calls(void);
#endif
#if DKR_PROBE_HAS_AUDIO_ISOLATION
extern "C" int dkr_probe_audio_link_canaries_begin();
extern "C" int dkr_probe_audio_link_canaries_unchanged();
#endif

namespace {
const char* checked_service=nullptr;
std::array<std::uint8_t,4096> checked_dma_dmem;
std::uint32_t checked_dma_dmem_address=0,checked_dma_dram_address=0,checked_dma_length=0;
int checked_dma_write=0;
void audio_dma_entry(std::uint8_t* ram,recomp_context*) {
    dkr_probe_audio_dma(ram,checked_dma_dmem.data(),checked_dma_dmem_address,
        checked_dma_dram_address,checked_dma_length,checked_dma_write,__FILE__,__LINE__);
}
void audio_dma_services_test() {
    recomp_context context{};dkr::runtime::netplay::repair_float_register_pointer(context);
    std::vector<std::uint8_t> ram(8192);
    for(unsigned i=0;i<ram.size();++i)ram[i]=std::uint8_t(i*37+11);
    for(const unsigned count:{1U,7U,8U,15U,16U,127U,4096U}) {
        for(const unsigned start:{0U,1U,3U,16U}) {
            if(start+count>4096)continue;
            checked_dma_dmem.fill(0xCD);const auto original=ram;
            checked_dma_dmem_address=start;checked_dma_dram_address=4096;
            checked_dma_length=count-1;checked_dma_write=0;
            auto result=dkr_probe_run(audio_dma_entry,ram.data(),ram.size(),&context,100);
            assert(result.completed && result.memory_accesses==1 && ram==original);
            for(unsigned i=0;i<4096;++i) {
                const bool transferred=i>=start&&i<start+count;
                assert(checked_dma_dmem[i^3U]==(transferred?original[(4096+i-start)^3U]:0xCD));
            }
            // A write uses the identical byte lanes and changes only its span.
            checked_dma_write=1;std::fill(ram.begin()+4096,ram.end(),0);
            const auto before=ram;
            result=dkr_probe_run(audio_dma_entry,ram.data(),ram.size(),&context,100);
            assert(result.completed && result.memory_accesses==1);
            for(unsigned i=0;i<ram.size();++i)
                assert(ram[i^3U]==(i>=4096&&i<4096+count?original[i^3U]:before[i^3U]));
            ram=original;
        }
    }
    // The entire transfer must be rejected before *either* destination changes.
    for(unsigned fault=0;fault<6;++fault) {
        checked_dma_dmem_address=0;checked_dma_dram_address=4096;checked_dma_length=7;checked_dma_write=0;
        std::size_t bytes=ram.size();
        if(fault==0)checked_dma_length=UINT32_MAX;
        if(fault==1)checked_dma_dmem_address=4096;
        if(fault==2)checked_dma_dmem_address=4090;
        if(fault==3)checked_dma_dram_address=8192;
        if(fault==4){checked_dma_length=0;bytes=4097;} // Byte lane ^3 would cross RAM's end.
        if(fault==5){checked_dma_write=1;checked_dma_dram_address=8184;checked_dma_length=8;}
        const auto before=ram;const auto before_dmem=checked_dma_dmem;
        const auto result=dkr_probe_run(audio_dma_entry,ram.data(),bytes,&context,100);
        assert(!result.completed && result.blocked && result.source_file && result.source_line);
        assert(ram==before && checked_dma_dmem==before_dmem);
    }
}
void video_start_entry(std::uint8_t* ram,recomp_context*) {dkr_probe_video_start(ram);}
void video_tail_entry(std::uint8_t* ram,recomp_context* context) {
#if DKR_PROBE_HAS_AUTHORED_CPU
    dkr_probe_video_begin_frame(ram);dkr_probe_authored_video_cpu(ram,context);dkr_probe_video_finish_frame();
#else
    (void)ram;(void)context;dkr_probe_block("unaudited-private-video-pipeline");
#endif
}
void owned_video_test() {
#if DKR_PROBE_HAS_AUTHORED_CPU
    using namespace dkr::runtime::netplay;
    std::vector<std::uint8_t> ram(kRollbackMemoryBytes),before;
    recomp_context context{};repair_float_register_pointer(context);context.r29=std::int32_t(0x80FF0000U);
    context.r31=0x12345678;context.r16=71;context.r17=72;context.r18=73;
    const auto base=DKR_PROBE_REVISION==77 ? 0x1262C0:0x126860;
    const auto z=DKR_PROBE_REVISION==77 ? 0xDE770:0xDECF0;
    const auto put=[&](unsigned offset,std::uint32_t value){std::memcpy(ram.data()+offset,&value,4);};
    const auto word=[&](unsigned offset){std::uint32_t value;std::memcpy(&value,ram.data()+offset,4);return value;};
    put(base,0x80400000);put(base+4,0x80425800);put(z,0x80450000);
    put(base+8,1);put(base+16,2);put(base+20,0x80425800);put(base+24,0x80400000);
    put(base+28,0x80450000);put(base+32,0x80450000);
    dkr_probe_offline_services(1);dkr_probe_canonical_presentation(1);dkr_probe_clock_start(100);
    assert(dkr_probe_run(video_start_entry,ram.data(),ram.size(),&context,10000).completed);
    const auto initial=dkr_probe_native_capture();before=ram;
    for(unsigned f=0;f<3;++f) {
        context.r4=0;
        assert(dkr_probe_run(video_tail_entry,ram.data(),ram.size(),&context,10000).completed);
        const auto state=dkr_probe_native_capture();
        assert(state.video_frames==f+1 && state.video_black==(f==0) && word(base+16)==(f ? 0:1));
        assert(context.r2==2 && context.r16==71 && context.r17==72 && context.r18==73 &&
            context.r31==0x12345678 && context.r29==std::uint64_t(std::int64_t(std::int32_t(0x80FF0000U))));
        assert(state.video_framebuffer==(f%2 ? 0x80425800U:0x80400000U));
    }
    const auto expected=ram;const auto ended=dkr_probe_native_capture();
    ram=before;dkr_probe_native_restore(initial);
    for(unsigned f=0;f<3;++f) {context.r4=0;assert(dkr_probe_run(video_tail_entry,ram.data(),ram.size(),&context,10000).completed);}
    const auto replayed=dkr_probe_native_capture();
    assert(ram==expected && std::memcmp(&ended,&replayed,sizeof(ended))==0);
    // Skip-buffer has real retail semantics without fabricating a VI event.
    const auto index=word(base+8);context.r4=8;
    assert(dkr_probe_run(video_tail_entry,ram.data(),ram.size(),&context,10000).completed && word(base+8)==index);
    // Broken roles reject BEFORE changing RAM or the owner.
    put(base+8,2);const auto corrupt=ram;const auto saved=dkr_probe_native_capture();
    assert(!dkr_probe_run(video_start_entry,ram.data(),ram.size(),&context,10000).completed && ram==corrupt);
    const auto after=dkr_probe_native_capture();assert(std::memcmp(&saved,&after,sizeof(saved))==0);
    dkr_probe_offline_services(1);
    std::cout<<"Owned video CPU: retail blackout expiry, both buffer roles, skip branch, saved registers and restore/replay exact; no VI completion.\n";
#endif
}
void checked_service_entry(std::uint8_t* ram,recomp_context* context) {
    // Only primitive C frames lie between this entry and the probe's trap.
    dkr_probe_native(checked_service,ram,context);
}
void register_services_test() {
    using namespace dkr::runtime::netplay;
    std::vector<std::uint8_t> ram(kRollbackMemoryBytes);
    recomp_context context{}; repair_float_register_pointer(context);
    const auto call=[&](const char* name) {
        checked_service=name;
        return dkr_probe_run(checked_service_entry,ram.data(),ram.size(),&context,10000);
    };
    dkr_probe_offline_services(1);
    assert(!call("osGetCount_recomp").completed); // No unowned wall-clock fallback.
    dkr_probe_clock_start(UINT32_MAX-2ULL);
    assert(call("osGetCount_recomp").completed && context.r2==UINT32_MAX-1ULL);
    assert(call("osGetCount_recomp").completed && context.r2==UINT32_MAX);
    const auto checkpoint=dkr_probe_native_capture();
    assert(call("osGetCount_recomp").completed && context.r2==0); // unsigned wrap ABI.
    dkr_probe_native_restore(checkpoint);
    assert(call("osGetCount_recomp").completed && context.r2==0);
    context.r4=0; context.r5=100;
    assert(call("osSetTime_recomp").completed);
    assert(call("osGetTime_recomp").completed && context.r2==0 && context.r3==101);
    // Advancing is tested from a C trampoline, not across C++ live objects.
    const auto advance=[](std::uint8_t*,recomp_context*) { dkr_probe_clock_advance(); };
    assert(dkr_probe_run(advance,ram.data(),ram.size(),&context,1000).completed);
    assert(call("osGetCount_recomp").completed && context.r2==1562498);
    auto corrupt=dkr_probe_native_capture(); corrupt.clock_count_hi=UINT32_MAX;
    dkr_probe_native_restore(corrupt);
    const auto before=context;
    assert(!call("osGetCount_recomp").completed && std::memcmp(&before,&context,sizeof(context))==0);
    const auto rejected=dkr_probe_native_capture();
    assert(std::memcmp(&corrupt,&rejected,sizeof(corrupt))==0);
    dkr_probe_offline_services(1);
    for(const char* name:{"dkr_apply_gameplay_fov","dkr_hud_element_begin","dkr_presentation_object_begin"}) {
        assert(!call(name).completed); // audited identities require explicit profile.
        dkr_probe_canonical_presentation(1);
        // Full-scene semantic hooks run inside a draw pass in production;
        // initialise that pass's pinned revision addresses/scopes here too.
        dkr_probe_draw_begin();const auto saved=context;
        const auto result=call(name);
        if(!result.completed || std::memcmp(&saved,&context,sizeof(context))!=0)
            std::cerr<<"Default presentation service check failed: "<<name<<" ("<<(result.blocked?result.blocked:"register mutation")<<")\n";
        assert(result.completed && std::memcmp(&saved,&context,sizeof(context))==0);
        dkr_probe_canonical_presentation(0);
    }
    dkr_probe_canonical_presentation(1);
    assert(!call("dkr_unreviewed_presentation_hook").completed);
    for(float value:{-12345.75F,0.0F,98765.5F,-9223372036854775808.0F}) {
        context.f12.fl=value;
        assert(call("__f_to_ll_recomp").completed);
        const auto bits=(std::uint64_t(context.r2)<<32)|(context.r3&UINT32_MAX);
        assert(std::int64_t(bits)==std::int64_t(value));
    }
    for(float value:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN(),9223372036854775808.0F}) {
        context.f12.fl=value; const auto saved=context;
        assert(!call("__f_to_ll_recomp").completed && std::memcmp(&saved,&context,sizeof(context))==0);
    }
    for(auto bits:{0ULL,1ULL,0x0123456789ABCDEFULL,0x8000000000000000ULL,0xFFFFFFFFFFFFFFFFULL}) {
        context.r4=std::int32_t(bits>>32); context.r5=std::int32_t(bits);
        assert(call("__ll_to_f_recomp").completed && context.f0.fl==float(std::int64_t(bits)));
        for(auto operand:{1ULL,7ULL,63ULL}) for(const char* name:{"__ull_rshift_recomp","__ll_lshift_recomp","__ull_div_recomp","__ull_rem_recomp"}) {
            context.r6=operand>>32; context.r7=operand&UINT32_MAX;
            assert(call(name).completed);
            const auto actual=(std::uint64_t(context.r2)<<32)|(context.r3&UINT32_MAX);
            const auto expected=std::strcmp(name,"__ull_rshift_recomp")==0 ? bits>>operand :
                std::strcmp(name,"__ll_lshift_recomp")==0 ? bits<<operand :
                std::strcmp(name,"__ull_div_recomp")==0 ? bits/operand : bits%operand;
            assert(actual==expected);
        }
    }
    for(const char* name:{"__ull_rshift_recomp","__ll_lshift_recomp","__ull_div_recomp","__ull_rem_recomp"}) {
        context.r6=0; context.r7=std::strstr(name,"shift") ? 64:0;
        const auto saved=context;
        assert(!call(name).completed && std::memcmp(&saved,&context,sizeof(context))==0);
    }
    std::cout<<"Owned NTSC clock, explicit canonical-profile guards and register-only math boundaries passed.\n";
}
void material_service_test() {
    using namespace dkr::runtime::netplay;
    using namespace dkr::runtime::steering_wheel;
    std::vector<std::uint8_t> ram(kRollbackMemoryBytes);
    const auto word=[&](unsigned a,std::uint32_t v) { std::memcpy(ram.data()+a,&v,4); };
    const auto half=[&](unsigned a,std::uint16_t v) { std::memcpy(ram.data()+(a^2U),&v,2); };
    const auto byte=[&](unsigned a,std::uint8_t v) { ram[a^3U]=v; };
    word(0x30B4,0x80005300U); half(0x5348,kRacerBehaviour); word(0x5364,0x80005400U);
    byte(0x55D6,kCarVehicle); half(0x5028,3); half(0x5022,1); word(0x5000,0x80005600U);
    byte(0x5100,0); byte(0x5200,16); byte(0x5201,16); byte(0x5202,0); half(0x5206,kClampBothAxes);
    word(0x5600,0x80005200U); byte(0x5604,16); byte(0x5605,16); byte(0x5606,0);
    const auto original=ram;
    recomp_context setup{}; repair_float_register_pointer(setup);
    setup.r23=std::int32_t(0x80005000U); setup.r2=std::int32_t(0x80005100U);
    setup.r18=std::int32_t(0x80005200U); setup.r29=std::int32_t(0x80003000U);
    setup.r10=2; setup.r17=4; setup.r21=2; setup.r8=setup.r6=kAuthoredBatchFlags;
    dkr_probe_offline_services(1); dkr_probe_canonical_presentation(1);
    checked_service="dkr_fix_car_steering_wheel_material";
    for(unsigned fault=0;fault<18;++fault) {
        ram=original; auto context=setup; repair_float_register_pointer(context);
        if(fault==1) half(0x5348,2);
        if(fault==2) byte(0x55D6,1);
        if(fault==3) half(0x5028,0);
        if(fault==4) context.r10=1;
        if(fault==5) byte(0x5100,255);
        if(fault==6) context.r17=5;
        if(fault==7) context.r21=3;
        if(fault==8) context.r8^=1;
        if(fault==9) byte(0x5200,17);
        if(fault==10) byte(0x5201,17);
        if(fault==11) byte(0x5202,1);
        if(fault==12) half(0x5206,0);
        if(fault==13) word(0x5600,0x80005210U);
        if(fault==14) byte(0x5604,17);
        if(fault==15) word(0x5364,0);
        if(fault==16) context.r23=0;
        if(fault==17) word(0x30B4,0);
        const auto saved=ram; auto expected=context;
        if(!fault) expected.r6&=~std::uint64_t(kDecal);
        assert(dkr_probe_run(checked_service_entry,ram.data(),ram.size(),&context,10000).completed);
        assert(ram==saved && std::memcmp(&expected,&context,sizeof(context))==0);
    }
    std::cout<<"Exact production steering-wheel field/ABI bridge: positive match and 17 narrowed refusals passed.\n";
}
void sky_and_reset_services_test() {
    using namespace dkr::runtime::netplay;
    std::vector<std::uint8_t> ram(kRollbackMemoryBytes);
    const unsigned layout=DKR_PROBE_REVISION==77 ? 0x11D37C:0x11D8FC;
    const unsigned reset=DKR_PROBE_REVISION==77 ? 0x123548:0x123AC8;
    const auto word=[&](unsigned a,std::uint32_t value) { std::memcpy(ram.data()+a,&value,4); };
    const auto half=[&](unsigned a,std::int16_t value) { std::memcpy(ram.data()+(a^2U),&value,2); };
    const auto read_half=[&](unsigned a) { std::int16_t value; std::memcpy(&value,ram.data()+(a^2U),2); return value; };
    recomp_context context{}; repair_float_register_pointer(context);
    const auto call=[&](const char* name) {
        checked_service=name; return dkr_probe_run(checked_service_entry,ram.data(),ram.size(),&context,10000);
    };
    dkr_probe_offline_services(1);
    context.r3=std::int32_t(0x80004000U);
    assert(!call("dkr_widen_gradient_sky").completed);
    dkr_probe_canonical_presentation(1);
    for(unsigned view=0;view<4;++view) {
        word(layout,view);
        for(unsigned v=0;v<4;++v) { half(0x4000+v*10,v%2 ? 20000:-100); half(0x4002+v*10,75); }
        const auto old=context; assert(call("dkr_widen_gradient_sky").completed);
        assert(std::memcmp(&old,&context,sizeof(context))==0);
        for(unsigned v=0;v<4;++v) {
            // Production policy: 2-player sky X doubles, clamp to signed16;
            // its vertical policy is (21:9 / 4:3)^2 / 2, rounded as in the
            // production hook: 75 becomes 115. Quadrants remain unity.
            assert(read_half(0x4000+v*10)==(v%2 ? (view==1 ? 32767:20000):(view==1 ? -200:-100)));
            assert(read_half(0x4002+v*10)==(view==1 ? 115:75));
        }
    }
    word(layout,1); context.r3=std::int32_t(0x80FFFFF0U);
    auto before=ram; assert(!call("dkr_widen_gradient_sky").completed && ram==before);
    std::uint32_t basis[4]; dkr_probe_void_basis_addresses(basis);
    const auto real=[&](unsigned address,float value) { std::memcpy(ram.data()+(address&0xFFFFFFU),&value,4); };
    context.r2=std::int32_t(0x80004028U);
    for(unsigned rotated=0;rotated<2;++rotated) {
        real(basis[0],rotated?0.6F:1.0F); real(basis[1],rotated?0.8F:0.0F);
        real(basis[2],10.0F); real(basis[3],20.0F);
        for(unsigned v=0;v<4;++v) {
            half(0x4000+v*10,rotated?70:110);half(0x4002+v*10,75);half(0x4004+v*10,rotated?100:30);
        }
        const auto regs=context; assert(call("dkr_widen_void_primitive").completed);
        assert(std::memcmp(&regs,&context,sizeof(context))==0);
        for(unsigned v=0;v<4;++v) {
            assert(read_half(0x4000+v*10)==(rotated?130:210));
            assert(read_half(0x4004+v*10)==(rotated?180:30));
            assert(read_half(0x4002+v*10)==75);
        }
    }
    real(basis[0],1.0F);real(basis[1],0.0F);real(basis[2],0.0F);
    for(unsigned v=0;v<4;++v) half(0x4000+v*10,v%2?20000:-20000);
    assert(call("dkr_widen_void_primitive").completed);
    for(unsigned v=0;v<4;++v) assert(read_half(0x4000+v*10)==(v%2?32767:-32768));
    for(unsigned fault=0;fault<6;++fault) {
        real(basis[0],1.0F);real(basis[1],0.0F);real(basis[2],0.0F);real(basis[3],0.0F);
        if(fault<4) real(basis[fault],std::numeric_limits<float>::quiet_NaN());
        if(fault==4) real(basis[0],0.1F);
        if(fault==5) real(basis[0],2.0F);
        const auto original=ram;assert(call("dkr_widen_void_primitive").completed && ram==original);
    }
    context.r2=std::int32_t(0x81000010U); before=ram;
    assert(!call("dkr_widen_void_primitive").completed && ram==before);
    // No general message-queue escape. Exact capacity-one empty NMI poll is
    // supported only after the private owner explicitly registers the policy.
    word(reset+16,1); word(reset+20,0x80000000U+reset-4);
    context.r4=std::int32_t(0x80000000U+reset); context.r5=context.r6=0;
    assert(!call("osRecvMesg_recomp").completed);
    dkr_probe_reset_poll_configure(1); before=ram;
    assert(call("osRecvMesg_recomp").completed && context.r2==std::uint64_t(-1LL) && ram==before);
    for(unsigned fault=0;fault<7;++fault) {
        ram=before; context.r5=context.r6=0;
        if(fault==0) context.r5=0x80004000U;
        if(fault==1) context.r6=1;
        if(fault==2) word(reset,1);
        if(fault==3) word(reset+4,1);
        if(fault==4) word(reset+8,1);
        if(fault==5) word(reset+16,2);
        if(fault==6) word(reset+20,0x80001000U);
        const auto untouched=ram; const auto regs=context;
        assert(!call("osRecvMesg_recomp").completed && ram==untouched && std::memcmp(&regs,&context,sizeof(context))==0);
    }
    dkr_probe_reset_poll_configure(0);
    std::cout<<"Exact multiplayer gradient-sky/void-basis policies and narrowly owned empty reset polls passed.\n";
}
void fullscreen_clear_service_test() {
    using namespace dkr::runtime::netplay;
    std::vector<std::uint8_t> ram(kRollbackMemoryBytes);
    recomp_context context{};repair_float_register_pointer(context);
    dkr_probe_offline_services(1);dkr_probe_canonical_presentation(1);
    const auto set=[&](std::uint32_t value){std::memcpy(ram.data()+0x1004,&value,4);};
    const auto get=[&]{std::uint32_t value;std::memcpy(&value,ram.data()+0x1004,4);return value;};
    checked_service="dkr_fix_fullscreen_clear_scissor";
    for(auto pointer:{0x80001000U,0xA0001000U,0x1000U}) {
        context.r3=std::int32_t(pointer);context.r2=119;
        set((319U*4<<12)|(239U*4));
        assert(dkr_probe_run(checked_service_entry,ram.data(),ram.size(),&context,1000).completed);
        assert(get()==((320U*4<<12)|(240U*4)) && context.r2==119);
        set(0x12345678U);const auto original=ram;
        assert(dkr_probe_run(checked_service_entry,ram.data(),ram.size(),&context,1000).completed && ram==original);
    }
    for(auto pointer:{0x807FFFF8U,0x80800000U,0xA0800000U,0x7FFFFFFCU}) {
        context.r3=std::int32_t(pointer);const auto original=ram;
        assert(dkr_probe_run(checked_service_entry,ram.data(),ram.size(),&context,1000).completed && ram==original);
    }
    dkr_probe_canonical_presentation(0);
    assert(!dkr_probe_run(checked_service_entry,ram.data(),ram.size(),&context,1000).completed);
}
void native_service_entry(std::uint8_t* ram,recomp_context* context) {
    // Primitive-only trampoline: no C++ destructor crosses the C probe trap.
    const char* name=context->r3==0 ? "dkr_legacy_pi_start_dma" :
        context->r3==1 ? "osRecvMesg_recomp" :
        context->r3==2 ? "dkr_v11_asset_mutex_acquire" : "dkr_v11_asset_mutex_release";
    dkr_probe_native(name,ram,context);
}
void asset_services_test() {
    using namespace dkr::runtime::netplay;
    std::vector<std::uint8_t> ram(kRollbackMemoryBytes);
    const unsigned queue=DKR_PROBE_REVISION==77 ? 0x124220 : 0x1247A0;
    const unsigned mutex=0x124818,buffer=0x2500,target=0x4000,stack=0x3000;
    const auto word=[&](unsigned offset,std::uint32_t value) { std::memcpy(ram.data()+offset,&value,4); };
    word(queue+16,1); word(queue+20,0x80000000U+buffer);
    word(stack+0x10,0x80000000U+target); word(stack+0x14,16); word(stack+0x18,0x80000000U+queue);
    std::array<std::uint8_t,128> image;
    for(unsigned i=0;i<image.size();++i) image[i]=std::uint8_t(i*3);
    dkr_probe_offline_services(1); dkr_probe_rom(image.data(),image.size());
    recomp_context context{};
    const auto reset_dma=[&] { context={}; context.r29=std::uint64_t(std::int64_t(std::int32_t(0x80000000U+stack))); context.r7=32; };
    reset_dma(); const auto original=ram;
    assert(dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,1000).completed && context.r2==0);
    for(unsigned i=0;i<16;++i) assert(ram[(target+i)^3U]==image[32+i]);
    context.r3=1; context.r4=std::uint64_t(std::int64_t(std::int32_t(0x80000000U+queue)));
    context.r5=std::uint64_t(std::int64_t(std::int32_t(0x80002000U))); context.r6=1;
    assert(dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,1000).completed && context.r2==0);
    const auto expected=ram;
    ram=original; reset_dma();
    assert(dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,1000).completed);
    context.r3=1; context.r4=std::uint64_t(std::int64_t(std::int32_t(0x80000000U+queue)));
    context.r5=std::uint64_t(std::int64_t(std::int32_t(0x80002000U))); context.r6=1;
    assert(dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,1000).completed && ram==expected);
    // Bad range/direction/alignment/ownership/queue states must stop before a
    // single ROM byte is copied. Blocking on an empty queue never succeeds.
    for(unsigned failure=0;failure<7;++failure) {
        ram=original; reset_dma();
        if(failure==0) context.r7=image.size()-8;
        if(failure==1) context.r6=1;
        if(failure==2) word(stack+0x10,0x80000000U+target+1);
        if(failure==3) word(stack+0x18,0x80001000U);
        if(failure==4) word(queue+8,1);
        if(failure==5) word(queue,0x80001234U);
        if(failure==6) { context.r3=1; context.r4=std::uint64_t(std::int64_t(std::int32_t(0x80000000U+queue))); context.r6=1; }
        const auto before=ram;
        const auto rejected=dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,1000);
        assert(!rejected.completed && rejected.blocked && ram==before);
    }
    if(DKR_PROBE_REVISION==80) {
        ram=original; word(mutex+16,1); word(mutex+20,0x80002600U); word(mutex+8,1); word(0x2600,1);
        context={}; context.r3=2; context.r4=std::uint64_t(std::int64_t(std::int32_t(0x80000000U+mutex))); context.r6=1;
        const auto before=ram;
        assert(dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,1000).completed);
        context.r3=3; context.r5=1; context.r6=0;
        assert(dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,1000).completed && ram==before);
    }
    dkr_probe_rom(nullptr,0);
    std::cout<<"Owned retail ROM/asset queue replay and refusal checks passed.\n";
}
#if DKR_PROBE_HAS_AUDIO
void audio_dispatch_entry(std::uint8_t*,recomp_context* context) {
    const auto callback=get_function((int32_t)context->r4);
    context->r2=callback==__amDMA;
}
void audio_event_service_entry(std::uint8_t* ram,recomp_context* context) {
    context->r2=dkr_probe_audio_event_next(ram,context);
}
void audio_guard_service_entry(std::uint8_t* ram,recomp_context* context) {
    context->r3=dkr_probe_native(checked_service,ram,context);
}
void audio_event_services_test() {
    using namespace dkr::runtime::netplay;
    std::vector<std::uint8_t> ram(kRollbackMemoryBytes);
    constexpr unsigned queue=0x4000,item=0x4100,next=0x4200,free=0x4300,destination=0x4400;
    const auto word=[&](unsigned offset,std::uint32_t value) { std::memcpy(ram.data()+offset,&value,4); };
    const auto read=[&](unsigned offset) { std::uint32_t value=0; std::memcpy(&value,ram.data()+offset,4); return value; };
    recomp_context context{}; repair_float_register_pointer(context);
    context.r4=std::int32_t(0x80000000U+queue); context.r5=std::int32_t(0x80000000U+destination);
    dkr_probe_offline_services(1); dkr_probe_audio_guards_initialize();
    assert(!dkr_probe_run(audio_event_service_entry,ram.data(),ram.size(),&context,10000).completed);
    dkr_probe_audio_configure(1);
    word(queue,0x80000000U+free); word(queue+8,0x80000000U+item); word(queue+12,0x80000000U+next);
    word(item,0x80000000U+next); word(item+4,0x80000000U+queue+8); word(item+8,1234);
    word(next+4,0x80000000U+item);
    for(unsigned b=0;b<16;b+=4) word(item+12+b,0xABCD0000U+b);
    const auto initial=ram;
    for(unsigned fault=0;fault<6;++fault) {
        ram=initial;
        if(fault==0) word(item+4,0x80005000U);
        if(fault==1) word(next+4,0x80005000U);
        if(fault==2) word(item,0x80FFFFFCU);
        if(fault==3) word(queue,0x80FFFFFCU);
        if(fault==4) context.r5=std::int32_t(0x80FFFFFCU);
        if(fault==5) context.r4=std::int32_t(0x80004001U);
        const auto before=ram; std::array<dkr_probe_audio_guard,8> old{},after{};
        dkr_probe_audio_guards_capture(old.data());
        assert(!dkr_probe_run(audio_event_service_entry,ram.data(),ram.size(),&context,10000).completed && ram==before);
        dkr_probe_audio_guards_capture(after.data()); assert(!std::memcmp(old.data(),after.data(),sizeof(old)));
        context.r4=std::int32_t(0x80000000U+queue); context.r5=std::int32_t(0x80000000U+destination);
    }
    ram=initial;
    assert(dkr_probe_run(audio_event_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r2==1234);
    assert(read(queue)==0x80000000U+item && read(queue+8)==0x80000000U+next &&
           read(next+4)==0x80000000U+queue+8 && read(free+4)==0x80000000U+item);
    for(unsigned b=0;b<16;b+=4) assert(read(destination+b)==0xABCD0000U+b);
    // Repeated zero-delta callbacks exercise the native counter across a
    // checkpoint. The 257th applies the EXACT existing recovery policy.
    for(unsigned count=0;count<257;++count) {
        word(queue+8,0x80000000U+item); word(queue+12,0x80000000U+item); word(queue,0x80000000U+free);
        word(item,0); word(item+4,0x80000000U+queue+8); word(item+8,0);
        if(count==256) {
            std::array<dkr_probe_audio_guard,8> saved{}; dkr_probe_audio_guards_capture(saved.data());
            const auto before=ram;
            assert(dkr_probe_run(audio_event_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r2==16667);
            const auto expected=ram;
            ram=before; dkr_probe_audio_guards_restore(saved.data());
            assert(dkr_probe_run(audio_event_service_entry,ram.data(),ram.size(),&context,10000).completed && ram==expected && context.r2==16667);
        } else assert(dkr_probe_run(audio_event_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r2==0);
    }
    assert(!read(queue+8) && !read(queue+12) && ram[(destination+0)^3]==255 && ram[(destination+1)^3]==255);
    assert(dkr_probe_run(audio_event_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r2==16667);
    // Voice guard: actual list membership, bus normalization, bounded cycle
    // and refusal. Bus guard changes v0 ONLY on its real invalid branch.
    context.r4=std::int32_t(0x80005000U); context.r5=std::int32_t(0x80006000U); context.r6=1;
    word(0x5004,0x80006000U); ram[0x60DC^3]=255;
    checked_service="dkr_audio_voice_guard";
    assert(dkr_probe_run(audio_guard_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r3==0 && ram[0x60DC^3]==0);
    ram[0x60DC^3]=255; word(0x5004,0x80007000U); word(0x7000,0x80007000U);
    const auto before=ram;
    assert(dkr_probe_run(audio_guard_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r3==1 && ram==before);
    checked_service="dkr_audio_bus_guard"; word(0x5014,64); word(0x501C,0x80008000U); context.r2=123;
    assert(dkr_probe_run(audio_guard_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r3==0 && context.r2==123);
    word(0x5014,65);
    assert(dkr_probe_run(audio_guard_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r3==1 && context.r2==0);
    dkr_probe_audio_configure(0);
    std::cout<<"Owned audio event list semantics/counter rewind/recovery and voice/bus guards passed.\n";
}
void audio_ownership_services_test() {
    using namespace dkr::runtime::netplay;
    std::vector<std::uint8_t> ram(kRollbackMemoryBytes);
    const unsigned queue=DKR_PROBE_REVISION==77 ? 0x119AF0:0x11A070;
    const unsigned stack=0x3000,target=0x4000;
    const auto word=[&](unsigned offset,std::uint32_t value) { std::memcpy(ram.data()+offset,&value,4); };
    word(queue+16,50); word(queue+20,0x80000000U+queue+24);
    word(stack+0x10,0x80000000U+target); word(stack+0x14,1024); word(stack+0x18,0x80000000U+queue);
    std::array<std::uint8_t,2048> image{};
    for(unsigned i=0;i<image.size();++i) image[i]=std::uint8_t(i*5);
    dkr_probe_offline_services(1); dkr_probe_rom(image.data(),image.size());
    recomp_context context{}; repair_float_register_pointer(context);
    context.r4=std::int32_t(0x80002E38U);
    auto result=dkr_probe_run(audio_dispatch_entry,ram.data(),ram.size(),&context,1000);
    assert(!result.completed && !std::strcmp(result.blocked,"unowned-audio-indirect-dispatch"));
    dkr_probe_audio_configure(1);
    assert(dkr_probe_run(audio_dispatch_entry,ram.data(),ram.size(),&context,1000).completed && context.r2==1);
    for(auto address:{0U,0x80002E39U,0x80000400U,0x80101000U}) {
        context.r4=std::int32_t(address); const auto saved=context;
        result=dkr_probe_run(audio_dispatch_entry,ram.data(),ram.size(),&context,1000);
        assert(!result.completed && !std::strcmp(result.blocked,"unreviewed-indirect-callback") &&
               !std::memcmp(&saved,&context,sizeof(context)));
    }
#if DKR_PROBE_HAS_FULL_SCENES
    // Audio ownership alone cannot grant access to the menu callback.
    context.r4=std::int32_t(DKR_PROBE_REVISION==77 ? 0x8008F618U:0x8008FAD0U);
    assert(!dkr_probe_run(audio_dispatch_entry,ram.data(),ram.size(),&context,1000).completed);
    dkr_probe_canonical_presentation(1);dkr_probe_scene_configure(1);
    dkr_probe_menu_tick_configure(1);
    assert(dkr_probe_run(audio_dispatch_entry,ram.data(),ram.size(),&context,1000).completed && context.r2==0);
    // Restoring a checkpoint clears permission; it is never serialized.
    dkr_probe_native_restore(dkr_probe_native_capture());
    assert(!dkr_probe_run(audio_dispatch_entry,ram.data(),ram.size(),&context,1000).completed);
    dkr_probe_canonical_presentation(0);dkr_probe_scene_configure(0);
#endif
    const auto original=ram;
    for(unsigned transfers=0;transfers<50;++transfers) {
        context={}; repair_float_register_pointer(context);
        context.r29=std::int32_t(0x80000000U+stack); context.r7=0;
        assert(dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,10000).completed);
    }
    const auto full=ram;
    assert(!dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,10000).completed && ram==full);
    for(unsigned transfers=0;transfers<50;++transfers) {
        context.r3=1; context.r4=std::int32_t(0x80000000U+queue); context.r5=0; context.r6=0;
        assert(dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r2==0);
    }
    assert(dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,10000).completed && (int32_t)context.r2==-1);
    // Changed queue, blocking waiter, count, buffer or read length never
    // grant a native escape and never partially copy source bytes.
    for(unsigned fault=0;fault<6;++fault) {
        ram=original; context={}; repair_float_register_pointer(context);
        context.r29=std::int32_t(0x80000000U+stack);
        if(fault==0) word(queue+16,49);
        if(fault==1) word(queue+20,0x80002000U);
        if(fault==2) word(queue,1);
        if(fault==3) word(queue+4,1);
        if(fault==4) word(stack+0x14,512);
        if(fault==5) word(stack+0x18,0x80003000U);
        const auto before=ram;
        assert(!dkr_probe_run(native_service_entry,ram.data(),ram.size(),&context,10000).completed && ram==before);
    }
    dkr_probe_rom(nullptr,0); dkr_probe_audio_configure(0);
    std::cout<<"Exact private audio dispatch and 50-slot synchronous ROM DMA ownership/refusals passed.\n";
}
#endif
void eeprom_service_entry(std::uint8_t* ram,recomp_context* context) {
    // Exercise the ACTUAL emitted import trampolines without linking a live
    // save worker. Primitive-only call frames may safely cross the C trap.
    if(context->r3==0) osEepromProbe_recomp(ram,context);
    else if(context->r3==1) osEepromRead_recomp(ram,context);
    else if(context->r3==2) osEepromWrite_recomp(ram,context);
    else dkr_probe_native(context->r3==3 ? "osEepromLongRead_recomp":"osEepromLongWrite_recomp",ram,context);
}
void eeprom_services_test() {
    using namespace dkr::runtime::netplay::experimental;
    std::vector<std::uint8_t> ram(0x10000);
    std::array<std::uint8_t,512> image{};
    for(unsigned b=0;b<image.size();++b) image[b]=std::uint8_t(b*19);
    Eeprom save; assert(save.start(1,image));
    dkr_probe_offline_services(1); dkr_probe_bind_eeprom(&save);
    std::array<std::uint8_t,Eeprom::kCheckpointBytes> checkpoint{}; assert(save.capture(checkpoint));
    recomp_context context{}; context.r3=0;
    assert(dkr_probe_run(eeprom_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r2==1);
    context.r3=1; context.r5=63; context.r6=std::uint64_t(std::int64_t(std::int32_t(0x80003000U)));
    assert(dkr_probe_run(eeprom_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r2==0);
    for(unsigned i=0;i<8;++i) assert(ram[(0x3000+i)^3]==image[504+i]);
    assert(save.begin_frame(0)); context.r3=2;
    for(unsigned i=0;i<8;++i) ram[(0x3000+i)^3]=std::uint8_t(i+1);
    assert(dkr_probe_run(eeprom_service_entry,ram.data(),ram.size(),&context,10000).completed && context.r2==0);
    assert(std::equal(image.begin(),image.end(),save.confirmed_image().begin()));
    std::array<std::uint8_t,8> read{}; assert(save.read(504,read) && read[0]==1 && read[7]==8);
    assert(save.restore(checkpoint)); assert(save.read(504,read) && read[0]==image[504]);
    assert(save.begin_frame(0)); context.r3=2;
    assert(dkr_probe_run(eeprom_service_entry,ram.data(),ram.size(),&context,10000).completed);
    for(unsigned fault=0;fault<5;++fault) {
        context.r3=4; context.r5=0; context.r6=std::uint64_t(std::int64_t(std::int32_t(0x80003000U))); context.r7=8;
        if(fault==0) context.r7=9;
        if(fault==1) context.r7=std::uint64_t(-8LL);
        if(fault==2) context.r5=64;
        if(fault==3) context.r6=0x8000FFFFU;
        if(fault==4) context.r6=0x90000000U;
        const auto before=ram; assert(save.read(504,read)); const auto old=read;
        assert(!dkr_probe_run(eeprom_service_entry,ram.data(),ram.size(),&context,10000).completed);
        assert(ram==before && save.read(504,read) && read==old);
    }
    std::vector<std::uint8_t> journal; assert(save.end_frame(journal) && save.commit(1,0,journal));
    assert(!save.restore(checkpoint));
    context.r3=3; context.r5=0; context.r6=std::uint64_t(std::int64_t(std::int32_t(0x80003000U))); context.r7=512;
    assert(dkr_probe_run(eeprom_service_entry,ram.data(),ram.size(),&context,10000).completed);
    for(unsigned i=0;i<512;++i) assert(ram[(0x3000+i)^3]==save.confirmed_image()[i]);
    dkr_probe_bind_eeprom(nullptr); context.r3=0;
    assert(!dkr_probe_run(eeprom_service_entry,ram.data(),ram.size(),&context,10000).completed);
    std::cout<<"Private emitted EEPROM import bridge: MIPS byte lanes, reversible/confirmed writes, rejected ranges exact=1; no live file persistence.\n";
}
void print(const dkr_probe_result& result) {
    std::cout << "completed=" << result.completed << " blocked="
              << (result.blocked ? result.blocked : "none")
              << " memory_accesses=" << result.memory_accesses
              << " operations=" << result.operations << " bad_address=0x"
              << std::hex << result.bad_address << std::dec << '\n';
    for (unsigned i = 0; i < result.stack_depth; ++i)
        std::cout << "  " << result.stack[i] << '\n';
    std::cout << "guest_entries=" << result.guest_entries << '\n';
}
// Component qualification, NOT admission of the full game adapter. The
// isolated world has no audio/render/native workers. obj_update alone does
// not include the retail camera/water/HUD/transition tick and cannot be used
// as the eventual gameplay tick simply because this check passes.
bool component_replay(dkr_probe_entry root, std::vector<std::uint8_t>& ram,
                      recomp_context& context, unsigned frames) {
    using namespace dkr::runtime::netplay;
    RuntimeState codec;
    if (!codec.register_context(&context)) return false;
    struct Checkpoint {
        std::vector<std::uint8_t> bytes;
        dkr_probe_native_state native{};
    };
    std::vector<Checkpoint> checkpoints(frames + 1);
    std::vector<std::vector<dkr_probe_effect>> expected_effects(frames);
    std::vector<std::uint8_t> actual(codec.snapshot_size());
    const auto capture = [&](Checkpoint& checkpoint, unsigned frame) {
        checkpoint.bytes.resize(codec.snapshot_size());
        checkpoint.native = dkr_probe_native_capture();
        return codec.capture(ram.data(), frame, checkpoint.bytes);
    };
    const auto tick = [&](std::vector<dkr_probe_effect>& journal) {
        context.r4 = 2;
        dkr_probe_effects_begin();
        const auto result = dkr_probe_run(root, ram.data(), ram.size(), &context, 5000000);
        if (!result.completed) { print(result); return false; }
        const auto native = dkr_probe_native_capture();
        if (native.vehicle_audio_scope || native.nature_audio_scope) {
            std::cerr << "Unbalanced native audio scope\n"; return false;
        }
        journal.resize(256);
        const auto count = dkr_probe_effects_capture(journal.data(), 256);
        if (count > 256) return false;
        journal.resize(count);
        return true;
    };
    const auto start = std::chrono::steady_clock::now();
    if (!capture(checkpoints[0], 0)) return false;
    for (unsigned frame = 0; frame < frames; ++frame) {
        if (!tick(expected_effects[frame]) || !capture(checkpoints[frame + 1], frame + 1)) return false;
    }
    unsigned replayed = 0;
    for (unsigned from = 0; from < frames; ++from) {
        unsigned restored = 0;
        if (!codec.restore(ram.data(), checkpoints[from].bytes, restored) || restored != from) return false;
        dkr_probe_native_restore(checkpoints[from].native);
        for (unsigned frame = from; frame < frames; ++frame) {
            std::vector<dkr_probe_effect> journal;
            if (!tick(journal) || !codec.capture(ram.data(), frame + 1, actual)) return false;
            ++replayed;
            const auto mismatch = std::mismatch(actual.begin(), actual.end(), checkpoints[frame + 1].bytes.begin());
            const auto native = dkr_probe_native_capture();
            const auto expected = checkpoints[frame + 1].native;
            if (mismatch.first != actual.end() ||
                std::memcmp(&native,&expected,sizeof(native)) != 0) {
                std::cerr << "Component replay mismatch from=" << from << " frame=" << frame
                          << " byte=" << (mismatch.first - actual.begin()) << '\n';
                return false;
            }
            if (journal.size() != expected_effects[frame].size() || !std::equal(journal.begin(), journal.end(),
                expected_effects[frame].begin(), [](const auto& a, const auto& b) {
                    return a.kind == b.kind && a.object == b.object;
                })) {
                std::cerr << "Component lifecycle journal mismatch from=" << from << " frame=" << frame << '\n';
                return false;
            }
        }
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto changed = std::inner_product(checkpoints[0].bytes.begin(), checkpoints[0].bytes.end(),
        checkpoints[frames].bytes.begin(), std::size_t{}, std::plus<std::size_t>{},
        [](auto a, auto b) { return std::size_t(a != b); });
    std::cout << "COMPONENT ONLY: frames=" << frames << " suffix_replays=" << replayed
              << " changed_bytes=" << changed << " exact=1 seconds=" << seconds << '\n';
    return changed > sizeof(unsigned);
}
void self_test() {
    owned_video_test();
    audio_dma_services_test();
#if DKR_PROBE_HAS_AUDIO_ISOLATION
    assert(dkr_probe_audio_link_canaries_begin());
    dkr_probe_audio_rsp_initialize();
    assert(dkr_probe_audio_link_canaries_unchanged());
    std::cout<<"Private DSP storage/entry coexist without stable interposition.\n";
#endif
#if DKR_PROBE_HAS_SYMBOL_ISOLATION
    assert(dkr_probe_check_stable_link_canaries());
#endif
    register_services_test();
    material_service_test();
    sky_and_reset_services_test();
    fullscreen_clear_service_test();
    assert(dkr_probe_pak_import_check());
    assert(dkr_probe_magic_checks());
    eeprom_services_test();
    using namespace dkr::runtime::netplay;
    asset_services_test();
#if DKR_PROBE_HAS_AUDIO
    audio_event_services_test();
    audio_ownership_services_test();
#endif
    std::vector<std::uint8_t> ram(kRollbackMemoryBytes);
    recomp_context context{};
    repair_float_register_pointer(context);
    // Real retail input_swap_id exchanges the first two bytes of its own
    // PlayerIdMap. Find the address through its actual instructions: it stores
    // the pointer in r3, so an initial all-zero invocation remains safe.
    auto result = dkr_probe_run(input_swap_id, ram.data(), ram.size(), &context, 100);
    assert(result.completed && result.memory_accesses == 4);
    const auto offset = static_cast<std::uint32_t>(context.r3) - 0x80000000U;
    assert(offset + 4 < ram.size());
    ram[offset ^ 3U] = 1; ram[(offset + 1U) ^ 3U] = 2;
    const auto original = ram;
    result = dkr_probe_run(input_swap_id, ram.data(), ram.size(), &context, 100);
    assert(result.completed && ram[offset ^ 3U] == 2 && ram[(offset + 1U) ^ 3U] == 1);
    const auto expected = ram;
    ram = original;
    result = dkr_probe_run(input_swap_id, ram.data(), ram.size(), &context, 100);
    assert(result.completed && ram == expected);
    result = dkr_probe_run(input_swap_id, ram.data(), 64, &context, 100);
    assert(!result.completed && std::strcmp(result.blocked, "out-of-range-guest-address") == 0);
    assert(result.source_file && result.source_line != 0);
    dkr_probe_watch_word(UINT64_C(0xffffffff80000002));
    result = dkr_probe_run(input_swap_id, ram.data(), ram.size(), &context, 100);
    assert(!result.completed && std::strcmp(result.blocked,"invalid-private-watchpoint") == 0);
    dkr_probe_watch_word(0);
    result = dkr_probe_run(input_swap_id, ram.data(), ram.size(), &context, 1);
    assert(!result.completed && std::strcmp(result.blocked, "operation-budget") == 0);
    // A real main-loop native boundary must trap, not return fake success.
    result = dkr_probe_run(main_game_loop, ram.data(), ram.size(), &context, 10000);
    assert(!result.completed && result.blocked && result.stack_depth != 0);
    print(result);
#if DKR_PROBE_HAS_SYMBOL_ISOLATION
    // Both the real guest input mutation and fenced original main entry ran
    // above. Neither may resolve to any coexisting stable C link canary.
    assert(dkr_probe_stable_link_calls()==8);
    std::cout << "Private guest/import C symbols coexist without stable interposition.\n";
#endif
    std::cout << "Real guest closure checks passed (v" << DKR_PROBE_REVISION << ").\n";
}
}
int main(int argc, char** argv) {
    using namespace dkr::runtime::netplay;
    if (const char* watched = std::getenv("DKR_PROBE_WATCH_WORD")) {
        char* end = nullptr;
        const auto address = std::strtoull(watched,&end,0);
        if (!end || *end || !address) return 2;
        dkr_probe_watch_word(address);
    }
    if (argc == 2 && std::strcmp(argv[1], "--self-test") == 0) { self_test(); return 0; }
    unsigned test_frames = 12;
    if (const char* value = std::getenv("DKR_PROBE_TEST_FRAMES")) {
        char* end = nullptr;
        const auto parsed = std::strtoul(value,&end,10);
        if (!*value || !end || *end || parsed<12 || parsed>300) return 2;
        test_frames = static_cast<unsigned>(parsed);
    }
    if (argc < 3 || argc > 5 || (argc >= 4 && std::strcmp(argv[3], "--offline-services") != 0) ||
        (argc == 5 && std::strcmp(argv[4], "--component-replay") != 0 && std::strcmp(argv[4], "--component-driver") != 0 && std::strcmp(argv[4], "--component-timeline") != 0 &&
         std::strcmp(argv[4], "--water-component-driver") != 0 && std::strcmp(argv[4], "--water-component-timeline") != 0 &&
         std::strcmp(argv[4], "--component-network") != 0 && std::strcmp(argv[4], "--water-component-network") != 0 &&
         std::strcmp(argv[4], "--component-network-paced") != 0 && std::strcmp(argv[4], "--water-component-network-paced") != 0 &&
         std::strcmp(argv[4], "--canonical-presentation") != 0 &&
         std::strcmp(argv[4], "--mode-component-driver") != 0 && std::strcmp(argv[4], "--mode-component-network-paced") != 0 &&
         std::strcmp(argv[4], "--mode-scene-cut-check") != 0 && std::strcmp(argv[4], "--retail-save-check") != 0 &&
         std::strcmp(argv[4], "--retail-audio-check") != 0 && std::strcmp(argv[4], "--retail-input-check") != 0 && std::strcmp(argv[4], "--retail-pak-check") != 0 &&
         std::strcmp(argv[4], "--mode-owned-driver") != 0 && std::strcmp(argv[4], "--mode-owned-network-paced") != 0 &&
         std::strcmp(argv[4], "--authored-owned-driver") != 0 && std::strcmp(argv[4], "--authored-owned-network-paced") != 0 && std::strcmp(argv[4], "--authored-scene-cut-check") != 0 &&
         std::strcmp(argv[4], "--retail-scene-unload-check") != 0 &&
         std::strcmp(argv[4], "--retail-scene-load-check") != 0 &&
         std::strcmp(argv[4], "--retail-scene-resume-check") != 0 &&
         std::strcmp(argv[4], "--retail-menu-flow-check") != 0 &&
         std::strcmp(argv[4], "--retail-adventure-flow-check") != 0 &&
         std::strcmp(argv[4], "--retail-scene-session-check") != 0 &&
         std::strcmp(argv[4], "--owned-adapter-check") != 0 &&
         std::strcmp(argv[4], "--owned-title-check") != 0 &&
         std::strcmp(argv[4], "--mode-audio-driver") != 0 && std::strcmp(argv[4], "--mode-audio-network-paced") != 0)) {
        std::cerr << "Usage: DKRGuestReplayProbe --self-test | PRIVATE_SNAPSHOT obj_update|mode_game|main_game_loop [--offline-services [--component-replay|--component-driver|--component-timeline|--component-network|--water-component-driver|--water-component-timeline|--water-component-network|--component-network-paced|--water-component-network-paced]]\n";
        return 2;
    }
    const auto root = std::strcmp(argv[2], "obj_update") == 0 ? obj_update :
        std::strcmp(argv[2], "mode_game") == 0 ? mode_game :
        std::strcmp(argv[2], "main_game_loop") == 0 ? main_game_loop : nullptr;
    if (!root) return 2;
    RuntimeState state;
    const auto bytes = std::filesystem::file_size(argv[1]);
    if (bytes != state.snapshot_size()) { std::cerr << "Wrong private fixture size\n"; return 2; }
    std::vector<std::uint8_t> fixture(bytes), ram(kRollbackMemoryBytes);
    std::ifstream input(argv[1], std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(fixture.data()), fixture.size())) return 2;
    recomp_context context{};
    state.register_context(&context);
    std::uint32_t revision = 0;
    // Private fixtures use the frame tag for their ROM revision. They contain
    // local ROM assets/save data and must never enter a release package.
    if (!state.restore(ram.data(), fixture, revision) || revision != DKR_PROBE_REVISION) {
        std::cerr << "Wrong snapshot format or ROM revision\n"; return 2;
    }
    context.r4 = 2; // Fixed retail authored update rate for object-update probe.
    dkr_probe_offline_services(argc >= 4);
    const bool canonical=argc==5 && std::strcmp(argv[4],"--canonical-presentation")==0;
    dkr_probe_canonical_presentation(canonical);
    dkr_probe_scene_configure(canonical && DKR_PROBE_HAS_SCENE_CUTS);
    if(canonical) {
        std::uint32_t count=0;
        std::memcpy(&count,ram.data()+(DKR_PROBE_REVISION==77 ? 0xDC64C:0xDCBBC),4);
        dkr_probe_clock_start(count);
    }
    std::vector<std::uint8_t> rom;
    if(const char* path=std::getenv("DKR_PROBE_ROM")) {
        // Match the runtime's canonical retail fingerprint, not just a header
        // revision. A patched/wrong ROM must not silently invalidate the test.
        std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
        if(!file || file.tellg()!=dkr::runtime::rom::kRetailRomSize) return 2;
        rom.resize(dkr::runtime::rom::kRetailRomSize); file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(rom.data()),rom.size()) ||
           XXH3_64bits(rom.data(),rom.size())!=(DKR_PROBE_REVISION==77 ?
               dkr::runtime::rom::kUsV77Xxh3 : dkr::runtime::rom::kUsV80Xxh3)) {
            std::cerr<<"Private replay requires the exact canonical retail ROM revision.\n"; return 2;
        }
        dkr_probe_rom(rom.data(),rom.size());
    }
    const bool water_phases=argc==5 && std::strncmp(argv[4],"--water-component-",18)==0;
    if(argc==5 && std::strcmp(argv[4],"--owned-adapter-check")==0)
        return dkr_probe_owned_adapter_check(fixture,rom,test_frames)?0:3;
    if(argc==5 && std::strcmp(argv[4],"--owned-title-check")==0)
        return dkr_probe_owned_title_check(fixture,rom)?0:3;
    if (water_phases) {
        const unsigned offset=DKR_PROBE_REVISION==77 ? 0x11D384 : 0x11D904;
        std::uint32_t wave_count=0; std::memcpy(&wave_count,ram.data()+offset,4);
        // A multiplayer water track may disable waves entirely. Do not count
        // a no-wave fixture as evidence of replaying actual water simulation.
        if(!wave_count) { std::cerr<<"Water replay requires an actual active-wave fixture.\n"; return 2; }
        std::cout<<"Private active water blocks="<<wave_count<<'\n';
    }
    const bool authored_cpu=argc==5 && std::strncmp(argv[4],"--authored-owned-",17)==0;
    const bool input_phases=authored_cpu || (argc==5 && std::strncmp(argv[4],"--mode-owned-",13)==0);
    const bool audio_phases=input_phases || (argc==5 && std::strncmp(argv[4],"--mode-audio-",13)==0);
    const bool mode_phases=argc==5 && (audio_phases || std::strncmp(argv[4],"--mode-component-",17)==0);
    if(argc==5 && std::strcmp(argv[4],"--retail-save-check")==0)
        return dkr_probe_retail_save_check(fixture) ? 0:3;
    if(argc==5 && std::strcmp(argv[4],"--retail-audio-check")==0)
        return dkr_probe_retail_audio_check(fixture) ? 0:3;
    if(argc==5 && std::strcmp(argv[4],"--retail-input-check")==0) {
        if(!DKR_PROBE_HAS_INPUT) { std::cerr<<"Regenerate through --input-services to audit native ownership first.\n";return 2; }
        return dkr_probe_retail_input_check(fixture) ? 0:3;
    }
    if(argc==5 && std::strcmp(argv[4],"--retail-pak-check")==0) {
        if(!DKR_PROBE_HAS_INPUT){std::cerr<<"Regenerate through --input-services to audit Pak ownership first.\n";return 2;}
        return dkr_probe_retail_pak_check(fixture)?0:3;
    }
    if(argc==5 && std::strcmp(argv[4],"--mode-scene-cut-check")==0)
        return root==mode_game && dkr_probe_scene_cut_check(fixture) ? 0:3;
    if(argc==5 && std::strcmp(argv[4],"--authored-scene-cut-check")==0) {
        if(!DKR_PROBE_HAS_AUTHORED_CPU) return 2;
        return root==mode_game && dkr_probe_scene_cut_check(fixture,true) ? 0:3;
    }
    if(argc==5 && std::strcmp(argv[4],"--retail-scene-unload-check")==0)
        return root==mode_game && dkr_probe_scene_unload_check(fixture) ? 0:3;
    if(argc==5 && std::strcmp(argv[4],"--retail-scene-load-check")==0)
        return root==mode_game && dkr_probe_scene_unload_check(fixture,true) ? 0:3;
    if(argc==5 && std::strcmp(argv[4],"--retail-scene-resume-check")==0)
        return root==mode_game && dkr_probe_scene_resume_check(fixture) ? 0:3;
    if(argc==5 && std::strcmp(argv[4],"--retail-menu-flow-check")==0)
        return root==mode_game && dkr_probe_menu_flow_check(fixture) ? 0:3;
    if(argc==5 && std::strcmp(argv[4],"--retail-adventure-flow-check")==0)
        return root==mode_game && dkr_probe_menu_flow_check(fixture,true) ? 0:3;
    if(argc==5 && std::strcmp(argv[4],"--retail-scene-session-check")==0) {
        if(root!=mode_game)return 2;
        for(unsigned players:{2,3,4})if(!dkr_probe_scene_session_check(fixture,players,test_frames))return 3;
        return 0;
    }
    if(mode_phases) {
        if(root!=mode_game) return 2;
        if(input_phases && !DKR_PROBE_HAS_INPUT) { std::cerr<<"Unaudited private input pipeline.\n";return 2; }
        if(authored_cpu && !DKR_PROBE_HAS_AUTHORED_CPU){std::cerr<<"Regenerate with --authored-cpu for exact phase audit.\n";return 2;}
        if(authored_cpu)std::cout<<"PRIVATE AUTHORED MAIN CPU: exact retail CPU span, no renderer/VI/task admission.\n";
        const bool paced=std::strcmp(argv[4],"--mode-component-network-paced")==0 || std::strcmp(argv[4],"--mode-audio-network-paced")==0 || std::strcmp(argv[4],"--mode-owned-network-paced")==0 || std::strcmp(argv[4],"--authored-owned-network-paced")==0;
        for(unsigned players:{2,3,4}) {
            const bool passed=paced ? dkr_probe_network_check(fixture,players,test_frames,false,true,true,audio_phases,input_phases,authored_cpu) :
                dkr_probe_driver_check(fixture,players,test_frames,false,true,audio_phases,input_phases,authored_cpu);
            if(!passed) return 3;
        }
        return 0;
    }
    const bool impaired_paced=argc==5 && (std::strcmp(argv[4],"--component-network-paced") == 0 || std::strcmp(argv[4],"--water-component-network-paced") == 0);
    if (argc == 5 && (impaired_paced || std::strcmp(argv[4],"--component-network") == 0 || std::strcmp(argv[4],"--water-component-network") == 0)) {
        if(root!=obj_update) return 2;
        for(unsigned players:{2,3,4}) if(!dkr_probe_network_check(fixture,players,test_frames,water_phases,impaired_paced)) return 3;
        return 0;
    }
    if (argc == 5 && (std::strcmp(argv[4],"--component-timeline") == 0 || std::strcmp(argv[4],"--water-component-timeline") == 0)) {
        if (root != obj_update) return 2;
        for (unsigned players : {2,3,4}) if (!dkr_probe_timeline_check(fixture,players,test_frames,water_phases)) return 3;
        return 0;
    }
    if (argc == 5 && (std::strcmp(argv[4],"--component-driver") == 0 || std::strcmp(argv[4],"--water-component-driver") == 0)) {
        if (root != obj_update) return 2;
        for (unsigned players : {2,3,4}) if (!dkr_probe_driver_check(fixture,players,test_frames,water_phases)) return 3;
        return 0;
    }
    if (argc == 5 && !canonical) return component_replay(root, ram, context, 12) ? 0 : 3;
    const auto result = dkr_probe_run(root, ram.data(), ram.size(), &context, 5000000);
    print(result);
    return result.completed ? 0 : 3;
}
