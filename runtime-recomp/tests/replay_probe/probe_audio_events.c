#include "recomp.h"
#include "probe_audio.h"
#include <string.h>

// Private equivalent of runtime_stubs.cpp's healthy/empty/non-progressing
// audio-event branches. No mutex/native thread/logger is linked. Guards belong
// to the private audio world and are restored with RAM; they MUST NOT be shared
// with a future live runtime. Malformed pointers remain fences, not recovery
// successes which conceal an unowned memory access.
static dkr_probe_audio_guard guards[8];
void dkr_probe_audio_guards_initialize(void) { memset(guards,0,sizeof(guards)); }
void dkr_probe_audio_guards_capture(dkr_probe_audio_guard out[8]) { memcpy(out,guards,sizeof(guards)); }
void dkr_probe_audio_guards_restore(const dkr_probe_audio_guard in[8]) { memcpy(guards,in,sizeof(guards)); }
static gpr address(uint32_t value) { return (gpr)(int32_t)value; }
static uint32_t physical(uint32_t value) { return value&0x1FFFFFFFU; }
static void range(uint8_t* rdram,uint32_t value,unsigned bytes) {
    if((value&3) || value<0x80000000U || value>0x81000000U-bytes)
        dkr_probe_block("invalid-owned-audio-event-pointer");
    dkr_probe_memory(rdram,address(value),4,0);
    dkr_probe_memory(rdram,address(value)+bytes-4,4,0);
}
static dkr_probe_audio_guard* guard_for(uint32_t queue) {
    for(unsigned i=0;i<8;++i) if(physical(guards[i].queue)==physical(queue)) return &guards[i];
    for(unsigned i=0;i<8;++i) if(!guards[i].queue) { guards[i].queue=queue; return &guards[i]; }
    unsigned quietest=0;
    for(unsigned i=1;i<8;++i) if(guards[i].immediate_events<guards[quietest].immediate_events) quietest=i;
    guards[quietest]=(dkr_probe_audio_guard){queue,0,0,0}; return &guards[quietest];
}
uint32_t dkr_probe_audio_event_next(uint8_t* rdram,recomp_context* ctx) {
    if(!dkr_probe_audio_enabled()) dkr_probe_block("unowned-audio-event-queue");
    const uint32_t queue=(uint32_t)ctx->r4,destination=(uint32_t)ctx->r5;
    range(rdram,queue,0x14); range(rdram,destination,0x10);
    const gpr q=address(queue);
    const uint32_t item=(uint32_t)MEM_W(8,q);
    if(!item) {
        dkr_probe_audio_guard* guard=guard_for(queue);
        MEM_H(0,address(destination))=-1;
        guard->immediate_events=0; guard->logged_empty=1;
        return 16667;
    }
    range(rdram,item,0x1C);
    const gpr p=address(item);
    const uint32_t next=(uint32_t)MEM_W(0,p),previous=(uint32_t)MEM_W(4,p),sentinel=queue+8;
    if(physical(previous)!=physical(sentinel)) dkr_probe_block("malformed-owned-audio-event-list");
    if(next) {
        range(rdram,next,8);
        if(physical((uint32_t)MEM_W(4,address(next)))!=physical(item))
            dkr_probe_block("malformed-owned-audio-event-backlink");
    }
    const uint32_t free_head=(uint32_t)MEM_W(0,q);
    if(free_head) range(rdram,free_head,8);
    // All ranges/list links validated BEFORE any guest or native write.
    dkr_probe_audio_guard* guard=guard_for(queue);
    MEM_W(8,q)=next;
    if(next) MEM_W(4,address(next))=sentinel;
    else MEM_W(12,q)=0;
    for(unsigned b=0;b<16;b+=4) MEM_W(b,address(destination))=MEM_W(12+b,p);
    const uint32_t delta=(uint32_t)MEM_W(8,p);
    MEM_W(0,p)=free_head; MEM_W(4,p)=queue;
    if(free_head) MEM_W(4,address(free_head))=item;
    MEM_W(0,q)=item;
    if(delta) { guard->immediate_events=0; guard->logged_empty=0; return delta; }
    if(++guard->immediate_events<=256) return 0;
    MEM_W(8,q)=0; MEM_W(12,q)=0; MEM_H(0,address(destination))=-1;
    guard->immediate_events=0; guard->logged_corrupt=1;
    return 16667;
}
static int retail_pointer(uint32_t value) { return value>=0x80000000U && value<0x80800000U; }
int dkr_probe_audio_voice_guard(uint8_t* rdram,recomp_context* ctx) {
    if(!dkr_probe_audio_enabled()) dkr_probe_block("unowned-audio-voice-guard");
    const uint32_t synth=(uint32_t)ctx->r4,voice=(uint32_t)ctx->r5;
    if(!retail_pointer(synth) || !retail_pointer(voice) || (uint16_t)ctx->r6>1) return 1;
    // Same three lists, 128-node bound and normalization as runtime_stubs.cpp.
    // Checked memory adds a fence for alignment/range errors instead of UB.
    for(unsigned list=4;list<=20;list+=8) {
        uint32_t node=(uint32_t)MEM_W(list,address(synth));
        for(unsigned count=0;node && count<128;++count) {
            if(!retail_pointer(node)) break;
            if(node==voice) {
                if(MEM_BU(0xDC,address(voice))>1) MEM_B(0xDC,address(voice))=0;
                return 0;
            }
            node=(uint32_t)MEM_W(0,address(node));
        }
    }
    return 1;
}
int dkr_probe_audio_bus_guard(uint8_t* rdram,recomp_context* ctx) {
    if(!dkr_probe_audio_enabled()) dkr_probe_block("unowned-audio-bus-guard");
    const uint32_t bus=(uint32_t)ctx->r4;
    if(!retail_pointer(bus) || (uint32_t)MEM_W(0x14,address(bus))>64 ||
       !retail_pointer((uint32_t)MEM_W(0x1C,address(bus)))) { ctx->r2=0; return 1; }
    return 0;
}
