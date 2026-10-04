#include "funcs.h"
#include "probe_input.h"
#include "probe_pak.h"
#include <string.h>

static int enabled;
static dkr_probe_input_state state;
static dkr_probe_pad arguments[4];
static unsigned flags;
static int test_policy;
static dkr_probe_motor_event motors[128];
static unsigned motor_count;
static uint32_t si_queue(void) { return DKR_PROBE_REVISION==77 ? 0x801210E0U:0x80121660U; }
static gpr address(uint32_t value) { return (int32_t)value; }
static void range(uint8_t* ram,gpr pointer,unsigned size) {
    if((uint32_t)pointer<0x80000000U || !size || (uint32_t)pointer>0x80FFFFFFU ||
       size>0x81000000U-(uint32_t)pointer) dkr_probe_block("invalid-owned-controller-buffer");
    dkr_probe_memory(ram,pointer,1,3); dkr_probe_memory(ram,pointer+size-1,1,3);
}
static void queue(uint8_t* rdram) {
    const gpr q=address(si_queue());
    range(rdram,q,28);
    if(MEM_W(0,q) || MEM_W(4,q) || MEM_W(12,q) || MEM_W(16,q)!=1 ||
       (uint32_t)MEM_W(20,q)!=si_queue()+24 || MEM_W(8,q)<0 || MEM_W(8,q)>1)
        dkr_probe_block("unowned-controller-queue-or-thread");
}
void dkr_probe_input_configure(int value) { enabled=value!=0; }
int dkr_probe_input_enabled(void) { return enabled; }
void dkr_probe_input_initialize(void) { memset(&state,0,sizeof(state)); state.requested=1; motor_count=0; }
dkr_probe_input_state dkr_probe_input_capture(void) { return state; }
void dkr_probe_input_restore(dkr_probe_input_state value) { state=value; motor_count=0; }
void dkr_probe_input_arguments(const dkr_probe_pad pads[4],unsigned value) {
    memcpy(arguments,pads,sizeof(arguments)); flags=value; test_policy=0;
}
void dkr_probe_input_test_arguments(const dkr_probe_pad pads[4],unsigned value) {
    dkr_probe_input_arguments(pads,value);test_policy=1;
}
unsigned dkr_probe_motor_events(dkr_probe_motor_event out[128]) {
    memcpy(out,motors,motor_count*sizeof(*motors)); return motor_count;
}
int dkr_probe_input_service(const char* name,uint8_t* rdram,recomp_context* ctx) {
    if(!enabled) return 0;
    if(strcmp(name,"osRecvMesg_recomp")==0 && (uint32_t)ctx->r4==si_queue()) {
        queue(rdram);
        if(ctx->r6 || !state.ready || state.delivered || MEM_W(8,address(si_queue()))!=1)
            dkr_probe_block("controller-sample-not-owned-or-ready");
        if(ctx->r5) range(rdram,address((uint32_t)ctx->r5),4);
        if(ctx->r5) MEM_W(0,address((uint32_t)ctx->r5))=MEM_W(24,address(si_queue()));
        MEM_W(8,address(si_queue()))=0; state.ready=0; state.delivered=1; ctx->r2=0; return 1;
    }
    if(strcmp(name,"osContGetReadData_recomp")==0) {
        const gpr output=address((uint32_t)ctx->r4);
        if(!state.delivered || state.ready || state.requested) dkr_probe_block("unowned-controller-read-order");
        range(rdram,output,24);
        // Device presence is part of the checkpointed accepted lobby roster,
        // not the current scene's visible racer count or local SDL devices.
        // Older isolated input fixtures have no roster and retain four ports.
        const unsigned owners=dkr_probe_native_capture().owner_mask;
        for(unsigned p=0;p<4;++p) {
            if(owners && !(owners&(1U<<p))) {
                // Match ultramodern's no-response branch exactly: only err_no
                // is written; the prior button/stick bytes remain untouched.
                MEM_B(p*6+4,output)=8;
                continue;
            }
            MEM_H(p*6,output)=state.pads[p].buttons;
            MEM_B(p*6+2,output)=state.pads[p].stick_x;
            MEM_B(p*6+3,output)=state.pads[p].stick_y;
            MEM_B(p*6+4,output)=0; // Exact connected callback branch; padding is NOT written.
        }
        return 1; // The real void import preserves r2.
    }
    if(strcmp(name,"osContStartReadData_recomp")==0) {
        queue(rdram);
        // Retail Pak helpers can request another controller read several
        // times within one logical tick. This owner's outstanding next-sample
        // request is idempotent; it never generates extra physical samples or
        // fake completion messages. Available ONLY with the separate Pak owner.
        if(dkr_probe_paks_enabled() && (uint32_t)ctx->r4==si_queue() && state.requested &&
           !state.delivered && !state.ready && !MEM_W(8,address(si_queue()))) {ctx->r2=0;return 1;}
        if((uint32_t)ctx->r4!=si_queue() || !state.delivered || state.ready || state.requested ||
           MEM_W(8,address(si_queue()))) dkr_probe_block("controller-read-start-order");
        state.delivered=0; state.requested=1; ctx->r2=0; return 1;
    }
    if(strcmp(name,"dkr_netplay_resolve_authored_input_frame")==0) {
        // Experimental owner inputs were frozen BEFORE the retail tick. Do
        // not enter stable host-authority polling/waits or the physical broker.
        // This is an explicit alternative adapter boundary, not an assertion
        // that the production hook is pure or a generic netplay no-op.
        if(!state.delivered || state.ready || state.requested)
            dkr_probe_block("experimental-owner-input-boundary-order");
        return 1;
    }
    if(strcmp(name,"osPfsIsPlug_recomp")==0) {
        // Used only without the separate Pak owner. Preserve the same virtual
        // roster; this does not permit any physical save/Pak file access.
        const unsigned owners=dkr_probe_native_capture().owner_mask;
        range(rdram,address((uint32_t)ctx->r5),1);
        MEM_B(0,address((uint32_t)ctx->r5))=owners ? owners:15; ctx->r2=0; return 1;
    }
    if(strcmp(name,"osMotorInit_recomp")==0) {
        const gpr pfs=address((uint32_t)ctx->r5);
        if((uint32_t)ctx->r6>=4) dkr_probe_block("invalid-owned-rumble-channel");
        range(rdram,pfs,104);
        // input.cpp's native OSPfs layout: activebank is physical byte 102,
        // therefore guest byte 101 under the checked MIPS byte-lane macros.
        MEM_W(0,pfs)=8; MEM_W(4,pfs)=(uint32_t)ctx->r4; MEM_W(8,pfs)=(uint32_t)ctx->r6;
        MEM_B(101,pfs)=255; ctx->r2=0; return 1;
    }
    if(strcmp(name,"osMotorStart_recomp")==0 || strcmp(name,"osMotorStop_recomp")==0 ||
       strcmp(name,"__osMotorAccess_recomp")==0) {
        const gpr pfs=address((uint32_t)ctx->r4); range(rdram,pfs,12);
        if(!(MEM_W(0,pfs)&8)) { ctx->r2=5; return 1; }
        const unsigned channel=MEM_W(8,pfs);
        const unsigned flag=strcmp(name,"__osMotorAccess_recomp")==0 ? (uint32_t)ctx->r5 : strcmp(name,"osMotorStart_recomp")==0;
        if(channel>=4 || flag>1 || motor_count==128) dkr_probe_block("invalid-owned-rumble-intent");
        motors[motor_count++]=(dkr_probe_motor_event){channel,flag};
        state.motors=(state.motors&~(1U<<channel))|(flag<<channel); ctx->r2=0; return 1;
    }
    return 0;
}
void dkr_probe_input_begin_sample(uint8_t* rdram,recomp_context* ctx) {
    if(!enabled || !state.requested || state.ready || state.delivered)
        dkr_probe_block("controller-frame-not-ready");
    queue(rdram);
    // This private owner starts from a reviewed idle CPU fixture. No native
    // thread exists. Its own sample is made available once per LOGICAL tick;
    // replay never polls hardware or fabricates a live SI/SP/DP completion.
    memcpy(state.pads,arguments,sizeof(arguments));
    state.requested=0; state.ready=1; motor_count=0;
    MEM_W(8,address(si_queue()))=1; MEM_W(24,address(si_queue()))=0;
    if(test_policy && (flags&64)) {
        get_settings(rdram,ctx);
        MEM_B(0x4B,ctx->r2)=0;
        MEM_W(0x50,ctx->r2)=arguments[1].buttons;
    }
    if(test_policy && (arguments[1].buttons&0x2000)) rumble_kill(rdram,ctx);
}
void dkr_probe_input_sample_complete(void) {
    if(!enabled || !state.requested || state.ready || state.delivered)
        dkr_probe_block("retail-controller-frame-not-complete");
}
void dkr_probe_input_frame(uint8_t* rdram,recomp_context* ctx) {
    dkr_probe_enter("private-retail-controller-save-tick");
    dkr_probe_input_begin_sample(rdram,ctx);
    ctx->r4=flags; ctx->r5=2;
    input_update(rdram,ctx);
    if(ctx->r2) dkr_probe_block("retail-controller-save-flags-not-complete");
    dkr_probe_input_sample_complete();
    // main_game_loop stores the returned pending-save flags. Do not keep
    // replaying a completed EEPROM request on every later frame.
    MEM_W(0,address(DKR_PROBE_REVISION==77 ? 0x800DD37CU:0x800DD8ECU))=ctx->r2;
    dkr_probe_leave();
}
void dkr_probe_input_tick(uint8_t* rdram,recomp_context* ctx) {
    dkr_probe_input_frame(rdram,ctx);
    mempool_free_queue_clear(rdram,ctx);
}
