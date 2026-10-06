#ifndef DKR_REPLAY_PROBE_BRIDGE_H
#define DKR_REPLAY_PROBE_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#include "netplay/experimental_draw_events.h"
#include "probe_presentation.h"
#ifndef DKR_PROBE_HAS_FULL_SCENES
#define DKR_PROBE_HAS_FULL_SCENES 0
#endif
enum { DKR_PROBE_SCENE_SITE_MAX = DKR_PROBE_HAS_FULL_SCENES ? 7 : 2 };
/* Reviewed retail AssetLevelHeadersEnum has 65 entries, including post-boss
   cinematics/endings at 54..64. Full-game construction must admit those too.
   Limited, non-full-scene closures retain their original 0..53 boundary.
   This is only an ID range; ownership/service/argument gates still apply. */
enum {
    DKR_PROBE_RETAIL_SCENE_LEVEL_COUNT = 65,
    DKR_PROBE_SCENE_LEVEL_COUNT = DKR_PROBE_HAS_FULL_SCENES ?
        DKR_PROBE_RETAIL_SCENE_LEVEL_COUNT : 54
};
#ifdef __cplusplus
extern "C" {
#define DKR_PROBE_NORETURN [[noreturn]]
#elif defined(_MSC_VER)
#define DKR_PROBE_NORETURN __declspec(noreturn)
#else
#define DKR_PROBE_NORETURN _Noreturn
#endif
struct recomp_context;
typedef void (*dkr_probe_entry)(uint8_t*, struct recomp_context*);
typedef struct dkr_probe_result {
    int completed;
    const char* blocked;
    uint64_t memory_accesses;
    uint64_t operations;
    uint64_t guest_entries;
    uint64_t bad_address;
    const char* source_file;
    unsigned source_line;
    unsigned stack_depth;
    const char* stack[128];
} dkr_probe_result;
/* Private, single-threaded process. This trampoline calls C guest functions;
   no C++ objects/destructors may live across its setjmp/longjmp boundary. */
dkr_probe_result dkr_probe_run(dkr_probe_entry entry, uint8_t* ram, size_t bytes,
                               struct recomp_context* context, uint64_t budget);
/* A native mod adapter may invoke ONLY reviewed private guest callbacks. A
   nested C trampoline catches guest traps before returning to its C++ caller,
   so no longjmp crosses C++ locks, leases or destructors. Failed adapters must
   unwind normally and return -1; dispatch then traps on the C side. */
typedef int (*dkr_probe_mod_service)(void* user,const char* operation,uint8_t* ram,
    struct recomp_context* context,const uint64_t* args,unsigned count,
    const uint32_t* fields,unsigned event,uint64_t* result);
int dkr_probe_bind_mod_service(dkr_probe_mod_service service,void* user);
int dkr_probe_mod_dispatch(const char* operation,uint8_t* ram,struct recomp_context* context,
    const uint64_t* args,unsigned count,const uint32_t* fields,unsigned event,uint64_t* result);
dkr_probe_result dkr_probe_run_native(dkr_probe_entry entry,uint8_t* ram,struct recomp_context* context);
void* dkr_probe_memory(uint8_t* ram, uint64_t address, unsigned width, unsigned lane_xor);
void* dkr_probe_memory_at(uint8_t* ram, uint64_t address, unsigned width, unsigned lane_xor,
                          const char* file, unsigned line);
/* Fixed-width entry points for emitted MEM macros. They use the SAME checked
   implementation; compile-time width/lane constants eliminate parameter and
   dispatch work, never pointer validation, counters or the failure boundary. */
void* dkr_probe_memory_1_at(uint8_t* ram,uint64_t address,const char* file,unsigned line);
void* dkr_probe_memory_2_at(uint8_t* ram,uint64_t address,const char* file,unsigned line);
void* dkr_probe_memory_4_at(uint8_t* ram,uint64_t address,const char* file,unsigned line);
/* Private audio RSP DMA, after its hardware address/alignment normalization.
   Validate BOTH complete spans before copying any byte. The loop is bounded
   by 4 KiB and preserves the retail byte-lane layout; no per-byte trap call. */
void dkr_probe_audio_dma(uint8_t* ram, uint8_t* dmem, uint32_t dmem_address,
                         uint32_t dram_address, uint32_t inclusive_length, int write,
                         const char* file, unsigned line);
DKR_PROBE_NORETURN void dkr_probe_block(const char* operation);
void dkr_probe_enter(const char* function);
void dkr_probe_leave(void);
void dkr_probe_checkpoint(void);
/* Private diagnostic watchpoint; 0 disables it. Reports the preceding checked
   memory site when this word changes. Does not patch or mask guest memory. */
void dkr_probe_watch_word(uint64_t address);
/* Observation-only Playtest 5 diagnostics. Not checkpointed, not a service,
   and never consulted by gameplay. Raw reads neither trap nor consume the
   guest operation budget. Inactive unless the new pipeline arms a finish. */
int dkr_probe_diagnostic_read(uint8_t* ram, uint32_t address, unsigned width, uint32_t* value);
void dkr_probe_boss_diagnostic_tick(uint64_t epoch, uint32_t frame);
void dkr_probe_boss_trace(const char* stage, uint8_t* ram, struct recomp_context* context);
void dkr_probe_boss_diagnostic_failure(const dkr_probe_result* result, struct recomp_context* context);
void dkr_probe_boss_diagnostic_addresses(uint32_t addresses[8]);
/* Optional, audited offline services. Unknown operations always trap. */
void dkr_probe_offline_services(int enabled);
/* Canonical private CPU presentation, with no window or live registry reads.
   Audited draw observations travel with immutable render copies; local aspect
   settings never change checkpointed CPU bytes. Unknown calls still trap. */
void dkr_probe_canonical_presentation(int enabled);
/* Immutable, canonical retail ROM, borrowed for the entire private process.
   Installed BEFORE any tick, never read from disk by a replayed operation. */
void dkr_probe_rom(const uint8_t* image, size_t bytes);
void dkr_probe_profile_disabled(const char* name);
int dkr_probe_native(const char* name, uint8_t* ram, struct recomp_context* context);
uint64_t dkr_probe_native_args(const char* name, uint8_t* ram, struct recomp_context* context,
                              const uint64_t* args, unsigned count);
int dkr_probe_model_safety(uint8_t* ram,unsigned operation,uint32_t address);
int dkr_probe_native_fields(const char* name, uint8_t* ram, struct recomp_context* context,
                            unsigned event, const uint32_t* fields);
uint64_t dkr_probe_cop0_read(struct recomp_context* context);
void dkr_probe_cop0_write(struct recomp_context* context, uint64_t value);
typedef struct dkr_probe_native_state {
    int vehicle_audio_scope;
    int nature_audio_scope;
    uint32_t requested_table;
    uint32_t load_section;
    uint32_t load_destination;
    uint32_t load_offset;
    int32_t load_size;
    /* Owned logical NTSC clock: 32-bit fields avoid host-dependent padding.
       No wall clock, OS timer thread or shared native time offset is used. */
    uint32_t clock_enabled;
    uint32_t clock_base_hi, clock_base_lo;
    uint32_t clock_count_hi, clock_count_lo;
    uint32_t clock_offset_hi, clock_offset_lo;
    uint32_t pending_scene_site;
    // Private fixed-cadence VI state, not native registers or a GPU queue.
    uint32_t video_enabled, video_black, video_frames;
    uint32_t video_framebuffer, video_depthbuffer;
    uint32_t scene_unload_phase; // 0 running, 1 teardown, 2 retired, 3 constructing, 4 prepared.
    uint32_t scene_load_resets; // Exactly one owned reset in a retail constructor.
    uint32_t title_tail_phase, title_tail_units;
    uint32_t owner_mask; // Immutable contiguous lobby membership, not scene viewport count.
    uint32_t host_control, assigned_ports_released;
    uint32_t initial_world_vacant; // Genuine pre-menu cold boot, not a skipped teardown.
    uint32_t character_animation_active, character_animation_phase; // IEEE-754 phase bits.
    uint32_t postrace_contracted, postrace_pending; // Owned wooden-frame gate, not renderer globals.
    uint32_t preview_loads; // Confirmed preview replacements; renderer history is separate from the network epoch.
    uint32_t restore_multiplayer_music; // Host's initial policy, admitted/captured with the owned world.
    dkr_owned_identity_state presentation;
} dkr_probe_native_state;
dkr_probe_native_state dkr_probe_native_capture(void);
void dkr_probe_native_restore(dkr_probe_native_state state);
void dkr_probe_draw_begin(void);
unsigned dkr_probe_draw_capture(dkr_owned_draw_event* events, unsigned capacity);
// Optional read-only scene capture. Bound by the exclusively owned CPU thread;
// reset/object notifications may not throw or write guest state. The renderer
// receives immutable copied assets, never this temporary RDRAM pointer.
typedef void (*dkr_probe_scenery_observer)(void*,const uint8_t*,uint32_t,uint32_t,uint32_t);
void dkr_probe_scenery_observe(dkr_probe_scenery_observer observer,void* user);
void dkr_probe_local_world_draw(uint8_t* ram,struct recomp_context* context,unsigned pass);
void dkr_probe_scenery_addresses(uint32_t addresses[11]);
void dkr_probe_presentation_addresses(uint32_t addresses[8]);
int dkr_probe_postrace_scope(int mode,int postrace,int players,int trophy,int layout);
int dkr_probe_contracted_wood(const int32_t bounds[4]);
int dkr_probe_lens_scope(const int32_t bounds[8],int tracks);
void dkr_probe_clock_start(uint64_t initial_count);
void dkr_probe_clock_advance(void);
void dkr_probe_video_start(uint8_t* ram);
void dkr_probe_video_begin_frame(uint8_t* ram);
void dkr_probe_video_finish_frame(void);
/* Private world owns no asynchronous pre-NMI producer. Only the exact empty,
   nonblocking reset poll is supported; messages/blocking/foreign queues trap. */
void dkr_probe_reset_poll_configure(int enabled);
/* Private synchronous audio owner. Exact retail audio ROM-DMA queue and
   reviewed guest callbacks only; no AI/VI/SP/DP or device completion. */
void dkr_probe_audio_configure(int enabled);
int dkr_probe_audio_enabled(void);
/* Private Patch Pipeline continuation. No C/native thread stack is retained.
   An intent does not load/free a scene. Resumption requires explicit confirmed
   owner authorization; the real loader's unowned imports STILL trap. */
void dkr_probe_scene_configure(int enabled);
unsigned dkr_probe_scene_entry(void);
int dkr_probe_scene_cut(unsigned site);
int dkr_probe_scene_authorize(unsigned confirmed_site);
unsigned dkr_probe_scene_pending(void);
/* Only the private owner may arm this AFTER confirmed intent and immutable
   render leases drain. No production runtime/GP worker links into this target. */
int dkr_probe_scene_unload_authorize(unsigned confirmed_site);
void dkr_probe_scene_unload_begin(void);
void dkr_probe_scene_unload_render_drained(void);
void dkr_probe_scene_unload_end(void);
void dkr_probe_scene_load_begin(uint8_t* ram, struct recomp_context* context);
void dkr_probe_scene_load_reset(uint8_t* ram, struct recomp_context* context);
void dkr_probe_scene_load_ready(uint8_t* ram, struct recomp_context* context);
void dkr_probe_menu_load_begin(uint8_t* ram, struct recomp_context* context);
void dkr_probe_menu_construct_begin(void);
void dkr_probe_menu_construct_ready(uint8_t* ram, struct recomp_context* context);
// Noncheckpointed permission for ONE already-agreed menu tick. Its renderer
// reads a separate owned image; actual epoch/restore drains are independent.
// Cleared before checkpoint capture; never enabled by a guest/native import.
void dkr_probe_menu_tick_configure(int confirmed);
// Copied-image runtime only: service Track Select's pending background before
// the next confirmed CPU draw, without creating a gameplay scene epoch.
void dkr_probe_menu_preview_configure(int enabled);
void dkr_probe_menu_preview_begin(uint8_t* ram, struct recomp_context* context);
int dkr_probe_menu_callback_enabled(void);
void dkr_probe_menu_level_change_begin(uint8_t* ram, struct recomp_context* context);
void dkr_probe_menu_level_change_ready(uint8_t* ram, struct recomp_context* context);
void dkr_probe_menu_tick_finish(void);
void dkr_probe_menu_tick_end(uint8_t* ram);
void dkr_probe_menu_background_request(uint8_t* ram, struct recomp_context* context);
void dkr_probe_menu_background_cut(uint8_t* ram);
void dkr_probe_menu_background_resume(uint8_t* ram, struct recomp_context* context);
int dkr_probe_scene_transaction_cut(unsigned confirmed_site);
void dkr_probe_scene_transaction_end(void);
typedef struct dkr_probe_effect {
    uint32_t kind;
    uint32_t object;
} dkr_probe_effect;
// A confirmed retail level constructor can spawn more than 256 objects.
// Keep a hard 8-KiB event cap; unknown/unbounded construction still fences.
enum { DKR_PROBE_MAX_OBJECT_EFFECTS = 1024 };
void dkr_probe_effects_begin(void);
unsigned dkr_probe_effects_capture(dkr_probe_effect* effects, unsigned capacity);
/* Private owned EEPROM import bridge. Disabled unless the standalone owner
   binds its initialized Eeprom component. Never a live save/file fallback. */
void dkr_probe_bind_eeprom(void* owned_eeprom);
int dkr_probe_eeprom_enabled(void);
int dkr_probe_eeprom_transfer(int write, unsigned offset, uint8_t* bytes, unsigned count);
/* Pure policies: return before any C probe trap; no live/native world state. */
int dkr_probe_valid_material_pointer(uint32_t address, uint32_t final_offset);
uint32_t dkr_probe_material_flags(const uint32_t fields[13], uint32_t effective);
void dkr_probe_sky_scales(int viewport_layout, float* horizontal, float* vertical);
void dkr_probe_void_basis_addresses(uint32_t addresses[4]);
uint32_t dkr_probe_fullscreen_clear_scissor(uint32_t lower_right);
int dkr_probe_valid_scissor_pointer(uint32_t command);
int dkr_probe_valid_preview_pointer(uint32_t viewport);
void dkr_probe_title_tail_addresses(uint32_t addresses[2]);
int dkr_probe_title_tail_valid(uint32_t phase, uint32_t units);
int dkr_probe_title_tail_update(uint32_t* phase, uint32_t* units,
    int cinematic_complete, int first_title_demo, int title_revealed, uint32_t update_rate);
void dkr_probe_roster_addresses(uint32_t addresses[7]);
void dkr_probe_roster_seed(uint32_t mask, int8_t active[4], int8_t characters[8], uint8_t ids[16]);
uint32_t dkr_probe_roster_buttons(int occupied, int8_t status, uint32_t buttons);
void dkr_probe_character_music_addresses(uint32_t addresses[6]);
uint32_t dkr_probe_character_music_mask(int selected);
uint32_t dkr_probe_character_animation_advance(uint32_t phase, int update_rate, int tempo);
int dkr_probe_character_animation_valid(uint32_t active, uint32_t phase);
int dkr_probe_fullscreen_preview_bounds(const int32_t viewport[8]);
void dkr_probe_parity_addresses(uint32_t addresses[24]);
uint32_t dkr_probe_object_identity(uint32_t scene,uint32_t object,uint32_t generation,uint16_t id,uint16_t behaviour);
uint32_t dkr_probe_camera_identity(uint32_t scene,uint32_t camera,uint32_t epoch,unsigned role);
uint32_t dkr_probe_matrix_identity(uint32_t object,uint32_t ordinal,uint32_t camera);
uint32_t dkr_probe_wave_identity(uint32_t scene,uint32_t viewport,uint32_t block,const uint32_t fields[7],uint32_t camera);
int dkr_probe_camera_discontinuous(const uint32_t previous[4],const uint32_t current[4]);
void dkr_probe_hud_element(uint32_t element,uint32_t fields[4]);
int dkr_probe_finish_shot(uint32_t mode,uint32_t previous_mode,uint32_t owner,uint32_t node,
    uint32_t previous_owner,uint32_t previous_node);
void dkr_probe_geometry_key(uint32_t scene,uint32_t address,unsigned segment,unsigned pass,uint32_t fields[2]);
uint32_t dkr_probe_part_identity(uint32_t owner,uint32_t ordinal,uint32_t transform,int mirrored,uint32_t camera);
unsigned dkr_probe_address_variant(uint32_t address);
unsigned dkr_probe_part_variant(uint32_t frame,uint32_t count);
unsigned dkr_probe_water_tag(uint32_t x,uint32_t y);
#ifdef __cplusplus
}
#endif
#endif
