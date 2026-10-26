/* A renderer-only, bounded copy of the native per-view draw closure.
 * NEVER pass live RAM here. No native game import or callback is linked.
 * A C-only fence avoids longjmp through C++ owners/destructors.
 */
#include "camera_scene_addresses.h"
#include "funcs.h"
#include "camera_kernel.h"
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>
#include <fenv.h>
#include <limits.h>
enum {
    COMMANDS = 0x80800000U,
    COMMAND_END = 0x80900000U,
    MATRICES = 0x80900000U,
    MATRIX_END = 0x80980000U,
    VERTICES = 0x80980000U,
    VERTEX_END = 0x80A80000U,
    TRIANGLES = 0x80A80000U,
    TRIANGLE_END = 0x80B80000U,
    STACK = 0x80BF0000U
};
#if defined(_MSC_VER)
#define CAMERA_TLS __declspec(thread)
#else
#define CAMERA_TLS _Thread_local
#endif
static CAMERA_TLS struct {
    uint8_t* ram;
    int active;
    uint32_t operations, scene, view;
    float cover, sky_y[4];
    unsigned sky_active, wave_active, wave_block, wave_selection, full_screen;
    dkr_camera_draw_result* result;
    struct {
        uint32_t first, identity;
    } objects[16];
    unsigned depth;
    const char* failure;
    jmp_buf fence;
    fenv_t environment;
} drawing;
void dkr_camera_fault(const char* why) {
    drawing.failure = why;
    longjmp(drawing.fence, 1);
}
void dkr_camera_step(void) {
    if (!drawing.active || ++drawing.operations > 6000000U)
        dkr_camera_fault("camera draw operation budget");
}
void* dkr_camera_access(uint64_t address, unsigned width) {
    dkr_camera_step();
    if (address < UINT64_C(0xffffffff80000000) || address > UINT64_C(0xffffffff80bfffff) ||
        (width != 1 && width != 2 && width != 4) || (address & (width - 1)))
        dkr_camera_fault("camera draw memory span");
    const uint32_t p = (uint32_t)address - 0x80000000U;
    if (p > 0xC00000U - width)
        dkr_camera_fault("camera draw memory end");
    return drawing.ram + (p ^ (width == 1 ? 3U : width == 2 ? 2U : 0U));
}
static uint64_t guest(uint32_t p) {
    return (uint64_t)(int64_t)(int32_t)p;
}
static uint32_t read32(uint32_t p) {
    return *(uint32_t*)dkr_camera_access(guest(p), 4);
}
static uint16_t read16(uint32_t p) {
    return *(uint16_t*)dkr_camera_access(guest(p), 2);
}
static uint8_t read8(uint32_t p) {
    return *(uint8_t*)dkr_camera_access(guest(p), 1);
}
static void put32(uint32_t p, uint32_t value) {
    *(uint32_t*)dkr_camera_access(guest(p), 4) = value;
}
static void put16(uint32_t p, uint16_t value) {
    *(uint16_t*)dkr_camera_access(guest(p), 2) = value;
}
static void put8(uint32_t p, uint8_t value) {
    *(uint8_t*)dkr_camera_access(guest(p), 1) = value;
}
static void real(uint32_t p, float value) {
    uint32_t bits;
    if (!isfinite(value))
        dkr_camera_fault("camera draw non-finite transform");
    memcpy(&bits, &value, 4);
    put32(p, bits);
}
static float read_real(uint32_t p) {
    const uint32_t bits = read32(p);
    float value;
    memcpy(&value, &bits, 4);
    if (!isfinite(value))
        dkr_camera_fault("camera draw non-finite input");
    return value;
}
static uint32_t mix32(uint32_t p) {
    p ^= p >> 16;
    p *= 0x7feb352dU;
    p ^= p >> 15;
    p *= 0x846ca68bU;
    p = (p ^ (p >> 16)) | 1U;
    return p == UINT32_MAX ? 0xFFFFFFFDU : p;
}
static uint16_t angle(float value) {
    if (!isfinite(value))
        dkr_camera_fault("camera angle");
    return (uint16_t)(int32_t)lroundf(remainderf(value, 6.28318530718F) * 10430.3783505F);
}
static void matrix(uint32_t address, uint32_t identity, unsigned root) {
    if (address < MATRICES || address > MATRIX_END - 64 || (address & 63))
        dkr_camera_fault("camera matrix allocation");
    for (unsigned i = 0; i < drawing.result->matrix_count; ++i)
        if (drawing.result->matrices[i].address == address)
            return;
    if (drawing.result->matrix_count == DKR_CAMERA_MAX_MATRICES)
        dkr_camera_fault("camera matrix metadata budget");
    dkr_camera_matrix* m = &drawing.result->matrices[drawing.result->matrix_count++];
    m->address = address;
    m->identity = mix32(drawing.scene ^ (drawing.view * 0x9e3779b9U) ^ identity);
    m->root = root;
}
unsigned dkr_presentation_world_origin_matrix(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    matrix(read32((uint32_t)ctx->r16), 0x43414d52U, 1);
    return 0;
}
unsigned dkr_presentation_object_begin(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    if (drawing.depth == 16)
        dkr_camera_fault("camera draw object nesting");
    const uint32_t object = (uint32_t)ctx->r7;
    const uint32_t holder = read32((uint32_t)ctx->r29 + 0x24);
    drawing.objects[drawing.depth].first = read32(holder);
    drawing.objects[drawing.depth++].identity = mix32(object ^ read16(object + 0x2C));
    return 0;
}
unsigned dkr_presentation_object_end(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    (void)ctx;
    if (!drawing.depth)
        dkr_camera_fault("camera draw object scope");
    const unsigned slot = --drawing.depth;
    const uint32_t first = drawing.objects[slot].first, end = read32(CAMERA_gObjectCurrMatrix);
    if (end < first || end - first > 256 * 64U || (end - first) % 64)
        dkr_camera_fault("camera object matrix range");
    for (uint32_t p = first; p < end; p += 64)
        matrix(p, drawing.objects[slot].identity ^ (p - first), 0);
    return 0;
}
unsigned dkr_extend_hub_segment_bitfield(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    ctx->r12 = 1;
    return 0; // Re-evaluate visibility from the local camera.
}
unsigned dkr_legacy_texture_animation(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    (void)ctx;
    return 1; // Freeze the captured, already advanced material phase.
}
unsigned dkr_skybox_cover_begin(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    (void)ctx;
    if (drawing.sky_active)
        return 0;
    const float scale = drawing.cover <= 1.75F ? drawing.cover : 3.0625F / drawing.cover;
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t p = CAMERA_gViewProjMatrixF + i * 16 + 4, bits = read32(p);
        memcpy(&drawing.sky_y[i], &bits, 4);
        real(p, drawing.sky_y[i] * scale);
    }
    drawing.sky_active = 1;
    return 0;
}
unsigned dkr_skybox_cover_end(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    (void)ctx;
    if (!drawing.sky_active)
        return 0;
    for (unsigned i = 0; i < 4; ++i)
        real(CAMERA_gViewProjMatrixF + i * 16 + 4, drawing.sky_y[i]);
    drawing.sky_active = 0;
    return 0;
}
static float sky_y_scale(float cover) {
    return cover <= 1.75F ? cover : 3.0625F / cover;
}
static float split_cover(void) {
    return drawing.cover * (read32(CAMERA_gViewportLayout) == 1 && !drawing.full_screen ? 2.F : 1.F);
}
unsigned dkr_widen_gradient_sky(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    const uint32_t start = (uint32_t)ctx->r3;
    const float horizontal = split_cover();
    float vertical = sky_y_scale(horizontal);
    if (drawing.full_screen) {
        vertical = fmaxf(1, vertical);
        if (read32(CAMERA_gViewportLayout) == 1)
            vertical *= 2;
    }
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t p = start + i * 10;
        put16(p, (uint16_t)(int16_t)fmaxf(-32768, fminf(32767, lroundf((int16_t)read16(p) * horizontal))));
        put16(p + 2, (uint16_t)(int16_t)fmaxf(-32768, fminf(32767, lroundf((int16_t)read16(p + 2) * vertical))));
    }
    return 0;
}
unsigned dkr_widen_void_primitive(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    const uint32_t start = (uint32_t)ctx->r2 - 40;
    const float lx = read_real(CAMERA_D_8011D4A0), lz = read_real(CAMERA_D_8011D4A4);
    const float cx = read_real(CAMERA_D_8011D4AC), cz = read_real(CAMERA_D_8011D4B0), length = lx * lx + lz * lz;
    if (length < 0.5F || length > 1.5F)
        return 0;
    const float scale = split_cover() - 1;
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t p = start + i * 10;
        const float x = (int16_t)read16(p), z = (int16_t)read16(p + 4);
        const float lateral = ((x - cx) * lx + (z - cz) * lz) / length;
        put16(p, (uint16_t)(int16_t)fmaxf(-32768, fminf(32767, lroundf(x + lx * lateral * scale))));
        put16(p + 4, (uint16_t)(int16_t)fmaxf(-32768, fminf(32767, lroundf(z + lz * lateral * scale))));
    }
    return 0;
}
unsigned dkr_presentation_wave_begin(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    (void)ctx;
    drawing.wave_active = 1;
    drawing.wave_block = 0;
    drawing.wave_selection = 256;
    return 0;
}
unsigned dkr_presentation_wave_end(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    (void)ctx;
    drawing.wave_active = 0;
    return 0;
}
unsigned dkr_presentation_wave_block(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    drawing.wave_block = (uint32_t)ctx->r2;
    return 0;
}
unsigned dkr_presentation_wave_selection(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    drawing.wave_selection = read32((uint32_t)ctx->r29 + 0x104) & 255U;
    return 0;
}
unsigned dkr_presentation_wave_matrix(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    if (!drawing.wave_active || !drawing.wave_block || drawing.wave_selection > 25)
        return 0;
    const uint32_t transform = (uint32_t)ctx->r6;
    uint32_t identity = drawing.wave_block ^ mix32(drawing.wave_selection);
    for (unsigned i = 0; i < 4; ++i)
        identity = mix32(identity ^ read32(transform + i * 4));
    identity ^= mix32(read32(CAMERA_gWaveController)) ^ mix32(read32(CAMERA_gWaveController + 0x28));
    matrix(read32((uint32_t)ctx->r5), identity, 2);
    drawing.wave_selection = 256;
    return 0;
}
unsigned dkr_fix_car_steering_wheel_material(uint8_t* ram, recomp_context* ctx, ...) {
    (void)ram;
    const uint32_t model = (uint32_t)ctx->r23, batch = (uint32_t)ctx->r2, tex = (uint32_t)ctx->r18;
    const uint32_t obj = read32((uint32_t)ctx->r29 + 0xB4), racer = read32(obj + 0x64);
    if (read16(obj + 0x48) != 1 || !racer || read8(racer + 0x1D6) != 0 ||
        (uint32_t)ctx->r10 + 1U != read16(model + 0x28) || read8(batch) == 255 || (int32_t)ctx->r17 != 4 ||
        (int32_t)ctx->r21 != 2 || (uint32_t)ctx->r8 != 0x80FU || read8(tex) != 16 || read8(tex + 1) != 16 ||
        (read8(tex + 2) & 15) != 0 || (read16(tex + 6) & 0xC0) != 0xC0)
        return 0;
    const unsigned index = read8(batch), count = read16(model + 0x22);
    const uint32_t table = read32(model);
    if (index < count && read32(table + index * 8) == tex && read8(table + index * 8 + 4) == 16 &&
        read8(table + index * 8 + 5) == 16 && !(read8(table + index * 8 + 6) & 15))
        ctx->r8 &= ~(gpr)0x800;
    return 0;
}
void __ll_to_f_recomp(uint8_t* ram, recomp_context* ctx) {
    (void)ram;
    int64_t value = (int64_t)((ctx->r4 << 32) | (uint32_t)ctx->r5);
    ctx->f0.fl = (float)value;
}
void __f_to_ll_recomp(uint8_t* ram, recomp_context* ctx) {
    (void)ram;
    const double x = ctx->f12.fl;
    if (!isfinite(x) || x >= 9223372036854775808.0 || x < -9223372036854775808.0)
        dkr_camera_fault("camera integer conversion");
    const int64_t value = (int64_t)x;
    ctx->r2 = (uint64_t)(int64_t)(int32_t)((uint64_t)value >> 32);
    ctx->r3 = (uint64_t)(int64_t)(int32_t)value;
}
void do_break(uint32_t pc) {
    (void)pc;
    dkr_camera_fault("camera native division/overflow");
}
void switch_error(const char* name, uint32_t pc, uint32_t table) {
    (void)name;
    (void)pc;
    (void)table;
    dkr_camera_fault("camera native jump table");
}
static void call(void (*function)(uint8_t*, recomp_context*), recomp_context* ctx) {
    function(drawing.ram, ctx);
    if (read32(CAMERA_gTrackDL) > COMMAND_END - 16 || read32(CAMERA_gTrackMtxPtr) > MATRIX_END ||
        read32(CAMERA_gTrackVtxPtr) > VERTEX_END || read32(CAMERA_gTrackTriPtr) > TRIANGLE_END)
        dkr_camera_fault("camera per-view draw allocation budget");
}
int CAMERA_ENTRY(uint8_t* ram, size_t bytes, const dkr_camera_draw_request* request, dkr_camera_draw_result* result) {
    if (!result)
        return 0;
    memset(result, 0, sizeof(*result));
    if (!ram || bytes < DKR_CAMERA_KERNEL_BYTES || !request || request->view > 3 || drawing.active) {
        result->failure = "camera draw ownership";
        return 0;
    }
    drawing.ram = ram;
    drawing.operations = drawing.depth = 0;
    drawing.result = result;
    drawing.failure = 0;
    drawing.sky_active = drawing.wave_active = 0;
    drawing.cover = 1;
    drawing.full_screen = request->full_screen;
    drawing.scene = request->scene;
    drawing.view = request->view;
    if (fegetenv(&drawing.environment)) {
        result->failure = "camera floating environment";
        return 0;
    }
    drawing.active = 1;
    if (setjmp(drawing.fence)) {
        result->failure = drawing.failure;
        result->operations = drawing.operations;
        drawing.active = 0;
        fesetenv(&drawing.environment);
        return 0;
    }
    const uint32_t camera = CAMERA_gCameras + request->view * 0x44;
    for (unsigned i = 0; i < 3; ++i)
        real(camera + 12 + i * 4, request->position[i]);
    put16(camera, angle(3.14159265359F - request->yaw));
    put16(camera + 2, angle(request->pitch));
    put16(camera + 4, angle(request->roll));
    put16(camera + 0x38, 0);
    real(camera + 0x30, 0);
    /* The new camera can occupy a different regional visibility cell. Use
       its own plane culling, not the old camera's authored visibility bitset. */
    put16(camera + 0x34, 0xFFFFU);
    put32(CAMERA_gActiveCameraID, request->view);
    put32(CAMERA_gSceneCurrentPlayerID, request->view);
    put32(CAMERA_gSceneActiveCamera, camera);
    put8(CAMERA_gCutsceneCameraActive, 0);
    put32(CAMERA_gTrackDL, COMMANDS);
    put32(CAMERA_gTrackMtxPtr, MATRICES);
    put32(CAMERA_gTrackVtxPtr, VERTICES);
    put32(CAMERA_gTrackTriPtr, TRIANGLES);
    put32(CAMERA_gSceneRenderSkyDome, 1);
    put8(CAMERA_gDrawLevelSegments, 1);
    put32(CAMERA_gDisableShadows, 0);
    put32(CAMERA_gModelMatrixStackPos, 0);
    if (request->hide_object) {
        const uint32_t p = request->hide_object;
        if (p < 0x80000000U || p > 0x807FFF80U)
            dkr_camera_fault("camera local model owner");
        put16(p + 6, (uint16_t)(read16(p + 6) | 0x200U << (request->view & 1U)));
    }
    const float n = request->near_plane, f = request->far_plane, y = 1.F / tanf(request->fov * 0.5F);
    if (!isfinite(y) || n <= 0 || f <= n)
        dkr_camera_fault("camera projection");
    for (unsigned i = 0; i < 16; ++i)
        real(CAMERA_gPerspectiveMatrixF + i * 4, 0);
    real(CAMERA_gPerspectiveMatrixF, y / (4.F / 3.F));
    real(CAMERA_gPerspectiveMatrixF + 20, y);
    real(CAMERA_gPerspectiveMatrixF + 40, (n + f) / (n - f));
    real(CAMERA_gPerspectiveMatrixF + 44, -1);
    real(CAMERA_gPerspectiveMatrixF + 56, 2 * n * f / (n - f));
    put16(CAMERA_perspNorm, (uint16_t)fmaxf(1, fminf(65535, 131072.F / (n + f))));
    recomp_context ctx = {0};
    ctx.f_odd = &ctx.f0.u32h;
    ctx.r29 = guest(STACK);
    ctx.r4 = guest(CAMERA_gPerspectiveMatrixF);
    ctx.r5 = guest(CAMERA_gPerspectiveMatrix);
    call(mtxf_to_mtx, &ctx);
    ctx.r4 = request->view;
    call(apply_fog, &ctx);
    ctx.r4 = guest(CAMERA_gTrackDL);
    ctx.r5 = guest(CAMERA_gTrackMtxPtr);
    call(viewport_main, &ctx);
    /* DKR builds its three camera planes from authored reference points and
       the INVERSE VIEW matrix, not the perspective matrix. Widen those side
       points for this FOV/canvas, then restore the points after plane setup. */
    float saved_x[4];
    const unsigned side_offsets[] = {0x30, 0x3C, 0x54, 0x60};
    if (!isfinite(request->aspect) || request->aspect < 0.1F || request->aspect > 16)
        dkr_camera_fault("camera canvas");
    drawing.cover = fmaxf(1, request->aspect / (4.F / 3.F));
    const float canvas = request->aspect * (read32(CAMERA_gViewportLayout) == 1 && !request->full_screen ? 2.F : 1.F);
    const float cull_scale = fmaxf(1, tanf(request->fov * 0.5F) * canvas * 1.1F / 1.3F);
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t p = CAMERA_D_800DC8AC + side_offsets[i];
        uint32_t bits = read32(p);
        memcpy(&saved_x[i], &bits, 4);
        real(p, saved_x[i] * cull_scale);
    }
    call(func_8002A31C, &ctx);
    for (unsigned i = 0; i < 4; ++i)
        real(CAMERA_D_800DC8AC + side_offsets[i], saved_x[i]);
    const uint32_t header = read32(CAMERA_gCurrentLevelHeader2);
    ctx.r4 = guest(CAMERA_gTrackDL);
    ctx.r5 = guest(CAMERA_gTrackMtxPtr);
    call(mtx_world_origin, &ctx);
    if (read32(CAMERA_gScenePlayerViewports) == 0) {
        if ((int8_t)read8(header + 0x49) == -1)
            call(trackbg_render_flashy, &ctx);
        else
            call(skydome_render, &ctx);
    } else {
        ctx.r4 = guest(CAMERA_gTrackDL);
        ctx.r5 = guest(CAMERA_gTrackMtxPtr);
        call(mtx_perspective, &ctx);
        call(trackbg_render_gradient, &ctx);
        ctx.r4 = guest(CAMERA_gTrackDL);
        ctx.r5 = guest(CAMERA_gTrackMtxPtr);
        call(func_80067D3C, &ctx);
        ctx.r4 = guest(CAMERA_gTrackDL);
        ctx.r5 = guest(CAMERA_gTrackMtxPtr);
        call(mtx_world_origin, &ctx);
    }
    ctx.r4 = 0;
    call(initialise_player_viewport_vars, &ctx); // No logic/animation time advance.
    /* The retained world-end boundary is AFTER native weather/lens flares.
       Redraw only their frozen visual state: never call weather_update,
       snow_update, rain_update, rain_sound or rain_lightning. */
    ctx.r4 = guest((uint32_t)-1);
    ctx.r5 = guest((uint32_t)-512);
    call(weather_clip_planes, &ctx);
    if ((int16_t)read16(header + 0x90) > 0 && read32(CAMERA_gScenePlayerViewports) == 0) {
        put32(CAMERA_gCurrWeatherDisplayList, read32(CAMERA_gTrackDL));
        put32(CAMERA_gCurrWeatherMatrix, read32(CAMERA_gTrackMtxPtr));
        put32(CAMERA_gCurrWeatherVertexList, read32(CAMERA_gTrackVtxPtr));
        put32(CAMERA_gCurrWeatherTriList, read32(CAMERA_gTrackTriPtr));
        put32(CAMERA_gWeatherCamera, camera);
        call(get_camera_matrix, &ctx);
        put32(CAMERA_gWeatherCameraMatrix, (uint32_t)ctx.r2);
        if (read32(CAMERA_gWeatherType) == 0) {
            if (read32(CAMERA_gSnowParticleCount) > 0) {
                put32(CAMERA_gSnowVerts, read32(CAMERA_gSnowVertexData + (read32(CAMERA_gSnowVertexFlip) & 1U) * 4));
                call(snow_vertices, &ctx);
                call(snow_render, &ctx);
            }
        } else {
            // Prevent fresh splash spawning while retaining existing splashes.
            put32(CAMERA_gRainSplashDelay, 1);
            ctx.r4 = 0;
            call(rain_render_splashes, &ctx);
            if ((int32_t)read32(CAMERA_gLightningFrequency) > 255) {
                ctx.r4 = guest(CAMERA_gCurrWeatherDisplayList);
                ctx.r5 = guest(CAMERA_gCurrWeatherMatrix);
                call(mtx_ortho, &ctx);
                for (unsigned i = 0; i < 2; ++i) {
                    ctx.r4 = guest(CAMERA_gRainGfx + i * 0x18);
                    ctx.r5 = 0;
                    call(rain_render, &ctx);
                }
                ctx.r4 = guest(CAMERA_gCurrWeatherDisplayList);
                call(rendermode_reset, &ctx);
                ctx.r4 = guest(CAMERA_gCurrWeatherDisplayList);
                call(viewport_reset, &ctx);
            }
        }
        put32(CAMERA_gTrackDL, read32(CAMERA_gCurrWeatherDisplayList));
        put32(CAMERA_gTrackMtxPtr, read32(CAMERA_gCurrWeatherMatrix));
        put32(CAMERA_gTrackVtxPtr, read32(CAMERA_gCurrWeatherVertexList));
        put32(CAMERA_gTrackTriPtr, read32(CAMERA_gCurrWeatherTriList));
    }
    ctx.r4 = guest(camera);
    call(lensflare_override, &ctx);
    ctx.r4 = guest(CAMERA_gTrackDL);
    ctx.r5 = guest(CAMERA_gTrackMtxPtr);
    ctx.r6 = guest(CAMERA_gTrackVtxPtr);
    ctx.r7 = guest(camera);
    call(lensflare_render, &ctx);
    const uint32_t end = read32(CAMERA_gTrackDL);
    if (request->return_address < 0x80000000U || request->return_address > 0x807FFFF8U || (request->return_address & 7))
        dkr_camera_fault("camera return command");
    put32(end, 0x06010000U);
    put32(end + 4, request->return_address & 0xFFFFFFU);
    result->start = COMMANDS;
    result->end = end + 8;
    result->operations = drawing.operations;
    drawing.active = 0;
    fesetenv(&drawing.environment);
    return 1;
}
