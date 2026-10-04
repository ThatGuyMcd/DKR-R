#include "funcs.h"
#include "probe_component.h"
#include "probe_input.h"
#include "probe_magic.h"
#include <string.h>

static dkr_probe_pad frame_inputs[4];
void dkr_probe_component_inputs(const dkr_probe_pad inputs[4]) {
    memcpy(frame_inputs, inputs, sizeof(frame_inputs));
}
static void component_tick(uint8_t* rdram, struct recomp_context* ctx, int water_phases) {
    /* Addresses verified in ver/symbols/symbol_addrs.us.v{77,80}.txt and
       joypad.c's OSContPad layout. No live SI poll/queue/native input broker.
       Pad edges are simulation state and are restored with the guest image. */
    const gpr current = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x80121110U : 0x80121690U);
    const gpr previous = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x80121128U : 0x801216A8U);
    const gpr pressed = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x80121140U : 0x801216C0U);
    const gpr released = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x80121148U : 0x801216C8U);
    const gpr mask = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x800DD304U : 0x800DD874U);
    const gpr display_lists = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x801211F0U : 0x80121770U);
    const gpr current_display = display_lists + 8;
    const gpr task_number = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x801234E8U : 0x80123A68U);
    dkr_probe_enter("private-component-input-and-object-tick");
    if(dkr_probe_magic_enabled())dkr_probe_native("dkr_apply_launch_magic_codes",rdram,ctx);
    // The real main loop resets the display-list cursor before any CPU draw
    // phase. Debug text is generated even by racer updates and its real flush
    // writes commands into this guest buffer; no GPU task is submitted here.
    MEM_W(0,current_display) = MEM_W((MEM_W(0,task_number) & 1) * 4,display_lists);
    if(water_phases==2) {
        const unsigned buffer=(MEM_W(0,task_number)&1)*4;
        // Retail main_game_loop resets every CPU output heap, not just DL.
        // These are owned/checkpointed private RAM. No asynchronous task is
        // submitted, no SP/DP completion is fabricated and no live lease exists.
        MEM_W(24,display_lists)=MEM_W(16+buffer,display_lists);
        MEM_W(40,display_lists)=MEM_W(32+buffer,display_lists);
        MEM_W(56,display_lists)=MEM_W(48+buffer,display_lists);
    }
    if(dkr_probe_input_enabled()) {
        // Run the RETAIL input/save/rumble path. Its external owner already
        // froze this frame's pads. Allocator aging stays at the retail frame
        // end below, rather than being advanced once here and once later.
        dkr_probe_input_frame(rdram,ctx);
    }
    else for (unsigned p = 0; p < 4; ++p) {
        const unsigned offset = p * 6;
        const unsigned old = MEM_HU(offset,current);
        for (unsigned half = 0; half < 3; ++half)
            MEM_H(offset + half * 2,previous) = MEM_HU(offset + half * 2,current);
        MEM_H(offset,current) = frame_inputs[p].buttons;
        MEM_B(offset + 2,current) = frame_inputs[p].stick_x;
        MEM_B(offset + 3,current) = frame_inputs[p].stick_y;
        MEM_H(offset + 4,current) = 0;
        MEM_H(p * 2,pressed) = ((old ^ frame_inputs[p].buttons) & frame_inputs[p].buttons) & MEM_HU(0,mask);
        MEM_H(p * 2,released) = ((old ^ frame_inputs[p].buttons) & old) & MEM_HU(0,mask);
    }
    ctx->r4 = 2;
    if(water_phases==2) {
        mode_game(rdram,ctx);
        // The guest stack/register continuation is intentionally retained.
        // Do not run the frame-end tail or free anything speculatively.
        if(dkr_probe_scene_pending()) { dkr_probe_leave(); return; }
    }
    else obj_update(rdram,ctx);
    /* mode_game drains the deferred object/particle deletion list after
       obj_update. Omitting this REAL guest phase allows repeated particle
       deaths to fill its 200-pointer allocation and overwrite the adjacent
       cutscene-path array. It is not safe to replay the object function alone.
       Keep AI maintenance and frame-end allocator aging in their retail order
       as well. No native queue or scheduler completion is fabricated here. */
    if(water_phases!=2) {
        gParticlePtrList_flush(rdram,ctx);
        ainode_update(rdram,ctx);
    }
    if (water_phases==1) {
        // render_scene's simulation-relevant phases also write guest state.
        // Execute the actual emitted functions, including their patched UV
        // wrap behavior, not a separate replacement wave implementation.
        const gpr viewports = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x8011D37CU : 0x8011D8FCU);
        ctx->r4 = MEM_W(0,viewports);
        cam_set_layout(rdram,ctx);
        const gpr viewport_count = ctx->r2;
        const gpr wave_count = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x8011D384U : 0x8011D904U);
        const gpr header_pointer = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x800DC91CU : 0x800DCE8CU);
        const gpr model_pointer = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x800DC918U : 0x800DCE88U);
        const gpr shadow_flip = (gpr)(int32_t)(DKR_PROBE_REVISION == 77 ? 0x8011B0C8U : 0x8011B648U);
        const gpr header = (gpr)(int32_t)MEM_W(0,header_pointer);
        const gpr model = (gpr)(int32_t)MEM_W(0,model_pointer);
        if (MEM_W(0,wave_count)) { ctx->r4 = 2; waves_update(rdram,ctx); }
        // Actor shading, contact-shadow/water meshes and lighting are CPU
        // writes in render_scene, not disposable GPU-only output. Exercise
        // their real guest implementations in retail order, including the
        // double-buffer flip. This component still excludes camera/HUD draw,
        // weather, scene loads and native audio/render workers.
        ctx->r4 = 2; ctx->r5 = 2; ctx->r6 = 2; shadow_update(rdram,ctx);
        for (unsigned cycle = 0; cycle < 7; ++cycle) {
            const int32_t data = MEM_W(0x74 + cycle * 4,header);
            if (data != -1) { ctx->r4 = data; ctx->r5 = 2; update_colour_cycle(rdram,ctx); }
        }
        const int32_t light = MEM_W(0xAC,header);
        if (light != -1) { ctx->r4 = light; ctx->r5 = 2; update_pulsating_light_data(rdram,ctx); }
        ctx->r4 = viewport_count; ctx->r5 = 2; update_fog(rdram,ctx);
        ctx->r4 = 2; scroll_particle_textures(rdram,ctx);
        if (MEM_H(0x1E,model) > 0) { ctx->r4 = 2; track_tex_anim(rdram,ctx); }
        MEM_W(0,shadow_flip) = 1 - MEM_W(0,shadow_flip);
    }
    if(water_phases==2) {
        ctx->r4=2; sound_update_queue(rdram,ctx);
    }
    // main_game_loop:318 drains debug commands once per tick. Without this,
    // racer debug commands overflow the 0x900-byte buffer into its own cursor
    // on longer runs. Call the retail drain, not a forced cursor reset or a
    // disabled debug-write function. This remains only a closed component.
    ctx->r4 = current_display;
    debug_text_print(rdram,ctx);
    if(water_phases==2) {
        ctx->r4=current_display; ctx->r5=display_lists+24; ctx->r6=display_lists+40;
        render_dialogue_boxes(rdram,ctx);
        ctx->r4=4; dialogue_close(rdram,ctx); ctx->r4=4; dialogue_clear(rdram,ctx);
        ctx->r4=2; transition_update(rdram,ctx);
        if(ctx->r2) {
            ctx->r4=current_display; ctx->r5=display_lists+24; ctx->r6=display_lists+40;
            transition_render(rdram,ctx);
        }
        copy_viewports_to_stack(rdram,ctx);
    }
    mempool_free_queue_clear(rdram,ctx);
    if(dkr_probe_magic_enabled())dkr_probe_native("dkr_magic_codes_frame_complete",rdram,ctx);
    if(water_phases==2) {
        // Retail frame-end state, after allocator aging and before the next
        // VI: do not accidentally retain a cutscene camera forever in replay.
        const gpr paused=(gpr)(int32_t)(DKR_PROBE_REVISION==77 ? 0x80123515U : 0x80123A95U);
        if(!MEM_BU(0,paused)) disable_cutscene_camera(rdram,ctx);
        dkr_probe_clock_advance();
    }
    dkr_probe_leave();
}
void dkr_probe_component_tick(uint8_t* rdram, struct recomp_context* ctx) { component_tick(rdram,ctx,0); }
void dkr_probe_water_component_tick(uint8_t* rdram, struct recomp_context* ctx) { component_tick(rdram,ctx,1); }
void dkr_probe_mode_component_tick(uint8_t* rdram, struct recomp_context* ctx) { component_tick(rdram,ctx,2); }

#if DKR_PROBE_HAS_AUTHORED_CPU
static void authored_finish_frame(uint8_t* rdram, struct recomp_context* ctx) {
    const gpr task = (int32_t)(DKR_PROBE_REVISION==77 ? 0x801234E8U:0x80123A68U);
    const gpr display = (int32_t)(DKR_PROBE_REVISION==77 ? 0x801211F0U:0x80121770U);
    dkr_probe_input_sample_complete();
    if(!dkr_probe_scene_pending()) {
        const unsigned slot=MEM_W(0,task);
        const gpr first=(int32_t)MEM_W(slot*4,display);
        const gpr end=(int32_t)MEM_W(8,display);
        if(slot>1 || (uint32_t)end<(uint32_t)first+16 || ((uint32_t)end-(uint32_t)first)%8 ||
           (uint32_t)MEM_W(-16,end)!=0xE9000000U || MEM_W(-12,end) ||
           (uint32_t)MEM_W(-8,end)!=0xB8000000U || MEM_W(-4,end))
            dkr_probe_block("incomplete-authored-display-list");
        ctx->r4=0;
        dkr_probe_authored_video_cpu(rdram,ctx);
        dkr_probe_video_finish_frame();
        MEM_W(0,task)=1-slot;
        dkr_probe_clock_advance();
    }
}
#endif
void dkr_probe_authored_component_tick(uint8_t* rdram, struct recomp_context* ctx) {
#if DKR_PROBE_HAS_AUTHORED_CPU
    const gpr task = (int32_t)(DKR_PROBE_REVISION==77 ? 0x801234E8U:0x80123A68U);
    const gpr rate = (int32_t)(DKR_PROBE_REVISION==77 ? 0x800DD404U:0x800DD974U);
    const gpr timer = (int32_t)(DKR_PROBE_REVISION==77 ? 0x800DD3F0U:0x800DD960U);
    dkr_probe_enter("private-authored-main-cpu-owner");
    // Fixed-cadence experiment is NOT the VI-derived retail frame skipper.
    // Retain the retail timer's CPU-only decrement/reset/copy semantics.
    if(!dkr_probe_input_enabled() || !dkr_probe_magic_enabled() ||
       MEM_W(0,rate)!=2 || (unsigned)MEM_BU(0,timer)>2 || (unsigned)MEM_W(0,task)>1)
        dkr_probe_block("unsupported-authored-cpu-owner-state");
    if(!dkr_probe_scene_pending()) {
        dkr_probe_menu_preview_begin(rdram,ctx);
        if(MEM_BU(0,timer))MEM_B(0,timer)=MEM_BU(0,timer)-1;
        dkr_probe_video_begin_frame(rdram);
        dkr_probe_input_begin_sample(rdram,ctx);
        dkr_probe_native("dkr_apply_launch_magic_codes",rdram,ctx);
    }
    dkr_probe_authored_main_cpu(rdram,ctx);
    authored_finish_frame(rdram,ctx);
    dkr_probe_menu_tick_end(rdram);
    dkr_probe_leave();
#else
    (void)rdram; (void)ctx;
    dkr_probe_block("authored-cpu-pipeline-unavailable");
#endif
}
void dkr_probe_authored_scene_resume(uint8_t* rdram, struct recomp_context* ctx) {
#if DKR_PROBE_HAS_AUTHORED_CPU
    dkr_probe_enter("private-confirmed-authored-resume-owner");
    if(!dkr_probe_input_enabled() || !dkr_probe_magic_enabled() || !dkr_probe_scene_pending())
        dkr_probe_block("unowned-authored-scene-resume");
    dkr_probe_scene_resume_main_cpu(rdram,ctx);
    authored_finish_frame(rdram,ctx);
    dkr_probe_leave();
#else
    (void)rdram;(void)ctx;dkr_probe_block("authored-cpu-pipeline-unavailable");
#endif
}
