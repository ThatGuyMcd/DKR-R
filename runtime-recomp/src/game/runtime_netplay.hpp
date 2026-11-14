#pragma once

#include "netplay/direct_session.hpp"
#include "netplay/netplay_pacing_policy.hpp"
#include "online_wait_state.hpp"
#include "recomp.h"

#include <cstdint>

namespace dkr::runtime::netplay {

DirectSession& session();
// Alternate generated game engines execute inside the public process on
// Windows. Bind them to the launcher-owned session so revision selection can
// never create a second lobby or lose an established online connection.
void bind_external_session(DirectSession* external_session);
void reset_runtime_state();
void register_runtime_context(std::uint8_t* rdram, recomp_context* context);
void unregister_runtime_context(std::uint8_t* rdram, recomp_context* context);
void on_frame_boundary(std::uint8_t* rdram, recomp_context* context);
void prepare_controller_init();
void seed_character_select_ai(std::uint8_t* rdram, recomp_context* context);
void resolve_authored_input_frame(std::uint8_t* rdram, recomp_context* context);
std::uint32_t authored_frame();
void begin_gameplay_level(std::uint8_t* rdram, recomp_context* context);
void complete_gameplay_level(std::uint8_t* rdram, recomp_context* context);
void end_gameplay_level();
void seal_race_finish(std::uint8_t* rdram, std::uint8_t transition_kind);
// Closes an authored gameplay tick against Player 1's portable state. Clients
// correct only simulation-owned fields and leave presentation/audio state
// local, which prevents visual history and host pointers crossing machines.
void commit_authoritative_gameplay_frame(std::uint8_t* rdram);
// Called from thread3_main immediately before its ordinary main_game_loop
// call. Returns non-zero only when rollback has already driven (or safely
// stalled) this authored tick and the generated call must be skipped.
int drive_authored_tick(std::uint8_t* rdram, recomp_context* context);
bool rollback_replay_active();
bool external_side_effects_allowed();
// Fixed-point multiplier consumed by the outer VI scheduler. Player 1 and all
// lifecycle barriers always publish 1000 (1.0x); authenticated clients may
// briefly publish up to 1333 while retiring bounded gameplay or frontend debt.
std::uint32_t authored_simulation_pacing_scale_milli();

// Presentation-only status published by the authored tick gate. It never
// changes simulation timing; the overlay uses it to explain a deliberate,
// non-blocking wait instead of making a synchronized transition look frozen.
OnlineWaitReason online_wait_reason();
std::uint64_t online_wait_generation();
std::uint64_t online_wait_episode();
bool online_wait_active();
bool physical_input_poll_allowed();
void begin_presentation_random_scope();
void end_presentation_random_scope();
bool override_presentation_random_range(recomp_context* context);

struct RollbackMetrics {
    bool active = false;
    bool replaying = false;
    std::uint32_t simulation_frame = 0U;
    std::uint32_t rollback_count = 0U;
    std::uint32_t replayed_frames = 0U;
    std::uint32_t largest_rollback = 0U;
    float frames_ahead = 0.0F;
    FrameDebtSample frame_debt{};
    ClientCatchUpState catch_up_state = ClientCatchUpState::Normal;
    std::uint32_t pacing_scale_milli = 1000U;
    std::uint32_t pacing_target_hz = 30U;
    std::uint32_t target_frame_debt = 0U;
    std::uint32_t input_epoch = 0U;
    std::uint32_t scene_epoch = 0U;
};
RollbackMetrics rollback_metrics();

} // namespace dkr::runtime::netplay

extern "C" void dkr_netplay_character_select_enter(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_netplay_character_select_lock(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_netplay_character_select_ai_seed(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_netplay_gameplay_level_begin(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_netplay_gameplay_level_ready(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_netplay_gameplay_level_end(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_netplay_prepare_controller_init(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_netplay_resolve_authored_input_frame(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_netplay_authoritative_frame_commit(std::uint8_t* rdram, recomp_context* context);
extern "C" int dkr_netplay_drive_authored_tick(std::uint8_t* rdram, recomp_context* context);
extern "C" int dkr_netplay_presentation_output_allowed();
extern "C" void dkr_netplay_presentation_random_begin(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_netplay_presentation_random_end(std::uint8_t* rdram, recomp_context* context);
extern "C" int dkr_netplay_presentation_random_range(std::uint8_t* rdram, recomp_context* context);
