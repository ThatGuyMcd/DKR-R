#include "probe_audio.h"
#if DKR_PROBE_HAS_AUDIO
#include "librecomp/rsp.hpp"
#include <cstring>
#include <type_traits>

// Private standalone process: never link librecomp's shared DMEM or runtime
// RSP callbacks. Immutable tables have the same integer construction as the
// reviewed runtime. All persistent DSP filter state in RDRAM is checkpointed.
uint8_t dmem[4096];
uint16_t rspReciprocals[512],rspInverseSquareRoots[512];
RspExitReason dkrAspMain(uint8_t*,uint32_t);
extern "C" void dkr_probe_audio_rsp_initialize(void) {
    std::memset(dmem,0,sizeof(dmem));
    rspReciprocals[0]=UINT16_MAX;
    for(uint16_t index=1;index<512;++index) {
        const uint64_t b=(uint64_t{1}<<34)/(index+512);
        rspReciprocals[index]=uint16_t((b+1)>>8);
    }
    for(uint16_t index=0;index<512;++index) {
        const uint64_t a=(index+512)>>((index%2==1) ? 1:0);
        uint64_t b=1<<17;
        while(a*(b+1)*(b+1)<(uint64_t{1}<<44)) ++b;
        rspInverseSquareRoots[index]=uint16_t(b>>1);
    }
}
extern "C" void dkr_probe_audio_dmem_capture(uint8_t out[4096]) { std::memcpy(out,dmem,4096); }
extern "C" void dkr_probe_audio_dmem_restore(const uint8_t in[4096]) { std::memcpy(dmem,in,4096); }
extern "C" void dkr_probe_audio_rsp(uint8_t* rdram,uint32_t commands,unsigned bytes) {
    if(!dkr_probe_audio_enabled() || commands!=DKR_PROBE_AUDIO_COMMANDS-0x80000000U ||
       !bytes || bytes>0xA000 || bytes%8) dkr_probe_block("unowned-audio-rsp-task");
    // Exact same 16-word task layout and data loading as rsp::run_task;
    // no external task pointer/callback/device is consulted or signalled.
    uint32_t task[16]={}; task[0]=2;
    task[4]=DKR_PROBE_REVISION==77 ? 0xD7600:0xD7B60;
    task[6]=DKR_PROBE_REVISION==77 ? 0xE98D0:0xE9E50;
    task[7]=0x800; task[12]=commands; task[13]=bytes;
    std::memcpy(dmem+0xFC0,task,sizeof(task));
    dma_rdram_to_dmem(rdram,0,task[6],0xF80-1);
    if(dkrAspMain(rdram,task[4])!=RspExitReason::Broke)
        dkr_probe_block("retail-audio-rsp-did-not-complete");
}
#else
extern "C" void dkr_probe_audio_rsp_initialize(void) {}
extern "C" void dkr_probe_audio_dmem_capture(uint8_t[4096]) {}
extern "C" void dkr_probe_audio_dmem_restore(const uint8_t[4096]) {}
extern "C" void dkr_probe_audio_rsp(uint8_t*,uint32_t,unsigned) { dkr_probe_block("private-audio-payload-not-generated"); }
#endif
