#include "recomp.h"

#include "runtime_enhancements.hpp"
#include "f3ddkr_rt64.hpp"
#include "intro_tail_policy.hpp"
#include "scheduler_event_policy.hpp"
#include "host_task_lifetime.hpp"
#include "presentation_identity.hpp"
#include "runtime_platform.hpp"
#include "revision_addresses.hpp"
#include "steering_wheel_policy.hpp"
#include "vi_presentation_policy.hpp"
#include "widescreen_policy.hpp"
#include "finish_presentation_policy.hpp"

#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"

#if DKR_RUNTIME_HAS_RT64
#include <SDL.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <mutex>

namespace {

#if DKR_RUNTIME_HAS_RT64
const std::uint32_t& kOrthoMatrixAddress =
    dkr::runtime::revision_addresses::OrthoMatrix;
const std::uint32_t& kViewProjectionMatrixAddress =
    dkr::runtime::revision_addresses::ViewProjectionMatrix;
const std::uint32_t& kTrackDisplayListAddress =
    dkr::runtime::revision_addresses::TrackDisplayList;
const std::uint32_t& kVoidLateralXAddress =
    dkr::runtime::revision_addresses::VoidLateralX;
const std::uint32_t& kVoidLateralZAddress =
    dkr::runtime::revision_addresses::VoidLateralZ;
const std::uint32_t& kVoidCentreXAddress =
    dkr::runtime::revision_addresses::VoidCentreX;
const std::uint32_t& kVoidCentreZAddress =
    dkr::runtime::revision_addresses::VoidCentreZ;
constexpr float kOriginalAspect = 4.0F / 3.0F;
using dkr::runtime::presentation::PresentationMarkerKind;
constexpr float kSplitViewportCoverQuantisation = 1024.0F;
enum class PresentationGroupMode : std::uint32_t {
    World = 0U,
    StaticAuto = 1U,
    DynamicShadow = 2U,
    BackgroundFillStretch = 3U,
    DynamicVehiclePart = 4U,
    AspectAdjust = 5U,
    DynamicBillboard = 6U,
    DynamicSurface = 7U,
    LevelSegment = 9U,
};
float g_saved_transition_x = 1.0F;
float g_saved_transition_y = 1.0F;
bool g_transition_cover_active = false;
bool g_transition_interpolation_active = false;
bool g_background_fill_stretch_active = false;
bool g_three_player_panel_active = false;
bool g_postrace_background_stretch_active = false;
bool g_chequer_background_stretch_active = false;
bool g_shadow_interpolation_active = false;
bool g_vehicle_part_interpolation_active = false;
bool g_billboard_interpolation_active = false;
bool g_surface_interpolation_active = false;
bool g_level_segment_interpolation_active = false;
dkr::runtime::presentation::PresentationKey g_level_segment_pending_key{};
bool g_split_world_aspect_active = false;
bool g_postrace_framed_scope_active = false;
bool g_track_select_lens_flare_scope_active = false;
dkr::runtime::intro::TailGate g_title_intro_tail_gate{};
std::array<float, 8> g_saved_sky_projection_columns{};
bool g_sky_cover_active = false;
dkr::runtime::presentation::PostraceFrameGate g_postrace_frame_gate{};
bool g_postrace_world_framed = false;

float ExpandedCoverScale() {
    using ultramodern::renderer::AspectRatio;
    if (ultramodern::renderer::get_graphics_config().ar_option != AspectRatio::Expand) {
        return 1.0F;
    }
    auto* window = static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window());
    if (window == nullptr) {
        return 1.0F;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return 1.0F;
    }
    return std::max(1.0F,
        (static_cast<float>(width) / static_cast<float>(height)) / kOriginalAspect);
}

float ReadRdramFloat(std::uint8_t* rdram, std::uint32_t address) {
    const auto signed_address = static_cast<gpr>(static_cast<std::int32_t>(address));
    return std::bit_cast<float>(static_cast<std::uint32_t>(MEM_W(0, signed_address)));
}

void WriteRdramFloat(std::uint8_t* rdram, std::uint32_t address, float value) {
    const auto signed_address = static_cast<gpr>(static_cast<std::int32_t>(address));
    MEM_W(0, signed_address) = std::bit_cast<std::uint32_t>(value);
}

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

bool PostraceFullViewScope(std::uint8_t* rdram, float cover) {
    namespace addresses = dkr::runtime::revision_addresses;
    const auto word = [rdram](std::uint32_t address) {
        return static_cast<std::int32_t>(MEM_W(0, RdramAddress(address)));
    };
    return dkr::runtime::presentation::postrace_full_view_scope(
        dkr::runtime::enhancements::modern_presentation_enabled(),
        cover > 1.0001F, word(addresses::GameMode) == 0,
        static_cast<std::int8_t>(MEM_B(0, RdramAddress(addresses::PostRaceViewport))),
        word(addresses::NumberOfActivePlayers), word(addresses::TrophyRaceWorldId),
        word(addresses::ViewportLayout));
}

bool AppendPresentationGroupCommand(std::uint8_t* rdram,
                                    gpr display_list_pointer,
                                    PresentationGroupMode mode,
                                    std::uint16_t token = 0U,
                                    std::uint8_t variant = 0U,
                                    PresentationMarkerKind kind =
                                        PresentationMarkerKind::Geometry) {
    if (display_list_pointer == 0) {
        return false;
    }
    const std::uint32_t current = static_cast<std::uint32_t>(
        MEM_W(0, display_list_pointer));
    if (current < 0x80000000U || current > 0x807FFFF8U) {
        return false;
    }
    (void)rdram;
    return dkr::runtime::presentation::record_presentation_marker(
        current, static_cast<std::uint8_t>(mode), token, variant, kind);
}

void EnsureLevelSegmentInterpolation(std::uint8_t* rdram) {
    if (g_level_segment_interpolation_active ||
        g_level_segment_pending_key.token == 0U) {
        return;
    }
    g_level_segment_interpolation_active = AppendPresentationGroupCommand(
        rdram, RdramAddress(kTrackDisplayListAddress),
        PresentationGroupMode::LevelSegment,
        g_level_segment_pending_key.token,
        g_level_segment_pending_key.variant);
}

#endif

std::atomic<std::uint64_t> g_scheduler_sp_late_events{0};
std::atomic<std::uint64_t> g_scheduler_dp_late_events{0};

bool IsRdramWordAddress(std::uint32_t address, std::uint32_t final_offset) {
    // libultra passes both KSEG0 pointers and, in a few low-level paths,
    // physical RDRAM offsets. Reject every other segment before MEM_W masks
    // the address so a stale host completion cannot alias arbitrary memory.
    return dkr::runtime::scheduler::is_rdram_word_address(address,
                                                           final_offset);
}

constexpr std::uint32_t kAudioEventSize = 0x10U;
constexpr std::uint32_t kAudioEventItemSize = 0x1CU;
constexpr std::uint32_t kAudioEventQueueSize = 0x14U;
constexpr std::uint32_t kAudioRecoveryDelayUs = 16667U;
constexpr std::uint32_t kMaximumImmediateAudioEvents = 256U;

struct AudioEventQueueGuard {
    std::uint32_t queue = 0U;
    std::uint32_t immediate_events = 0U;
    bool logged_empty = false;
    bool logged_corrupt = false;
};

std::mutex g_audio_event_mutex;
std::array<AudioEventQueueGuard, 8> g_audio_event_guards{};

std::uint32_t PhysicalRdramAddress(std::uint32_t address) {
    return address & 0x1FFFFFFFU;
}

bool SameRdramAddress(std::uint32_t lhs, std::uint32_t rhs) {
    return PhysicalRdramAddress(lhs) == PhysicalRdramAddress(rhs);
}

AudioEventQueueGuard& AudioGuardFor(std::uint32_t queue) {
    for (auto& guard : g_audio_event_guards) {
        if (SameRdramAddress(guard.queue, queue)) {
            return guard;
        }
    }
    for (auto& guard : g_audio_event_guards) {
        if (guard.queue == 0U) {
            guard.queue = queue;
            return guard;
        }
    }
    // DKR owns three sequence players and one sound player. Reusing the
    // quietest slot here remains bounded even if a damaged pointer invents
    // more queue identities than the retail audio heap can contain.
    auto& guard = *std::min_element(
        g_audio_event_guards.begin(), g_audio_event_guards.end(),
        [](const AudioEventQueueGuard& lhs, const AudioEventQueueGuard& rhs) {
            return lhs.immediate_events < rhs.immediate_events;
        });
    guard = {};
    guard.queue = queue;
    return guard;
}

void WriteNoAudioEvent(std::uint8_t* rdram, std::uint32_t event) {
    if (rdram != nullptr && IsRdramWordAddress(event, kAudioEventSize - 4U)) {
        MEM_H(0, RdramAddress(event)) = static_cast<std::int16_t>(-1);
    }
}

void QuarantineAllocatedAudioEvents(std::uint8_t* rdram,
                                    std::uint32_t queue) {
    if (rdram == nullptr || !IsRdramWordAddress(queue, kAudioEventQueueSize - 4U)) {
        return;
    }
    MEM_W(0x08, RdramAddress(queue)) = 0U;
    MEM_W(0x0C, RdramAddress(queue)) = 0U;
}

void ResetAudioEventGuards() {
    std::scoped_lock lock(g_audio_event_mutex);
    g_audio_event_guards = {};
}

} // namespace

extern "C" std::uint32_t dkr_audio_event_queue_next(
    std::uint8_t* rdram, recomp_context* context) {
    const std::uint32_t queue = context != nullptr
        ? static_cast<std::uint32_t>(context->r4)
        : 0U;
    const std::uint32_t destination = context != nullptr
        ? static_cast<std::uint32_t>(context->r5)
        : 0U;

    std::scoped_lock lock(g_audio_event_mutex);
    AudioEventQueueGuard& guard = AudioGuardFor(queue);
    const auto recover = [&](bool corrupt, const char* reason) {
        WriteNoAudioEvent(rdram, destination);
        guard.immediate_events = 0U;
        bool& logged = corrupt ? guard.logged_corrupt : guard.logged_empty;
        if (!logged) {
            std::fprintf(stderr,
                         "[stability][audio] recovered %s event queue "
                         "queue=%08X; scheduling next callback in %u us\n",
                         reason, queue, kAudioRecoveryDelayUs);
            logged = true;
        }
        return kAudioRecoveryDelayUs;
    };

    if (rdram == nullptr || context == nullptr ||
        !IsRdramWordAddress(queue, kAudioEventQueueSize - 4U) ||
        !IsRdramWordAddress(destination, kAudioEventSize - 4U)) {
        return recover(true, "invalid");
    }

    const gpr queue_address = RdramAddress(queue);
    const std::uint32_t item = static_cast<std::uint32_t>(
        MEM_W(0x08, queue_address));
    if (item == 0U) {
        // libultra returns delta=0 for an exhausted queue. Rare's custom
        // sequence-player handler immediately asks again while delta is zero,
        // producing an infinite audio-thread loop. Preserve the error event,
        // but return one authored audio-frame so the player can yield and its
        // public API can safely repopulate the queue.
        return recover(false, "exhausted");
    }
    if (!IsRdramWordAddress(item, kAudioEventItemSize - 4U)) {
        QuarantineAllocatedAudioEvents(rdram, queue);
        return recover(true, "malformed");
    }

    const gpr item_address = RdramAddress(item);
    const std::uint32_t next = static_cast<std::uint32_t>(MEM_W(0, item_address));
    const std::uint32_t previous = static_cast<std::uint32_t>(MEM_W(4, item_address));
    const std::uint32_t alloc_sentinel = queue + 0x08U;
    if (!SameRdramAddress(previous, alloc_sentinel) ||
        (next != 0U && !IsRdramWordAddress(next, 4U))) {
        QuarantineAllocatedAudioEvents(rdram, queue);
        return recover(true, "malformed");
    }
    if (next != 0U) {
        const std::uint32_t next_previous = static_cast<std::uint32_t>(
            MEM_W(4, RdramAddress(next)));
        if (!SameRdramAddress(next_previous, item)) {
            QuarantineAllocatedAudioEvents(rdram, queue);
            return recover(true, "malformed");
        }
    }

    const std::uint32_t free_head = static_cast<std::uint32_t>(
        MEM_W(0, queue_address));
    if (free_head != 0U && !IsRdramWordAddress(free_head, 4U)) {
        QuarantineAllocatedAudioEvents(rdram, queue);
        return recover(true, "malformed");
    }

    // Exact alUnlink(item) semantics for the allocated-list head.
    MEM_W(0x08, queue_address) = next;
    if (next != 0U) {
        MEM_W(4, RdramAddress(next)) = alloc_sentinel;
    } else {
        MEM_W(0x0C, queue_address) = 0U;
    }

    // ALEvent is 16 bytes in this ABI. Word copies preserve the recomp's N64
    // byte ordering without introducing unaligned host accesses.
    for (std::uint32_t offset = 0U; offset < kAudioEventSize; offset += 4U) {
        MEM_W(offset, RdramAddress(destination)) =
            MEM_W(0x0CU + offset, item_address);
    }
    const std::uint32_t delta = static_cast<std::uint32_t>(
        MEM_W(0x08, item_address));

    // Exact alLink(item, &evtq->freeList) semantics.
    MEM_W(0, item_address) = free_head;
    MEM_W(4, item_address) = queue;
    if (free_head != 0U) {
        MEM_W(4, RdramAddress(free_head)) = item;
    }
    MEM_W(0, queue_address) = item;

    if (delta != 0U) {
        guard.immediate_events = 0U;
        guard.logged_empty = false;
        return delta;
    }
    if (++guard.immediate_events <= kMaximumImmediateAudioEvents) {
        return 0U;
    }

    // No retail DKR player owns more than 150 event items. A longer run of
    // zero-delta callbacks therefore proves a damaged/cyclic queue rather
    // than a legal simultaneous-event batch.
    QuarantineAllocatedAudioEvents(rdram, queue);
    return recover(true, "non-progressing");
}

extern "C" void dkr_custom_tracks_prepare_memory(std::uint8_t*, recomp_context*);
extern "C" void dkr_custom_tracks_prepare_vehicle(std::uint8_t*, recomp_context*);

extern "C" void dkr_runtime_scene_reset(std::uint8_t* rdram,
                                          recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    // A transition can abandon one of these scoped patches before its normal
    // end hook runs. Restore any in-place matrix edit while the outgoing scene
    // still owns the backing storage, then clear every host-only draw scope.
    if (rdram != nullptr && g_transition_cover_active) {
        WriteRdramFloat(rdram, kOrthoMatrixAddress, g_saved_transition_x);
        WriteRdramFloat(rdram,
                        kOrthoMatrixAddress + 5U * sizeof(float),
                        g_saved_transition_y);
    }
    if (rdram != nullptr && g_sky_cover_active) {
        for (std::uint32_t row = 0U; row < 4U; ++row) {
            for (std::uint32_t column = 0U; column < 2U; ++column) {
                const std::uint32_t slot = row * 2U + column;
                const std::uint32_t address = kViewProjectionMatrixAddress +
                    (row * 4U + column) * sizeof(float);
                WriteRdramFloat(rdram, address,
                                g_saved_sky_projection_columns[slot]);
            }
        }
    }
    g_transition_cover_active = false;
    g_transition_interpolation_active = false;
    g_background_fill_stretch_active = false;
    g_three_player_panel_active = false;
    g_postrace_background_stretch_active = false;
    g_chequer_background_stretch_active = false;
    g_shadow_interpolation_active = false;
    g_vehicle_part_interpolation_active = false;
    g_billboard_interpolation_active = false;
    g_surface_interpolation_active = false;
    g_level_segment_interpolation_active = false;
    g_level_segment_pending_key = {};
    g_split_world_aspect_active = false;
    g_postrace_framed_scope_active = false;
    g_track_select_lens_flare_scope_active = false;
    g_sky_cover_active = false;
    g_saved_transition_x = 1.0F;
    g_saved_transition_y = 1.0F;
    g_saved_sky_projection_columns = {};
    g_postrace_frame_gate.reset();
    g_postrace_world_framed = false;
#else
    (void)rdram;
#endif
    g_title_intro_tail_gate.reset();
    ResetAudioEventGuards();
    // The existing level_load entry hook covers initial Track Lab loads,
    // restarts and Track Select previews. Give a custom track its memory and
    // model heap before the level allocates, and synchronize its vehicle
    // before retail consumes r7.
    if (rdram && context) {
        dkr_custom_tracks_prepare_memory(rdram, context);
        dkr_custom_tracks_prepare_vehicle(rdram, context);
    }
}

void dkr::runtime::presentation::postrace_presentation_begin_frame() {
#if DKR_RUNTIME_HAS_RT64
    g_postrace_frame_gate.begin_frame();
#endif
}

void dkr::runtime::presentation::postrace_presentation_submit_frame(bool presented) {
#if DKR_RUNTIME_HAS_RT64
    g_postrace_frame_gate.submit_frame(presented);
#else
    (void)presented;
#endif
}

extern "C" void dkr_postrace_presentation_start(std::uint8_t*, recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    g_postrace_frame_gate.reset();
    g_postrace_world_framed = false;
#endif
}

extern "C" void dkr_postrace_wooden_frame_draw(std::uint8_t* rdram, recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    if (!rdram) return;
    // This hook is the actual menu_element_render(4) call in postrace_viewport,
    // after retail has updated the rectangle; asset-loading/stage gates have
    // already passed. Merely entering postrace_start cannot trigger this.
    const auto viewport = RdramAddress(dkr::runtime::revision_addresses::ScreenViewports);
    g_postrace_frame_gate.observe_wood(
        MEM_W(0, viewport), MEM_W(4, viewport), MEM_W(8, viewport), MEM_W(12, viewport),
        dkr::runtime::presentation::kCanonicalViWidth,
        dkr::runtime::presentation::kCanonicalViHeight);
#else
    (void)rdram;
#endif
}

extern "C" void dkr_fix_fullscreen_clear_scissor(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (rdram == nullptr || context == nullptr) {
        return;
    }
    const auto command = static_cast<std::uint32_t>(context->r3);
    if (!IsRdramWordAddress(command, 7U)) {
        return;
    }
    const auto command_address = static_cast<gpr>(
        static_cast<std::int32_t>(command));
    const auto lower_right = static_cast<std::uint32_t>(
        MEM_W(4, command_address));
    MEM_W(4, command_address) =
        dkr::runtime::presentation::correct_fullscreen_clear_scissor(
            lower_right,
            dkr::runtime::presentation::kCanonicalViWidth,
            dkr::runtime::presentation::kCanonicalViHeight);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_split_screen_viewport_fill(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (rdram == nullptr || context == nullptr) {
        return;
    }
    const float cover = ExpandedCoverScale();
    const bool postrace = PostraceFullViewScope(rdram, cover);
    g_postrace_world_framed = postrace &&
        g_postrace_frame_gate.contracted_frame_submitted;
    if (postrace && !g_postrace_world_framed) {
        // Presentation-only viewport/scissor correction: leave the authored
        // viewport, camera, menu animation and shared simulation RAM intact.
        AppendPresentationGroupCommand(
            rdram, context->r17, PresentationGroupMode::World, 0U,
            0U, PresentationMarkerKind::PostraceFullViewport);
    }
    const int viewport_layout = static_cast<std::int32_t>(
        MEM_W(0, RdramAddress(
            dkr::runtime::revision_addresses::ViewportLayout)));
    if (!dkr::runtime::enhancements::needs_split_world_aspect_adjust(
            dkr::runtime::enhancements::modern_presentation_enabled(),
            cover > 1.0001F, viewport_layout)) {
        return;
    }

    const auto camera = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(MEM_W(
            0, RdramAddress(
                   dkr::runtime::revision_addresses::ActiveCameraId))) &
        3U);
    const auto quantised_cover = static_cast<std::uint16_t>(std::clamp(
        std::lround(cover * kSplitViewportCoverQuantisation),
        0L, 0x3FFFL));
    const auto token = static_cast<std::uint16_t>(
        (quantised_cover << 2U) | camera);
    AppendPresentationGroupCommand(
        rdram, context->r17, PresentationGroupMode::World, token,
        0U, PresentationMarkerKind::SplitViewport);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_split_screen_world_aspect_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_split_world_aspect_active = false;
    g_postrace_framed_scope_active = false;
    if (rdram == nullptr || context == nullptr) {
        return;
    }
    const int viewport_layout = static_cast<std::int32_t>(
        MEM_W(0, RdramAddress(
            dkr::runtime::revision_addresses::ViewportLayout)));
    const auto policy = dkr::runtime::enhancements::world_aspect_policy(
        dkr::runtime::enhancements::modern_presentation_enabled(),
        ExpandedCoverScale() > 1.0001F, viewport_layout);
    if (policy ==
        dkr::runtime::enhancements::WorldAspectPolicy::AdjustToHost) {
        g_split_world_aspect_active = AppendPresentationGroupCommand(
            rdram, context->r17, PresentationGroupMode::AspectAdjust);
    }

    // Use the SAME decision as the pre-viewport hook. No competing menu-stage
    // predicate can enable 4:3 before the first contracted frame was submitted.
    if (g_postrace_world_framed) {
        g_postrace_framed_scope_active = AppendPresentationGroupCommand(
            rdram, context->r17, PresentationGroupMode::StaticAuto, 0U,
            0U, PresentationMarkerKind::FramedResults);
    }
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_split_screen_world_aspect_end(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (rdram == nullptr || context == nullptr) {
        return;
    }
    // Close in reverse order. Dedicated markers restore the exact renderer
    // state they captured, so they cannot consume an unrelated group scope.
    if (g_postrace_framed_scope_active) {
        AppendPresentationGroupCommand(
            rdram, context->r17, PresentationGroupMode::World, 0U,
            0U, PresentationMarkerKind::FramedResults);
        g_postrace_framed_scope_active = false;
    }
    if (g_split_world_aspect_active) {
        AppendPresentationGroupCommand(
            rdram, context->r17, PresentationGroupMode::World);
        g_split_world_aspect_active = false;
    }
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_track_select_fullscreen_preview(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (rdram == nullptr || context == nullptr) {
        return;
    }
    const auto viewport = static_cast<std::uint32_t>(context->r2);
    if (!IsRdramWordAddress(viewport, 0x2FU)) {
        return;
    }
    const auto address = static_cast<gpr>(
        static_cast<std::int32_t>(viewport));
    const auto read_signed = [rdram, address](std::uint32_t offset) {
        return static_cast<std::int32_t>(MEM_W(offset, address));
    };
    constexpr int width = static_cast<int>(
        dkr::runtime::presentation::kCanonicalViWidth);
    constexpr int height = static_cast<int>(
        dkr::runtime::presentation::kCanonicalViHeight);
    if (!dkr::runtime::enhancements::is_fullscreen_track_preview(
            read_signed(0x00U), read_signed(0x04U),
            read_signed(0x08U), read_signed(0x0CU),
            read_signed(0x20U), read_signed(0x24U),
            read_signed(0x28U), read_signed(0x2CU), width, height)) {
        return;
    }
    MEM_W(0x28U, address) = width;
    MEM_W(0x2CU, address) = height;
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_track_select_lens_flare_tint_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_track_select_lens_flare_scope_active = false;
    if (rdram == nullptr || context == nullptr ||
        !IsRdramWordAddress(
            dkr::runtime::revision_addresses::ScreenViewports, 0x2FU)) {
        return;
    }

    const auto viewport = RdramAddress(
        dkr::runtime::revision_addresses::ScreenViewports);
    const auto read_viewport = [rdram, viewport](const std::uint32_t offset) {
        return static_cast<std::int32_t>(MEM_W(offset, viewport));
    };
    constexpr int width = static_cast<int>(
        dkr::runtime::presentation::kCanonicalViWidth);
    constexpr int height = static_cast<int>(
        dkr::runtime::presentation::kCanonicalViHeight);
    const bool fullscreen_preview =
        dkr::runtime::enhancements::is_presented_fullscreen_track_preview(
            read_viewport(0x00U), read_viewport(0x04U),
            read_viewport(0x08U), read_viewport(0x0CU),
            read_viewport(0x20U), read_viewport(0x24U),
            read_viewport(0x28U), read_viewport(0x2CU), width, height);
    const bool tracks_menu_active = MEM_W(
        0, RdramAddress(
            dkr::runtime::revision_addresses::IsInTracksMenu)) != 0;
    if (!dkr::runtime::enhancements::preserve_track_select_lens_flare_tint(
            dkr::runtime::enhancements::modern_presentation_enabled(),
            ExpandedCoverScale() > 1.0001F, tracks_menu_active,
            fullscreen_preview)) {
        return;
    }

    g_track_select_lens_flare_scope_active =
        AppendPresentationGroupCommand(
            rdram, context->r17, PresentationGroupMode::StaticAuto, 0U,
            0U, PresentationMarkerKind::TrackSelectLensFlare);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_track_select_lens_flare_tint_end(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_track_select_lens_flare_scope_active ||
        rdram == nullptr || context == nullptr) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, context->r17, PresentationGroupMode::World, 0U,
        0U, PresentationMarkerKind::TrackSelectLensFlare);
    g_track_select_lens_flare_scope_active = false;
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_scheduler_keep_host_tasks(std::uint8_t* rdram,
    std::uint32_t scheduler, std::uint32_t sp_counter, std::uint32_t dp_counter) {
    if (!rdram || !IsRdramWordAddress(scheduler, 0x278U) ||
        !IsRdramWordAddress(sp_counter, 0U) || !IsRdramWordAddress(dp_counter, 0U)) return;
    using namespace dkr::runtime::host_tasks;
    const gpr sc = static_cast<std::int32_t>(scheduler);
    const auto protect = [&](std::uint32_t task, std::uint32_t counter, Engine engine) {
        const gpr address = static_cast<std::int32_t>(counter);
        const auto ticks = static_cast<std::uint32_t>(MEM_W(0, address));
        // The next retail retrace increments this to eleven and would retire
        // the task as failed N64 hardware. Native work can legitimately take
        // longer (shader compilation, scheduling, slower drivers). Do not let
        // the guest recycle its task/buffers while the host still owns them.
        if (ticks >= 10 && task && IsRdramWordAddress(task, 0x4CU) &&
            registry.protect(task + 0x10U, engine, ticks)) MEM_W(0, address) = 0;
    };
    protect(static_cast<std::uint32_t>(MEM_W(0x274, sc)), sp_counter, Engine::Sp);
    protect(static_cast<std::uint32_t>(MEM_W(0x278, sc)), dp_counter, Engine::Dp);
}

extern "C" int dkr_scheduler_sp_event_valid(std::uint8_t* rdram,
                                               recomp_context* context) {
    if (rdram != nullptr && context != nullptr) {
        const std::uint32_t scheduler =
            static_cast<std::uint32_t>(context->r4);
        if (IsRdramWordAddress(scheduler, 0x274U)) {
            const std::uint32_t task = static_cast<std::uint32_t>(
                MEM_W(0x274, static_cast<gpr>(
                    static_cast<std::int32_t>(scheduler))));
            // __scHandleRSP's first task access is task + 0x10. A null task
            // means the scheduler already consumed this SP edge; it is not a
            // valid retail task and must not reach that unchecked load.
            if (dkr::runtime::scheduler::is_valid_sp_task_pointer(task)) {
                using namespace dkr::runtime::host_tasks;
                registry.consume(registry.lookup(task + 0x10U, Engine::Sp), Engine::Sp);
                return 1;
            }
        }
    }

    const std::uint64_t late_event =
        g_scheduler_sp_late_events.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (late_event <= 8U) {
        const std::uint32_t scheduler = context != nullptr
            ? static_cast<std::uint32_t>(context->r4)
            : 0U;
        std::uint32_t task = 0U;
        if (rdram != nullptr && IsRdramWordAddress(scheduler, 0x274U)) {
            task = static_cast<std::uint32_t>(MEM_W(
                0x274,
                static_cast<gpr>(static_cast<std::int32_t>(scheduler))));
        }
        std::fprintf(stderr,
                     "[boot][scheduler] dropped late SP completion "
                     "scheduler=%08X task=%08X count=%llu\n",
                     scheduler, task,
                     static_cast<unsigned long long>(late_event));
    }

    // The normal acknowledgement lives at __scHandleRSP's common exit. This
    // early-return path must publish the same acknowledgement exactly once or
    // the host worker can remain blocked behind the discarded completion.
    ultramodern::acknowledge_external_message_src(
        ultramodern::EventMessageSource::Sp);
    return 0;
}

extern "C" int dkr_scheduler_dp_event_valid(std::uint8_t* rdram,
                                               recomp_context* context) {
    if (rdram != nullptr && context != nullptr) {
        const std::uint32_t scheduler =
            static_cast<std::uint32_t>(context->r4);
        if (IsRdramWordAddress(scheduler, 0x278U)) {
            const std::uint32_t task = static_cast<std::uint32_t>(
                MEM_W(0x278, static_cast<gpr>(
                    static_cast<std::int32_t>(scheduler))));
            if (dkr::runtime::scheduler::is_valid_dp_task_pointer(task)) {
                using namespace dkr::runtime::host_tasks;
                registry.consume(registry.lookup(task + 0x10U, Engine::Dp), Engine::Dp);
                return 1;
            }
        }
    }

    const std::uint64_t late_event =
        g_scheduler_dp_late_events.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (late_event <= 8U) {
        const std::uint32_t scheduler = context != nullptr
            ? static_cast<std::uint32_t>(context->r4)
            : 0U;
        std::uint32_t task = 0U;
        if (rdram != nullptr && IsRdramWordAddress(scheduler, 0x278U)) {
            task = static_cast<std::uint32_t>(MEM_W(
                0x278,
                static_cast<gpr>(static_cast<std::int32_t>(scheduler))));
        }
        std::fprintf(stderr,
                     "[boot][scheduler] dropped late DP completion "
                     "scheduler=%08X task=%08X count=%llu\n",
                     scheduler, task,
                     static_cast<unsigned long long>(late_event));
    }

    // DP completion publication is non-blocking in N64ModernRuntime, unlike
    // the dependent SP edge. Consuming this stale queue message therefore
    // needs no host-worker acknowledgement; there is no retail task left to
    // clear, complete, or reschedule.
    return 0;
}

extern "C" void dkr_scheduler_sp_handled(std::uint8_t*, recomp_context*) {
    // The host graphics/audio worker cannot safely publish a dependent DP edge
    // until DKR's scheduler has finished clearing and rescheduling the active
    // SP task. This hook is placed at the common exit of __scHandleRSP by the
    // Patch Pipeline and provides that exact acknowledgement.
    ultramodern::acknowledge_external_message_src(
        ultramodern::EventMessageSource::Sp);
}

extern "C" void dkr_scheduler_dp_handled(std::uint8_t*, recomp_context*) {
}

extern "C" void dkr_shadow_interpolation_begin(std::uint8_t* rdram,
                                                  recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_shadow_interpolation_active = false;
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    const std::uint32_t object = static_cast<std::uint32_t>(context->r6);
    const std::uint32_t shadow = static_cast<std::uint32_t>(context->r7);
    const auto key = dkr::runtime::presentation::shadow_presentation_key(
        rdram, object, shadow);
    // Keep failed/unknown shadow captures inside the explicit shadow scope.
    // A zero token maps to G_EX_ID_IGNORE and cleanly disables interpolation;
    // StaticAuto can otherwise match this frame's reused shadow heap against
    // unrelated vertices from the preceding authored frame.
    const PresentationGroupMode mode = PresentationGroupMode::DynamicShadow;
    g_shadow_interpolation_active = AppendPresentationGroupCommand(
        rdram, RdramAddress(kTrackDisplayListAddress), mode, key.token,
        key.batch_count);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_vehicle_part_interpolation_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_vehicle_part_interpolation_active = false;
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }

    // This branch has already replaced s0 with DKR's single shared transform
    // scratch buffer, so s0 is deliberately *not* an object identity. Pair the
    // attachment with its active parent render_object and the ordinal of the
    // private matrix that DKR is about to submit. This keeps four wheels,
    // propellers and steering wheels distinct while allowing directional
    // sprite frames to change discretely (vehicle-part vertices are never
    // interpolated by the F3DDKR bridge).
    const std::uint32_t matrix_reference =
        static_cast<std::uint32_t>(context->r6);
    if (matrix_reference < 0x80000000U || matrix_reference > 0x807FFFFCU) {
        return;
    }
    const std::uint32_t attachment_matrix = static_cast<std::uint32_t>(
        MEM_W(0, static_cast<gpr>(static_cast<std::int32_t>(
                     matrix_reference))));
    const auto key =
        dkr::runtime::presentation::active_vehicle_part_presentation_key(
            attachment_matrix);
    if (key.attachment_token == 0U ||
        key.attachment_slot ==
            dkr::runtime::presentation::kInvalidVehiclePartSlot) {
        return;
    }
    const std::uint32_t sprite = static_cast<std::uint32_t>(
        MEM_W(0x70, context->r29));
    if (sprite < 0x80000000U || sprite > 0x807FFFFCU) {
        return;
    }
    const std::uint16_t frame_count = static_cast<std::uint16_t>(
        MEM_H(0, static_cast<gpr>(static_cast<std::int32_t>(sprite))));
    const std::uint8_t frame_variant =
        dkr::runtime::presentation::vehicle_part_frame_variant(
            static_cast<std::uint32_t>(context->r18), frame_count);
    g_vehicle_part_interpolation_active = AppendPresentationGroupCommand(
        rdram, context->r17, PresentationGroupMode::DynamicVehiclePart,
        key.attachment_token, frame_variant);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_vehicle_part_matrix_identity(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    const std::uint32_t transform = static_cast<std::uint32_t>(context->r16);
    const std::uint32_t matrix_reference = static_cast<std::uint32_t>(
        MEM_W(0x64, context->r29));
    if (matrix_reference < 0x80000000U || matrix_reference > 0x807FFFFCU) {
        return;
    }
    const std::uint32_t matrix = static_cast<std::uint32_t>(
        MEM_W(0, RdramAddress(matrix_reference)));
    // render_sprite_billboard owns this flag. It is initialised for the
    // normal attachment half and cleared when DKR selects the mirrored half.
    // Keep those histories distinct instead of inferring handedness from a
    // camera- or frame-dependent transform.
    const bool mirrored = MEM_W(0x30, context->r29) == 0U;
    dkr::runtime::presentation::register_active_vehicle_part_matrix(
        transform, matrix, mirrored);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_billboard_interpolation_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_billboard_interpolation_active = false;
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }

    // Each billboard needs a stable token that follows its owning object rather
    // than its transient display-list address. render_sprite_billboard keeps
    // its ObjectTransform* in s0/r16, not the owning Object*. Use the validated
    // render_object capture established by the surrounding Patch Pipeline hook;
    // calls outside render_object have no active capture and remain discrete.
    const std::uint16_t token =
        dkr::runtime::presentation::presentation_token_for_active_capture();
    const std::uint32_t sprite = static_cast<std::uint32_t>(
        MEM_W(0x70, context->r29));
    if (token == 0U || sprite == 0U) {
        dkr::runtime::presentation::interpolation_trace_billboard(
            token != 0U, sprite != 0U, false);
        return;
    }

    const std::uint8_t variant =
        dkr::runtime::presentation::presentation_variant_for_address(sprite);
    g_billboard_interpolation_active = AppendPresentationGroupCommand(
        rdram, context->r17, PresentationGroupMode::DynamicBillboard,
        token, variant);
    dkr::runtime::presentation::interpolation_trace_billboard(
        true, true, g_billboard_interpolation_active);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_fix_car_steering_wheel_material(
    std::uint8_t* rdram, recomp_context* context) {
    using namespace dkr::runtime::steering_wheel;

    if (rdram == nullptr || context == nullptr) {
        return;
    }

    const auto object_model = static_cast<std::uint32_t>(context->r23);
    const auto batch = static_cast<std::uint32_t>(context->r2);
    const auto texture = static_cast<std::uint32_t>(context->r18);
    const auto stack = static_cast<std::uint32_t>(context->r29);
    if (!is_valid_render_pointer(object_model, 0x28U) ||
        !is_valid_render_pointer(batch, 0x08U) ||
        !is_valid_render_pointer(texture, 0x06U) ||
        !is_valid_render_pointer(stack, 0xB4U)) {
        return;
    }

    const auto object = static_cast<std::uint32_t>(
        MEM_W(0xB4, static_cast<gpr>(static_cast<std::int32_t>(stack))));
    if (!is_valid_render_pointer(object, 0x64U)) {
        return;
    }

    const auto model_address =
        static_cast<gpr>(static_cast<std::int32_t>(object_model));
    const auto batch_address =
        static_cast<gpr>(static_cast<std::int32_t>(batch));
    const auto texture_address =
        static_cast<gpr>(static_cast<std::int32_t>(texture));
    const auto object_address =
        static_cast<gpr>(static_cast<std::int32_t>(object));

    BatchIdentity identity{};
    identity.behaviour =
        static_cast<std::uint16_t>(MEM_H(0x48, object_address));
    if (identity.behaviour == kRacerBehaviour) {
        const auto racer =
            static_cast<std::uint32_t>(MEM_W(0x64, object_address));
        if (!is_valid_render_pointer(racer, 0x1D6U)) {
            return;
        }
        const auto racer_address =
            static_cast<gpr>(static_cast<std::int32_t>(racer));
        identity.vehicle =
            static_cast<std::int8_t>(MEM_BU(0x1D6, racer_address));
    } else if (identity.behaviour == kTitleVehicleAnimationBehaviour) {
        // Object+0x64 is AnimatedObject state in the title demo, not a Racer.
        identity.vehicle = kNoVehicle;
    } else {
        return;
    }

    identity.batch_count =
        static_cast<std::uint16_t>(MEM_H(0x28, model_address));
    identity.batch_index = static_cast<std::uint16_t>(context->r10);
    identity.texture_index = MEM_BU(0x00, batch_address);
    identity.vertex_count = static_cast<std::int32_t>(context->r17);
    identity.triangle_count = static_cast<std::int32_t>(context->r21);
    identity.authored_flags = static_cast<std::uint32_t>(context->r8);
    identity.texture_width = MEM_BU(0x00, texture_address);
    identity.texture_height = MEM_BU(0x01, texture_address);
    identity.texture_format = MEM_BU(0x02, texture_address);
    identity.texture_flags =
        static_cast<std::uint16_t>(MEM_H(0x06, texture_address));

    // A loaded ObjectModel texture table stores one eight-byte TextureInfo per
    // entry. Require this batch's table entry to resolve to the same loaded
    // TextureHeader and to repeat its dimensions/format before admitting the
    // otherwise exact four-vertex steering-wheel predicate. This avoids
    // relying on a revision-specific ROM address while remaining much narrower
    // than matching render flags alone.
    const auto texture_count =
        static_cast<std::uint16_t>(MEM_H(0x22, model_address));
    const auto texture_table =
        static_cast<std::uint32_t>(MEM_W(0x00, model_address));
    if (identity.texture_index < texture_count && texture_table != 0U) {
        const auto entry_offset =
            static_cast<std::uint32_t>(identity.texture_index) * 8U;
        if (IsRdramWordAddress(texture_table, entry_offset + 7U)) {
            const auto table_address =
                static_cast<gpr>(static_cast<std::int32_t>(texture_table));
            identity.texture_asset_matches =
                static_cast<std::uint32_t>(
                    MEM_W(entry_offset, table_address)) == texture &&
                MEM_BU(entry_offset + 4U, table_address) ==
                    identity.texture_width &&
                MEM_BU(entry_offset + 5U, table_address) ==
                    identity.texture_height &&
                MEM_BU(entry_offset + 6U, table_address) ==
                    identity.texture_format;
        }
    }

    context->r6 = static_cast<gpr>(corrected_material_flags(
        identity, static_cast<std::uint32_t>(context->r6)));
}

extern "C" void dkr_vehicle_part_interpolation_end(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_vehicle_part_interpolation_active &&
        !g_billboard_interpolation_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, context->r17, PresentationGroupMode::World);
    g_vehicle_part_interpolation_active = false;
    g_billboard_interpolation_active = false;
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_shadow_interpolation_end(std::uint8_t* rdram,
                                                recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_shadow_interpolation_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, RdramAddress(kTrackDisplayListAddress),
        PresentationGroupMode::World);
    g_shadow_interpolation_active = false;
#else
    (void)rdram;
#endif
}

extern "C" void dkr_surface_interpolation_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_surface_interpolation_active = false;
    // This hook runs only after DKR accepts a batch for drawing. Begin the
    // outer segment scope lazily here so empty/hidden passes cannot pile up
    // begin/end metadata at one unchanged display-list address.
    EnsureLevelSegmentInterpolation(rdram);
    constexpr std::uint32_t kRenderWater = 0x2000U;
    if (!dkr::runtime::enhancements::modern_presentation_enabled() ||
        (static_cast<std::uint32_t>(context->r17) & kRenderWater) == 0U) {
        return;
    }
    const auto key = dkr::runtime::presentation::surface_presentation_key(
        static_cast<std::uint32_t>(context->r5));
    if (key.token == 0U) {
        return;
    }
    g_surface_interpolation_active = AppendPresentationGroupCommand(
        rdram, RdramAddress(kTrackDisplayListAddress),
        PresentationGroupMode::DynamicSurface, key.token, key.variant);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_level_segment_interpolation_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_level_segment_interpolation_active = false;
    g_level_segment_pending_key = {};
    if (rdram == nullptr || context == nullptr ||
        !dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    g_level_segment_pending_key =
        dkr::runtime::presentation::level_segment_presentation_key(
            static_cast<std::uint32_t>(context->r4), context->r5 != 0);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_surface_interpolation_end(
    std::uint8_t* rdram, recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_surface_interpolation_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, RdramAddress(kTrackDisplayListAddress),
        PresentationGroupMode::World);
    g_surface_interpolation_active = false;
#else
    (void)rdram;
#endif
}

extern "C" void dkr_level_segment_interpolation_end(
    std::uint8_t* rdram, recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    g_level_segment_pending_key = {};
    if (!g_level_segment_interpolation_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, RdramAddress(kTrackDisplayListAddress),
        PresentationGroupMode::World);
    g_level_segment_interpolation_active = false;
#else
    (void)rdram;
#endif
}

extern "C" void dkr_title_intro_audio_tail(
    std::uint8_t* rdram, recomp_context* context) {
    const std::uint32_t kTitleDemoIndexAddress =
        dkr::runtime::revision_addresses::TitleDemoIndex;
    const std::uint32_t kTitleRevealTimerAddress =
        dkr::runtime::revision_addresses::TitleRevealTimer;
    const bool cinematic_complete = context->r2 != 0;
    const bool first_title_demo =
        MEM_W(0, RdramAddress(kTitleDemoIndexAddress)) == 0;
    const bool title_revealed =
        MEM_W(0, RdramAddress(kTitleRevealTimerAddress)) != 0;
    const std::uint32_t update_rate = std::max(
        static_cast<std::uint32_t>(MEM_W(0x30, context->r29)), 1U);
    const auto action = g_title_intro_tail_gate.update(
        cinematic_complete, first_title_demo, title_revealed, update_rate);
    if (action == dkr::runtime::intro::TailAction::Hold) {
        context->r2 = 0;
        // sp28 is the title-demo timer completion path. Suppress it together
        // with the cinematic completion signal while the tail is active.
        MEM_W(0x28, context->r29) = 0;
    } else if (action == dkr::runtime::intro::TailAction::Release) {
        // Replay the latched edge exactly once. menu_title_screen_loop then
        // follows its unmodified transition path at 0x80083B9C.
        context->r2 = 1;
    }
}

extern "C" void dkr_transition_cover_begin(std::uint8_t* rdram,
                                             recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    const float cover_zoom = ExpandedCoverScale();
    if (cover_zoom <= 1.0001F) {
        return;
    }
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        g_transition_interpolation_active = AppendPresentationGroupCommand(
            rdram, context->r16, PresentationGroupMode::StaticAuto);
    }
    g_saved_transition_x = ReadRdramFloat(rdram, kOrthoMatrixAddress);
    g_saved_transition_y = ReadRdramFloat(rdram, kOrthoMatrixAddress + 5U * sizeof(float));
    WriteRdramFloat(rdram, kOrthoMatrixAddress, g_saved_transition_x * cover_zoom);
    WriteRdramFloat(rdram, kOrthoMatrixAddress + 5U * sizeof(float),
                    g_saved_transition_y * cover_zoom);
    g_transition_cover_active = true;
#else
    (void)rdram;
#endif
}

extern "C" void dkr_widen_gradient_sky(std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (rdram == nullptr || context == nullptr || context->r3 == 0) {
        return;
    }
    const int viewport_layout = static_cast<std::int32_t>(
        MEM_W(0, RdramAddress(
            dkr::runtime::revision_addresses::ViewportLayout)));
    const float cover_scale = ExpandedCoverScale();
    const float horizontal_scale =
        dkr::runtime::enhancements::split_sky_horizontal_cover_scale(
            cover_scale, viewport_layout);
    const float vertical_scale =
        dkr::runtime::enhancements::split_sky_vertical_cover_scale(
            cover_scale, viewport_layout);
    if (horizontal_scale <= 1.0001F && vertical_scale <= 1.0001F) {
        return;
    }
    // trackbg_render_gradient has just populated four 10-byte Vertex records;
    // r3 still points at the first record. Multiplayer never calls
    // skydome_render(), so mirror the accepted skydome cover policy directly
    // on this private quad. DKR has already halved Y for its horizontal
    // two-player layout; scaling about zero preserves the split divider and
    // the top/bottom colour ordering while keeping HUD and world geometry out
    // of this correction.
    for (gpr offset : {gpr{0}, gpr{10}, gpr{20}, gpr{30}}) {
        const std::int16_t x = static_cast<std::int16_t>(MEM_H(offset, context->r3));
        const std::int16_t y = static_cast<std::int16_t>(
            MEM_H(offset + 2U, context->r3));
        const long widened = std::lround(
            static_cast<float>(x) * horizontal_scale);
        const long covered_y = std::lround(
            static_cast<float>(y) * vertical_scale);
        MEM_H(offset, context->r3) = static_cast<std::int16_t>(
            std::clamp(widened, -32768L, 32767L));
        MEM_H(offset + 2U, context->r3) = static_cast<std::int16_t>(
            std::clamp(covered_y, -32768L, 32767L));
    }
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_transition_cover_end(std::uint8_t* rdram, recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_transition_cover_active) {
        return;
    }
    WriteRdramFloat(rdram, kOrthoMatrixAddress, g_saved_transition_x);
    WriteRdramFloat(rdram, kOrthoMatrixAddress + 5U * sizeof(float), g_saved_transition_y);
    g_transition_cover_active = false;
#else
    (void)rdram;
#endif
}

extern "C" void dkr_transition_interpolation_end(std::uint8_t* rdram,
                                                   recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (g_transition_interpolation_active) {
        AppendPresentationGroupCommand(
            rdram, context->r16, PresentationGroupMode::World);
        g_transition_interpolation_active = false;
    }
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_background_fill_stretch_begin(std::uint8_t* rdram,
                                                     recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_background_fill_stretch_active = false;
    if (ExpandedCoverScale() <= 1.0001F) {
        return;
    }
    g_background_fill_stretch_active = AppendPresentationGroupCommand(
        rdram, context->r16, PresentationGroupMode::StaticAuto, 1U,
        0U, PresentationMarkerKind::BackgroundAspect);
#else
    (void)rdram;
    (void)context;
#endif
}

// These two hooks bracket only divider_draw's optional lower-right fill,
// after retail has decided whether to show the map/AI panel or T.T. camera.
extern "C" void dkr_three_player_panel_begin(std::uint8_t* rdram,
                                             recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_three_player_panel_active = false;
    if (rdram == nullptr || context == nullptr ||
        !dkr::runtime::enhancements::modern_presentation_enabled() ||
        ExpandedCoverScale() <= 1.0001F ||
        MEM_W(0, RdramAddress(dkr::runtime::revision_addresses::ViewportLayout)) != 2) return;
    // Token two extends the fill from centre to right. Token one is the
    // unrelated full-width clear; a rect-aspect override alone cannot affect
    // RT64's fill-cycle clear path.
    g_three_player_panel_active = AppendPresentationGroupCommand(
        rdram, context->r16, PresentationGroupMode::StaticAuto, 2U,
        0U, PresentationMarkerKind::BackgroundAspect);
#else
    (void)rdram; (void)context;
#endif
}

extern "C" void dkr_three_player_panel_end(std::uint8_t* rdram,
                                           recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_three_player_panel_active) return;
    AppendPresentationGroupCommand(
        rdram, context->r16, PresentationGroupMode::World, 0U,
        0U, PresentationMarkerKind::BackgroundAspect);
    g_three_player_panel_active = false;
#else
    (void)rdram; (void)context;
#endif
}

extern "C" void dkr_background_fill_stretch_end(std::uint8_t* rdram,
                                                   recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_background_fill_stretch_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, context->r16, PresentationGroupMode::World, 0U,
        0U, PresentationMarkerKind::BackgroundAspect);
    g_background_fill_stretch_active = false;
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_postrace_background_stretch_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_postrace_background_stretch_active = false;
    if (!dkr::runtime::enhancements::modern_presentation_enabled() ||
        ExpandedCoverScale() <= 1.0001F) {
        return;
    }

    // bgdraw_texture owns only DKR's repeating post-race mosaic. Scope RT64's
    // rectangle stretch to that function so the texture reaches the host
    // edges while the framed race viewport and every menu/HUD coordinate keep
    // their authored 4:3 placement.
    g_postrace_background_stretch_active = AppendPresentationGroupCommand(
        rdram, context->r4, PresentationGroupMode::StaticAuto, 0U,
        0U, PresentationMarkerKind::BackgroundAspect);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_postrace_background_stretch_end(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_postrace_background_stretch_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, context->r4, PresentationGroupMode::World, 0U,
        0U, PresentationMarkerKind::BackgroundAspect);
    g_postrace_background_stretch_active = false;
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_chequer_background_stretch_begin(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    g_chequer_background_stretch_active = false;
    if (!dkr::runtime::enhancements::modern_presentation_enabled() ||
        ExpandedCoverScale() <= 1.0001F) {
        return;
    }
    // Battle/challenge results select bgdraw_chequer rather than the normal
    // post-race mosaic. It is the same background-only layer, so give it the
    // same host-width policy without widening the authored replay viewport.
    g_chequer_background_stretch_active = AppendPresentationGroupCommand(
        rdram, context->r4, PresentationGroupMode::StaticAuto, 0U,
        0U, PresentationMarkerKind::BackgroundAspect);
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_chequer_background_stretch_end(
    std::uint8_t* rdram, recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_chequer_background_stretch_active) {
        return;
    }
    AppendPresentationGroupCommand(
        rdram, context->r4, PresentationGroupMode::World, 0U,
        0U, PresentationMarkerKind::BackgroundAspect);
    g_chequer_background_stretch_active = false;
#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_widen_void_primitive(std::uint8_t* rdram,
                                            recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    if (rdram == nullptr || context == nullptr || context->r2 < 40U) {
        return;
    }
    const int viewport_layout = static_cast<std::int32_t>(
        MEM_W(0, RdramAddress(
            dkr::runtime::revision_addresses::ViewportLayout)));
    const float cover_scale =
        dkr::runtime::enhancements::split_sky_horizontal_cover_scale(
            ExpandedCoverScale(), viewport_layout);
    if (cover_scale <= 1.0001F) {
        return;
    }

    // void_generate_primitive has just written four vertices and advanced v0
    // by their 40-byte total. DKR's void is the flat-colour, camera-facing
    // mesh behind level geometry; its original +/-300 lateral limit can be
    // exposed by the multiplayer gradient path on a wide viewport. Expand it
    // only for a genuine split layout and solely along its own
    // camera-horizontal basis, preserving every vertex's Y coordinate and
    // forward depth.
    const gpr vertices = context->r2 - static_cast<gpr>(4 * 10);
    if (!IsRdramWordAddress(static_cast<std::uint32_t>(vertices), 36U)) {
        return;
    }
    const float lateral_x = ReadRdramFloat(rdram, kVoidLateralXAddress);
    const float lateral_z = ReadRdramFloat(rdram, kVoidLateralZAddress);
    const float centre_x = ReadRdramFloat(rdram, kVoidCentreXAddress);
    const float centre_z = ReadRdramFloat(rdram, kVoidCentreZAddress);
    const float basis_length_sq =
        lateral_x * lateral_x + lateral_z * lateral_z;
    if (!std::isfinite(lateral_x) || !std::isfinite(lateral_z) ||
        !std::isfinite(centre_x) || !std::isfinite(centre_z) ||
        basis_length_sq < 0.5F || basis_length_sq > 1.5F) {
        return;
    }

    for (std::uint32_t index = 0; index < 4U; ++index) {
        const gpr vertex = vertices + static_cast<gpr>(index * 10U);
        const float x = static_cast<float>(
            static_cast<std::int16_t>(MEM_H(0, vertex)));
        const float z = static_cast<float>(
            static_cast<std::int16_t>(MEM_H(4, vertex)));
        const float dx = x - centre_x;
        const float dz = z - centre_z;
        const float lateral =
            (dx * lateral_x + dz * lateral_z) / basis_length_sq;
        const float expansion = lateral * (cover_scale - 1.0F);
        const long widened_x = std::clamp(
            std::lround(x + expansion * lateral_x), -32768L, 32767L);
        const long widened_z = std::clamp(
            std::lround(z + expansion * lateral_z), -32768L, 32767L);
        MEM_H(0, vertex) = static_cast<std::int16_t>(widened_x);
        MEM_H(4, vertex) = static_cast<std::int16_t>(widened_z);
    }

#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_track_select_background_cover(std::uint8_t* rdram,
                                                     recomp_context* context) {
#if DKR_RUNTIME_HAS_RT64
    const float cover_zoom = ExpandedCoverScale();
    if (cover_zoom <= 1.0001F || context->r8 == 0) {
        return;
    }

    // func_8008F618 has finished populating one private four-vertex strip and
    // restored its base pointer to t0/r8. Widen only those background vertices.
    // Writing absolute coordinates makes this idempotent across frames and
    // avoids mutating gOrthoMatrixF or gViewProjMatrixF, which are shared by
    // subsequent menu, HUD, and world draws.
    const long half_width = std::clamp(
        std::lround(160.0F * cover_zoom), 160L, 32767L);
    for (std::uint32_t vertex = 0; vertex < 4U; ++vertex) {
        const long x = ((vertex & 1U) == 0U) ? -half_width : half_width;
        MEM_H(static_cast<gpr>(vertex * 10U), context->r8) =
            static_cast<std::int16_t>(x);
    }

#else
    (void)rdram;
    (void)context;
#endif
}

extern "C" void dkr_skybox_cover_begin(std::uint8_t* rdram,
                                         recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    const float cover_zoom = ExpandedCoverScale();
    if (cover_zoom <= 1.0001F || g_sky_cover_active) {
        return;
    }

    // This hook runs after mtx_world_origin() has rebuilt the current camera
    // projection and immediately before render_object() builds skydome-only
    // matrices. Preserve the accepted uniform cover through 21:9. At wider
    // ratios, invert only the excess vertical zoom so the horizon moves back
    // down while horizontal coverage continues to reach the viewport edges.
    const float vertical_zoom =
        dkr::runtime::enhancements::sky_vertical_cover_scale(cover_zoom);
    for (std::uint32_t row = 0; row < 4U; ++row) {
        for (std::uint32_t column = 0; column < 2U; ++column) {
            const std::uint32_t slot = row * 2U + column;
            const std::uint32_t address = kViewProjectionMatrixAddress +
                (row * 4U + column) * sizeof(float);
            g_saved_sky_projection_columns[slot] = ReadRdramFloat(rdram, address);
            WriteRdramFloat(rdram, address,
                            g_saved_sky_projection_columns[slot] *
                                (column == 0U ? cover_zoom : vertical_zoom));
        }
    }
    g_sky_cover_active = true;
#else
    (void)rdram;
#endif
}

extern "C" void dkr_skybox_cover_end(std::uint8_t* rdram, recomp_context*) {
#if DKR_RUNTIME_HAS_RT64
    if (!g_sky_cover_active) {
        return;
    }
    for (std::uint32_t row = 0; row < 4U; ++row) {
        for (std::uint32_t column = 0; column < 2U; ++column) {
            const std::uint32_t slot = row * 2U + column;
            const std::uint32_t address = kViewProjectionMatrixAddress +
                (row * 4U + column) * sizeof(float);
            WriteRdramFloat(rdram, address, g_saved_sky_projection_columns[slot]);
        }
    }
    g_sky_cover_active = false;
#else
    (void)rdram;
#endif
}

extern "C" int dkr_audio_voice_guard(std::uint8_t* rdram, recomp_context* context) {
    const auto synth_address = static_cast<std::uint32_t>(context->r4);
    const auto voice_address = static_cast<std::uint32_t>(context->r5);
    const auto requested_bus = static_cast<std::uint16_t>(context->r6);
    const auto is_rdram_address = [](std::uint32_t address) {
        return address >= 0x80000000U && address < 0x80800000U;
    };
    if (!is_rdram_address(synth_address) || !is_rdram_address(voice_address) ||
        requested_bus > 1U) {
        std::fprintf(stderr,
                     "[boot][audio] ignored invalid voice route synth=%08X voice=%08X bus=%u\n",
                     synth_address, voice_address, static_cast<unsigned>(requested_bus));
        return 1;
    }

    const auto read_word = [rdram](std::uint32_t address, std::uint32_t offset) {
        const auto signed_address = static_cast<gpr>(static_cast<std::int32_t>(address));
        return static_cast<std::uint32_t>(MEM_W(offset, signed_address));
    };
    bool found_voice = false;
    for (const std::uint32_t list_offset : {0x04U, 0x0CU, 0x14U}) {
        std::uint32_t node = read_word(synth_address, list_offset);
        for (unsigned count = 0; node != 0 && count < 128; ++count) {
            if (!is_rdram_address(node)) {
                break;
            }
            if (node == voice_address) {
                found_voice = true;
                break;
            }
            node = read_word(node, 0);
        }
        if (found_voice) {
            break;
        }
    }
    if (!found_voice) {
        std::fprintf(stderr,
                     "[boot][audio] ignored voice outside synth lists synth=%08X voice=%08X bus=%u\n",
                     synth_address, voice_address, static_cast<unsigned>(requested_bus));
        return 1;
    }

    const auto signed_voice = static_cast<gpr>(static_cast<std::int32_t>(voice_address));
    const auto current_bus = static_cast<std::uint32_t>(MEM_BU(0xDC, signed_voice));
    if (current_bus > 1U) {
        std::fprintf(stderr,
                     "[boot][audio] normalized uninitialized voice bus voice=%08X value=%u\n",
                     voice_address, current_bus);
        MEM_B(0xDC, signed_voice) = 0;
    }
    return 0;
}

extern "C" int dkr_audio_bus_guard(std::uint8_t* rdram, recomp_context* context) {
    const auto bus_address = static_cast<std::uint32_t>(context->r4);
    const auto is_rdram_address = [](std::uint32_t address) {
        return address >= 0x80000000U && address < 0x80800000U;
    };

    if (!is_rdram_address(bus_address)) {
        std::fprintf(stderr,
                     "[boot][audio] ignored invalid auxiliary bus=%08X param=%u source=%08X\n",
                     bus_address, static_cast<unsigned>(context->r5),
                     static_cast<unsigned>(context->r6));
        context->r2 = 0;
        return 1;
    }

    const auto signed_bus = static_cast<gpr>(static_cast<std::int32_t>(bus_address));
    const auto source_count = static_cast<std::uint32_t>(MEM_W(0x14, signed_bus));
    const auto sources_address = static_cast<std::uint32_t>(MEM_W(0x1C, signed_bus));
    if (source_count > 64U || !is_rdram_address(sources_address)) {
        std::fprintf(stderr,
                     "[boot][audio] ignored malformed auxiliary bus=%08X param=%u "
                     "count=%u sources=%08X\n",
                     bus_address, static_cast<unsigned>(context->r5), source_count,
                     sources_address);
        context->r2 = 0;
        return 1;
    }
    return 0;
}

extern "C" void rmonPrintf_recomp(std::uint8_t*, recomp_context*) {
    // Retail debug output has no observable game-state effect.
}

extern "C" void __osSpSetStatus_recomp(std::uint8_t*, recomp_context*) {
    // SP task state is owned by ultramodern's scheduler.
}

extern "C" void __osSiGetAccess_recomp(std::uint8_t*, recomp_context*) {}
extern "C" void __osSiRelAccess_recomp(std::uint8_t*, recomp_context*) {}
