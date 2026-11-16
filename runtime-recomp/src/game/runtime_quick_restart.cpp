#include "guest_call_context.hpp"
#include "quick_restart_policy.hpp"

#include "game_payload.hpp"
#include "runtime_enhancements.hpp"
#include "runtime_input.hpp"
#include "revision_addresses.hpp"

#include "recomp.h"

#include <cstdint>

namespace {

std::uint32_t trophy_race_world_id_address() {
    return dkr::runtime::revision_addresses::current().TrophyRaceWorldId;
}
std::uint32_t tracks_mode_address() {
    return dkr::runtime::revision_addresses::current().TracksMode;
}
std::uint32_t level_load_timer_address() {
    return dkr::runtime::revision_addresses::current().LevelLoadTimer;
}
std::uint32_t paused_address() {
    return dkr::runtime::revision_addresses::current().IsPaused;
}
std::uint32_t post_race_address() {
    return dkr::runtime::revision_addresses::current().PostRaceViewport;
}
constexpr std::uint32_t kSettingsWorldIdOffset = 0x48U;
constexpr std::uint32_t kSettingsCourseIdOffset = 0x49U;
constexpr gpr kRetailRestartButtons = 0x2020U; // L_TRIG | Z_TRIG

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}
std::int32_t ReadWord(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::int32_t>(MEM_W(0, RdramAddress(address)));
}

std::int32_t ReadSignedByte(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::int8_t>(MEM_B(0, RdramAddress(address)));
}

std::int32_t ReadUnsignedByte(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::uint8_t>(MEM_BU(0, RdramAddress(address)));
}

gpr CallAndReadV0(void (*function)(std::uint8_t*, recomp_context*), std::uint8_t* rdram, const recomp_context& source,
                  gpr argument = 0, bool set_argument = false) {
    dkr::runtime::GuestCallContext call(source);
    if (set_argument) {
        call->r4 = argument;
    }
    function(rdram, call.get());
    return call->r2;
}

} // namespace

extern "C" void dkr_quick_restart_poll(std::uint8_t* rdram, recomp_context* context) {
    if (rdram == nullptr || context == nullptr || !dkr::runtime::input::consume_quick_restart_request()) {
        return;
    }

    const dkr::runtime::GamePayload* payload = dkr::runtime::active_payload();
    if (payload == nullptr || payload->get_settings == nullptr || payload->leveltable_type == nullptr ||
        payload->sound_clear_delayed == nullptr || payload->reset_delayed_text == nullptr ||
        payload->should_check_lead_player == nullptr || payload->is_in_two_player_adventure == nullptr ||
        payload->swap_lead_player == nullptr) {
        return;
    }

    dkr::runtime::quick_restart::State state{};
    state.modern_profile = dkr::runtime::enhancements::modern_presentation_enabled();
    state.paused = ReadSignedByte(rdram, paused_address()) != 0;
    state.post_race = ReadUnsignedByte(rdram, post_race_address()) != 0;
    state.level_loading = ReadWord(rdram, level_load_timer_address()) != 0;
    state.trophy_race_world = ReadWord(rdram, trophy_race_world_id_address());
    state.tracks_mode = ReadWord(rdram, tracks_mode_address()) != 0;

    const gpr settings = CallAndReadV0(payload->get_settings, rdram, *context);
    const std::uint32_t settings_address = static_cast<std::uint32_t>(settings);
    if (settings_address < 0x80000000U || settings_address > 0x807FFFB6U) {
        return;
    }
    state.world_id = ReadUnsignedByte(rdram, settings_address + kSettingsWorldIdOffset);
    const std::int32_t course_id = ReadUnsignedByte(rdram, settings_address + kSettingsCourseIdOffset);
    state.race_type = static_cast<std::int32_t>(
        CallAndReadV0(payload->leveltable_type, rdram, *context, static_cast<gpr>(course_id), true));

    if (!dkr::runtime::quick_restart::retail_restart_available(state)) {
        return;
    }

    dkr::runtime::GuestCallContext call(*context);
    payload->sound_clear_delayed(rdram, call.get());
    call.reset(*context);
    payload->reset_delayed_text(rdram, call.get());

    const bool should_check_lead_player = CallAndReadV0(payload->should_check_lead_player, rdram, *context) != 0;
    if (should_check_lead_player && CallAndReadV0(payload->is_in_two_player_adventure, rdram, *context) != 0) {
        call.reset(*context);
        payload->swap_lead_player(rdram, call.get());
    }

    // mode_game keeps buttonHeldInputs in s0 at this boundary. Feed the same
    // L+Z transition consumed by PAUSE_RESET so save/load and scene teardown
    // remain entirely owned by the retail game.
    context->r16 |= kRetailRestartButtons;
}
