#include "recomp.h"
#include "funcs.h"
#include "probe_bridge.h"
#include "probe_audio.h"
#include "probe_input.h"
#include "probe_pak.h"
#include "probe_magic.h"
#include "../../src/game/camera_obstruction_guest.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static int offline;
static int canonical_presentation;
static int scene_cuts;
static int reset_poll;
static int audio_owned;
static unsigned scene_authorization;
static int confirmed_menu_tick;
static int inline_menu_preview;
static dkr_owned_draw_event draw_events[DKR_OWNED_MAX_DRAW_EVENTS];
static unsigned draw_count;
static int draw_invalid;
static int framed_scope, lens_scope, split_world_scope, panel_scope;
static int clear_scope, mosaic_scope, chequer_scope;
static unsigned scenery_viewmask;

void dkr_probe_draw_begin(void) {
    draw_count=0;draw_invalid=0;framed_scope=0;lens_scope=0;split_world_scope=0;panel_scope=0;
    clear_scope=mosaic_scope=chequer_scope=0;
    scenery_viewmask=0;
    dkr_probe_parity_begin();
}
static void parity_emit(const dkr_owned_draw_event* event) {
    if(draw_invalid)return;
    if(!dkr_owned_draw_event_valid(event)||draw_count>=DKR_OWNED_MAX_DRAW_EVENTS-1){draw_invalid=1;return;}
    draw_events[draw_count++]=*event;
}
static dkr_probe_native_state state;
static dkr_probe_scenery_observer scenery_observer;
static void* scenery_user;
void dkr_probe_scenery_observe(dkr_probe_scenery_observer observer,void* user) {
    scenery_observer=observer;scenery_user=user;
}
void dkr_probe_local_world_draw(uint8_t* rdram,recomp_context* ctx,unsigned pass) {
    if(!scenery_observer || !canonical_presentation || state.scene_unload_phase || pass>1)return;
    uint32_t a[11];dkr_probe_scenery_addresses(a);
    uint32_t pointer,slot,header,camera,model,segments,cutscene;
    if(!dkr_probe_diagnostic_read(rdram,a[0],4,&pointer) || pointer<0x80000000U || pointer>0x807FFFD8U || (pointer&7) ||
       !dkr_probe_diagnostic_read(rdram,a[1],4,&slot) || slot>3 ||
       !dkr_probe_diagnostic_read(rdram,a[2],4,&header) || header<0x80000000U || header>0x807FFFB0U ||
       !dkr_probe_diagnostic_read(rdram,a[3],4,&camera) || camera<0x80000000U || camera>0x807FFFE8U ||
       !dkr_probe_diagnostic_read(rdram,a[9],4,&model) || model<0x80000000U || model>0x807FFFB4U ||
       !dkr_probe_diagnostic_read(rdram,model+0x1A,2,&segments) || !segments || segments>=128 ||
       !dkr_probe_diagnostic_read(rdram,a[10],1,&cutscene) || cutscene>1)return;
    const unsigned view_bit=slot+pass*4U;
    if(scenery_viewmask&(1U<<view_bit))return;
    dkr_owned_draw_event event={DKR_OWNED_LOCAL_SCENERY,pointer,slot,{0}};
    event.parameters[0]=state.presentation.scene;
    if(!dkr_probe_diagnostic_read(rdram,header+0x4C,1,&event.parameters[1]))return;
    for(unsigned i=0;i<3;++i)
        if(!dkr_probe_diagnostic_read(rdram,camera+12+i*4,4,&event.parameters[2+i]))return;
    if(!dkr_probe_diagnostic_read(rdram,camera+4,2,&event.parameters[5]) ||
       !dkr_probe_diagnostic_read(rdram,a[5],4,&event.parameters[6]) ||
       !dkr_probe_diagnostic_read(rdram,a[6],4,&event.parameters[7]))return;
    if(event.parameters[6]>1)return;
    // This observation is inserted at the pinned render caller, after opaque
    // region visibility was built and before sorting mutates the actor list.
    // Copy the flags, never retain a pointer into the guest stack.
    const uint32_t stack=(uint32_t)ctx->r29;
    if(stack<0x80000000U || stack>0x807FFE90U)return;
    event.parameters[8]=segments;
    for(unsigned segment=0;segment<segments;++segment) {
        uint32_t visible;
        if(!dkr_probe_diagnostic_read(rdram,stack+0x59+segment,1,&visible) || visible>1)return;
        event.parameters[9+segment/32]|=visible<<(segment&31U);
    }
    const unsigned camera_slot=slot+(cutscene?4U:0U);
    event.parameters[13]=camera_slot;
    event.parameters[14]=state.presentation.cameras[camera_slot].epoch;
    if(!event.parameters[14])return;
    event.parameters[15]=pass;
    scenery_viewmask|=1U<<view_bit;
    parity_emit(&event);
}
/* Deliberately outside dkr_probe_native_state: diagnostics cannot become a
   checkpoint, journal, prediction decision or gameplay input. The owned C
   trampoline serializes access. No thread, signal handler or file is added. */
static struct {
    uint64_t epoch, armed_epoch;
    uint32_t frame, armed_frame, racer, timer, settings;
    unsigned ticks_left, lines_left, elapsed, failure_logged;
    const char* stage;
} boss_diagnostic;

void dkr_probe_boss_diagnostic_tick(uint64_t epoch,uint32_t frame) {
    if(boss_diagnostic.epoch!=epoch || boss_diagnostic.frame!=frame) {
        if(boss_diagnostic.ticks_left) {
            --boss_diagnostic.ticks_left;
            ++boss_diagnostic.elapsed;
        }
    }
    boss_diagnostic.epoch=epoch;boss_diagnostic.frame=frame;
}
static uint32_t boss_read(uint8_t* ram,uint32_t address,unsigned width) {
    uint32_t value=UINT32_MAX;
    if(!dkr_probe_diagnostic_read(ram,address,width,&value))return UINT32_MAX;
    return value;
}
static void boss_snapshot(const char* stage,uint8_t* ram,recomp_context* c) {
    uint32_t a[8],v[8],valid=0;
    const unsigned widths[8]={4,4,4,2,2,4,4,1};
    dkr_probe_boss_diagnostic_addresses(a);
    for(unsigned i=0;i<8;++i) {
        v[i]=UINT32_MAX;
        if(dkr_probe_diagnostic_read(ram,a[i],widths[i],&v[i]))valid|=1U<<i;
    }
    const uint32_t header_type=v[2]<=UINT32_MAX-0x4C ? boss_read(ram,v[2]+0x4C,1):UINT32_MAX;
    const uint32_t finish=boss_diagnostic.racer<=UINT32_MAX-0x1AC ?
        boss_read(ram,boss_diagnostic.racer+0x1AC,2):UINT32_MAX;
    const uint32_t settings=boss_diagnostic.settings;
    const int valid_settings=settings>=0x80000000U && settings<=UINT32_MAX-0x49;
    fprintf(stderr,"[rollback][boss-finish] epoch=%llu frame=%u stage=%s valid=%02x "
        "mode=%u map=%u type=%u load=%d race-end=%d racers=%u finished=%u music-next=%u "
        "racer=%08x timer=%08x timer-value=%u finish=%u settings=%08x bosses=%u world=%u course=%u "
        "pending=%u unload-phase=%u video=%u a0=%08x a1=%08x a2=%08x a3=%08x v0=%08x sp=%08x ra=%08x\n",
        (unsigned long long)boss_diagnostic.epoch,boss_diagnostic.frame,stage,valid,
        v[0],v[1],header_type,(int)(int16_t)v[3],(int)(int16_t)v[4],v[5],v[6],v[7],
        boss_diagnostic.racer,boss_diagnostic.timer,boss_read(ram,boss_diagnostic.timer,1),finish,settings,
        valid_settings?boss_read(ram,settings+0xC,2):UINT32_MAX,
        valid_settings?boss_read(ram,settings+0x48,1):UINT32_MAX,
        valid_settings?boss_read(ram,settings+0x49,1):UINT32_MAX,
        state.pending_scene_site,state.scene_unload_phase,state.video_frames,
        c?(uint32_t)c->r4:0,c?(uint32_t)c->r5:0,c?(uint32_t)c->r6:0,c?(uint32_t)c->r7:0,
        c?(uint32_t)c->r2:0,c?(uint32_t)c->r29:0,c?(uint32_t)c->r31:0);
    fflush(stderr); // Keep the last successful milestone if native code crashes.
}
void dkr_probe_boss_trace(const char* stage,uint8_t* ram,recomp_context* c) {
    if(!stage || !c)return;
    if(strcmp(stage,"begin")==0) {
        uint32_t timer=0;
        const int valid=dkr_probe_diagnostic_read(ram,(uint32_t)c->r5,1,&timer);
        const int same_finish=boss_diagnostic.armed_epoch==boss_diagnostic.epoch &&
            boss_diagnostic.armed_frame==boss_diagnostic.frame && boss_diagnostic.racer==(uint32_t)c->r4;
        if((!valid || timer==1) && !same_finish && !boss_diagnostic.ticks_left) {
            boss_diagnostic.racer=(uint32_t)c->r4;boss_diagnostic.timer=(uint32_t)c->r5;
            boss_diagnostic.settings=0;boss_diagnostic.armed_epoch=boss_diagnostic.epoch;
            boss_diagnostic.armed_frame=boss_diagnostic.frame;
            boss_diagnostic.ticks_left=300;boss_diagnostic.lines_left=160;
            boss_diagnostic.elapsed=0;boss_diagnostic.failure_logged=0;
        }
    }
    if(!boss_diagnostic.ticks_left)return;
    boss_diagnostic.stage=stage;
    if(strcmp(stage,"after-get_settings")==0)boss_diagnostic.settings=(uint32_t)c->r2;
    // Every milestone on the finish tick, then a sparse audio/transition tail.
    // Replay is bounded too: exhausting the logging cap never stops gameplay.
    if(!boss_diagnostic.lines_left || (boss_diagnostic.elapsed>2 && boss_diagnostic.elapsed%15))return;
    --boss_diagnostic.lines_left;
    boss_snapshot(stage,ram,c);
}
void dkr_probe_boss_diagnostic_failure(const dkr_probe_result* result,recomp_context* c) {
    if(!boss_diagnostic.ticks_left || boss_diagnostic.failure_logged || !result)return;
    boss_diagnostic.failure_logged=1;
    fprintf(stderr,"[rollback][boss-finish-failure] epoch=%llu frame=%u last-stage=%s "
        "blocked=%s address=%016llx source=%s:%u operations=%llu memory=%llu entries=%llu depth=%u\n",
        (unsigned long long)boss_diagnostic.epoch,boss_diagnostic.frame,
        boss_diagnostic.stage?boss_diagnostic.stage:"none",result->blocked?result->blocked:"none",
        (unsigned long long)result->bad_address,result->source_file?result->source_file:"native-boundary",
        result->source_line,(unsigned long long)result->operations,
        (unsigned long long)result->memory_accesses,(unsigned long long)result->guest_entries,result->stack_depth);
    if(c)fprintf(stderr,"[rollback][boss-finish-registers] s0=%016llx s1=%016llx s2=%016llx s3=%016llx "
        "s7=%016llx t6=%016llx t8=%016llx sp=%016llx ra=%016llx\n",
        (unsigned long long)c->r16,(unsigned long long)c->r17,(unsigned long long)c->r18,
        (unsigned long long)c->r19,(unsigned long long)c->r23,(unsigned long long)c->r14,
        (unsigned long long)c->r24,(unsigned long long)c->r29,(unsigned long long)c->r31);
    for(unsigned i=0;i<result->stack_depth && i<128;++i)
        fprintf(stderr,"[rollback][boss-finish-stack] %u %s\n",i,result->stack[i]?result->stack[i]:"unknown");
    fflush(stderr);
}
unsigned dkr_probe_draw_capture(dkr_owned_draw_event* events,unsigned capacity) {
    if(draw_invalid)return 0; // Never publish a truncated scope transaction.
    dkr_owned_draw_event metadata={DKR_OWNED_FRAME_METADATA,0x80000000U,0,{0}};
    metadata.parameters[0]=state.presentation.scene;
    if(draw_count<DKR_OWNED_MAX_DRAW_EVENTS&&(!draw_count||draw_events[draw_count-1].kind!=DKR_OWNED_FRAME_METADATA))draw_events[draw_count++]=metadata;
    if(!events || capacity<draw_count)return draw_count; // Bounded count query; never a null memcpy.
    memcpy(events,draw_events,draw_count*sizeof(*events));return draw_count;
}
static void draw_event(unsigned kind,uint32_t address,unsigned token) {
    if(!address)return;
    const dkr_owned_draw_event event={kind,address,token,{0}};
    // Presentation observations are optional, not a simulation service.
    // Discard the entire sidecar on overflow instead of stopping the match
    // or publishing a partial begin/end scope transaction.
    parity_emit(&event);
}
static dkr_probe_effect effects[DKR_PROBE_MAX_OBJECT_EFFECTS];
static unsigned effect_count;
static const uint8_t* rom;
static size_t rom_bytes;
void dkr_probe_rom(const uint8_t* image, size_t bytes) { rom=image; rom_bytes=bytes; }

static gpr guest_address(uint32_t value) { return (gpr)(int32_t)value; }
static uint32_t dma_queue(void) { return DKR_PROBE_REVISION==77 ? 0x80124220U : 0x801247A0U; }
static uint32_t mutex_queue(void) { return DKR_PROBE_REVISION==80 ? 0x80124818U : 0; }
static uint32_t audio_dma_queue(void) { return DKR_PROBE_REVISION==77 ? 0x80119AF0U : 0x8011A070U; }
/* ultra64.h's OSMesgQueue and mesgqueue.cpp's uncontended operations. Only
   DKR's owned asset queues are registered here. No audio/VI/SP/DP completion,
   blocked thread, host event or scheduler continuation is fabricated. The
   queue counters/message words live in the checkpointed RAM. */
static void check_queue(uint8_t* rdram, uint32_t address) {
    const int audio=audio_owned && address==audio_dma_queue();
    if (address!=dma_queue() && (!mutex_queue() || address!=mutex_queue()) && !audio)
        dkr_probe_block("unowned-asset-message-queue");
    const gpr queue=guest_address(address);
    if (MEM_W(0,queue) || MEM_W(4,queue)) dkr_probe_block("asset-queue-needs-scheduler");
    const int count=MEM_W(16,queue),valid=MEM_W(8,queue),first=MEM_W(12,queue);
    if(count!=(audio ? 50:1) || valid<0 || valid>count || first<0 || first>=count)
        dkr_probe_block("invalid-asset-queue-state");
    // Validate the message buffer before either the DMA or queue state changes.
    dkr_probe_memory(rdram,guest_address((uint32_t)MEM_W(20,queue)),4,0);
    if(audio) {
        if((uint32_t)MEM_W(20,queue)!=address+24) dkr_probe_block("unowned-audio-dma-message-buffer");
        dkr_probe_memory(rdram,guest_address((uint32_t)MEM_W(20,queue))+196,4,0);
    }
}
static void queue_send(uint8_t* rdram,uint32_t address,uint32_t message) {
    check_queue(rdram,address);
    const gpr queue=guest_address(address);
    if(MEM_W(8,queue)==MEM_W(16,queue)) dkr_probe_block("asset-queue-full");
    const unsigned last=(MEM_W(12,queue)+MEM_W(8,queue)) % MEM_W(16,queue);
    MEM_W(last*4,guest_address((uint32_t)MEM_W(20,queue)))=message;
    ++MEM_W(8,queue);
}
static void queue_receive(uint8_t* rdram,recomp_context* context) {
    if(dkr_probe_input_service("osRecvMesg_recomp",rdram,context)) return;
    const uint32_t address=(uint32_t)context->r4;
    const uint32_t reset=DKR_PROBE_REVISION==77 ? 0x80123548U : 0x80123AC8U;
    if(reset_poll && address==reset) {
        const gpr queue=guest_address(address);
        // thread3_main initializes a capacity-one pre-NMI queue. This private
        // world has no reset producer and cannot consume native events. Exact
        // empty nonblocking poll returns -1, never fabricated completion.
        if(context->r5 || context->r6 || MEM_W(0,queue) || MEM_W(4,queue) ||
           MEM_W(8,queue) || MEM_W(12,queue) || MEM_W(16,queue)!=1 ||
           (uint32_t)MEM_W(20,queue)!=reset-4)
            dkr_probe_block("unowned-reset-poll-state");
        context->r2=(gpr)(int32_t)-1; return;
    }
    check_queue(rdram,address);
    const gpr queue=guest_address(address);
    if((uint32_t)context->r6>1) dkr_probe_block("invalid-asset-queue-flags");
    if(!MEM_W(8,queue)) {
        if(context->r6) dkr_probe_block("asset-queue-would-block");
        context->r2=(gpr)(int32_t)-1; return;
    }
    if(context->r5) MEM_W(0,guest_address((uint32_t)context->r5))=
        MEM_W(MEM_W(12,queue)*4,guest_address((uint32_t)MEM_W(20,queue)));
    MEM_W(12,queue)=(MEM_W(12,queue)+1)%MEM_W(16,queue);
    --MEM_W(8,queue);
    context->r2=0;
}
static void rom_dma(uint8_t* rdram,recomp_context* context) {
    /* librecomp/src/pi.cpp: osPiStartDma's register/stack ABI; its IO message
       and priority arguments are unused in that implementation. Asset reads
       are synchronous copies followed by PI completion. Here the completion
       belongs solely to the same private world and never an external worker. */
    const uint32_t address=(uint32_t)MEM_W(0x18,context->r29);
    const uint32_t destination=(uint32_t)MEM_W(0x10,context->r29);
    const uint32_t size=(uint32_t)MEM_W(0x14,context->r29);
    const uint32_t physical=((uint32_t)context->r7|0x10000000U)&0x1FFFFFFFU;
    const uint32_t offset=physical-0x10000000U;
    if(!rom || !rom_bytes) dkr_probe_block("immutable-retail-rom-not-installed");
    const int virtual_asset=(((uint32_t)context->r7&0xc0000000U)==0x40000000U);
    if(context->r6 || (destination&7) || !size || size>0x5000 ||
       (!virtual_asset && (physical<0x10000000U || (physical&1) || offset>rom_bytes || size>rom_bytes-offset)))
        dkr_probe_block("invalid-owned-rom-dma");
    const int audio=audio_owned && address==audio_dma_queue();
    if(address!=dma_queue() && !audio) dkr_probe_block("unowned-rom-dma-completion");
    if(audio && size!=0x400) dkr_probe_block("unreviewed-audio-rom-dma-size");
    check_queue(rdram,address);
    if(MEM_W(8,guest_address(address))==(audio ? 50:1)) dkr_probe_block("rom-dma-completion-still-pending");
    // Complete validation before copying; no partial copy of a corrupt range.
    const gpr target=guest_address(destination);
    dkr_probe_memory(rdram,target,1,3);
    dkr_probe_memory(rdram,target+size-1,1,3);
    if(virtual_asset) {
        // Resolve the tagged namespace BEFORE physical cartridge masking.
        // All queue/destination validation above remains on the C trap side;
        // the C++ resource owner must unwind before a failure is raised here.
        const uint64_t args[3]={(uint32_t)context->r7,destination,size};
        uint64_t result=0;
        if(!dkr_probe_mod_dispatch("dkr_owned_asset_dma",rdram,context,args,3,0,0,&result))
            dkr_probe_block("unowned-virtual-asset-dma");
    } else for(unsigned i=0;i<size;++i) MEM_B(i,target)=rom[offset+i];
    // librecomp/src/pi.cpp::do_dma sends the literal zero for a ROM read.
    // This completion belongs to the private synchronous copy, not a worker.
    queue_send(rdram,address,0);
    context->r2=0;
}
void dkr_probe_profile_disabled(const char* name) {
    dkr_probe_checkpoint();
    if (!offline) dkr_probe_block(name);
    // Exact diagnostic-disabled branch in water_profile.cpp. This standalone
    // process has no timing profiler, renderer or running game's timer state.
}
uint64_t dkr_probe_cop0_read(struct recomp_context* context) {
    dkr_probe_checkpoint();
    if (!offline) dkr_probe_block("cop0_status_read");
    return (gpr)(int32_t)context->status_reg;
}
void dkr_probe_cop0_write(struct recomp_context* context, uint64_t value) {
    dkr_probe_checkpoint();
    if (!offline) dkr_probe_block("cop0_status_write");
    const uint32_t next = (uint32_t)value;
    const uint32_t changed = context->status_reg ^ next;
    /* Identical register semantics to librecomp/src/recomp.cpp:410-446.
       Do not pretend this controls a host interrupt/scheduler or grant a
       general native-operation escape. All of this state is checkpointed. */
    if (changed & ~UINT32_C(0x04000000)) dkr_probe_block("unhandled-cop0-status-change");
    if (changed & UINT32_C(0x04000000)) {
        context->mips3_float_mode = (next & UINT32_C(0x04000000)) != 0;
        context->f_odd = context->mips3_float_mode ? &context->f1.u32l : &context->f0.u32h;
    }
    context->status_reg = next;
}
void dkr_probe_effects_begin(void) { effect_count = 0; }
unsigned dkr_probe_effects_capture(dkr_probe_effect* output, unsigned capacity) {
    if (capacity < effect_count) return UINT32_MAX;
    memcpy(output, effects, effect_count * sizeof(*effects));
    return effect_count;
}
static void object_effect(uint32_t kind, uint32_t object) {
    if (effect_count == DKR_PROBE_MAX_OBJECT_EFFECTS) dkr_probe_block("object-effect-budget");
    effects[effect_count++] = (dkr_probe_effect){kind, object};
}
void dkr_probe_offline_services(int enabled) {
    offline = enabled != 0; memset(&state, 0, sizeof(state));
    memset(&boss_diagnostic,0,sizeof(boss_diagnostic));
    canonical_presentation=0;
    scene_cuts=0; scene_authorization=0; confirmed_menu_tick=0; inline_menu_preview=0;
    reset_poll=0;
    audio_owned=0;
    state.requested_table=state.load_section=UINT32_MAX;
    dkr_probe_parity_scene(&state.presentation);
}
void dkr_probe_canonical_presentation(int enabled) { canonical_presentation=offline && enabled; }
void dkr_probe_reset_poll_configure(int enabled) { reset_poll=offline && enabled; }
void dkr_probe_audio_configure(int enabled) { audio_owned=offline && enabled; }
int dkr_probe_audio_enabled(void) { return audio_owned; }
static uint64_t clock_base(void) { return ((uint64_t)state.clock_base_hi<<32)|state.clock_base_lo; }
static uint64_t clock_count(void) { return ((uint64_t)state.clock_count_hi<<32)|state.clock_count_lo; }
void dkr_probe_clock_start(uint64_t count) {
    state.clock_enabled=offline!=0;
    state.clock_base_hi=state.clock_count_hi=(uint32_t)(count>>32);
    state.clock_base_lo=state.clock_count_lo=(uint32_t)count;
    state.clock_offset_hi=state.clock_offset_lo=0;
}
static uint64_t clock_read(void) {
    // timer.cpp uses 46,875,000 Count ticks per second. A retail US logic
    // tick has two 60 Hz VIs: exactly 1,562,500 Count ticks. Reads progress
    // deterministically within that frame, so music_animation_fraction never
    // sees two identical counts and takes its unsigned wraparound branch.
    // A wall-clock hitch/replay does not move this clock. The next frame's
    // deadline is independent of how many counter reads the guest executed.
    const uint64_t base=clock_base(), count=clock_count();
    if(!state.clock_enabled || base>UINT64_MAX-1562500 || count<base || count>=base+1562500)
        dkr_probe_block("unowned-or-exhausted-guest-clock");
    const uint64_t next=count+1;
    state.clock_count_hi=(uint32_t)(next>>32); state.clock_count_lo=(uint32_t)next;
    return next;
}
void dkr_probe_clock_advance(void) {
    const uint64_t base=clock_base(), count=clock_count();
    if(!offline || !state.clock_enabled || base>UINT64_MAX-3125000 || count<base || count>base+1562500)
        dkr_probe_block("invalid-guest-clock-frame");
    const uint64_t next=base+1562500;
    state.clock_base_hi=state.clock_count_hi=(uint32_t)(next>>32);
    state.clock_base_lo=state.clock_count_lo=(uint32_t)next;
}
static void video_roles_valid(uint8_t* rdram) {
    const gpr base=guest_address(DKR_PROBE_REVISION==77 ? 0x801262C0U:0x80126860U);
    const gpr depth=guest_address(DKR_PROBE_REVISION==77 ? 0x800DE770U:0x800DECF0U);
    if(!offline || !canonical_presentation || !state.clock_enabled ||
       (unsigned)MEM_W(8,base)>1 || (unsigned)MEM_W(16,base)>12)
        dkr_probe_block("invalid-owned-video-state");
    for(unsigned i=0;i<2;++i) {
        const uint32_t p=(uint32_t)MEM_W(i*4,base);
        if(p<0x80000000U || p>0x80800000U-320U*240U*2U || (p&7))
            dkr_probe_block("invalid-owned-video-buffer");
    }
    const uint32_t z=(uint32_t)MEM_W(0,depth);
    if(z<0x80000000U || z>0x80800000U-320U*240U*2U || (z&7))
        dkr_probe_block("invalid-owned-video-depth");
    // Last is the CPU draw target, not the next swapped VI buffer.
    if((uint32_t)MEM_W(24,base)!=(uint32_t)MEM_W(0,base) &&
       (uint32_t)MEM_W(24,base)!=(uint32_t)MEM_W(4,base))
        dkr_probe_block("foreign-owned-video-draw-target");
    if((uint32_t)MEM_W(32,base)!=z)dkr_probe_block("foreign-owned-video-depth-target");
}
void dkr_probe_video_start(uint8_t* rdram) {
    video_roles_valid(rdram);
    const gpr base=guest_address(DKR_PROBE_REVISION==77 ? 0x801262C0U:0x80126860U);
    state.video_enabled=1;state.video_black=MEM_W(16,base)!=0;state.video_frames=0;
    state.video_framebuffer=state.video_depthbuffer=0;
}
void dkr_probe_video_begin_frame(uint8_t* rdram) {
    if(!state.video_enabled || state.video_frames==UINT32_MAX)dkr_probe_block("unowned-video-frame");
    video_roles_valid(rdram);
    const gpr base=guest_address(DKR_PROBE_REVISION==77 ? 0x801262C0U:0x80126860U);
    state.video_framebuffer=(uint32_t)MEM_W(24,base);state.video_depthbuffer=(uint32_t)MEM_W(32,base);
    state.postrace_pending=0;
}
void dkr_probe_video_finish_frame(void) {
    if(!state.video_enabled || state.video_black>1 || state.video_frames==UINT32_MAX)
        dkr_probe_block("invalid-owned-video-frame-end");
    ++state.video_frames;
    if(!state.video_black && !state.pending_scene_site)
        state.postrace_contracted|=state.postrace_pending;
    state.postrace_pending=0;
}
void dkr_probe_scene_configure(int enabled) { scene_cuts=offline && enabled; }
unsigned dkr_probe_scene_pending(void) { return state.pending_scene_site; }
int dkr_probe_scene_authorize(unsigned site) {
    if(!offline || !canonical_presentation || !scene_cuts || !site || site>DKR_PROBE_SCENE_SITE_MAX ||
       scene_authorization || state.pending_scene_site!=site) return 0;
    scene_authorization=site; return 1;
}
int dkr_probe_scene_unload_authorize(unsigned site) {
    if(state.scene_unload_phase || !state.video_enabled || !dkr_probe_scene_authorize(site))return 0;
    return 1;
}
void dkr_probe_scene_unload_begin(void) {
    dkr_probe_checkpoint();
    if(DKR_PROBE_HAS_FULL_SCENES && offline && canonical_presentation && scene_cuts &&
       state.video_enabled && !scene_authorization &&
       ((confirmed_menu_tick && !state.pending_scene_site && !state.scene_unload_phase) ||
        (state.pending_scene_site && state.scene_unload_phase==4))) {
        // A confirmed constructor may replace its initial background again
        // inside menu_init. Each actual teardown still runs, after the owner
        // has drained presentation; this is not a second speculative cut.
        state.scene_load_resets=0;state.scene_unload_phase=1;return;
    }
    if(!offline || !canonical_presentation || !scene_cuts || !state.video_enabled ||
       state.scene_unload_phase || !state.pending_scene_site ||
       scene_authorization!=state.pending_scene_site)
        dkr_probe_block("scene-unload-not-confirmed-and-drained");
    scene_authorization=0;state.scene_unload_phase=1;
}
void dkr_probe_scene_unload_render_drained(void) {
    dkr_probe_checkpoint();
    // This private target never submits a GP task. Its owner additionally
    // refuses admission while immutable consumers retain old scene bytes.
    if(!offline || !canonical_presentation || state.scene_unload_phase!=1)
        dkr_probe_block("unowned-scene-render-drain");
}
void dkr_probe_scene_unload_end(void) {
    dkr_probe_scene_unload_render_drained();state.scene_unload_phase=2;
}
void dkr_probe_scene_load_begin(uint8_t* rdram,recomp_context* ctx) {
    dkr_probe_checkpoint();
    if(!offline || !canonical_presentation || !scene_cuts || !state.video_enabled ||
       state.scene_unload_phase!=2 || (!state.pending_scene_site && !confirmed_menu_tick) || scene_authorization ||
       state.scene_load_resets || (uint32_t)ctx->r5>3 ||
       (uint32_t)ctx->r6>255 || (uint32_t)ctx->r7>2 || !dkr_probe_input_enabled() ||
       !dkr_probe_eeprom_enabled() || !dkr_probe_paks_enabled() || !dkr_probe_magic_enabled())
        dkr_probe_block("unowned-confirmed-scene-constructor");
    uint64_t admission=0;const uint64_t scene_args[]={(uint32_t)ctx->r4};
    const int mod_owned=dkr_probe_mod_dispatch("dkr_owned_scene_admission",rdram,ctx,scene_args,1,0,0,&admission);
    if(mod_owned ? !admission : (uint32_t)ctx->r4>=DKR_PROBE_SCENE_LEVEL_COUNT)
        dkr_probe_block("unadmitted-confirmed-scene-id");
    // Reviewed no-custom-track branch in custom_tracks_hooks.cpp. A retail
    // table needs no write or allocator invalidation. Refuse a changed table,
    // rather than silently undoing the user's .dkrmap display-list budget.
    const gpr table=guest_address(DKR_PROBE_REVISION==77 ? 0x800DD3B0U:0x800DD920U);
    const int32_t sizes[4]={4500,7000,11000,11000};
    for(unsigned i=0;!mod_owned && i<4;++i)if(MEM_W(i*4,table)!=sizes[i])
        dkr_probe_block("non-retail-scene-display-list-budget");
    state.scene_unload_phase=3;
}
void dkr_probe_menu_construct_begin(void) {
    dkr_probe_checkpoint();
    // init_game creates display heaps/video but has not loaded ANY level.
    // The retail INTRO's first menu constructor therefore has no previous
    // scene to destroy. This capability is captured and consumed once; it
    // cannot authorize subsequent loads or masquerade as a completed teardown.
    if(state.initial_world_vacant && confirmed_menu_tick && !state.pending_scene_site &&
       !scene_authorization && !state.scene_unload_phase && !state.scene_load_resets)
        state.scene_unload_phase=2;
    if(!DKR_PROBE_HAS_FULL_SCENES || !offline || !canonical_presentation || !scene_cuts ||
       !state.video_enabled || state.scene_unload_phase!=2 || (!state.pending_scene_site && !confirmed_menu_tick) ||
       scene_authorization || state.scene_load_resets)
        dkr_probe_block("unowned-confirmed-menu-constructor");
}
void dkr_probe_menu_load_begin(uint8_t* rdram,recomp_context* ctx) {
    dkr_probe_menu_construct_begin();
    // Retail menu backgrounds pass -1 to mean no playable racers. Never
    // rewrite the guest's argument to zero: its constructor depends on it.
    if((int32_t)ctx->r5 < -1 || (int32_t)ctx->r5 > 3)
        dkr_probe_block("invalid-owned-menu-player-count");
    const gpr players=ctx->r5;
    ctx->r5=0;
    dkr_probe_scene_load_begin(rdram,ctx);
    ctx->r5=players;
}
static int menu_without_level(uint8_t* rdram) {
    const gpr loading=guest_address(DKR_PROBE_REVISION==77 ? 0x80123514U:0x80123A94U);
    const gpr mode=guest_address(DKR_PROBE_REVISION==77 ? 0x801234ECU:0x80123A6CU);
    return DKR_PROBE_HAS_FULL_SCENES && MEM_BU(0,loading)==1 && MEM_W(0,mode)==1;
}
void dkr_probe_menu_construct_ready(uint8_t* rdram,recomp_context* ctx) {
    (void)ctx;dkr_probe_checkpoint();
    if(!DKR_PROBE_HAS_FULL_SCENES || !offline || !canonical_presentation || !scene_cuts ||
       (!state.pending_scene_site && !confirmed_menu_tick) || scene_authorization)
        dkr_probe_block("unowned-confirmed-menu-ready");
    // A no-background menu still runs alloc_displaylist_heap and menu_init.
    // It must not fake level_load/reset/allocation completion.
    if(state.scene_unload_phase==2 && !state.scene_load_resets && menu_without_level(rdram))
        state.scene_unload_phase=4;
    else if(state.scene_unload_phase!=4 || state.scene_load_resets!=1)
        dkr_probe_block("incomplete-owned-menu-background");
    // Even a genuine first menu without a background consumes cold-boot
    // admission. Later constructors must perform their normal teardown.
    state.initial_world_vacant=0;
}
void dkr_probe_menu_tick_configure(int enabled) {
    confirmed_menu_tick=DKR_PROBE_HAS_FULL_SCENES && offline && canonical_presentation && scene_cuts && enabled;
    if(!confirmed_menu_tick)inline_menu_preview=0;
}
void dkr_probe_menu_preview_configure(int enabled) {
    inline_menu_preview=confirmed_menu_tick && enabled;
}
int dkr_probe_menu_callback_enabled(void) {
    return DKR_PROBE_HAS_FULL_SCENES && offline && canonical_presentation && scene_cuts &&
        (confirmed_menu_tick || (state.pending_scene_site && state.scene_unload_phase==4));
}
void dkr_probe_menu_tick_finish(void) {
    dkr_probe_checkpoint();
    if(!confirmed_menu_tick || scene_authorization ||
       (state.scene_unload_phase && state.scene_unload_phase!=4) ||
       (state.scene_unload_phase && state.pending_scene_site))
        dkr_probe_block("incomplete-confirmed-menu-tick");
    state.scene_unload_phase=0;state.scene_load_resets=0;
}
void dkr_probe_menu_level_change_begin(uint8_t* rdram,recomp_context* ctx) {
    (void)ctx;dkr_probe_checkpoint();
    const int background=state.pending_scene_site==7 && !state.scene_unload_phase && scene_authorization==7;
    const int construction=state.pending_scene_site && !scene_authorization &&
        (state.scene_unload_phase==2 || state.scene_unload_phase==4);
    if(!DKR_PROBE_HAS_FULL_SCENES || !offline || !canonical_presentation || !state.video_enabled ||
       (!confirmed_menu_tick && !background && !construction))
        dkr_probe_block("menu-level-change-not-confirmed-and-owned");
    const gpr loading=guest_address(DKR_PROBE_REVISION==77 ? 0x80123514U:0x80123A94U);
    // The retail no-level branch deliberately skips teardown. Take resource
    // ownership without claiming to have run a destructor on absent geometry.
    if(MEM_BU(0,loading)) {
        if(background) {dkr_probe_scene_unload_begin();dkr_probe_scene_unload_end();}
        else if(!state.scene_unload_phase || state.scene_unload_phase==4) {
            state.scene_unload_phase=2;state.scene_load_resets=0;
        }
    }
}
void dkr_probe_menu_level_change_ready(uint8_t* rdram,recomp_context* ctx) {
    (void)ctx;dkr_probe_checkpoint();
    const gpr loading=guest_address(DKR_PROBE_REVISION==77 ? 0x80123514U:0x80123A94U);
    if(!DKR_PROBE_HAS_FULL_SCENES || (!confirmed_menu_tick && !state.pending_scene_site) || scene_authorization)
        dkr_probe_block("unowned-menu-level-change-result");
    if(MEM_BU(0,loading)) {
        if(state.scene_unload_phase!=2 || state.scene_load_resets)
            dkr_probe_block("incomplete-no-level-menu-change");
        state.scene_unload_phase=4;
    } else if(state.scene_unload_phase!=4 || state.scene_load_resets!=1)
        dkr_probe_block("incomplete-menu-level-change");
    if(confirmed_menu_tick) {state.scene_unload_phase=0;state.scene_load_resets=0;}
}
static gpr background_globals(void) {return guest_address(DKR_PROBE_REVISION==77 ? 0x800E3770U:0x800E3D00U);}
static int inline_track_preview(uint8_t* rdram) {
    if(!inline_menu_preview || !confirmed_menu_tick)return 0;
    const gpr mode=guest_address(DKR_PROBE_REVISION==77 ? 0x801234ECU:0x80123A6CU);
    const gpr menu=guest_address(DKR_PROBE_REVISION==77 ? 0x800DF470U:0x800DF9F0U);
    return MEM_W(0,mode)==1 && MEM_W(0,menu)==15;
}
void dkr_probe_menu_preview_begin(uint8_t* rdram,recomp_context* ctx) {
    if(!inline_track_preview(rdram) || state.pending_scene_site)return;
    const gpr globals=background_globals();
    if(MEM_W(0,globals)!=1 || MEM_W(12,globals))return;
    dkr_probe_checkpoint();
    if(state.scene_unload_phase || scene_authorization || !state.video_enabled || state.preview_loads==UINT32_MAX)
        dkr_probe_block("unowned-confirmed-track-preview");
    // Run the actual thread30 constructor on its separate guest stack BEFORE
    // video_begin/main draws. Loading after draw would publish old display
    // commands against freed/reused geometry. Prior GPU work owns independent
    // copied RAM and can finish normally; no completion is fabricated here.
    recomp_context background=*ctx;
    background.f_odd=background.mips3_float_mode ? &background.f1.u32l:&background.f0.u32h;
    background.r29=guest_address(0x80FE0000U);
    background.r4=MEM_W(4,globals);background.r5=guest_address(UINT32_MAX);background.r6=MEM_W(8,globals);
    load_level_for_menu(rdram,&background);
    if(background.r29!=guest_address(0x80FE0000U) || state.scene_unload_phase || state.scene_load_resets ||
       state.pending_scene_site || scene_authorization)
        dkr_probe_block("incomplete-confirmed-track-preview");
    // Publish request completion only after the real constructor succeeds.
    // This counter is checkpointed; its consumer retires visual history only,
    // not input/audio history or the peers' continuously confirmed frontier.
    MEM_W(0,globals)=0;
    ++state.preview_loads;
}
void dkr_probe_menu_background_request(uint8_t* rdram,recomp_context* ctx) {
    dkr_probe_checkpoint();
    const gpr globals=background_globals();
    const uint32_t queue=DKR_PROBE_REVISION==77 ? 0x8012ACA0U:0x8012B260U;
    if(!confirmed_menu_tick || !state.video_enabled || state.pending_scene_site || state.scene_unload_phase ||
       (uint32_t)ctx->r4!=queue || ctx->r5!=10 || ctx->r6 || MEM_W(0,globals)!=1 || MEM_W(12,globals))
        dkr_probe_block("unowned-menu-background-request");
    // No worker is signalled here. The request remains outstanding in owned
    // guest RAM until confirmed admission executes its real constructor.
    ctx->r2=0;
}
void dkr_probe_menu_background_cut(uint8_t* rdram) {
    if(!confirmed_menu_tick || state.pending_scene_site)return;
    // Track previews use the next fully confirmed tick. All other menu loads,
    // gameplay transitions and the conservative private proof retain epochs.
    if(inline_track_preview(rdram))return;
    const gpr globals=background_globals();
    if(MEM_W(0,globals)==1 && MEM_W(12,globals)==0) {
        if(state.scene_unload_phase)dkr_probe_block("background-request-during-menu-construction");
        if(dkr_probe_scene_cut(7)!=1)dkr_probe_block("background-request-not-deferred");
    }
}
void dkr_probe_menu_tick_end(uint8_t* rdram) {
    if(!confirmed_menu_tick)return;
    dkr_probe_menu_tick_finish();
    dkr_probe_menu_background_cut(rdram);
}
void dkr_probe_menu_background_resume(uint8_t* rdram,recomp_context* ctx) {
    dkr_probe_checkpoint();
    const gpr globals=background_globals();
    if(!DKR_PROBE_HAS_FULL_SCENES || !state.video_enabled || state.pending_scene_site!=7 ||
       scene_authorization!=7 || state.scene_unload_phase || MEM_W(0,globals)!=1 || MEM_W(12,globals))
        dkr_probe_block("unconfirmed-menu-background-continuation");
    ctx->r4=MEM_W(4,globals);ctx->r5=(gpr)(int32_t)-1;ctx->r6=MEM_W(8,globals);
    load_level_for_menu(rdram,ctx);
    // Match thread30_bgload: completion is published only AFTER the actual
    // constructor returns. No synthetic SP/DP/OS completion or repeat input.
    MEM_W(0,globals)=0;
    dkr_probe_scene_transaction_end();
}
void dkr_probe_scene_load_reset(uint8_t* rdram,recomp_context* ctx) {
    (void)rdram;(void)ctx;dkr_probe_checkpoint();
    if(!offline || !canonical_presentation || state.scene_unload_phase!=3 || state.scene_load_resets ||
       state.vehicle_audio_scope || state.nature_audio_scope || state.requested_table!=UINT32_MAX ||
       state.load_section!=UINT32_MAX || state.load_destination || state.load_offset || state.load_size)
        dkr_probe_block("unowned-confirmed-scene-reset");
    uint64_t ignored=0;
    dkr_probe_mod_dispatch("dkr_legacy_scene_begin",rdram,ctx,0,0,0,0,&ignored);
    dkr_probe_mod_dispatch("dkr_owned_course_prepare",rdram,ctx,0,0,0,0,&ignored);
    // Accurate/4:3 private world has no live interpolation registry and every
    // reversible drawing scope is balanced. No mod session exists here. The
    // native reset DOES clear audio guards even in that profile: retain this
    // operation explicitly, with the same owned guards captured by the DSP.
    dkr_probe_audio_guards_initialize();state.scene_load_resets=1;
    dkr_probe_parity_scene(&state.presentation);
    state.title_tail_phase=state.title_tail_units=0; // Production scene_reset's TailGate reset.
    if(scenery_observer)scenery_observer(scenery_user,rdram,0,state.presentation.scene,0);
    state.postrace_contracted=state.postrace_pending=0;
}
void dkr_probe_scene_load_ready(uint8_t* rdram,recomp_context* ctx) {
    (void)ctx;dkr_probe_checkpoint();
    if(!offline || !canonical_presentation || state.scene_unload_phase!=3 || state.scene_load_resets!=1)
        dkr_probe_block("incomplete-confirmed-scene-constructor");
    // Actual retail constructor has returned through HUD, particles and
    // rumble. Check its real level header/model allocations before retiring
    // construction. Epoch release and native/GPU admission are separate.
    const gpr globals=guest_address(DKR_PROBE_REVISION==77 ? 0x800DC918U:0x800DCE88U);
    for(unsigned i=0;i<2;++i) {
        const uint32_t pointer=(uint32_t)MEM_W(i*4,globals);
        if(pointer<0x80000000U || pointer>0x807FFF00U || (pointer&3))
            dkr_probe_block("invalid-confirmed-scene-allocation");
    }
    state.scene_unload_phase=4;
    state.initial_world_vacant=0;
}
int dkr_probe_scene_transaction_cut(unsigned site) {
    dkr_probe_checkpoint();
    if(!offline || !canonical_presentation || !scene_cuts || !site || site>DKR_PROBE_SCENE_SITE_MAX ||
       state.scene_unload_phase || state.pending_scene_site!=site || scene_authorization!=site)
        dkr_probe_block("unconfirmed-scene-transaction-continuation");
    // Unlike a reversible cut, retain its identity/authorization until the
    // confirmed unload actually takes ownership. Do not allocate mode's guest
    // stack frame a second time, and do not free an old scene speculatively.
    return 0;
}
void dkr_probe_scene_transaction_end(void) {
    dkr_probe_checkpoint();
    if(!offline || !canonical_presentation || state.scene_unload_phase!=4 ||
       !state.pending_scene_site || state.scene_load_resets>1 ||
       (!state.scene_load_resets && !DKR_PROBE_HAS_FULL_SCENES) || scene_authorization)
        dkr_probe_block("incomplete-scene-transaction-continuation");
    state.pending_scene_site=0;state.scene_unload_phase=0;state.scene_load_resets=0;
}
unsigned dkr_probe_scene_entry(void) {
    if(!offline || !canonical_presentation || !scene_cuts)
        dkr_probe_block("unowned-mode-scene-continuation");
    if(state.pending_scene_site) {
        if(state.pending_scene_site>DKR_PROBE_SCENE_SITE_MAX || scene_authorization!=state.pending_scene_site)
            dkr_probe_block("scene-continuation-not-confirmed");
        return state.pending_scene_site;
    }
    if(scene_authorization) dkr_probe_block("orphaned-scene-authorization");
    return 0;
}
int dkr_probe_scene_cut(unsigned site) {
    if(!offline || !canonical_presentation || !scene_cuts || !site || site>DKR_PROBE_SCENE_SITE_MAX)
        dkr_probe_block("invalid-owned-scene-cut");
    if(scene_authorization) {
        if(scene_authorization!=site || state.pending_scene_site!=site)
            dkr_probe_block("wrong-confirmed-scene-continuation");
        // Exactly once: reaching the actual loader still requires audited
        // resource ownership. This does not fabricate SP/DP completion.
        scene_authorization=0; state.pending_scene_site=0; return 0;
    }
    if(state.pending_scene_site) dkr_probe_block("nested-owned-scene-cut");
    state.pending_scene_site=site; return 1;
}
static int canonical_identity_hook(const char* name) {
    /* These exact hooks were audited in runtime_stubs.cpp, runtime_enhancements.cpp
       and presentation_identity.cpp. With no window, Accurate presentation,
       4:3 cover=1, default FOV/draw distances and inactive scopes they do not
       change RAM/registers. This is NOT a generic dkr_* or render bypass. CPU
       camera, wave, lighting, geometry and HUD functions still run normally.
       Owned semantic hooks are dispatched first by owned_presentation_hook;
       this list supplies only the canonical fallback/default branches. */
    static const char* const identities[]={
        "dkr_split_screen_viewport_fill", "dkr_split_screen_world_aspect_begin", "dkr_split_screen_world_aspect_end",
        "dkr_apply_gameplay_fov", "dkr_apply_maximum_racer_detail", "dkr_extend_object_draw_distance",
        "dkr_extended_frustum_begin", "dkr_extended_frustum_end",
        "dkr_extend_hub_segment_bitfield", "dkr_keep_hub_segment_visible",
        "dkr_anchor_persistent_water_to_camera", "dkr_maximise_persistent_water_detail", "dkr_stabilise_persistent_water_transition",
        "dkr_skybox_cover_begin", "dkr_skybox_cover_end",
        "dkr_three_player_panel_begin", "dkr_three_player_panel_end",
        "dkr_background_fill_stretch_begin", "dkr_background_fill_stretch_end",
        "dkr_track_select_background_cover", // Exact 4:3 cover<=1 early return.
        "dkr_track_select_lens_flare_tint_begin", "dkr_track_select_lens_flare_tint_end", // Accurate/inactive scope.
        "dkr_transition_cover_begin", "dkr_transition_cover_end", "dkr_transition_interpolation_end",
        "dkr_shadow_interpolation_begin", "dkr_shadow_interpolation_end",
        "dkr_level_segment_interpolation_begin", "dkr_level_segment_interpolation_end",
        "dkr_surface_interpolation_begin", "dkr_surface_interpolation_end",
        "dkr_billboard_interpolation_begin", "dkr_vehicle_part_matrix_identity", "dkr_vehicle_part_interpolation_end",
        "dkr_presentation_perspective_matrix", "dkr_presentation_world_origin_matrix",
        "dkr_presentation_object_begin", "dkr_presentation_object_end",
        "dkr_presentation_wave_begin", "dkr_presentation_wave_end", "dkr_presentation_wave_block",
        "dkr_presentation_wave_selection", "dkr_presentation_wave_matrix",
        "dkr_quick_restart_poll", // Private owner has no external restart request broker.
        "dkr_custom_tracks_track_id_override", // Explicit no-custom-content contract: kNoTrackOverride.
        "dkr_water_private_scene", // Exact non-DKR_WATER_QUALIFICATION branch: no diagnostic map override.
        "dkr_audio_mix_tick", "dkr_scale_sequence_player_volume", // Canonical default unity gain.
        // No-mods identity only. A pinned ModWorld handles these hooks first
        // with its own checkpointed song/row and confirmed-only PCM journal.
        "dkr_custom_music_sequence_loaded", "dkr_custom_music_sequence_started",
        "dkr_hud_element_begin", "dkr_hud_element_end", "dkr_hud_minimap_begin", "dkr_hud_minimap_end",
        "dkr_hud_player_pass_begin", "dkr_hud_player_pass_end", "dkr_hud_general_pass_begin", "dkr_hud_general_pass_end",
        "dkr_hud_dialogue_pass_begin", "dkr_hud_dialogue_pass_end", "dkr_hud_text_begin", "dkr_hud_text_end",
        "dkr_hud_rect_extent", "dkr_hud_rect_end", "dkr_hud_timer_end"
    };
    for(unsigned i=0;i<sizeof(identities)/sizeof(identities[0]);++i)
        if(strcmp(name,identities[i])==0) return 1;
    return 0;
}
static void draw_marker(uint8_t* rdram,gpr pointer,unsigned kind,unsigned token) {
    // Same pointer-to-current-command ABI as AppendPresentationGroupCommand.
    if(!pointer)return;
    draw_event(kind,(uint32_t)MEM_W(0,pointer),token);
}
static int owned_presentation_hook(const char* name,uint8_t* rdram,recomp_context* ctx) {
    if(dkr_probe_parity_hook(name,rdram,ctx,&state.presentation,state.video_frames,parity_emit))return 1;
    if(strcmp(name,"dkr_netplay_postrace_barrier")==0 ||
       strcmp(name,"dkr_netplay_adventure_finish_barrier")==0) {
        // runtime_netplay::seal_race_finish mutates the stable coordinator's
        // native g_finish/g_recovery counters, NOT guest registers or RAM.
        // This backend uses DirectSession for transport, not that coordinator.
        // Its replacement is the owned tick/confirmation and Epoch flow:
        // this world already confirms every speculative tick and seals actual
        // load/unload intents through
        // its Epoch coordinator; invoking the legacy barrier would introduce
        // a second authority. Do not synthesize a legacy completion or scene.
        if(!state.video_enabled || state.scene_unload_phase)
            dkr_probe_block("unowned-finish-observation");
        return 1;
    }
    if(strcmp(name,"dkr_postrace_presentation_start")==0) {
        state.postrace_contracted=state.postrace_pending=0;return 1;
    }
    if(strcmp(name,"dkr_postrace_wooden_frame_draw")==0) {
        uint32_t a[8];dkr_probe_presentation_addresses(a);
        int32_t bounds[4];for(unsigned i=0;i<4;++i)bounds[i]=MEM_W(i*4,guest_address(a[0]));
        state.postrace_pending|=dkr_probe_contracted_wood(bounds);return 1;
    }
    if(strcmp(name,"dkr_presentation_finish_camera_node")==0)return 1;
    if(strcmp(name,"dkr_track_select_background_cover")==0) {
        if(ctx->r8) {
            // This is four packed 10-byte vertices, not one scalar access.
            // The checked memory helper accepts only retail scalar widths.
            // Validate both ends with halfword alignment before recording the
            // entire contiguous block; keep its bounds/sign-extension checks.
            dkr_probe_memory(rdram,ctx->r8,2,2);
            dkr_probe_memory(rdram,ctx->r8+38,2,2);
            draw_event(DKR_OWNED_BACKGROUND_QUAD,(uint32_t)ctx->r8,0);}
        return 1;
    }
    const int clear_begin=strcmp(name,"dkr_background_fill_stretch_begin")==0;
    const int clear_end=strcmp(name,"dkr_background_fill_stretch_end")==0;
    const int mosaic_begin=strcmp(name,"dkr_postrace_background_stretch_begin")==0 ||
        strcmp(name,"dkr_chequer_background_stretch_begin")==0;
    const int mosaic_end=strcmp(name,"dkr_postrace_background_stretch_end")==0 ||
        strcmp(name,"dkr_chequer_background_stretch_end")==0;
    if(clear_begin || clear_end || mosaic_begin || mosaic_end) {
        const int clear=clear_begin || clear_end;
        int* active=clear ? &clear_scope:
            strstr(name,"chequer") ? &chequer_scope:&mosaic_scope;
        if(clear_begin || mosaic_begin) {
            draw_marker(rdram,clear?ctx->r16:ctx->r4,DKR_OWNED_BACKGROUND_BEGIN,clear?1:0);
            *active=1;
        } else if(*active) {
            // bgdraw_render's common epilogue also runs when the fallback
            // fill was never entered. Match the production hook's begin guard
            // rather than emit an orphan end into the next UI/world command.
            draw_marker(rdram,clear?ctx->r16:ctx->r4,DKR_OWNED_BACKGROUND_END,0);
            *active=0;
        }
        return 1;
    }
    if(strcmp(name,"dkr_three_player_panel_begin")==0) {
        uint32_t a[8];dkr_probe_presentation_addresses(a);
        panel_scope=MEM_W(0,guest_address(a[5]))==2;
        if(panel_scope)draw_marker(rdram,ctx->r16,DKR_OWNED_BACKGROUND_BEGIN,2);
        return 1;
    }
    if(strcmp(name,"dkr_three_player_panel_end")==0) {
        if(panel_scope)draw_marker(rdram,ctx->r16,DKR_OWNED_BACKGROUND_END,0);
        panel_scope=0;return 1;
    }
    if(strcmp(name,"dkr_split_screen_viewport_fill")==0 ||
       strcmp(name,"dkr_split_screen_world_aspect_begin")==0) {
        uint32_t a[8];dkr_probe_presentation_addresses(a);
        const int layout=MEM_W(0,guest_address(a[5]));
        const int split=layout==2 || layout==3;
        const int postrace=dkr_probe_postrace_scope(MEM_W(0,guest_address(a[1])),
            (int8_t)MEM_B(0,guest_address(a[2])),MEM_W(0,guest_address(a[3])),
            MEM_W(0,guest_address(a[4])),layout);
        if(strcmp(name,"dkr_split_screen_viewport_fill")==0) {
            if(postrace && !state.postrace_contracted)
                draw_marker(rdram,ctx->r17,DKR_OWNED_POSTRACE_FULL_VIEWPORT,0);
            if(split)draw_marker(rdram,ctx->r17,DKR_OWNED_SPLIT_VIEWPORT,
                (uint32_t)MEM_W(0,guest_address(a[7]))&3U);
        } else {
            split_world_scope=split;
            if(split_world_scope)draw_marker(rdram,ctx->r17,DKR_OWNED_SPLIT_WORLD_BEGIN,0);
            framed_scope=postrace && state.postrace_contracted;
            if(framed_scope)draw_marker(rdram,ctx->r17,DKR_OWNED_FRAMED_BEGIN,0);
        }
        return 1;
    }
    if(strcmp(name,"dkr_split_screen_world_aspect_end")==0) {
        if(framed_scope)draw_marker(rdram,ctx->r17,DKR_OWNED_FRAMED_END,0);
        if(split_world_scope)draw_marker(rdram,ctx->r17,DKR_OWNED_SPLIT_WORLD_END,0);
        framed_scope=split_world_scope=0;return 1;
    }
    if(strcmp(name,"dkr_track_select_lens_flare_tint_begin")==0) {
        uint32_t a[8];dkr_probe_presentation_addresses(a);int32_t b[8];
        for(unsigned i=0;i<4;++i) {b[i]=MEM_W(i*4,guest_address(a[0]));b[i+4]=MEM_W(0x20+i*4,guest_address(a[0]));}
        lens_scope=dkr_probe_lens_scope(b,MEM_W(0,guest_address(a[6])));
        if(lens_scope)draw_marker(rdram,ctx->r17,DKR_OWNED_LENS_BEGIN,0);
        return 1;
    }
    if(strcmp(name,"dkr_track_select_lens_flare_tint_end")==0) {
        if(lens_scope)draw_marker(rdram,ctx->r17,DKR_OWNED_LENS_END,0);
        lens_scope=0;return 1;
    }
    return 0;
}
static void split_background_observation(uint8_t* rdram,gpr vertices,unsigned kind) {
    if(!vertices)return;
    uint32_t a[8];dkr_probe_presentation_addresses(a);
    const int layout=MEM_W(0,guest_address(a[5]));
    if(layout!=2 && layout!=3)return;
    dkr_probe_memory(rdram,vertices,2,2);dkr_probe_memory(rdram,vertices+38,2,2);
    const unsigned first=draw_count;
    draw_event(kind,(uint32_t)vertices,(unsigned)layout);
    if(!draw_invalid && draw_count==first+1 && kind==DKR_OWNED_SPLIT_VOID_QUAD) {
        uint32_t basis[4];dkr_probe_void_basis_addresses(basis);
        for(unsigned i=0;i<4;++i)draw_events[first].parameters[i]=(uint32_t)MEM_W(0,guest_address(basis[i]));
    }
}
static void material_hook(uint8_t* rdram,recomp_context* ctx) {
    // Exact field/ABI audit of runtime_stubs.cpp::dkr_fix_car_steering_wheel_material.
    // Keep every checked RAM read in C; no C++ destructor can cross a probe trap.
    const uint32_t model=(uint32_t)ctx->r23,batch=(uint32_t)ctx->r2;
    const uint32_t texture=(uint32_t)ctx->r18,stack=(uint32_t)ctx->r29;
    if(!dkr_probe_valid_material_pointer(model,0x28) || !dkr_probe_valid_material_pointer(batch,8) ||
       !dkr_probe_valid_material_pointer(texture,6) || !dkr_probe_valid_material_pointer(stack,0xB4)) return;
    const uint32_t object=(uint32_t)MEM_W(0xB4,guest_address(stack));
    if(!dkr_probe_valid_material_pointer(object,0x64)) return;
    uint32_t f[13]={0}; f[0]=MEM_HU(0x48,guest_address(object)); f[1]=UINT32_MAX;
    if(f[0]==1) {
        const uint32_t racer=(uint32_t)MEM_W(0x64,guest_address(object));
        if(!dkr_probe_valid_material_pointer(racer,0x1D6)) return;
        f[1]=(int8_t)MEM_BU(0x1D6,guest_address(racer));
    } else if(f[0]!=56) return;
    f[2]=MEM_HU(0x28,guest_address(model)); f[3]=(uint16_t)ctx->r10;
    f[4]=MEM_BU(0,guest_address(batch)); f[5]=(uint32_t)ctx->r17; f[6]=(uint32_t)ctx->r21;
    f[7]=(uint32_t)ctx->r8; f[8]=MEM_BU(0,guest_address(texture)); f[9]=MEM_BU(1,guest_address(texture));
    f[10]=MEM_BU(2,guest_address(texture)); f[11]=MEM_HU(6,guest_address(texture));
    const unsigned count=MEM_HU(0x22,guest_address(model));
    const uint32_t table=(uint32_t)MEM_W(0,guest_address(model));
    if(f[4]<count && table && dkr_probe_valid_material_pointer(table,f[4]*8+7)) {
        const unsigned offset=f[4]*8;
        f[12]=(uint32_t)MEM_W(offset,guest_address(table))==texture &&
            MEM_BU(offset+4,guest_address(table))==f[8] && MEM_BU(offset+5,guest_address(table))==f[9] &&
            MEM_BU(offset+6,guest_address(table))==f[10];
    }
    ctx->r6=dkr_probe_material_flags(f,(uint32_t)ctx->r6);
}
static void fullscreen_clear(uint8_t* rdram,recomp_context* ctx) {
    const uint32_t command=(uint32_t)ctx->r3;
    if(dkr_probe_valid_scissor_pointer(command)) {
        const gpr address=guest_address(0x80000000U|(command&0x1FFFFFFFU));
        MEM_W(4,address)=dkr_probe_fullscreen_clear_scissor(MEM_W(4,address));
    }
}
static void gradient_sky(uint8_t* rdram,recomp_context* ctx) {
    // Exact runtime_stubs.cpp ABI: r3 points to four 10-byte Vertex records.
    // The canonical cover is 1, but a two-player viewport still needs 2x
    // horizontal sky coverage. Do not silently identity-bypass that branch.
    if(!ctx->r3) return;
    const gpr layout=guest_address(DKR_PROBE_REVISION==77 ? 0x8011D37CU : 0x8011D8FCU);
    float horizontal,vertical;
    dkr_probe_sky_scales(MEM_W(0,layout),&horizontal,&vertical);
    if(horizontal<=1.0001f && vertical<=1.0001f) return;
    // Validate the entire output range before changing the first vertex.
    dkr_probe_memory(rdram,ctx->r3,2,2); dkr_probe_memory(rdram,ctx->r3+32,2,2);
    for(unsigned offset=0;offset<40;offset+=10) {
        long x=lroundf((float)MEM_H(offset,ctx->r3)*horizontal);
        long y=lroundf((float)MEM_H(offset+2,ctx->r3)*vertical);
        MEM_H(offset,ctx->r3)=x< -32768 ? -32768 : x>32767 ? 32767 : (int16_t)x;
        MEM_H(offset+2,ctx->r3)=y< -32768 ? -32768 : y>32767 ? 32767 : (int16_t)y;
    }
}
static float guest_float(uint8_t* rdram,uint32_t address) {
    const uint32_t bits=MEM_W(0,guest_address(address));float value;
    memcpy(&value,&bits,sizeof(value));return value;
}
static void void_primitive(uint8_t* rdram,recomp_context* ctx) {
    // Exact reviewed dkr_widen_void_primitive branch for canonical cover=1.
    // Two-player expands along the camera's lateral basis, NOT world X.
    if(ctx->r2<40U) return;
    float horizontal,vertical;
    dkr_probe_sky_scales(MEM_W(0,guest_address(DKR_PROBE_REVISION==77 ? 0x8011D37CU:0x8011D8FCU)),&horizontal,&vertical);
    if(horizontal<=1.0001f) return;
    const gpr vertices=ctx->r2-40U;
    dkr_probe_memory(rdram,vertices,2,2); dkr_probe_memory(rdram,vertices+34,2,2);
    uint32_t basis[4]; dkr_probe_void_basis_addresses(basis);
    const float lx=guest_float(rdram,basis[0]),lz=guest_float(rdram,basis[1]);
    const float cx=guest_float(rdram,basis[2]),cz=guest_float(rdram,basis[3]);
    const float length=lx*lx+lz*lz;
    if(!isfinite(lx) || !isfinite(lz) || !isfinite(cx) || !isfinite(cz) || length<0.5f || length>1.5f) return;
    int16_t widened[4][2];
    for(unsigned i=0;i<4;++i) {
        const float x=(float)MEM_H(i*10,vertices),z=(float)MEM_H(i*10+4,vertices);
        const float lateral=((x-cx)*lx+(z-cz)*lz)/length;
        const float expansion=lateral*(horizontal-1.0f);
        const float wx=x+expansion*lx,wz=z+expansion*lz;
        // Production finite healthy branch, plus fail-before-write guards for
        // extreme invalid inputs where lround would otherwise be undefined.
        if(!isfinite(wx) || !isfinite(wz)) dkr_probe_block("invalid-owned-void-coordinate");
        widened[i][0]=wx>=32767 ? 32767:wx<=-32768 ? -32768:(int16_t)lroundf(wx);
        widened[i][1]=wz>=32767 ? 32767:wz<=-32768 ? -32768:(int16_t)lroundf(wz);
    }
    for(unsigned i=0;i<4;++i) {
        MEM_H(i*10,vertices)=widened[i][0]; MEM_H(i*10+4,vertices)=widened[i][1];
    }
}
dkr_probe_native_state dkr_probe_native_capture(void) { return state; }
void dkr_probe_native_restore(dkr_probe_native_state value) { state = value; scene_authorization=0; confirmed_menu_tick=0; inline_menu_preview=0; }
int dkr_probe_native(const char* name, uint8_t* ram, struct recomp_context* context) {
    (void)ram; (void)context;
    dkr_probe_checkpoint();
    uint64_t mod_result=0;
    if(dkr_probe_mod_dispatch(name,ram,context,0,0,0,0,&mod_result))return (int)mod_result;
    if (offline) {
        if(DKR_PROBE_HAS_FULL_SCENES && canonical_presentation &&
           owned_presentation_hook(name,ram,context))return 0;
        if(DKR_PROBE_HAS_FULL_SCENES && canonical_presentation &&
           strcmp(name,"dkr_restore_multiplayer_race_music")==0) {
            // Same register-only Patch Pipeline ABI as runtime_enhancements:
            // reuse retail's existing level_music_start/fade/countdown branch.
            // Policy is checkpointed/admitted from the host, not a live local
            // preference read during a speculative or replayed CPU frame.
            if(state.restore_multiplayer_music && (int32_t)context->r2>=2)context->r2=1;
            return 0;
        }
        if(DKR_PROBE_HAS_FULL_SCENES && canonical_presentation &&
           strcmp(name,"dkr_track_select_fullscreen_preview")==0) {
            uint8_t* rdram=ram;
            if(!dkr_probe_menu_callback_enabled())dkr_probe_block("unowned-track-preview-scissor");
            const uint32_t viewport=(uint32_t)context->r2;
            // Same production validity and terminal-only predicate. All
            // intermediate/framed states retain their retail scissor.
            if(dkr_probe_valid_preview_pointer(viewport)) {
                const gpr address=guest_address(viewport);
                int32_t bounds[8];
                for(unsigned i=0;i<4;++i) {
                    bounds[i]=MEM_W(i*4,address);bounds[i+4]=MEM_W(0x20+i*4,address);
                }
                if(dkr_probe_fullscreen_preview_bounds(bounds)) {
                    MEM_W(0x28,address)=320;MEM_W(0x2C,address)=240;
                }
            }
            return 0;
        }
        if(DKR_PROBE_HAS_FULL_SCENES && canonical_presentation &&
           (strcmp(name,"dkr_character_select_music_unblock")==0 ||
            strcmp(name,"dkr_character_select_music_mask")==0 ||
            strcmp(name,"dkr_character_menu_music_mask")==0 ||
            strcmp(name,"dkr_character_select_animation_tick")==0 ||
            strcmp(name,"dkr_character_select_animation_fraction")==0)) {
            uint8_t* rdram=ram;
            if((!confirmed_menu_tick && !(state.pending_scene_site && state.scene_unload_phase==4)) ||
               !dkr_probe_character_animation_valid(
               state.character_animation_active,state.character_animation_phase))
                dkr_probe_block("unowned-character-music-animation");
            uint32_t a[6];dkr_probe_character_music_addresses(a);
            if(strcmp(name,"dkr_character_select_music_unblock")==0)MEM_W(0,guest_address(a[0]))=0;
            else if(strcmp(name,"dkr_character_select_animation_tick")==0) {
                if(state.character_animation_active)state.character_animation_phase=
                    dkr_probe_character_animation_advance(state.character_animation_phase,
                        (int32_t)context->r4,(int16_t)MEM_H(0,guest_address(a[5])));
            } else if(strcmp(name,"dkr_character_select_animation_fraction")==0) {
                if(state.character_animation_active)context->f0.u32l=state.character_animation_phase;
            } else if((uint8_t)MEM_BU(0,guest_address(a[1]))==0x1A) {
                const int select=strcmp(name,"dkr_character_select_music_mask")==0;
                MEM_W(0,guest_address(a[2]))=dkr_probe_character_music_mask(
                    (int8_t)MEM_BU(0,guest_address(a[select?3:4])));
                if(select) {state.character_animation_active=1;state.character_animation_phase=0;}
            }
            return 0;
        }
        if(DKR_PROBE_HAS_FULL_SCENES && canonical_presentation &&
           (strcmp(name,"dkr_netplay_character_select_enter")==0 ||
            strcmp(name,"dkr_netplay_character_select_lock")==0 ||
            strcmp(name,"dkr_netplay_character_select_ai_seed")==0)) {
            uint8_t* rdram=ram;
            if((!confirmed_menu_tick && !(state.pending_scene_site && state.scene_unload_phase==4)) || !state.owner_mask ||
               state.owner_mask>15 || (state.owner_mask&(state.owner_mask+1)))
                dkr_probe_block("unowned-character-roster-admission");
            // This backend owns the complete authored RNG stream, including
            // menu and audio consumption. Do not reseed it from the unrelated
            // stable DirectSession's descriptor or touch its phase counters.
            if(strcmp(name,"dkr_netplay_character_select_ai_seed")==0)return 0;
            uint32_t a[7];dkr_probe_roster_addresses(a);
            if(strcmp(name,"dkr_netplay_character_select_enter")==0) {
                state.assigned_ports_released=1;
                int8_t active[4],characters[8];uint8_t ids[16];
                dkr_probe_roster_seed(state.owner_mask,active,characters,ids);
                unsigned count=0;
                for(unsigned p=0;p<4;++p) {
                    MEM_B(p,guest_address(a[2]))=active[p];
                    MEM_B(p,guest_address(a[3]))=0;count+=active[p]!=0;
                }
                for(unsigned p=0;p<8;++p)MEM_B(p,guest_address(a[4]))=characters[p];
                for(unsigned p=0;p<16;++p)MEM_B(p,guest_address(a[5]))=ids[p];
                MEM_W(0,guest_address(a[0]))=count;MEM_W(0,guest_address(a[1]))=0;
            } else {
                for(unsigned p=0;p<4;++p)MEM_W(p*4,guest_address(a[6]))=
                    dkr_probe_roster_buttons((state.owner_mask>>p)&1,
                        (int8_t)MEM_B(p,guest_address(a[3])),MEM_W(p*4,guest_address(a[6])));
            }
            return 0;
        }
        if(DKR_PROBE_HAS_FULL_SCENES && canonical_presentation &&
           strcmp(name,"dkr_title_intro_audio_tail")==0) {
            uint8_t* rdram=ram; // Checked guest memory macros use this ABI name.
            // Identical register/stack ABI and production TailGate policy.
            // This latch belongs to the captured private world, never a shared
            // live-runtime global or an identity/no-op render hook.
            uint32_t addresses[2];dkr_probe_title_tail_addresses(addresses);
            const int first=MEM_W(0,guest_address(addresses[0]))==0;
            const int revealed=MEM_W(0,guest_address(addresses[1]))!=0;
            const uint32_t rate=(uint32_t)MEM_W(0x30,context->r29);
            const int action=dkr_probe_title_tail_update(&state.title_tail_phase,
                &state.title_tail_units,context->r2!=0,first,revealed,rate);
            if(action<0)dkr_probe_block("invalid-owned-title-tail-state");
            if(action==1) {context->r2=0;MEM_W(0x28,context->r29)=0;}
            else if(action==2) context->r2=1;
            return 0;
        }
        if(strcmp(name,"osViBlack_recomp")==0) {
            if(!canonical_presentation || !state.video_enabled || context->r4>1)
                dkr_probe_block("unowned-vi-black-state");
            state.video_black=(uint32_t)context->r4;return 0;
        }
        if(dkr_probe_magic_service(name,ram,context)) return 0;
        int pak_result=0;
        if(dkr_probe_pak_service(name,ram,context,&pak_result)) return pak_result;
        if(dkr_probe_input_service(name,ram,context)) return 0;
        if(audio_owned && strcmp(name,"dkr_audio_event_queue_next")==0)
            return (int)dkr_probe_audio_event_next(ram,context);
        if(audio_owned && strcmp(name,"dkr_audio_voice_guard")==0)
            return dkr_probe_audio_voice_guard(ram,context);
        if(audio_owned && strcmp(name,"dkr_audio_bus_guard")==0)
            return dkr_probe_audio_bus_guard(ram,context);
        if(audio_owned && strcmp(name,"osVirtualToPhysical_recomp")==0) {
            // ultra_translation.cpp -> misc_ultra.cpp: KSEG0/KSEG1 subtract
            // their segment base; the private ROM-DMA/PCM owner permits only
            // its checked KSEG0 RAM, never a masked corrupt/TLB address.
            dkr_probe_memory(ram,guest_address((uint32_t)context->r4),1,3);
            context->r2=(uint32_t)context->r4-0x80000000U; return 0;
        }
        if(canonical_presentation && strcmp(name,"dkr_widen_gradient_sky")==0) {
            gradient_sky(ram,context);
            if(DKR_PROBE_HAS_FULL_SCENES)split_background_observation(ram,context->r3,DKR_OWNED_SPLIT_SKY_QUAD);
            return 0;
        }
        // No .dkrmap/custom content is mounted in this constructor. The
        // reviewed hook's g_track_heap_bytes == 0 branch leaves r21 unchanged;
        // the retail generate_track allocation still executes in full.
        if(canonical_presentation && state.scene_unload_phase==3 &&
           strcmp(name,"dkr_custom_tracks_track_heap")==0)return 0;
        if(canonical_presentation && strcmp(name,"dkr_widen_void_primitive")==0) {
            void_primitive(ram,context);
            if(DKR_PROBE_HAS_FULL_SCENES && context->r2>=40U)
                split_background_observation(ram,context->r2-40U,DKR_OWNED_SPLIT_VOID_QUAD);
            return 0;
        }
        if(canonical_presentation && strcmp(name,"dkr_fix_fullscreen_clear_scissor")==0) {
            fullscreen_clear(ram,context);
            return 0;
        }
        if(strcmp(name,"osGetCount_recomp")==0) {
            // Exact unsigned u32 return ABI from ultra_translation.cpp.
            context->r2=(uint32_t)clock_read(); return 0;
        }
        if(strcmp(name,"osGetTime_recomp")==0) {
            const uint64_t offset=((uint64_t)state.clock_offset_hi<<32)|state.clock_offset_lo;
            const uint64_t value=clock_read()-offset;
            context->r2=(int32_t)(value>>32); context->r3=(int32_t)value; return 0;
        }
        if(strcmp(name,"osSetTime_recomp")==0) {
            const uint64_t value=(context->r4<<32)|(context->r5&UINT32_MAX);
            const uint64_t offset=clock_read()-value;
            state.clock_offset_hi=(uint32_t)(offset>>32); state.clock_offset_lo=(uint32_t)offset;
            return 0;
        }
        // Exact register-only ABIs audited in librecomp/src/math_routines.cpp.
        // Reject its undefined cast/shift/divide domains instead of treating a
        // corrupt guest operand as successful deterministic replay.
        if(strcmp(name,"__f_to_ll_recomp")==0) {
            const double value=context->f12.fl;
            if(!isfinite(value) || value< -9223372036854775808.0 || value>=9223372036854775808.0)
                dkr_probe_block("invalid-float-to-long-long");
            const int64_t result=(int64_t)value;
            context->r2=(int32_t)(result>>32); context->r3=(int32_t)result; return 0;
        }
        if(strcmp(name,"__ll_to_f_recomp")==0) {
            const uint64_t bits=(context->r4<<32)|(context->r5&UINT32_MAX);
            context->f0.fl=(float)(int64_t)bits; return 0;
        }
        if(strcmp(name,"__ull_rshift_recomp")==0 || strcmp(name,"__ll_lshift_recomp")==0 ||
           strcmp(name,"__ull_div_recomp")==0 || strcmp(name,"__ull_rem_recomp")==0) {
            const uint64_t a=(context->r4<<32)|(context->r5&UINT32_MAX);
            const uint64_t b=(context->r6<<32)|(context->r7&UINT32_MAX);
            uint64_t result;
            if(strcmp(name,"__ull_rshift_recomp")==0 || strcmp(name,"__ll_lshift_recomp")==0) {
                if(b>=64) dkr_probe_block("invalid-long-long-shift");
                result=strcmp(name,"__ull_rshift_recomp")==0 ? a>>b : a<<b;
            } else {
                if(!b) dkr_probe_block("invalid-long-long-divisor");
                result=strcmp(name,"__ull_div_recomp")==0 ? a/b : a%b;
            }
            context->r2=(int32_t)(result>>32); context->r3=(int32_t)result; return 0;
        }
        if(canonical_presentation && canonical_identity_hook(name)) return 0;
        // The full-scene owned world has neither an auto-boot request nor a
        // Track Lab override. The reviewed production hook returns unchanged
        // when auto_boot_enabled is false. No test/bootstrap hook runs here.
        if(DKR_PROBE_HAS_FULL_SCENES && canonical_presentation &&
           strcmp(name,"dkr_custom_tracks_auto_boot")==0)return 0;
        if(canonical_presentation && strcmp(name,"dkr_fix_car_steering_wheel_material")==0) {
            material_hook(ram,context); return 0;
        }
        if(strcmp(name,"osEepromProbe_recomp")==0) {
            if(!dkr_probe_eeprom_enabled()) dkr_probe_block("unowned-eeprom");
            // game_registration.cpp registers the retail game as Eep4k.
            context->r2=1; return 0;
        }
        if(strcmp(name,"osEepromRead_recomp")==0 || strcmp(name,"osEepromWrite_recomp")==0 ||
           strcmp(name,"osEepromLongRead_recomp")==0 || strcmp(name,"osEepromLongWrite_recomp")==0) {
            if(!dkr_probe_eeprom_enabled()) dkr_probe_block("unowned-eeprom");
            const unsigned offset=(uint8_t)context->r5*8;
            const int long_transfer=strstr(name,"Long")!=NULL;
            const unsigned count=long_transfer ? (uint32_t)context->r7 : 8;
            if(offset>512 || count>512-offset || count%8) dkr_probe_block("invalid-owned-eeprom-range");
            if(count) {
                const gpr buffer=guest_address((uint32_t)context->r6);
                uint8_t* rdram=ram;
                const int writing=strstr(name,"Write")!=NULL;
                uint8_t bytes[512];
                // Check the whole range before either guest or EEPROM bytes
                // change, including canonical MIPS byte-lane conversion.
                dkr_probe_memory(ram,buffer,1,3); dkr_probe_memory(ram,buffer+count-1,1,3);
                if(writing) for(unsigned i=0;i<count;++i) bytes[i]=MEM_BU(i,buffer);
                if(!dkr_probe_eeprom_transfer(writing,offset,bytes,count)) dkr_probe_block("owned-eeprom-transaction-rejected");
                if(!writing) for(unsigned i=0;i<count;++i) MEM_B(i,buffer)=bytes[i];
            }
            context->r2=0; return 0;
        }
        if (strcmp(name,"osInvalDCache_recomp")==0) return 0; // ultra_translation.cpp: empty.
        if (strcmp(name,"dkr_legacy_pi_start_dma")==0 || strcmp(name,"osPiStartDma_recomp")==0) {
            rom_dma(ram,context); return 0;
        }
        if (strcmp(name,"osRecvMesg_recomp")==0 || strcmp(name,"dkr_v11_asset_mutex_acquire")==0) {
            if(strcmp(name,"dkr_v11_asset_mutex_acquire")==0 && (uint32_t)context->r4!=mutex_queue())
                dkr_probe_block("unowned-revision-a-mutex");
            queue_receive(ram,context); return 0;
        }
        if (strcmp(name,"dkr_v11_asset_mutex_release")==0) {
            if((uint32_t)context->r4!=mutex_queue() || context->r6)
                dkr_probe_block("unowned-revision-a-mutex");
            queue_send(ram,(uint32_t)context->r4,(uint32_t)context->r5); context->r2=0; return 0;
        }
        /* custom_tracks_hooks.cpp: with no installed custom content the
           loader epilogue changes only these scoped argument records. Keep
           those native fields in the checkpoint even in the no-mod world. */
        if(strcmp(name,"dkr_custom_tracks_asset_load_begin")==0) {
            state.load_section=(uint32_t)context->r4; state.load_destination=(uint32_t)context->r5;
            state.load_offset=(uint32_t)context->r6; state.load_size=(int32_t)context->r7; return 0;
        }
        if(strcmp(name,"dkr_custom_tracks_asset_load_end")==0) {
            state.load_section=UINT32_MAX; state.load_destination=state.load_offset=0; state.load_size=0; return 0;
        }
        if(strcmp(name,"dkr_custom_tracks_table_load_begin")==0) {
            state.requested_table=(uint32_t)context->r4; return 0;
        }
        if(strcmp(name,"dkr_custom_tracks_table_load_end")==0) {
            state.requested_table=UINT32_MAX; return 0;
        }
        /* runtime_netplay.cpp: scopes are inactive with no online session.
           A zero return from range delegates RNG to the real guest function.
           This preserves its RAM seed; it is not an invented random result. */
        if (strcmp(name, "dkr_netplay_presentation_random_range") == 0 ||
            strcmp(name, "dkr_netplay_presentation_random_begin") == 0 ||
            strcmp(name, "dkr_netplay_presentation_random_end") == 0) return 0;
        /* ultra_translation.cpp explicitly implements this as an empty
           function; preserve r2 exactly as the running runtime does. */
        if (strcmp(name, "osSetIntMask_recomp") == 0) return 0;
        /* runtime_audio_controls.cpp, private fixture's default unity gains.
           Keep its scoped native counters checkpointable, not thread-local
           future state shared with a live audio runtime. */
        if (strcmp(name, "dkr_enter_vehicle_audio_scope") == 0) { ++state.vehicle_audio_scope; return 0; }
        if (strcmp(name, "dkr_leave_vehicle_audio_scope") == 0) { if (state.vehicle_audio_scope) --state.vehicle_audio_scope; return 0; }
        if (strcmp(name, "dkr_enter_nature_audio_scope") == 0) { ++state.nature_audio_scope; return 0; }
        if (strcmp(name, "dkr_leave_nature_audio_scope") == 0) { if (state.nature_audio_scope) --state.nature_audio_scope; return 0; }
        if (strcmp(name, "dkr_scale_sound_effect_volume") == 0) return 0;
        /* presentation_identity.cpp: these hooks change only the native
           renderer identity registry, never guest RAM/registers. This
           isolated world has no renderer. Journal the ordered lifecycle
           observations rather than letting replay mutate a live registry. */
        if (strcmp(name, "dkr_presentation_object_freed") == 0) {
            if(scenery_observer && state.scene_unload_phase==3) {
                uint32_t a[11],header,flags,behaviour,model_type,mode,time_trial;
                const uint32_t object=(uint32_t)context->r4;
                dkr_probe_scenery_addresses(a);
                if(dkr_probe_diagnostic_read(ram,a[7],4,&mode) && !mode &&
                   object>=0x80000000U && object<=0x807FFF94U &&
                   dkr_probe_diagnostic_read(ram,object+0x40,4,&header) && header>=0x80000000U && header<=0x807FFFA0U &&
                   dkr_probe_diagnostic_read(ram,header+0x30,2,&flags) && (flags&0x40U) &&
                   dkr_probe_diagnostic_read(ram,a[8],1,&time_trial) && !((flags&0x20U)&&time_trial) &&
                   dkr_probe_diagnostic_read(ram,object+0x48,2,&behaviour) && !behaviour &&
                   dkr_probe_diagnostic_read(ram,header+0x53,1,&model_type) && model_type==1)
                    scenery_observer(scenery_user,ram,object,state.presentation.scene,a[4]);
            }
            dkr_probe_parity_lifetime(&state.presentation,(uint32_t)context->r4,0);
            object_effect(1, (uint32_t)context->r4); return 0;
        }
        if (strcmp(name, "dkr_presentation_object_spawned") == 0) {
            dkr_probe_parity_lifetime(&state.presentation,(uint32_t)context->r2,1);
            object_effect(2, (uint32_t)context->r2); return 0;
        }
        if(strcmp(name,"dkr_legacy_character_hud_unbind")==0) return 0; // No installed custom-character session.
    }
    dkr_probe_block(name);
}
uint64_t dkr_probe_native_args(const char* name, uint8_t* ram, struct recomp_context* context,
                              const uint64_t* args, unsigned count) {
    (void)ram; (void)context;
    dkr_probe_checkpoint();
    uint64_t mod_result=0;
    if(dkr_probe_mod_dispatch(name,ram,context,args,count,0,0,&mod_result))return mod_result;
    if(offline && count==2 && args[0]<=3 && args[1]<=UINT32_MAX &&
       strcmp(name,"dkr_legacy_model_safety")==0) {
        const int checked=dkr_probe_model_safety(ram,(unsigned)args[0],(uint32_t)args[1]);
        if(checked<0)dkr_probe_block("invalid-model-reference");
        return (uint64_t)checked;
    }
    if(offline && canonical_presentation && count==1 && strcmp(name,"dkr_resolve_follow_camera")==0) {
        /* Same checked guest-only decision as the native Patch Pipeline. No
           live callback or local display/graphics preference enters replay. */
#if DKR_PROBE_REVISION == 77
        const uint32_t fields[8]={0x801234ECU,0x80121168U,0x80120CE0U,0x80120D14U,
            0x800DC918U,0x8011D508U,0x8011D586U,0x8011D55CU};
#else
        const uint32_t fields[8]={0x80123A6CU,0x801216E8U,0x80121260U,0x80121294U,
            0x800DCE88U,0x8011DA88U,0x8011DB06U,0x8011DADCU};
#endif
        dkr_cam_resolve(ram,(uint32_t)args[0],fields);return 0;
    }
    /* These private captures use fresh profiles with no custom characters.
       Match runtime_legacy_mods.cpp's explicit no-character-state paths. */
    if (offline) {
        if(count==1 && args[0]<4 && strcmp(name,"dkr_legacy_asset_cache_capacity")==0) {
            const uint32_t capacities[4]={700,100,70,100};return capacities[args[0]];
        }
        if(count==2 && args[0]<4 && strcmp(name,"dkr_legacy_asset_cache_guard")==0) {
            const uint32_t capacities[4]={700,100,70,100};
            if(args[1]<capacities[args[0]])return 0;
            dkr_probe_block("asset-cache-overflow");
        }
        // runtime_legacy_mods.cpp:404, no legacy session returns before any
        // operation. Private fixtures explicitly have no installed mods.
        if(count==1 && args[0]<=3 && strcmp(name,"dkr_legacy_asset_api")==0) return 0;
        if(canonical_presentation && count==1) {
            const uint32_t value=(uint32_t)args[0];
            if(dkr_probe_parity_args(name,ram,context,&value,count,parity_emit))return 0;
        }
        if(count==1 && (strcmp(name,"dkr_legacy_character_portrait_lookup")==0 || strcmp(name,"dkr_legacy_character_cinematic_portrait")==0)) return 0;
        if(count==2 && (strcmp(name,"dkr_legacy_character_hud_bind")==0 || strcmp(name,"dkr_legacy_character_hud_lookup")==0)) return 0;
        if (count == 2 && strcmp(name, "dkr_legacy_character_race_sound") == 0) return args[1];
        if (count == 1 && strcmp(name, "dkr_legacy_character_play_sound") == 0) return 0;
        if (count == 2 && strcmp(name, "dkr_legacy_character_cinematic_id") == 0) return args[1];
        if (count == 2 && strcmp(name, "dkr_legacy_character_event") == 0) return 0;
    }
    dkr_probe_block(name);
}
int dkr_probe_native_fields(const char* name, uint8_t* ram, struct recomp_context* context,
                            unsigned event, const uint32_t* fields) {
    (void)ram; (void)context; (void)event; (void)fields;
    dkr_probe_checkpoint();
    uint64_t mod_result=0;
    if(dkr_probe_mod_dispatch(name,ram,context,0,0,fields,event,&mod_result))return (int)mod_result;
    /* runtime_legacy_mods.cpp:122-125 returns 0 before inspecting fields/event
       when the character state/selector is absent. Fresh private profiles have
       no custom content. A horn reaches event 8 through sound_play even during
       a race; it does not imply a character-menu transition. */
    if (offline && strcmp(name, "dkr_legacy_character_menu") == 0) return 0;
    // runtime_legacy_mods.cpp's no-session/no-selector return precedes field
    // inspection. The full-scenes payload has the complete 18-event reviewed
    // stock menu surface (including menu_init/return-to-stock notifications).
    // It has no legacy or Track Lab catalogue, so that exact no-state branch
    // never dereferences a host field table or overwrites a guest return.
    if(offline && canonical_presentation && strcmp(name,"dkr_legacy_track_menu")==0 &&
       DKR_PROBE_HAS_FULL_SCENES && event<=17)return 0;
    if(offline && canonical_presentation && strcmp(name,"dkr_legacy_track_menu")==0 &&
       ((state.scene_unload_phase==3 && event==14) ||
        (state.scene_unload_phase==2 && event==10)))return 0;
    dkr_probe_block(name);
}
