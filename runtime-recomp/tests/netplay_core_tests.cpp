#include "netplay_lobby.hpp"
#include "netplay_build_identity.hpp"
#include "netplay_pacing_policy.hpp"
#include "two_player_adventure_policy.hpp"
#include "netplay_protocol.hpp"
#include "netplay_timeline.hpp"
#include "authoritative_state.hpp"
#include "determinism_state_hash.hpp"
#include "revision_addresses.hpp"
#include "rollback_ring.hpp"
#include "session_transport.hpp"
#include "authoritative_state_codec.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace {

dkr::runtime::netplay::CompatibilityManifest Manifest() {
    using namespace dkr::runtime::netplay;
    CompatibilityManifest manifest{};
    manifest.release_version = "1.0.0";
    manifest.build_fingerprint = "test-build";
    manifest.revision = Revision::UsV77;
    manifest.canonical_rom_hash = 0x1234U;
    manifest.patch_policy_hash = 0x2345U;
    manifest.gameplay_settings_hash = 0x3456U;
    manifest.magic_codes_hash = 0x4567U;
    manifest.session_save_hash = 0x5678U;
    manifest.architecture = "x86_64";
    manifest.floating_point_mode = "strict-v1";
    return manifest;
}

} // namespace

int main() {
    using namespace dkr::runtime::netplay;

    // Post-race rendering hitches must not advance the host's transition
    // timer faster than the guest's identical committed simulation ticks.
    int host_timer = 0, client_timer = 0;
    for (int tick = 0; tick < 120; ++tick) {
        host_timer += authored_logic_step(true, tick % 7 == 0 ? 6 : 2);
        client_timer += authored_logic_step(true, tick % 11 == 0 ? 4 : 2);
        assert(host_timer == client_timer);
    }
    assert(host_timer == 240);
    assert(authored_logic_step(false, 6) == 6); // Single-player stays retail.
    for (const int requested : {kHubWorldRaceType, kBossRaceType,
                                kCutsceneRaceType1, kCutsceneRaceType2}) {
        for (const int resolved : {kCutsceneRaceType1, kCutsceneRaceType2}) {
            assert(two_player_adventure_empty_cutscene_ready(
                true, true, true, requested, resolved, 1U, 0U, 2U));
        }
    }
    assert(!two_player_adventure_empty_cutscene_ready(
        true, true, true, kDefaultRaceType, kCutsceneRaceType1, 1U, 0U, 2U));
    assert(!two_player_adventure_empty_cutscene_ready(
        true, true, true, kHubWorldRaceType, kHubWorldRaceType, 1U, 0U, 2U));
    assert(!two_player_adventure_empty_cutscene_ready(
        true, true, true, kBossRaceType, kBossRaceType, 1U, 0U, 2U));
    assert(!two_player_adventure_empty_cutscene_ready(
        true, true, true, kHubWorldRaceType, kCutsceneRaceType1, 1U, 1U, 2U));
    assert(!two_player_adventure_empty_cutscene_ready(
        false, true, true, kHubWorldRaceType, kCutsceneRaceType1, 1U, 0U, 2U));
    assert(!two_player_adventure_empty_cutscene_ready(
        true, false, true, kHubWorldRaceType, kCutsceneRaceType1, 1U, 0U, 2U));
    assert(!two_player_adventure_empty_cutscene_ready(
        true, true, false, kHubWorldRaceType, kCutsceneRaceType1, 1U, 0U, 2U));
    assert(!two_player_adventure_empty_cutscene_ready(
        true, true, true, kHubWorldRaceType, kCutsceneRaceType1, 2U, 0U, 2U));
    assert(!two_player_adventure_empty_cutscene_ready(
        true, true, true, kHubWorldRaceType, kCutsceneRaceType1, 1U, 0U, 3U));

    // Model the actual outer-loop cadence with a fixed 30 Hz host. Every
    // client dispatch consumes one already committed frame, never a hidden
    // native tick. Six excess frames must drain to the one-frame deadband.
    {
        ClientCatchUpController controller;
        constexpr std::uint32_t target = 2U;
        double elapsed = 0.0;
        std::uint32_t consumed = 0U;
        std::uint32_t debt = target + 6U;
        while (elapsed < 2.0 && debt > target + 1U) {
            const auto decision = controller.update({true, debt, target, debt});
            assert(decision.pacing_scale_milli <= 1333U);
            elapsed += 1000.0 / (30.0 * decision.pacing_scale_milli);
            ++consumed;
            debt = target + 6U + static_cast<std::uint32_t>(elapsed * 30.0) - consumed;
            assert(debt >= target);
        }
        std::fprintf(stderr, "Six-frame backlog: %.3f seconds, %u excess frames remain\n",
                     elapsed, debt - target);
        assert(debt <= target + 1U && elapsed <= 2.0);
    }

    // Admission failures keep their machine-readable detail for recovery UI,
    // while the player-facing message stays concise. Magic Codes are checked
    // before the aggregate gameplay-settings hash so the launcher can name
    // the exact toggles that differ.
    {
        CompatibilityManifest host = Manifest();
        CompatibilityManifest racer = host;
        racer.magic_codes_hash ^= 0x21U;
        racer.gameplay_settings_hash ^= 0x100U;
        const std::string reason = incompatibility_reason(host, racer);
        assert(reason.rfind("Magic Codes do not match.|magic:", 0U) == 0U);

        const OnlineFailure failure = classify_online_failure(reason);
        assert(failure.code == OnlineFailureCode::MagicCodesMismatch);
        assert(failure.has_magic_code_details);
        assert(failure.expected_magic_codes == host.magic_codes_hash);
        assert(failure.candidate_magic_codes == racer.magic_codes_hash);
        assert(online_failure_display_message(reason) ==
               "Magic Codes do not match.");

        assert(classify_online_failure(
                   "The host did not answer the encrypted join request. "
                   "Verify that the host UDP port is reachable.")
                   .code == OnlineFailureCode::HostUnreachable);
        assert(classify_online_failure(
                   "Rollback determinism failed at simulation frame 35.")
                   .code == OnlineFailureCode::DeterminismFailure);
    }

    static_assert(kAuthoritativeDeltaKeyframeInterval %
                      kLiveReplicaCorrectionInterval == 0U,
                  "Every reliable replica anchor must be a correction frame.");
    assert(live_replica_correction_due(0U));
    assert(!live_replica_correction_due(1U));
    assert(!live_replica_correction_due(2U));
    assert(!live_replica_correction_due(3U));
    assert(!live_replica_correction_due(5U));
    assert(live_replica_correction_due(6U));
    assert(live_replica_correction_due(kAuthoritativeDeltaKeyframeInterval));
    assert(next_live_replica_frame(0U) == 6U);
    assert(next_live_replica_frame(1U) == 6U);
    assert(next_live_replica_frame(6U) == 12U);
    std::uint32_t first_replica = 0U;
    std::uint32_t last_replica = 0U;
    assert(!replica_fast_forward_window(
        100U, 103U, 2U, first_replica, last_replica));
    assert(replica_fast_forward_window(
        100U, 104U, 2U, first_replica, last_replica));
    assert(first_replica == 102U);
    assert(last_replica == 102U);
    // A published boundary only one frame ahead is deliberately left to the
    // normal timeline; a correction must remove at least two real frames.
    assert(!replica_fast_forward_window(
        101U, 104U, 2U, first_replica, last_replica));
    assert(replica_fast_forward_window(
        100U, 108U, 2U, first_replica, last_replica));
    assert(first_replica == 102U);
    assert(last_replica == 106U);
    assert(replica_fast_forward_window(
        102U, 110U, 2U, first_replica, last_replica));
    assert(first_replica == 108U);
    assert(last_replica == 108U);
    std::uint32_t corrections_per_second = 0U;
    for (std::uint32_t frame = 0U; frame < 30U; ++frame) {
        if (live_replica_correction_due(frame)) ++corrections_per_second;
    }
    assert(corrections_per_second == 5U);
    assert(kLiveReplicaRecoveryHistoryFrames == 48U);
    assert(live_replica_oldest_retained_frame(20U) == 0U);
    assert(live_replica_oldest_retained_frame(80U) == 32U);
    assert(live_replica_within_recovery_window(100U, 52U));
    assert(live_replica_within_recovery_window(100U, 148U));
    assert(!live_replica_within_recovery_window(100U, 51U));
    assert(!live_replica_within_recovery_window(100U, 149U));

    assert(rollback_replica_target_debt(0U, 0U, 5U) == 2U);
    assert(rollback_replica_target_debt(100U, 10U, 5U) == 2U);
    assert(rollback_replica_target_debt(500U, 100U, 5U) == 7U);
    assert(authored_timeline_target_debt(0U, 0U, 2U) == 2U);
    assert(authored_timeline_target_debt(100U, 10U, 5U) == 2U);
    assert(authored_timeline_target_debt(500U, 100U, 5U) == 7U);
    assert(authored_catch_up_budget(1U, 2U, 6U, 6U) == 1U);
    // A client may be several commits behind after an OS/GPU scheduling
    // hitch, but one outer authored dispatch must never nest multiple retail
    // main_game_loop calls. Frame debt is recovered by an atomic Player-1
    // replica hand-off, never by replaying hidden simulation ticks inside one
    // native call stack.
    assert(authored_catch_up_budget(8U, 2U, 6U, 6U) == 1U);
    assert(authored_catch_up_budget(8U, 2U, 3U, 6U) == 1U);
    assert(authored_catch_up_budget(8U, 2U, 0U, 6U) == 0U);

    // Client pacing changes only the outer authored-VI interval. It requires a
    // persistent debt signal, caps at 40 Hz, settles conservatively, and
    // returns immediately to retail pacing if complete authority data is no
    // longer available.
    assert(client_catch_up_rate_hz(3U, 2U) == 30U);
    assert(client_catch_up_rate_hz(4U, 2U) == 32U);
    assert(client_catch_up_rate_hz(7U, 2U) == 34U);
    assert(client_catch_up_rate_hz(10U, 2U) == 36U);
    assert(client_catch_up_rate_hz(14U, 2U) == 38U);
    assert(client_catch_up_rate_hz(18U, 2U) == 38U);
    assert(client_catch_up_rate_hz(40U, 2U) == 40U);
    assert(client_catch_up_scale_milli(30U) == 1000U);
    assert(client_catch_up_scale_milli(40U) == 1333U);
    assert(checkpoint_retry_milliseconds(0U, 0U) == 100U);
    assert(checkpoint_retry_milliseconds(100U, 20U) == 340U);
    assert(checkpoint_retry_milliseconds(300U, 30U) == 760U);
    assert(checkpoint_retry_milliseconds(65535U, 65535U) == 1000U);

    ClientCatchUpController catch_up;
    const ClientCatchUpSample delayed{true, 20U, 2U, 3U};
    auto catch_up_decision = catch_up.update(delayed);
    assert(catch_up_decision.state == ClientCatchUpState::Normal);
    catch_up_decision = catch_up.update(delayed);
    assert(catch_up_decision.state == ClientCatchUpState::Normal);
    catch_up_decision = catch_up.update(delayed);
    assert(catch_up_decision.state == ClientCatchUpState::CatchUp);
    assert(catch_up_decision.pacing_scale_milli > 1000U);
    for (int sample = 0; sample < 8; ++sample) {
        catch_up_decision = catch_up.update(delayed);
    }
    assert(catch_up_decision.pacing_scale_milli == 1333U);
    assert(catch_up_decision.target_simulation_hz == 40U);

    const ClientCatchUpSample settled{true, 3U, 2U, 3U};
    for (int sample = 0; sample < 5; ++sample) {
        catch_up_decision = catch_up.update(settled);
        assert(catch_up_decision.state == ClientCatchUpState::CatchUp);
    }
    catch_up_decision = catch_up.update(settled);
    assert(catch_up_decision.state == ClientCatchUpState::Cooldown);
    for (int sample = 0; sample < 20; ++sample) {
        catch_up_decision = catch_up.update(settled);
    }
    assert(catch_up_decision.state == ClientCatchUpState::Normal);
    assert(catch_up_decision.pacing_scale_milli == 1000U);

    catch_up.update(delayed);
    catch_up.update(delayed);
    catch_up_decision = catch_up.update(delayed);
    assert(catch_up_decision.state == ClientCatchUpState::CatchUp);
    catch_up_decision = catch_up.update(
        ClientCatchUpSample{true, 20U, 2U, 0U});
    assert(catch_up_decision.state == ClientCatchUpState::CatchUp);
    assert(catch_up_decision.pacing_scale_milli == 1000U);
    for (int missing = 1; missing < 6; ++missing) {
        catch_up_decision = catch_up.update({true, 20U, 2U, 0U});
    }
    assert(catch_up_decision.state == ClientCatchUpState::Normal);
    assert(catch_up_decision.pacing_scale_milli == 1000U);
    // Host lead covers the complete progress-acknowledgement journey. The LAN
    // floor prevents a normal three-to-five-frame in-flight window from
    // degenerating into alternating simulation and parked ticks; the WAN cap
    // still prevents the unbounded authority lead that caused old freezes.
    assert(host_authority_lead_limit(0U, 0U, 2U) == 8U);
    assert(host_authority_lead_limit(100U, 10U, 5U) == 11U);
    assert(host_authority_lead_limit(500U, 100U, 5U) == 24U);
    assert(!host_backpressure_required(105U, 97U, 8U));
    assert(host_backpressure_required(106U, 97U, 8U));
    assert(!host_backpressure_required(0U, 0U, 8U));
    assert(host_backpressure_release_limit(8U) == 5U);
    assert(host_backpressure_release_limit(3U) == 2U);
    assert(effective_host_authority_lead_limit(
               SynchronizationMode::Lockstep, 8U) == 8U);
    assert(effective_host_authority_lead_limit(
               SynchronizationMode::Rollback, 8U) == 8U);
    assert(effective_host_authority_lead_limit(
               SynchronizationMode::Rollback, 24U) == 24U);
    assert(host_authoritative_input_delay_frames(
               0.0, 0.0, 0.0F, true) == 2U);
    assert(host_authoritative_input_delay_frames(
               0.0, 0.0, 0.0F, false) == 2U);
    assert(host_authoritative_input_delay_frames(
               100.0, 10.0, 0.0F, false) == 5U);
    assert(host_authoritative_input_delay_frames(
               500.0, 100.0, 15.0F, false) ==
           kMaximumInputDelayFrames);
    assert(steady_commit_history_count(false) == 3U);
    assert(steady_commit_history_count(true) == 6U);
    assert(host_may_predict_input(false, SynchronizationMode::Lockstep));
    assert(host_may_predict_input(false, SynchronizationMode::Rollback));
    assert(!host_may_predict_input(true, SynchronizationMode::Lockstep));
    assert(host_may_predict_input(true, SynchronizationMode::Rollback));

    assert(frame_debt_phase_allows_catch_up(FrameDebtPhase::Frontend));
    assert(frame_debt_phase_allows_catch_up(FrameDebtPhase::Gameplay));
    assert(frame_debt_phase_allows_catch_up(FrameDebtPhase::PostRace));
    assert(!frame_debt_phase_allows_catch_up(
        FrameDebtPhase::LoadingBarrier));
    assert(!frame_debt_phase_allows_catch_up(
        FrameDebtPhase::GameplayStartBarrier));
    assert(!frame_debt_phase_allows_catch_up(
        FrameDebtPhase::FinishBarrier));
    assert(!frame_debt_phase_allows_catch_up(
        FrameDebtPhase::RecoveryBarrier));
    assert(!frame_debt_phase_allows_catch_up(FrameDebtPhase::Inactive));
    assert(frame_debt_phase_allows_host_backpressure(
        FrameDebtPhase::Frontend));
    assert(frame_debt_phase_allows_host_backpressure(
        FrameDebtPhase::PostRace));
    assert(!frame_debt_phase_allows_host_backpressure(
        FrameDebtPhase::FinishBarrier));

    // Frontend recovery begins after three sustained samples only three frames
    // beyond its delivery target; it no longer waits for an absolute ten-frame
    // delay before reacting on a healthy LAN.
    ClientCatchUpController frontend_catch_up;
    const ClientCatchUpSample frontend_delayed{true, 5U, 2U, 1U};
    assert(frontend_catch_up.update(frontend_delayed).state ==
           ClientCatchUpState::Normal);
    assert(frontend_catch_up.update(frontend_delayed).state ==
           ClientCatchUpState::Normal);
    assert(frontend_catch_up.update(frontend_delayed).state ==
           ClientCatchUpState::CatchUp);
    assert(frontend_catch_up.update(
               ClientCatchUpSample{false, 5U, 2U, 1U}).state ==
           ClientCatchUpState::Normal);

    // JOINTVENTURE keeps network slots stable. Menus remain host-guided,
    // retail input_swap_id chooses the shared-hub lead, barriers accept no
    // stale authored input, and races return to ordinary two-racer gameplay.
    assert(two_player_adventure_phase(
               true, false, true, false, FrameDebtPhase::Frontend) ==
           TwoPlayerAdventurePhase::GuidedMenus);
    assert(two_player_adventure_phase(
               true, true, true, false, FrameDebtPhase::Frontend) ==
           TwoPlayerAdventurePhase::SharedHubPlayer1);
    assert(two_player_adventure_phase(
               true, true, true, true, FrameDebtPhase::Frontend) ==
           TwoPlayerAdventurePhase::SharedHubPlayer2);
    assert(two_player_adventure_phase(
               true, true, true, true, FrameDebtPhase::LoadingBarrier) ==
           TwoPlayerAdventurePhase::TransitionBarrier);
    assert(two_player_adventure_phase(
               true, true, true, true, FrameDebtPhase::Gameplay) ==
           TwoPlayerAdventurePhase::DualGameplay);
    assert(two_player_adventure_accepts_assigned_input(
        TwoPlayerAdventurePhase::SharedHubPlayer2));
    assert(!two_player_adventure_accepts_assigned_input(
        TwoPlayerAdventurePhase::TransitionBarrier));
    assert(two_player_adventure_shared_hub_topology_ready(
        true, true, true, kHubWorldRaceType, kHubWorldRaceType,
        1U, 1U, 2U));
    assert(two_player_adventure_shared_hub_topology_ready(
        true, true, true, kHubWorldRaceType, kCutsceneRaceType1,
        1U, 10U, 2U));
    assert(!two_player_adventure_shared_hub_topology_ready(
        false, true, true, kHubWorldRaceType, kHubWorldRaceType,
        1U, 1U, 2U));
    assert(!two_player_adventure_shared_hub_topology_ready(
        true, false, true, kHubWorldRaceType, kHubWorldRaceType,
        1U, 1U, 2U));
    assert(!two_player_adventure_shared_hub_topology_ready(
        true, true, false, kHubWorldRaceType, kHubWorldRaceType,
        1U, 1U, 2U));
    assert(!two_player_adventure_shared_hub_topology_ready(
        true, true, true, kHubWorldRaceType, kHubWorldRaceType,
        2U, 2U, 2U));
    assert(!two_player_adventure_shared_hub_topology_ready(
        true, true, true, kHubWorldRaceType, kHubWorldRaceType,
        1U, 0U, 2U));
    assert(!two_player_adventure_shared_hub_topology_ready(
        true, true, true, kHubWorldRaceType, kHubWorldRaceType,
        1U, 11U, 2U));
    assert(!two_player_adventure_shared_hub_topology_ready(
        true, true, true, kHubWorldRaceType, kHubWorldRaceType,
        1U, 1U, 3U));
    assert(!two_player_adventure_shared_hub_topology_ready(
        true, true, true, kBossRaceType, kBossRaceType,
        1U, 2U, 2U));
    // Retail deliberately leaves gNumberOfActivePlayers at one in
    // JOINTVENTURE, then creates two human racers inside a six-racer normal
    // race or a four-racer challenge. The v1.0.3 hub/boss split must retain
    // this third topology or Player 2 never reaches the gameplay input phase.
    assert(two_player_adventure_dual_gameplay_topology_ready(
        true, true, true, kDefaultRaceType, kDefaultRaceType,
        1U, kTwoPlayerAdventureRaceRacerCount, 2U));
    assert(two_player_adventure_dual_gameplay_topology_ready(
        true, true, true, 64, 64,
        1U, kTwoPlayerAdventureChallengeRacerCount, 2U));
    assert(two_player_adventure_dual_gameplay_topology_ready(
        true, true, true, 65, 65,
        1U, kTwoPlayerAdventureChallengeRacerCount, 2U));
    assert(!two_player_adventure_dual_gameplay_topology_ready(
        true, true, true, kDefaultRaceType, kDefaultRaceType,
        1U, 2U, 2U));
    assert(!two_player_adventure_dual_gameplay_topology_ready(
        true, true, true, 64, 64,
        1U, kTwoPlayerAdventureRaceRacerCount, 2U));
    assert(!two_player_adventure_dual_gameplay_topology_ready(
        true, true, true, kDefaultRaceType, kCutsceneRaceType1,
        1U, kTwoPlayerAdventureRaceRacerCount, 2U));
    assert(!two_player_adventure_dual_gameplay_topology_ready(
        true, true, true, kBossRaceType, kBossRaceType,
        1U, 2U, 2U));
    assert(!two_player_adventure_dual_gameplay_topology_ready(
        true, true, true, kHubWorldRaceType, kHubWorldRaceType,
        1U, 1U, 2U));
    assert(!two_player_adventure_dual_gameplay_topology_ready(
        true, true, false, kDefaultRaceType, kDefaultRaceType,
        1U, kTwoPlayerAdventureRaceRacerCount, 2U));
    assert(!two_player_adventure_dual_gameplay_topology_ready(
        true, true, true, kDefaultRaceType, kDefaultRaceType,
        2U, kTwoPlayerAdventureRaceRacerCount, 2U));
    assert(!two_player_adventure_dual_gameplay_topology_ready(
        true, true, true, kDefaultRaceType, kDefaultRaceType,
        1U, kTwoPlayerAdventureRaceRacerCount, 3U));
    const std::array<std::uint8_t, 20U> retail_boss_intros{
        38, 57, 46, 57, 40, 61, 53, 61, 1, 59, 52, 59,
        41, 58, 54, 58, 37, 60, 55, 62};
    for (std::size_t i = 0; i < retail_boss_intros.size(); i += 2U) {
        assert(boss_introduction_redirect_matches(retail_boss_intros,
            retail_boss_intros[i], retail_boss_intros[i + 1U]));
    }
    assert(!boss_introduction_redirect_matches(retail_boss_intros, 40, 57));
    assert(!boss_introduction_redirect_matches(retail_boss_intros, 40, 40));
    assert(!boss_introduction_redirect_matches({}, 40, 61));
    assert(two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kHubWorldRaceType, 1U, 1U, 2U, true));
    assert(!two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kHubWorldRaceType, 1U, 1U, 2U, false));
    assert(!two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kHubWorldRaceType, 1U, 2U, 2U, true));
    assert(two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kBossRaceType, 1U, 2U, 2U));
    assert(two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kCutsceneRaceType1,
        1U, 8U, 2U));
    assert(two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kCutsceneRaceType2,
        1U, 1U, 2U));
    assert(!two_player_adventure_boss_topology_ready(
        false, true, true, kBossRaceType, kBossRaceType, 1U, 2U, 2U));
    assert(!two_player_adventure_boss_topology_ready(
        true, false, true, kBossRaceType, kBossRaceType, 1U, 2U, 2U));
    assert(!two_player_adventure_boss_topology_ready(
        true, true, false, kBossRaceType, kBossRaceType, 1U, 2U, 2U));
    assert(!two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kBossRaceType, 2U, 2U, 2U));
    assert(!two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kBossRaceType, 1U, 1U, 2U));
    assert(!two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kBossRaceType, 1U, 3U, 2U));
    assert(!two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kBossRaceType, 1U, 2U, 3U));
    assert(!two_player_adventure_boss_topology_ready(
        true, true, true, kHubWorldRaceType, kBossRaceType, 1U, 2U, 2U));
    assert(!two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kHubWorldRaceType, 1U, 2U, 2U));
    assert(!two_player_adventure_boss_topology_ready(
        true, true, true, kBossRaceType, kCutsceneRaceType1,
        1U, 11U, 2U));
    assert(two_player_adventure_bootstrap_hub_topology_ready(
        true, true, true, kHubWorldRaceType, kHubWorldRaceType,
        1U, 1U, 2U));
    assert(two_player_adventure_bootstrap_hub_topology_ready(
        true, true, true, kHubWorldRaceType, kCutsceneRaceType2,
        1U, 10U, 2U));
    assert(!two_player_adventure_bootstrap_hub_topology_ready(
        true, true, false, kHubWorldRaceType, kHubWorldRaceType,
        1U, 1U, 2U));
    assert(!two_player_adventure_bootstrap_hub_topology_ready(
        true, true, true, 0, kHubWorldRaceType, 1U, 1U, 2U));
    assert(!two_player_adventure_bootstrap_hub_topology_ready(
        true, true, true, kBossRaceType, kCutsceneRaceType1,
        1U, 1U, 2U));
    assert(!two_player_adventure_bootstrap_hub_topology_ready(
        true, true, true, kHubWorldRaceType, kHubWorldRaceType,
        2U, 2U, 2U));
    assert(!two_player_adventure_bootstrap_hub_topology_ready(
        true, true, true, kHubWorldRaceType, kHubWorldRaceType,
        1U, 1U, 3U));

    // Player 1's coordinator is Boolean-owned and has no Gekko pointer. Both
    // coordinator implementations must therefore count as present, and only
    // active gameplay may expose their race-local cursor to debt telemetry.
    assert(rollback_coordinator_present(true, false));
    assert(rollback_coordinator_present(false, true));
    assert(!rollback_coordinator_present(false, false));
    assert(frame_debt_uses_rollback_cursor(
        FrameDebtPhase::Gameplay, true, true));
    assert(!frame_debt_uses_rollback_cursor(
        FrameDebtPhase::Frontend, false, true));
    assert(!frame_debt_uses_rollback_cursor(
        FrameDebtPhase::PostRace, false, true));
    assert(frame_debt_local_cursor(
               FrameDebtPhase::Gameplay, true, true, 900U, 905U) == 900U);
    assert(frame_debt_local_cursor(
               FrameDebtPhase::PostRace, false, true, 900U, 905U) == 905U);
    assert(frame_debt_local_cursor(
               FrameDebtPhase::Frontend, false, true, 900U, 1200U) == 1200U);

    // Debt is scoped to an actively advancing timeline. Frontend and
    // post-race frames use their own authored cursor; barriers are intentional
    // parks, and a retired input epoch is never presented as accumulated work.
    {
        const auto frontend = measure_frame_debt(
            FrameDebtPhase::Frontend, true, 103U, 100U);
        assert(frontend.valid);
        assert(!frontend.intentionally_parked);
        assert(frontend.debt == 3U);

        const auto caught_up = measure_frame_debt(
            FrameDebtPhase::Frontend, true, 100U, 103U);
        assert(caught_up.valid);
        assert(caught_up.debt == 0U);

        const auto stale_epoch = measure_frame_debt(
            FrameDebtPhase::Gameplay, false, 4000U, 20U);
        assert(!stale_epoch.valid);
        assert(stale_epoch.debt == 0U);

        const auto loading = measure_frame_debt(
            FrameDebtPhase::LoadingBarrier, true, 104U, 100U);
        assert(!loading.valid);
        assert(loading.intentionally_parked);
        assert(loading.debt == 0U);

        const auto finish = measure_frame_debt(
            FrameDebtPhase::FinishBarrier, true, 900U, 899U);
        assert(!finish.valid);
        assert(finish.intentionally_parked);
        assert(finish.debt == 0U);

        const auto results = measure_frame_debt(
            FrameDebtPhase::PostRace, true, 905U, 904U);
        assert(results.valid);
        assert(results.debt == 1U);
    }

    // A normal five-frame acknowledgement pipeline must sustain one authored
    // host tick per outer dispatch indefinitely. This directly guards the
    // former 15 Hz run/park oscillation seen after a race had warmed up.
    constexpr std::uint32_t lan_lead = 8U;
    std::uint32_t steady_host_next = 0U;
    for (std::uint32_t outer = 0U; outer < 1800U; ++outer) {
        const std::uint32_t acknowledged =
            steady_host_next > 5U ? steady_host_next - 5U : 0U;
        assert(!host_backpressure_required(
            steady_host_next, acknowledged, lan_lead));
        ++steady_host_next;
    }

    // A short client/GPU hitch may fill the bounded window, but advancing the
    // completion watermark must release Player 1 without changing either
    // machine's fixed 30 Hz simulation cadence or allowing unbounded debt.
    std::uint32_t hitch_host_next = 100U;
    std::uint32_t hitch_client_completed = 100U;
    bool observed_backpressure = false;
    bool observed_recovery = false;
    for (std::uint32_t outer = 0U; outer < 40U; ++outer) {
        const bool parked = host_backpressure_required(
            hitch_host_next, hitch_client_completed, lan_lead);
        observed_backpressure = observed_backpressure || parked;
        if (!parked) ++hitch_host_next;
        if (outer >= 12U &&
            hitch_client_completed < hitch_host_next) {
            ++hitch_client_completed;
        }
        if (observed_backpressure && !parked) observed_recovery = true;
        assert(hitch_host_next - hitch_client_completed <= lan_lead + 1U);
    }
    assert(observed_backpressure);
    assert(observed_recovery);

    assert(valid_quick_join_code("ABCDE"));
    assert(valid_quick_join_code("ab-c de"));
    assert(!valid_quick_join_code("ABCD"));
    assert(!valid_quick_join_code("ABCDO"));
    assert(!valid_quick_join_code("ABCDE1"));

    // Windows, Linux and AppImage package labels must resolve to one network
    // identity when they are built from the same repository VERSION.
    assert(canonical_network_release(" 1.0.0\r\n") == "1.0.0");
    assert(canonical_network_build_fingerprint("1.0.0") ==
           canonical_network_build_fingerprint(" 1.0.0\n"));

    std::string error;
    Lobby lobby;
    Rules rules{};
    assert(lobby.create("room-1", "ABC123", "Timber's Test Track",
                        Visibility::Private, "host", "Host", Manifest(),
                        rules, error));
    assert(lobby.room().players[0].host);
    assert(lobby.room().players[0].slot == 0U);

    auto second = lobby.join("peer-2", "Pipsy", Manifest(), error);
    auto third = lobby.join("peer-3", "Timber", Manifest(), error);
    assert(second && *second == 1U);
    assert(!third);
    assert(error == "This lobby is full.");
    assert(!lobby.can_start());

    assert(lobby.set_ready("host", true, error));
    assert(lobby.set_ready("peer-2", true, error));
    assert(lobby.can_start());

    assert(lobby.room().players[0].host);
    assert(lobby.room().players[0].peer_id == "host");
    assert(lobby.room().players[1].peer_id == "peer-2");

    assert(lobby.set_ready("host", true, error));
    assert(lobby.set_ready("peer-2", true, error));
    assert(lobby.begin_loading("host", error));
    assert(!lobby.begin_running("host", error));
    assert(lobby.set_loaded("host", true));
    assert(lobby.set_loaded("peer-2", true));
    assert(lobby.begin_running("host", error));

    CompatibilityManifest incompatible = Manifest();
    incompatible.revision = Revision::UsV80;
    Lobby rejected;
    assert(rejected.create("room-2", "ABC124", "Revision Test",
                           Visibility::Public, "host", "Host", Manifest(),
                           rules, error));
    assert(!rejected.join("bad", "Wrong Pak", incompatible, error));
    assert(!error.empty());

    protocol::InputBatch batch{};
    batch.epoch = 17U;
    batch.player_slot = 2U;
    batch.first_frame = 100U;
    batch.inputs = {{0x8000U, 50, -20}, {0x4000U, -10, 70}};
    batch.revisions = {2U, 37U};
    batch.simulation_progress_present = true;
    batch.simulation_scene_epoch = 9U;
    batch.simulation_completed_frame = 97U;
    const auto payload = protocol::encode_input_batch(batch);
    protocol::Datagram source{{protocol::MessageType::Input, 99U, 7U, 100U},
                              payload};
    const auto bytes = protocol::encode(source);
    protocol::Datagram decoded{};
    assert(protocol::decode(bytes, decoded, error));
    assert(decoded.header.match_id == 99U);
    assert(decoded.header.sequence == 7U);
    protocol::InputBatch decoded_batch{};
    assert(protocol::decode_input_batch(decoded.payload, decoded_batch, error));
    assert(decoded_batch.epoch == batch.epoch);
    assert(decoded_batch.player_slot == batch.player_slot);
    assert(decoded_batch.first_frame == batch.first_frame);
    assert(decoded_batch.inputs == batch.inputs);
    assert(decoded_batch.revisions == batch.revisions);
    auto bad_revision_batch = batch;
    bad_revision_batch.revisions[0] = 0U;
    assert(protocol::encode_input_batch(bad_revision_batch).empty());
    bad_revision_batch.revisions = {1U};
    assert(protocol::encode_input_batch(bad_revision_batch).empty());
    assert(decoded_batch.simulation_progress_present);
    assert(decoded_batch.simulation_scene_epoch == 9U);
    assert(decoded_batch.simulation_completed_frame == 97U);
    auto frontend_batch = batch;
    frontend_batch.simulation_scene_epoch = 0U;
    protocol::InputBatch decoded_frontend_batch{};
    assert(protocol::decode_input_batch(
        protocol::encode_input_batch(frontend_batch), decoded_frontend_batch,
        error));
    assert(decoded_frontend_batch.simulation_progress_present);
    assert(decoded_frontend_batch.simulation_scene_epoch == 0U);
    assert(decoded_frontend_batch.simulation_completed_frame == 97U);
    auto legacy_batch = batch;
    legacy_batch.simulation_progress_present = false;
    protocol::InputBatch decoded_legacy_batch{};
    assert(protocol::decode_input_batch(
        protocol::encode_input_batch(legacy_batch), decoded_legacy_batch,
        error));
    assert(!decoded_legacy_batch.simulation_progress_present);
    auto overflowing_batch = batch;
    overflowing_batch.first_frame = 0xFFFFFFFFU;
    assert(protocol::encode_input_batch(overflowing_batch).empty());

    const protocol::InputAcknowledgePayload input_ack{
        17U, 2U, 103U, 101U};
    protocol::InputAcknowledgePayload decoded_input_ack{};
    assert(protocol::decode_input_acknowledge(
        protocol::encode_input_acknowledge(input_ack), decoded_input_ack));
    assert(decoded_input_ack.epoch == input_ack.epoch);
    assert(decoded_input_ack.player_slot == input_ack.player_slot);
    assert(decoded_input_ack.newest_frame == input_ack.newest_frame);
    assert(decoded_input_ack.first_missing_frame ==
           input_ack.first_missing_frame);

    const protocol::InputRepairRequestPayload input_repair_request{
        17U, 2U, 41U};
    protocol::InputRepairRequestPayload decoded_input_repair_request{};
    assert(protocol::decode_input_repair_request(
        protocol::encode_input_repair_request(input_repair_request),
        decoded_input_repair_request));
    assert(decoded_input_repair_request.epoch ==
           input_repair_request.epoch);
    assert(decoded_input_repair_request.player_slot ==
           input_repair_request.player_slot);
    assert(decoded_input_repair_request.first_missing_frame ==
           input_repair_request.first_missing_frame);
    auto invalid_input_repair_request = input_repair_request;
    invalid_input_repair_request.epoch = 0U;
    assert(protocol::encode_input_repair_request(
               invalid_input_repair_request).empty());
    invalid_input_repair_request = input_repair_request;
    invalid_input_repair_request.player_slot = kMaximumPlayers;
    assert(protocol::encode_input_repair_request(
               invalid_input_repair_request).empty());

    protocol::Datagram input_repair_datagram{
        {protocol::MessageType::InputRepair, 99U, 8U, 41U}, payload};
    protocol::Datagram decoded_input_repair_datagram{};
    assert(protocol::decode(protocol::encode(input_repair_datagram),
                            decoded_input_repair_datagram, error));
    assert(decoded_input_repair_datagram.header.type ==
           protocol::MessageType::InputRepair);

    const protocol::SimulationProgressPayload simulation_progress{
        9U, 17U, 6179U, 2U};
    protocol::SimulationProgressPayload decoded_simulation_progress{};
    assert(protocol::decode_simulation_progress(
        protocol::encode_simulation_progress(simulation_progress),
        decoded_simulation_progress));
    assert(decoded_simulation_progress.scene_epoch == 9U);
    assert(decoded_simulation_progress.input_epoch == 17U);
    assert(decoded_simulation_progress.completed_frame == 6179U);
    assert(decoded_simulation_progress.player_slot == 2U);
    auto frontend_progress = simulation_progress;
    frontend_progress.scene_epoch = 0U;
    protocol::SimulationProgressPayload decoded_frontend_progress{};
    assert(protocol::decode_simulation_progress(
        protocol::encode_simulation_progress(frontend_progress),
        decoded_frontend_progress));
    assert(decoded_frontend_progress.scene_epoch == 0U);
    assert(decoded_frontend_progress.input_epoch == 17U);
    assert(decoded_frontend_progress.completed_frame == 6179U);
    auto invalid_simulation_progress = simulation_progress;
    invalid_simulation_progress.input_epoch = 0U;
    assert(protocol::encode_simulation_progress(
               invalid_simulation_progress).empty());

    const protocol::RacerOrientationPayload racer_orientation{
        9U, 6179U, {0x44U, 0x4BU, 0x52U, 0x4FU}};
    protocol::RacerOrientationPayload decoded_racer_orientation{};
    assert(protocol::decode_racer_orientation(
        protocol::encode_racer_orientation(racer_orientation),
        decoded_racer_orientation));
    assert(decoded_racer_orientation.scene_epoch == 9U);
    assert(decoded_racer_orientation.frame == 6179U);
    assert(decoded_racer_orientation.state == racer_orientation.state);
    auto invalid_racer_orientation = racer_orientation;
    invalid_racer_orientation.state.clear();
    assert(protocol::encode_racer_orientation(
               invalid_racer_orientation).empty());
    invalid_racer_orientation = racer_orientation;
    invalid_racer_orientation.state.resize(
        protocol::kMaximumRacerOrientationBytes + 1U);
    assert(protocol::encode_racer_orientation(
               invalid_racer_orientation).empty());

    const protocol::FrameCommitRequestPayload commit_request{
        17U, 2U, 101U};
    protocol::FrameCommitRequestPayload decoded_commit_request{};
    assert(protocol::decode_frame_commit_request(
        protocol::encode_frame_commit_request(commit_request),
        decoded_commit_request));
    assert(decoded_commit_request.epoch == commit_request.epoch);
    assert(decoded_commit_request.player_slot == commit_request.player_slot);
    assert(decoded_commit_request.first_missing_frame ==
           commit_request.first_missing_frame);
    auto invalid_commit_request = commit_request;
    invalid_commit_request.epoch = 0U;
    assert(protocol::encode_frame_commit_request(invalid_commit_request).empty());
    invalid_commit_request = commit_request;
    invalid_commit_request.player_slot = kMaximumPlayers;
    assert(protocol::encode_frame_commit_request(invalid_commit_request).empty());

    const protocol::GameplayHandoffPayload gameplay_handoff{
        17U, 18U, 106U, 42U, 0U,
        protocol::GameplayHandoffStage::Suspend};
    const auto encoded_handoff =
        protocol::encode_gameplay_handoff(gameplay_handoff);
    protocol::GameplayHandoffPayload decoded_handoff{};
    assert(protocol::decode_gameplay_handoff(encoded_handoff,
                                              decoded_handoff));
    assert(decoded_handoff.current_input_epoch == 17U);
    assert(decoded_handoff.resume_input_epoch == 18U);
    assert(decoded_handoff.boundary_frame == 106U);
    assert(decoded_handoff.map == 42U);
    assert(decoded_handoff.player_slot == 0U);
    assert(decoded_handoff.stage ==
           protocol::GameplayHandoffStage::Suspend);

    protocol::FrameCommitPayload first_commit{};
    first_commit.epoch = 7U;
    first_commit.frame = 100U;
    first_commit.occupied_mask = 0x07U;
    first_commit.inputs = {{{0x8000U, 50, -20},
                            {0x4000U, -10, 70},
                            {0x2000U, 1, 2}, {}}};
    first_commit.previous_hash = 0x1020304050607080ULL;
    first_commit.commit_hash = protocol::frame_commit_hash(99U, first_commit);
    protocol::FrameCommitPayload second_commit = first_commit;
    second_commit.frame = 101U;
    second_commit.previous_hash = first_commit.commit_hash;
    second_commit.inputs[1].buttons = 0x0100U;
    second_commit.commit_hash = protocol::frame_commit_hash(99U, second_commit);
    protocol::FrameCommitBatch commit_batch{};
    commit_batch.commits = {first_commit, second_commit};
    const auto encoded_commits = protocol::encode_frame_commit_batch(
        commit_batch);
    protocol::FrameCommitBatch decoded_commits{};
    assert(protocol::decode_frame_commit_batch(encoded_commits,
                                                decoded_commits, error));
    assert(decoded_commits.commits.size() == 2U);
    assert(decoded_commits.commits[0] == first_commit);
    assert(decoded_commits.commits[1] == second_commit);
    assert(decoded_commits.generation == 0U);
    assert(!decoded_commits.correction);

    commit_batch.generation = 4U;
    commit_batch.correction_first = 100U;
    commit_batch.correction_last = 101U;
    commit_batch.correction = true;
    const auto encoded_correction = protocol::encode_frame_commit_batch(
        commit_batch);
    assert(protocol::decode_frame_commit_batch(encoded_correction,
                                                decoded_commits, error));
    assert(decoded_commits.generation == 4U);
    assert(decoded_commits.correction_first == 100U);
    assert(decoded_commits.correction_last == 101U);
    assert(decoded_commits.correction);
    const protocol::FrameCorrectionAcknowledgePayload correction_ack{
        7U, 4U, 2U};
    protocol::FrameCorrectionAcknowledgePayload decoded_correction_ack{};
    assert(protocol::decode_frame_correction_acknowledge(
        protocol::encode_frame_correction_acknowledge(correction_ack),
        decoded_correction_ack));
    assert(decoded_correction_ack.epoch == correction_ack.epoch);
    assert(decoded_correction_ack.generation == correction_ack.generation);
    assert(decoded_correction_ack.player_slot == correction_ack.player_slot);
    std::vector<std::uint8_t> corrupt_commits = encoded_commits;
    corrupt_commits.back() ^= 1U;
    assert(protocol::decode_frame_commit_batch(corrupt_commits,
                                               decoded_commits, error));
    assert(decoded_commits.commits.back().commit_hash !=
           protocol::frame_commit_hash(99U, decoded_commits.commits.back()));

    protocol::StateSnapshotChunk state_chunk{};
    state_chunk.scene_epoch = 3U;
    state_chunk.frame = 144U;
    state_chunk.checksum = 0x1020304050607080ULL;
    state_chunk.total_size = 7U;
    state_chunk.chunk_index = 1U;
    state_chunk.chunk_count = 2U;
    state_chunk.bytes = {4U, 5U, 6U, 7U};
    const auto encoded_chunk =
        protocol::encode_state_snapshot_chunk(state_chunk);
    protocol::StateSnapshotChunk decoded_chunk{};
    assert(protocol::decode_state_snapshot_chunk(encoded_chunk,
                                                   decoded_chunk, error));
    assert(decoded_chunk.frame == state_chunk.frame);
    assert(decoded_chunk.checksum == state_chunk.checksum);
    assert(decoded_chunk.total_size == state_chunk.total_size);
    assert(decoded_chunk.chunk_index == state_chunk.chunk_index);
    assert(decoded_chunk.chunk_count == state_chunk.chunk_count);
    assert(decoded_chunk.bytes == state_chunk.bytes);
    // Raw UDP keeps conservative path-MTU datagrams, while Quick Join may
    // carry one complete authoritative state as a single SCTP-framed message.
    // Both routes use the exact same protocol/checksum representation.
    protocol::StateSnapshotChunk coalesced_chunk{};
    coalesced_chunk.scene_epoch = 4U;
    coalesced_chunk.frame = 145U;
    coalesced_chunk.checksum = 0x8070605040302010ULL;
    coalesced_chunk.total_size = 14336U;
    coalesced_chunk.chunk_index = 0U;
    coalesced_chunk.chunk_count = 1U;
    coalesced_chunk.bytes.resize(14336U, 0xA5U);
    assert(protocol::encode_state_snapshot_chunk(coalesced_chunk).empty());
    const auto encoded_coalesced_chunk =
        protocol::encode_state_snapshot_chunk(
            coalesced_chunk, protocol::kMaximumQuickJoinDatagramBytes);
    assert(encoded_coalesced_chunk.size() == 14360U);
    protocol::Datagram coalesced_datagram{
        {protocol::MessageType::LiveReplicaSnapshot, 99U, 8U, 145U},
        encoded_coalesced_chunk};
    assert(protocol::encode(coalesced_datagram).empty());
    const auto encoded_coalesced_datagram = protocol::encode(
        coalesced_datagram, protocol::kMaximumQuickJoinDatagramBytes);
    assert(encoded_coalesced_datagram.size() == 14392U);
    protocol::Datagram decoded_coalesced_datagram{};
    assert(!protocol::decode(encoded_coalesced_datagram,
                             decoded_coalesced_datagram, error));
    assert(protocol::decode(encoded_coalesced_datagram,
                            decoded_coalesced_datagram, error,
                            protocol::kMaximumQuickJoinDatagramBytes));
    protocol::StateSnapshotChunk decoded_coalesced_chunk{};
    assert(protocol::decode_state_snapshot_chunk(
        decoded_coalesced_datagram.payload, decoded_coalesced_chunk, error));
    assert(decoded_coalesced_chunk.chunk_count == 1U);
    assert(decoded_coalesced_chunk.bytes == coalesced_chunk.bytes);
    const protocol::StateRequestPayload request{3U, 144U, 5U, 0x0015U};
    const auto state_request = protocol::encode_state_request(request);
    protocol::StateRequestPayload decoded_request{};
    assert(protocol::decode_state_request(state_request, decoded_request));
    assert(decoded_request.scene_epoch == request.scene_epoch);
    assert(decoded_request.frame == 144U);
    assert(decoded_request.chunk_count == 5U);
    assert(decoded_request.missing_chunks == 0x0015U);
    const protocol::RecoveryPayload recovery{3U, 180U, 1U};
    const auto encoded_recovery = protocol::encode_recovery(recovery);
    protocol::RecoveryPayload decoded_recovery{};
    assert(protocol::decode_recovery(encoded_recovery, decoded_recovery));
    assert(decoded_recovery.scene_epoch == recovery.scene_epoch);
    assert(decoded_recovery.frame == recovery.frame);
    assert(decoded_recovery.player_slot == recovery.player_slot);

    protocol::HelloPayload hello{"Pipsy", Manifest()};
    for (std::size_t index = 0; index < hello.friend_admission.size(); ++index)
        hello.friend_admission[index] = static_cast<std::uint8_t>(index + 1U);
    const auto encoded_hello = protocol::encode_hello(hello);
    protocol::HelloPayload decoded_hello{};
    assert(protocol::decode_hello(encoded_hello, decoded_hello, error));
    assert(decoded_hello.display_name == hello.display_name);
    assert(decoded_hello.manifest == hello.manifest);
    assert(decoded_hello.friend_admission == hello.friend_admission);
    auto truncated_hello = encoded_hello;
    truncated_hello.pop_back();
    assert(!protocol::decode_hello(truncated_hello, decoded_hello, error));

    protocol::HelloAckPayload save_ack{};
    save_ack.accepted = true;
    save_ack.player_slot = 1U;
    save_ack.message = "Session save synchronized.";
    save_ack.synchronized_save.assign(512U, 0x5AU);
    save_ack.online_save_generation = 9U;
    save_ack.online_save_hash = 0x1020304050607080ULL;
    const auto encoded_save_ack = protocol::encode_hello_ack(save_ack);
    protocol::HelloAckPayload decoded_save_ack{};
    assert(protocol::decode_hello_ack(encoded_save_ack, decoded_save_ack,
                                      error));
    assert(decoded_save_ack.accepted);
    assert(decoded_save_ack.player_slot == 1U);
    assert(decoded_save_ack.synchronized_save == save_ack.synchronized_save);
    assert(decoded_save_ack.online_save_generation == 9U);
    assert(decoded_save_ack.online_save_hash == 0x1020304050607080ULL);

    protocol::HelloAckPayload compatibility_ack{};
    compatibility_ack.player_slot = 1U;
    compatibility_ack.message = "Compatibility differs.";
    compatibility_ack.compatibility_offer = Manifest();
    protocol::HelloAckPayload decoded_compatibility_ack{};
    assert(protocol::decode_hello_ack(
        protocol::encode_hello_ack(compatibility_ack),
        decoded_compatibility_ack, error));
    assert(!decoded_compatibility_ack.accepted);
    assert(decoded_compatibility_ack.compatibility_offer ==
           compatibility_ack.compatibility_offer);

    const protocol::LaunchCommitPayload launch_commit{19U};
    protocol::LaunchCommitPayload decoded_launch_commit{};
    assert(protocol::decode_launch_commit(
        protocol::encode_launch_commit(launch_commit),
        decoded_launch_commit, error));
    assert(decoded_launch_commit.launch_epoch == launch_commit.launch_epoch);
    const protocol::LaunchCommitAckPayload launch_commit_ack{19U, 1U};
    protocol::LaunchCommitAckPayload decoded_launch_commit_ack{};
    assert(protocol::decode_launch_commit_ack(
        protocol::encode_launch_commit_ack(launch_commit_ack),
        decoded_launch_commit_ack, error));
    assert(decoded_launch_commit_ack.launch_epoch == 19U);
    assert(decoded_launch_commit_ack.player_slot == 1U);
    const protocol::LaunchReleasePayload launch_release{19U};
    protocol::LaunchReleasePayload decoded_launch_release{};
    assert(protocol::decode_launch_release(
        protocol::encode_launch_release(launch_release),
        decoded_launch_release, error));
    assert(decoded_launch_release.launch_epoch == 19U);
    const protocol::LaunchReleaseAckPayload launch_release_ack{19U, 1U};
    protocol::LaunchReleaseAckPayload decoded_launch_release_ack{};
    assert(protocol::decode_launch_release_ack(
        protocol::encode_launch_release_ack(launch_release_ack),
        decoded_launch_release_ack, error));
    assert(decoded_launch_release_ack.launch_epoch == 19U);
    assert(decoded_launch_release_ack.player_slot == 1U);

    const protocol::PreflightBeginPayload preflight_begin{7U, 7000U};
    protocol::PreflightBeginPayload decoded_preflight_begin{};
    assert(protocol::decode_preflight_begin(
        protocol::encode_preflight_begin(preflight_begin),
        decoded_preflight_begin, error));
    assert(decoded_preflight_begin.test_id == 7U);
    assert(decoded_preflight_begin.duration_ms == 7000U);
    protocol::PreflightProbePayload preflight_probe{
        7U, 12U, 123456U, false, std::vector<std::uint8_t>(880U, 0x5AU)};
    protocol::PreflightProbePayload decoded_preflight_probe{};
    assert(protocol::decode_preflight_probe(
        protocol::encode_preflight_probe(preflight_probe),
        decoded_preflight_probe, error));
    assert(decoded_preflight_probe.test_id == 7U);
    assert(decoded_preflight_probe.sequence == 12U);
    assert(decoded_preflight_probe.padding == preflight_probe.padding);
    const protocol::PreflightResultPayload preflight_result{
        7U, 1U, 8U, 54U, 6U, 3U, 4U, true};
    protocol::PreflightResultPayload decoded_preflight_result{};
    assert(protocol::decode_preflight_result(
        protocol::encode_preflight_result(preflight_result),
        decoded_preflight_result, error));
    assert(decoded_preflight_result.test_id == 7U);
    assert(decoded_preflight_result.player_slot == 1U);
    assert(decoded_preflight_result.score == 8U);
    assert(decoded_preflight_result.p95_rtt_ms == 54U);
    assert(decoded_preflight_result.queues_drained);

    const protocol::OnlineSaveReadyPayload online_save_ready{
        1U, 9U, 0x1020304050607080ULL};
    protocol::OnlineSaveReadyPayload decoded_online_save_ready{};
    assert(protocol::decode_online_save_ready(
        protocol::encode_online_save_ready(online_save_ready),
        decoded_online_save_ready, error));
    assert(decoded_online_save_ready.player_slot == 1U);
    assert(decoded_online_save_ready.generation == 9U);
    assert(decoded_online_save_ready.hash == 0x1020304050607080ULL);
    assert(protocol::encode_online_save_ready({4U, 9U, 1U}).empty());
    assert(protocol::encode_online_save_ready({1U, 0U, 1U}).empty());
    assert(protocol::encode_online_save_ready({1U, 9U, 0U}).empty());
    const protocol::OnlineSaveStatusPayload online_status{7U, 9U, 0x1020304050607080ULL, 15U};
    const auto status_bytes = protocol::encode_online_save_status(online_status);
    protocol::OnlineSaveStatusPayload decoded_status{};
    assert(protocol::decode_online_save_status(status_bytes, decoded_status, error));
    assert(decoded_status.room_generation == 7U && decoded_status.save_generation == 9U &&
           decoded_status.save_hash == online_status.save_hash && decoded_status.verified_mask == 15U);
    for (const auto invalid : {protocol::OnlineSaveStatusPayload{0U, 9U, 1U, 1U},
             {7U, 0U, 1U, 1U}, {7U, 9U, 0U, 1U}, {7U, 9U, 1U, 16U}, {7U, 9U, 1U, 2U}})
        assert(protocol::encode_online_save_status(invalid).empty());
    assert(!protocol::decode_online_save_status(std::span(status_bytes).first(20), decoded_status, error));
    auto trailing_status = status_bytes; trailing_status.push_back(0U);
    assert(!protocol::decode_online_save_status(trailing_status, decoded_status, error));

    protocol::LobbyStatePayload state_payload{};
    state_payload.generation = 14U;
    state_payload.phase = RoomPhase::ReadyCheck;
    state_payload.room_name = "Protocol Test";
    state_payload.visibility = Visibility::Public;
    state_payload.rules.automatic_input_delay = false;
    state_payload.rules.manual_input_delay = 3U;
    state_payload.rules.synchronization = SynchronizationMode::Lockstep;
    state_payload.rules.rollback_window = 0U;
    state_payload.input_delay_frames = 3U;
    state_payload.players[0] = {true, true, false, 0U, "Host"};
    state_payload.players[1] = {true, false, false, 1U, "Client"};
    state_payload.players[2].slot = 2U;
    state_payload.players[3].slot = 3U;
    const auto encoded_state = protocol::encode_lobby_state(state_payload);
    protocol::LobbyStatePayload decoded_state{};
    assert(protocol::decode_lobby_state(encoded_state, decoded_state, error));
    assert(decoded_state.generation == 14U);
    assert(decoded_state.room_name == "Protocol Test");
    assert(decoded_state.visibility == Visibility::Public);
    assert(decoded_state.rules.manual_input_delay == 3U);
    assert(decoded_state.rules.synchronization == SynchronizationMode::Lockstep);
    assert(decoded_state.rules.rollback_window == 0U);
    assert(decoded_state.input_delay_frames == 3U);
    assert(decoded_state.players[1].display_name == "Client");
    protocol::StateHashPayload state_hash{
        3U, 2U, 0x1122334455667788ULL, 1U, 2U, 3U};
    state_hash.racer_count = 2U;
    state_hash.racer_hashes[0] = 0x0102030405060708ULL;
    state_hash.racer_hashes[1] = 0x8877665544332211ULL;
    const auto encoded_hash = protocol::encode_state_hash(state_hash);
    protocol::StateHashPayload decoded_hash{};
    assert(protocol::decode_state_hash(encoded_hash, decoded_hash, error));
    assert(decoded_hash.player_slot == state_hash.player_slot);
    assert(decoded_hash.scene_epoch == state_hash.scene_epoch);
    const protocol::LoadedPayload loaded{
        1U, 0x8877665544332211ULL, 9U, 0x1020304050607080ULL};
    const auto encoded_loaded = protocol::encode_loaded(loaded);
    protocol::LoadedPayload decoded_loaded{};
    assert(protocol::decode_loaded(encoded_loaded, decoded_loaded, error));
    assert(decoded_loaded.player_slot == loaded.player_slot);
    assert(decoded_loaded.bootstrap_hash == loaded.bootstrap_hash);
    assert(decoded_loaded.online_save_generation ==
           loaded.online_save_generation);
    assert(decoded_loaded.online_save_hash == loaded.online_save_hash);
    assert(protocol::encode_loaded(
        {4U, loaded.bootstrap_hash, 9U, 1U}).empty());
    assert(protocol::encode_loaded({0U, 0U, 9U, 1U}).empty());
    assert(protocol::encode_loaded(
        {0U, loaded.bootstrap_hash, 0U, 1U}).empty());
    assert(protocol::encode_loaded(
        {0U, loaded.bootstrap_hash, 9U, 0U}).empty());
    assert(decoded_hash.hash == state_hash.hash);
    assert(decoded_hash.globals_hash == 1U);
    assert(decoded_hash.roster_hash == 2U);
    assert(decoded_hash.racers_hash == 3U);
    assert(decoded_hash.racer_count == 2U);
    assert(decoded_hash.racer_hashes[0] == state_hash.racer_hashes[0]);
    assert(decoded_hash.racer_hashes[1] == state_hash.racer_hashes[1]);
    assert(protocol::encode_state_hash(
               {3U, 2U, 1U, 1U, 1U, 1U, 11U, {}}).empty());
    LaunchDescriptor launch{};
    launch.match_id = 99U;
    launch.lobby_generation = 15U;
    launch.compatibility_hash = manifest_hash(Manifest());
    launch.occupied_mask = 0x03U;
    launch.player_count = 2U;
    launch.input_delay_frames = 3U;
    launch.rollback_window = 10U;
    launch.synchronization = SynchronizationMode::Rollback;
    launch.host_control = HostControlPolicy::GuidedUntilCharacterSelect;
    assert(valid_launch_descriptor(launch));
    protocol::StartPayload start{0U, launch, launch_descriptor_hash(launch)};
    const auto encoded_start = protocol::encode_start(start);
    protocol::StartPayload decoded_start{};
    assert(protocol::decode_start(encoded_start, decoded_start, error));
    assert(decoded_start.descriptor == launch);
    assert(decoded_start.descriptor_hash == start.descriptor_hash);
    LaunchDescriptor sparse = launch;
    sparse.occupied_mask = 0x01U;
    assert(!valid_launch_descriptor(sparse));
    LaunchDescriptor invalid_lockstep = launch;
    invalid_lockstep.synchronization = SynchronizationMode::Lockstep;
    assert(!valid_launch_descriptor(invalid_lockstep));
    invalid_lockstep.rollback_window = 0U;
    assert(valid_launch_descriptor(invalid_lockstep));
    Rules lockstep_rules{};
    lockstep_rules.synchronization = SynchronizationMode::Lockstep;
    assert(!valid_rules(lockstep_rules));
    lockstep_rules.rollback_window = 0U;
    assert(valid_rules(lockstep_rules));
    Rules unsupported_rules{};
    unsupported_rules.maximum_players = 5U;
    assert(!valid_rules(unsupported_rules));
    LaunchDescriptor unsupported_launch = launch;
    unsupported_launch.player_count = 5U;
    unsupported_launch.occupied_mask = 0x1FU;
    assert(!valid_launch_descriptor(unsupported_launch));
    for (std::uint8_t players = 2U; players <= 4U; ++players) {
        auto supported = launch;
        supported.player_count = players;
        supported.occupied_mask = (1U << players) - 1U;
        assert(valid_launch_descriptor(supported));
        auto rules = lockstep_rules;
        rules.maximum_players = players;
        assert(valid_rules(rules));
    }

    protocol::RollbackPayload rollback_payload{};
    rollback_payload.scene_epoch = 7U;
    rollback_payload.transport_epoch = 2U;
    rollback_payload.source_slot = 1U;
    rollback_payload.target_slot = 2U;
    rollback_payload.bytes = {0x47U, 0x4BU, 0x01U, 0x02U, 0x03U};
    const auto encoded_rollback =
        protocol::encode_rollback_payload(rollback_payload);
    protocol::RollbackPayload decoded_rollback{};
    assert(protocol::decode_rollback_payload(encoded_rollback,
                                               decoded_rollback, error));
    assert(decoded_rollback.scene_epoch == rollback_payload.scene_epoch);
    assert(decoded_rollback.transport_epoch ==
           rollback_payload.transport_epoch);
    assert(decoded_rollback.source_slot == rollback_payload.source_slot);
    assert(decoded_rollback.target_slot == rollback_payload.target_slot);
    assert(decoded_rollback.bytes == rollback_payload.bytes);
    protocol::RollbackPayload stale_rollback = rollback_payload;
    stale_rollback.scene_epoch = 0U;
    assert(protocol::encode_rollback_payload(stale_rollback).empty());
    stale_rollback = rollback_payload;
    stale_rollback.transport_epoch = 0U;
    assert(protocol::encode_rollback_payload(stale_rollback).empty());

    protocol::Datagram recovery_request{};
    recovery_request.header.type =
        protocol::MessageType::RollbackRecoveryRequest;
    recovery_request.header.match_id = 99U;
    recovery_request.header.sequence = 101U;
    recovery_request.header.frame = 123U;
    recovery_request.payload = protocol::encode_recovery({7U, 123U, 2U});
    const auto encoded_recovery_request = protocol::encode(recovery_request);
    protocol::Datagram decoded_recovery_request{};
    assert(protocol::decode(encoded_recovery_request,
                            decoded_recovery_request, error));
    assert(decoded_recovery_request.header.type ==
           protocol::MessageType::RollbackRecoveryRequest);
    assert(decoded_recovery_request.payload == recovery_request.payload);

    const protocol::GameplayBarrierPayload gameplay_barrier{
        7U, 42U, 8U, 1U,
        protocol::GameplayBarrierStage::Go};
    const auto encoded_gameplay_barrier =
        protocol::encode_gameplay_barrier(gameplay_barrier);
    protocol::GameplayBarrierPayload decoded_gameplay_barrier{};
    assert(protocol::decode_gameplay_barrier(
        encoded_gameplay_barrier, decoded_gameplay_barrier));
    assert(decoded_gameplay_barrier.scene_epoch ==
           gameplay_barrier.scene_epoch);
    assert(decoded_gameplay_barrier.map == gameplay_barrier.map);
    assert(decoded_gameplay_barrier.racer_count ==
           gameplay_barrier.racer_count);
    assert(decoded_gameplay_barrier.player_slot ==
           gameplay_barrier.player_slot);
    assert(decoded_gameplay_barrier.stage == gameplay_barrier.stage);
    auto invalid_gameplay_barrier = gameplay_barrier;
    invalid_gameplay_barrier.racer_count = 0U;
    // A retail key-unlock cinematic has no racers, but still needs the same
    // authenticated level-load/epoch handshake as the hub it redirected.
    const auto cinematic_barrier = protocol::encode_gameplay_barrier(
        invalid_gameplay_barrier);
    assert(!cinematic_barrier.empty());
    assert(protocol::decode_gameplay_barrier(cinematic_barrier,
                                              decoded_gameplay_barrier));
    assert(decoded_gameplay_barrier.racer_count == 0U);
    invalid_gameplay_barrier.racer_count = 11U;
    assert(protocol::encode_gameplay_barrier(invalid_gameplay_barrier).empty());
    std::vector<std::uint8_t> corrupt_start = encoded_start;
    corrupt_start.back() ^= 1U;
    assert(!protocol::decode_start(corrupt_start, decoded_start, error));
    std::vector<std::uint8_t> malformed = bytes;
    malformed[0] ^= 0xFFU;
    assert(!protocol::decode(malformed, decoded, error));

    InputTimeline timeline(64U);
    assert(timeline.set_local(0U, 1U, {1U, 10, 0}));
    assert(timeline.set_remote(1U, 1U, {2U, -10, 0}) == std::nullopt);
    const FrameInputs first = timeline.inputs_for(1U);
    assert(first[0].buttons == 1U && first[1].buttons == 2U);
    const FrameInputs predicted = timeline.inputs_for(2U);
    assert(predicted[1].buttons == 2U);
    assert(!timeline.slot_confirmed(1U, 2U));
    const auto correction = timeline.set_remote(1U, 2U, {4U, 20, 0});
    assert(correction && *correction == 2U);
    assert(timeline.slot_confirmed(1U, 2U));
    assert(!timeline.slot_confirmed(4U, 2U));

    std::array<std::uint8_t, 16> state{};
    for (std::uint8_t i = 0; i < state.size(); ++i) state[i] = i;
    RollbackRing ring(4U, state.size());
    const std::uint64_t checksum = state_checksum(state);
    assert(ring.save(10U, state, checksum));
    state.fill(0U);
    std::uint64_t loaded_checksum = 0U;
    assert(ring.load(10U, state, &loaded_checksum));
    assert(loaded_checksum == checksum);
    assert(state[15] == 15U);
    assert(!ring.load(9U, state));

    std::vector<std::uint8_t> rdram(0x00800000U);
    const auto write_word = [&rdram](std::uint32_t address,
                                      std::uint32_t value) {
        const std::size_t offset = address & 0x1FFFFFFFU;
        assert(offset + 4U <= rdram.size());
        std::memcpy(rdram.data() + offset, &value, 4U);
    };
    const auto write_byte = [&rdram](std::uint32_t address,
                                     std::uint8_t value) {
        const std::size_t offset = (address & 0x1FFFFFFFU) ^ 3U;
        assert(offset < rdram.size());
        rdram[offset] = value;
    };
    const auto write_half = [&rdram](std::uint32_t address,
                                     std::uint16_t value) {
        const std::size_t offset = (address & 0x1FFFFFFFU) ^ 2U;
        assert(offset + 2U <= rdram.size());
        std::memcpy(rdram.data() + offset, &value, 2U);
    };
    const auto read_word = [&rdram](std::uint32_t address) {
        const std::size_t offset = address & 0x1FFFFFFFU;
        assert(offset + 4U <= rdram.size());
        std::uint32_t value = 0U;
        std::memcpy(&value, rdram.data() + offset, 4U);
        return value;
    };
    const auto read_half = [&rdram](std::uint32_t address) {
        const std::size_t offset = (address & 0x1FFFFFFFU) ^ 2U;
        assert(offset + 2U <= rdram.size());
        std::uint16_t value = 0U;
        std::memcpy(&value, rdram.data() + offset, 2U);
        return value;
    };
    write_word(dkr::runtime::revision_addresses::CurrentMapId, 7U);
    write_word(dkr::runtime::revision_addresses::NumberOfRacers, 1U);
    write_word(dkr::runtime::revision_addresses::Racers, 0x80001000U);
    write_word(0x80001000U, 0x80002000U);
    write_word(0x80002064U, 0x80003000U);
    write_word(0x8000200CU, 0x3F800000U);
    write_word(0x8000302CU, 0x41200000U);
    write_word(0x80003160U, 0x01020304U);
    write_word(dkr::runtime::revision_addresses::CurrentRngSeed, 0x12345678U);
    write_byte(dkr::runtime::revision_addresses::NumberOfGameplayPlayers, 2U);
    write_byte(dkr::runtime::revision_addresses::PlayerIdMap, 3U);
    write_byte(dkr::runtime::revision_addresses::PlayersCharacterArray, 6U);
    std::vector<std::uint8_t> authority;
    assert(capture_authoritative_state(rdram.data(), rdram.size(), 42U,
                                       authority, error));
    assert(!authority.empty());
    assert(authoritative_state_checksum(authority) != 0U);
    const std::uint64_t authority_hash = canonical_gameplay_state_hash(
        rdram.data(), rdram.size());
    write_word(0x8000200CU, 0U);
    write_word(0x8000302CU, 0U);
    write_word(0x80003160U, 0U);
    write_word(dkr::runtime::revision_addresses::CurrentRngSeed, 0U);
    write_byte(dkr::runtime::revision_addresses::NumberOfGameplayPlayers, 0U);
    write_byte(dkr::runtime::revision_addresses::PlayerIdMap, 0U);
    write_byte(dkr::runtime::revision_addresses::PlayersCharacterArray, 0U);
    assert(apply_authoritative_state(rdram.data(), rdram.size(), authority,
                                     42U, error));
    assert(canonical_gameplay_state_hash(rdram.data(), rdram.size()) ==
           authority_hash);
    std::uint32_t restored = 0U;
    std::memcpy(&restored, rdram.data() + 0x200CU, 4U);
    assert(restored == 0x3F800000U);
    std::memcpy(&restored, rdram.data() + 0x302CU, 4U);
    assert(restored == 0x41200000U);
    std::memcpy(&restored, rdram.data() + 0x3160U, 4U);
    assert(restored == 0x01020304U);
    std::memcpy(&restored,
                rdram.data() +
                    (dkr::runtime::revision_addresses::CurrentRngSeed &
                     0x1FFFFFFFU), 4U);
    assert(restored == 0x12345678U);
    assert(rdram[(dkr::runtime::revision_addresses::NumberOfGameplayPlayers &
                  0x1FFFFFFFU) ^ 3U] == 2U);
    assert(rdram[(dkr::runtime::revision_addresses::PlayerIdMap &
                  0x1FFFFFFFU) ^ 3U] == 3U);
    assert(rdram[(dkr::runtime::revision_addresses::PlayersCharacterArray &
                  0x1FFFFFFFU) ^ 3U] == 6U);

    // Plane presentation values are corrected by the host-v-local delta from
    // the same authored frame. This preserves all motion accumulated while a
    // sample was in flight and handles the retail 16-bit angle wrap.
    write_word(0x80003094U, 0x3F800000U); // local history: zoom 1.0
    write_half(0x80003196U, 0xFFF0U);
    write_half(0x800031A0U, 0x1000U);
    std::vector<std::uint8_t> local_orientation;
    assert(capture_racer_orientation_state(
        rdram.data(), rdram.size(), 50U, local_orientation, error));
    write_word(0x80003094U, 0x40200000U); // host history: zoom 2.5
    write_half(0x80003196U, 0x0010U);
    write_half(0x800031A0U, 0x1200U);
    std::vector<std::uint8_t> host_orientation;
    assert(capture_racer_orientation_state(
        rdram.data(), rdram.size(), 50U, host_orientation, error));
    write_word(0x80003094U, 0x40400000U); // current: zoom 3.0
    write_half(0x80003196U, 0x0100U);
    write_half(0x800031A0U, 0x2000U);
    std::uint32_t corrected_racers = 0U;
    assert(apply_racer_orientation_correction(
        rdram.data(), rdram.size(), host_orientation, local_orientation,
        50U, corrected_racers, error));
    assert(corrected_racers == 1U);
    assert(read_word(0x80003094U) == 0x40900000U); // 3.0 + (2.5 - 1.0)
    assert(read_half(0x80003196U) == 0x0120U);
    assert(read_half(0x800031A0U) == 0x2200U);
    const std::uint32_t corrected_zoom = read_word(0x80003094U);
    assert(!apply_racer_orientation_correction(
        rdram.data(), rdram.size(), host_orientation, local_orientation,
        51U, corrected_racers, error));
    assert(read_word(0x80003094U) == corrected_zoom);
    return 0;
}
