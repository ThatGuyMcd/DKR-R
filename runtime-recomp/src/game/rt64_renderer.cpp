#include "rt64_renderer.hpp"
#include "render_power_policy.hpp"
#include "performance_trace.hpp"
#include "water_performance.hpp"
#include "graphics_health.hpp"
#if defined(__ANDROID__)
#include "android_surface_state.hpp"
#endif
#include "mobile_graphics_preset.hpp"
#include "render/rt64_generated_mip_config.h"

#include "game_registration.hpp"
#include "presentation_identity.hpp"
#include "renderer_snapshot.hpp"
#include "local_scenery_rt64.hpp"
#include "revision_addresses.hpp"
#include "runtime_enhancements.hpp"
#include "widescreen_policy.hpp"
#include "interpolation_state_policy.hpp"
#include "runtime_hud_layout.hpp"
#include "hud_reference_layout.hpp"
#include <bit>
#include "runtime_netplay.hpp"
#include "netplay/failure_recorder.hpp"
#include "runtime_telemetry.hpp"
#include "runtime_texture_packs.hpp"
#include "startup_performance.hpp"
#include "vi_presentation_policy.hpp"
#include "runtime_platform.hpp"
#include "runtime_ui.hpp"
#if defined(_WIN32)
#include <Unknwn.h>
#include <oaidl.h>
#endif
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
#include "netplay/experimental_present_gate.hpp"
#include "netplay/experimental_performance.hpp"
#include "netplay/experimental_presentation.hpp"
#include "hle/rt64_workload_queue.h"
#include "hle/rt64_present_queue.h"
#include "render/rt64_framebuffer_renderer.h"
#include "render/rt64_buffer_uploader.h"
#endif

#include "common/rt64_enhancement_configuration.h"
#include "common/rt64_user_configuration.h"
#include "hle/rt64_application.h"
#include "runtime_mipmap_loading.hpp"
#include "hle/rt64_state.h"
#include "render/rt64_shader_library.h"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"

#include <SDL.h>
#if defined(__ANDROID__)
#include <SDL_syswm.h>
#include <SDL_system.h>
#include "../android/android_storage.hpp"
#include "android_pipeline_cache.hpp"
#include <stdexcept>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <utility>

#if defined(DKR_EXPERIMENTAL_RACE_TEST)
namespace dkr::runtime {
// One decoder thread owns these buffers. TaskIdentityScope copies its bindings
// into RT64 before reuse; no queued GPU consumer retains these vectors.
struct OwnedDecodeScratch {
    LocalSceneryRT64Pass local_scenery;
    std::vector<presentation::LocatedPresentationMarker> markers;
    std::vector<presentation::OwnedMatrixBinding> matrices;
    std::vector<presentation::OwnedShadowBinding> shadows;
    std::vector<std::uint32_t> covered_matrices;
    void clear() {markers.clear();matrices.clear();shadows.clear();covered_matrices.clear();}
};
}
#endif

namespace {

class CanonicalViPresentationScope {
public:
    explicit CanonicalViPresentationScope(RT64::Application& application)
        : v_start_(application.core.VI_V_START_REG) {
        const auto* width = application.core.VI_WIDTH_REG;
        const auto* y_scale = application.core.VI_Y_SCALE_REG;
        if (v_start_ == nullptr || width == nullptr || y_scale == nullptr ||
            *width != dkr::runtime::presentation::kCanonicalViWidth) {
            return;
        }

        original_v_start_ = *v_start_;
        canonical_v_start_ =
            dkr::runtime::presentation::canonicalise_dkr_v_region(
                original_v_start_, *y_scale);
        if (canonical_v_start_ == original_v_start_) {
            return;
        }

        *v_start_ = canonical_v_start_;
        active_ = true;
        if (!logged_.exchange(true, std::memory_order_relaxed)) {
            std::fprintf(stderr,
                         "[boot][vi] canonical present 320x240 v-start=%08X->%08X "
                         "inferred=%u->%u\n",
                         original_v_start_, canonical_v_start_,
                         dkr::runtime::presentation::inferred_vi_height(
                             original_v_start_, *y_scale),
                         dkr::runtime::presentation::inferred_vi_height(
                             canonical_v_start_, *y_scale));
        }
    }

    ~CanonicalViPresentationScope() {
        if (active_) {
            *v_start_ = original_v_start_;
        }
    }

    CanonicalViPresentationScope(const CanonicalViPresentationScope&) = delete;
    CanonicalViPresentationScope& operator=(
        const CanonicalViPresentationScope&) = delete;

private:
    inline static std::atomic<bool> logged_{false};
    std::uint32_t* v_start_ = nullptr;
    std::uint32_t original_v_start_ = 0U;
    std::uint32_t canonical_v_start_ = 0U;
    bool active_ = false;
};

static_assert(
    std::tuple_size_v<decltype(
        std::declval<RT64::WorkloadQueue>().workloads)> >= 4,
    "Modern presentation requires RT64's four-slot owned workload ring");

std::array<std::uint8_t, 0x40> g_rom_header{};
std::array<std::uint8_t, 0x1000> g_dmem{};
std::array<std::uint8_t, 0x1000> g_imem{};
std::uint32_t g_mi_interrupt = 0;
std::array<std::uint32_t, 8> g_dpc_registers{};
std::mutex g_active_renderer_mutex;
dkr::runtime::RT64Renderer* g_active_renderer = nullptr;
int g_requested_refresh_target = 30;
int g_effective_refresh_target = 30;
int g_detected_display_rate = 60;
// High-refresh matching remains isolated until it has passed full visual
// validation across menus, hubs, races and every vehicle type. The public
// Modern profile currently falls back to the proven native cadence; visible
// development checkpoints opt in explicitly.
bool ExperimentalInterpolationEnabled() {
    // Accurate is the immutable 30 Hz baseline. Modern is the explicit user
    // opt-in to RT64 presentation interpolation; its selected display/manual
    // target must work from the launcher without a private environment flag.
    return dkr::runtime::enhancements::modern_presentation_enabled();
}

RT64::EnhancementConfiguration::Presentation::Mode PresentationMode(bool owned_frame = false) {
    using Mode=RT64::EnhancementConfiguration::Presentation::Mode;
    return dkr::runtime::presentation::present_from_vi_history(owned_frame)
        ? Mode::PresentEarly : Mode::Console;
}

void CheckInterrupts() {}

RT64::UserConfiguration::GraphicsAPI ToRT64(ultramodern::renderer::GraphicsApi api) {
    using UM = ultramodern::renderer::GraphicsApi;
    using RT = RT64::UserConfiguration::GraphicsAPI;
    switch (api) {
    case UM::D3D12: return RT::D3D12;
    case UM::Vulkan: return RT::Vulkan;
    case UM::Metal: return RT::Metal;
    default: return RT::Automatic;
    }
}

RT64::UserConfiguration::Antialiasing ToRT64(
    ultramodern::renderer::Antialiasing antialiasing) {
    using UM = ultramodern::renderer::Antialiasing;
    using RT = RT64::UserConfiguration::Antialiasing;
    switch (antialiasing) {
    case UM::MSAA2X: return RT::MSAA2X;
    case UM::MSAA4X: return RT::MSAA4X;
    case UM::MSAA8X: return RT::MSAA8X;
    default: return RT::None;
    }
}

RT64::UserConfiguration::AspectRatio ToRT64(
    ultramodern::renderer::AspectRatio aspect_ratio) {
    using UM = ultramodern::renderer::AspectRatio;
    using RT = RT64::UserConfiguration::AspectRatio;
    switch (aspect_ratio) {
    case UM::Expand: return RT::Expand;
    case UM::Manual: return RT::Manual;
    default: return RT::Original;
    }
}

int DetectDisplayRate() {
    auto* window = static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window());
    if (window == nullptr) {
        return 60;
    }
    const int display = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode mode{};
    if (display < 0 || SDL_GetCurrentDisplayMode(display, &mode) != 0 ||
        mode.refresh_rate <= 0) {
        return 60;
    }
    return dkr::runtime::enhancements::clamp_presentation_rate(mode.refresh_rate);
}

void ApplyConfig(RT64::Application& application,
                 const ultramodern::renderer::GraphicsConfig& config) {
    application.userConfig.idleWorkActive = dkr::runtime::render_power::gpu_keep_awake.load();
#if defined(__ANDROID__)
    std::fprintf(stderr, "[graphics][power] Android GPU keep-awake=%s\n",
                 application.userConfig.idleWorkActive ? "on" : "off");
#endif
    const bool modern = dkr::runtime::enhancements::modern_presentation_enabled();
    const auto effective_api = modern
        ? config.api_option
        : ultramodern::renderer::GraphicsApi::Auto;
    const auto effective_aspect = modern
        ? config.ar_option
        : ultramodern::renderer::AspectRatio::Original;
    dkr::runtime::enhancements::set_fit_to_window_enabled(
        modern && effective_aspect == ultramodern::renderer::AspectRatio::Expand);
    application.userConfig.graphicsAPI = ToRT64(effective_api);
    application.userConfig.antialiasing = ToRT64(config.msaa_option);
    application.userConfig.aspectRatio = ToRT64(effective_aspect);
    // HUD placement remains authored at its original 4:3 coordinates in both
    // presets. Modern widescreen expands only qualified world/background
    // passes; it never moves screen-space race information.
    application.userConfig.extAspectRatio =
        RT64::UserConfiguration::AspectRatio::Original;
    application.userConfig.resolution =
        config.res_option == ultramodern::renderer::Resolution::Auto
            ? RT64::UserConfiguration::Resolution::WindowIntegerScale
            : RT64::UserConfiguration::Resolution::Manual;
    application.userConfig.resolutionMultiplier =
        config.res_option == ultramodern::renderer::Resolution::Original2x
            ? 2.0 * std::max(config.ds_option, 1)
            : static_cast<double>(std::max(config.ds_option, 1));
    application.userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
    // Accurate is a hard renderer boundary, not a cosmetic launcher preset.
    // Modern may request presentation-only interpolation after DKR's sky,
    // transition, gradient and menu-background matrices have been explicitly
    // excluded by the custom F3DDKR bridge.
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        // RT64's swap-chain estimate can be implausibly high on hidden,
        // variable-refresh or newly-created Windows surfaces. That previously
        // let "Match display" saturate the GPU and stall the original 30 Hz
        // game producer. Resolve both Modern choices against SDL's active
        // desktop mode and send RT64 an explicit, bounded manual target.
        g_detected_display_rate = DetectDisplayRate();
        g_requested_refresh_target = config.rr_option ==
                ultramodern::renderer::RefreshRate::Manual
            ? dkr::runtime::enhancements::clamp_presentation_rate(
                  config.rr_manual_value)
            : g_detected_display_rate;
        if (ExperimentalInterpolationEnabled()) {
            // Match Display follows the active monitor. A deliberately chosen
            // manual rate remains deliberate, including rates above the
            // monitor refresh for latency testing; it is still bounded by the
            // public 30..500 FPS contract and RT64's paced presentation queue.
            // Manual 60 and Match Display are unchanged from the accepted
            // Modern-60 baseline.
            g_effective_refresh_target =
                dkr::runtime::enhancements::resolve_effective_presentation_rate(
                    dkr::runtime::enhancements::PresentationProfile::Modern,
                    config.rr_option ==
                        ultramodern::renderer::RefreshRate::Manual,
                    g_requested_refresh_target, g_detected_display_rate);
            application.userConfig.refreshRate =
                RT64::UserConfiguration::RefreshRate::Manual;
            application.userConfig.refreshRateTarget = g_effective_refresh_target;
        } else {
            g_effective_refresh_target = 30;
            application.userConfig.refreshRate =
                RT64::UserConfiguration::RefreshRate::Original;
            application.userConfig.refreshRateTarget = 30;
        }
    } else {
        g_requested_refresh_target = 30;
        g_effective_refresh_target = 30;
        g_detected_display_rate = DetectDisplayRate();
        application.userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
        application.userConfig.refreshRateTarget = 30;
    }
    application.userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
    application.userConfig.internalColorFormat =
        config.hpfb_option == ultramodern::renderer::HighPrecisionFramebuffer::On
            ? RT64::UserConfiguration::InternalColorFormat::High
            : config.hpfb_option == ultramodern::renderer::HighPrecisionFramebuffer::Off
                ? RT64::UserConfiguration::InternalColorFormat::Standard
                : RT64::UserConfiguration::InternalColorFormat::Automatic;
#if defined(__ANDROID__)
    dkr::runtime::android_surface::target_rate.store(g_effective_refresh_target);
    std::fprintf(stderr,
        "[android][graphics] resolution-mode=%u multiplier=%.1f downsample=%d "
        "msaa=%u precision=%u target=%d display=%d\n",
        static_cast<unsigned>(application.userConfig.resolution),
        application.userConfig.resolutionMultiplier,
        std::max(config.ds_option, 1), static_cast<unsigned>(config.msaa_option),
        static_cast<unsigned>(config.hpfb_option), g_effective_refresh_target,
        g_detected_display_rate);
#endif
}

ultramodern::renderer::SetupResult MapSetupResult(RT64::Application::SetupResult result) {
    using RT = RT64::Application::SetupResult;
    using UM = ultramodern::renderer::SetupResult;
    switch (result) {
    case RT::Success: return UM::Success;
    case RT::DynamicLibrariesNotFound: return UM::DynamicLibrariesNotFound;
    case RT::InvalidGraphicsAPI: return UM::InvalidGraphicsAPI;
    case RT::GraphicsAPINotFound: return UM::GraphicsAPINotFound;
    case RT::GraphicsDeviceNotFound: return UM::GraphicsDeviceNotFound;
    }
    return UM::GraphicsDeviceNotFound;
}

ultramodern::renderer::GraphicsApi MapGraphicsAPI(
    RT64::UserConfiguration::GraphicsAPI api) {
    using RT = RT64::UserConfiguration::GraphicsAPI;
    using UM = ultramodern::renderer::GraphicsApi;
    switch (api) {
    case RT::D3D12: return UM::D3D12;
    case RT::Vulkan: return UM::Vulkan;
    case RT::Metal: return UM::Metal;
    default: return UM::Auto;
    }
}

} // namespace

dkr::runtime::RT64Renderer::RT64Renderer(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode, bool owned_mode) : owned_mode_(owned_mode) {
    if(owned_mode_)netplay::experimental::performance::begin_report_window();
    const auto renderer_started_at =
        dkr::runtime::startup_performance::Clock::now();
    // RT64Renderer can be created more than once while the DKR-R process and
    // launcher window remain alive. These bridge buffers emulate N64 graphics
    // hardware registers and therefore belong to a game session, even though
    // their storage is process-static. Never let a stopped session seed the
    // next renderer with stale display-list or interrupt state.
    g_rom_header.fill(0);
    g_dmem.fill(0);
    g_imem.fill(0);
    g_mi_interrupt = 0;
    g_dpc_registers.fill(0);

    RT64::Application::Core core{};
#if defined(_WIN32)
    core.window = window_handle.window;
#elif defined(__ANDROID__)
    SDL_SysWMinfo native_window{};
    SDL_VERSION(&native_window.version);
    if (!window_handle || !SDL_GetWindowWMInfo(window_handle, &native_window) ||
        !native_window.info.android.window) {
        std::fprintf(stderr, "[boot][android] no game surface: %s\n", SDL_GetError());
        setup_result = ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        return;
    }
    core.window = native_window.info.android.window;
#elif defined(__linux__)
    core.window = window_handle;
#elif defined(__APPLE__)
    core.window.window = window_handle.window;
    core.window.view = window_handle.view;
#endif
    core.checkInterrupts = CheckInterrupts;
    core.HEADER = g_rom_header.data();
    core.RDRAM = rdram;
    core.DMEM = g_dmem.data();
    core.IMEM = g_imem.data();
    core.MI_INTR_REG = &g_mi_interrupt;
    core.DPC_START_REG = &g_dpc_registers[0];
    core.DPC_END_REG = &g_dpc_registers[1];
    core.DPC_CURRENT_REG = &g_dpc_registers[2];
    core.DPC_STATUS_REG = &g_dpc_registers[3];
    core.DPC_CLOCK_REG = &g_dpc_registers[4];
    core.DPC_BUFBUSY_REG = &g_dpc_registers[5];
    core.DPC_PIPEBUSY_REG = &g_dpc_registers[6];
    core.DPC_TMEM_REG = &g_dpc_registers[7];

    auto* vi = ultramodern::renderer::get_vi_regs();
    core.VI_STATUS_REG = &vi->VI_STATUS_REG;
    core.VI_ORIGIN_REG = &vi->VI_ORIGIN_REG;
    core.VI_WIDTH_REG = &vi->VI_WIDTH_REG;
    core.VI_INTR_REG = &vi->VI_INTR_REG;
    core.VI_V_CURRENT_LINE_REG = &vi->VI_V_CURRENT_LINE_REG;
    core.VI_TIMING_REG = &vi->VI_TIMING_REG;
    core.VI_V_SYNC_REG = &vi->VI_V_SYNC_REG;
    core.VI_H_SYNC_REG = &vi->VI_H_SYNC_REG;
    core.VI_LEAP_REG = &vi->VI_LEAP_REG;
    core.VI_H_START_REG = &vi->VI_H_START_REG;
    core.VI_V_START_REG = &vi->VI_V_START_REG;
    core.VI_V_BURST_REG = &vi->VI_V_BURST_REG;
    core.VI_X_SCALE_REG = &vi->VI_X_SCALE_REG;
    core.VI_Y_SCALE_REG = &vi->VI_Y_SCALE_REG;

    RT64::ApplicationConfiguration application_config{};
    application_config.appId = "dkr-port";
    application_config.useConfigurationFile = false;
    application_config.detectDataPath = true;
#if defined(__ANDROID__)
    // Desktop path detection can select /data/.dkr-port on Android and throw
    // from RT64's constructor before its setup error handling is reached.
    // Supply the existing public configuration hook; do not modify RT64.
    application_config.detectDataPath = false;
    std::string storage_error;
    if (!android::prepare_renderer_directory(SDL_AndroidGetInternalStoragePath(),
                                            application_config.dataPath, storage_error)) {
        std::fprintf(stderr, "[boot][android] %s\n", storage_error.c_str());
        setup_result = ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        return;
    }
    std::fprintf(stderr, "[boot][android] private renderer storage ready\n");
    android_pipeline_cache::directory = application_config.dataPath / "pipeline-cache";
#endif
    auto config = ultramodern::renderer::get_graphics_config();
#if defined(__ANDROID__)
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        dkr::runtime::mobile_graphics::apply_accurate_budget(config);
    }
#endif
    const auto effective_anisotropy =
        static_cast<std::uint32_t>(
#if defined(__ANDROID__)
            !dkr::runtime::enhancements::modern_presentation_enabled() ? 4 :
#endif
            dkr::runtime::enhancements::anisotropy_level());
    RT64::setDefaultSamplerAnisotropy(effective_anisotropy);
    const float texture_lod_bias =
        dkr::runtime::enhancements::effective_texture_lod_bias();
    RT64::setDefaultSamplerMipLODBias(texture_lod_bias);
    RT64::beginGeneratedMipSession(dkr::runtime::enhancements::generated_mipmaps_requested());
    RT64::setGeneratedMipSampling(dkr::runtime::enhancements::modern_presentation_enabled());
    std::fprintf(stderr, "[boot][graphics] generated_texture_mipmaps=%s (sampling=%s)\n",
        RT64::generatedMipSessionEnabled() ? "on" : "off",
        RT64::generatedMipSamplingEnabled() ? "on" : "off");
    std::fprintf(stderr,
                 "[boot][graphics] texture_lod_bias=%+.2f anisotropy=%u\n",
                 static_cast<double>(texture_lod_bias),
                 effective_anisotropy);
    const auto create_application = [&] {
        const auto application_started_at =
            dkr::runtime::startup_performance::Clock::now();
        application_ = std::make_unique<RT64::Application>(core, application_config);
        ApplyConfig(*application_, config);
        application_->userConfig.developerMode = developer_mode;
        // DKR renders a canonical 320x240 VI image. RT64's generic VI height
        // heuristic adds and rounds guard rows (often inferring 244), which
        // exposes the unused final rows as a thin bottom/right bar after Fit to
        // Window scaling. Present the authored 320x240 extent exactly.
        // Present the actual VI extent after the DKR-owned scoped normalizer
        // removes RT64's inferred guard rows. This applies equally to Accurate
        // and Modern and affects only the final source image sampling.
        application_->enhancementConfig.presentation.removeBlackBorders = true;
        application_->enhancementConfig.rect.fixRectLR = true;
        // DKR presents directly from its alternating rendered color buffers.
        // SkipBuffering can select a stale VI-history entry before either buffer
        // has been approved for interpolation, yielding an entirely black Modern
        // frame. PresentEarly follows the current VI buffer and remains valid both
        // before and after RT64 enables interpolation for that framebuffer.
        // Owned frames carry their own exact framebuffer and VI-black state.
        // PresentEarly uses historical VIs during DL decode and ignores the
        // explicit updateScreen below. That native scheduler policy must not
        // choose an obsolete buffer after an owned scene/rollback boundary.
        application_->enhancementConfig.presentation.mode = PresentationMode(owned_mode_);
        dkr::runtime::startup_performance::report(
            "rt64-application-create", application_started_at);
    };
    create_application();
    // DKR presents directly from its alternating rendered color buffers.
    // SkipBuffering can select a stale VI-history entry before either buffer
    // has been approved for interpolation, yielding an entirely black Modern
    // frame. PresentEarly follows the current VI buffer and remains valid both
    // before and after RT64 enables interpolation for that framebuffer.
    std::uint32_t thread_id = 0;
#if defined(_WIN32)
    thread_id = window_handle.thread_id;
#endif
    auto setup_started_at = dkr::runtime::startup_performance::Clock::now();
    setup_result = MapSetupResult(application_->setup(thread_id));
#if defined(__ANDROID__)
    if (dkr::runtime::graphics_health::failed()) {
        setup_result = ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }
#endif
    dkr::runtime::startup_performance::report("rt64-setup",
                                               setup_started_at);
    chosen_api = MapGraphicsAPI(application_->chosenGraphicsAPI);
    if (setup_result != ultramodern::renderer::SetupResult::Success &&
#if defined(__ANDROID__)
        !dkr::runtime::graphics_health::failed() &&
#endif
        config.api_option != ultramodern::renderer::GraphicsApi::Auto) {
        const auto failed_api = config.api_option;
        const auto failed_result = setup_result;
        std::fprintf(stderr,
                     "[boot][rt64] requested api=%u failed result=%u; retrying Automatic\n",
                     static_cast<unsigned>(failed_api),
                     static_cast<unsigned>(failed_result));
        // setup() can leave backend-owned objects partially initialised. A
        // clean Application is the only safe retry boundary.
        application_.reset();
        config.api_option = ultramodern::renderer::GraphicsApi::Auto;
        create_application();
        setup_started_at = dkr::runtime::startup_performance::Clock::now();
        setup_result = MapSetupResult(application_->setup(thread_id));
        dkr::runtime::startup_performance::report("rt64-setup-fallback",
                                                   setup_started_at);
        chosen_api = MapGraphicsAPI(application_->chosenGraphicsAPI);
        if (setup_result == ultramodern::renderer::SetupResult::Success) {
            dkr::runtime::ui::persist_graphics_api_fallback();
            std::fprintf(stderr,
                         "[boot][rt64] Automatic API recovery succeeded api=%u\n",
                         static_cast<unsigned>(chosen_api));
        }
    }
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        std::fprintf(stderr, "[boot][rt64] setup failed result=%u\n",
                     static_cast<unsigned>(setup_result));
        application_.reset();
        return;
    }
    // setup/fallback creates a fresh texture cache. Reapply persisted enabled
    // packs on its first presentation, even if the retired renderer had already
    // consumed this library generation. Do not change users' enabled settings.
    dkr::runtime::texture_packs::renderer_started();
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    if(owned_mode_) {
        owned_registration_=std::make_unique<netplay::experimental::present_gate::Registration>(application_->state.get());
        // Native workers have retired. Until the first owned frame arrives,
        // present a black host surface with the preparation UI, not stale VI
        // registers/framebuffers left over from the local bootstrap.
        *application_->core.VI_H_START_REG=0;
    }
#endif
    const bool fullscreen =
        config.wm_option == ultramodern::renderer::WindowMode::Fullscreen;
    application_->setFullScreen(fullscreen);
    std::fprintf(stderr,
                 "[boot][rt64] initialized api=%u profile=%s refresh-mode=%u "
                 "requested=%d effective=%d display=%d\n",
                 static_cast<unsigned>(chosen_api),
                 dkr::runtime::enhancements::modern_presentation_enabled()
                     ? "Modern" : "Accurate",
                 static_cast<unsigned>(application_->userConfig.refreshRate),
                 g_requested_refresh_target, g_effective_refresh_target,
                  g_detected_display_rate);
    {
        std::scoped_lock lock(g_active_renderer_mutex);
        g_active_renderer = this;
    }
    dkr::runtime::startup_performance::report("renderer-ready",
                                               renderer_started_at);
}

dkr::runtime::RT64Renderer::~RT64Renderer() {
    shutdown();
}

bool dkr::runtime::RT64Renderer::valid() {
    return application_ != nullptr;
}

bool dkr::runtime::RT64Renderer::update_config(
    const ultramodern::renderer::GraphicsConfig& old_config,
    const ultramodern::renderer::GraphicsConfig& new_config) {
    std::scoped_lock presentation_lock(presentation_mutex_);
    if (application_ == nullptr || old_config == new_config) {
        return false;
    }
    if (old_config.wm_option != new_config.wm_option) {
        const bool fullscreen = new_config.wm_option ==
            ultramodern::renderer::WindowMode::Fullscreen;
        application_->setFullScreen(fullscreen);
    }
    const bool resolution_or_aspect_changed =
        old_config.res_option != new_config.res_option ||
        old_config.ar_option != new_config.ar_option ||
        old_config.ds_option != new_config.ds_option;
    const bool multisampling_changed =
        old_config.msaa_option != new_config.msaa_option;
    ApplyConfig(*application_, new_config);
    if (owned_mode_) {
        application_->enhancementConfig.presentation.mode = PresentationMode(true);
    }
    // RT64's multisample resources (shader cache, render targets and frame
    // buffers) must be rebuilt while the new sample count is staged locally,
    // before that configuration is published to the present queues. Publishing
    // first allowed an in-flight frame to observe the new sample count while it
    // still owned old-sample resources, which is why changing AA live could
    // intermittently crash. This mirrors RT64's own inspector transaction.
    if (multisampling_changed) {
        application_->updateMultisampling();
    }
    // updateMultisampling() already waits for both RT64 queues, destroys every
    // sample-count-dependent framebuffer/render target and rebuilds the shader
    // pipelines. Publishing an AA-only change with discardFBs=true requested a
    // second framebuffer teardown after those new resources became visible.
    // That was usually tolerated between 2x/4x/8x, but crossing the 1-sample
    // boundary (None <-> MSAA) could tear down the newly selected resolve path
    // while the next presentation acquired it. RT64's own Inspector publishes
    // the completed AA transaction with discardFBs=false; mirror that here and
    // reserve the discard flag for changes that really alter framebuffer size
    // or aspect.
    application_->updateUserConfig(resolution_or_aspect_changed);
    if (multisampling_changed) {
        std::fprintf(stderr,
                     "[graphics][aa] live transition %u->%u complete; "
                     "framebuffer-discard=%u\n",
                     static_cast<unsigned>(old_config.msaa_option),
                     static_cast<unsigned>(new_config.msaa_option),
                     resolution_or_aspect_changed ? 1U : 0U);
    }
    return true;
}

void dkr::runtime::RT64Renderer::enable_instant_present() {
    std::scoped_lock presentation_lock(presentation_mutex_);
    if (application_ != nullptr) {
        application_->enhancementConfig.presentation.mode = PresentationMode(owned_mode_);
        application_->updateEnhancementConfig();
    }
}

void dkr::runtime::RT64Renderer::send_dl(const OSTask* task,
                                         std::uint8_t* rdram_snapshot) {
    std::scoped_lock presentation_lock(presentation_mutex_);
    if (application_ == nullptr || rdram_snapshot == nullptr) {
        return;
    }
    dkr::runtime::telemetry::record_graphics_task();
    RT64::MipWaitFeedbackScope mipFeedback([this] { present_mipmap_loading(*application_); });

    // A real RSP DMAs task inputs before notifying the CPU that it may recycle
    // them. DKR relies on that during scene transitions and can free texture
    // allocations while the host graphics queue is still pending. Parse this
    // task from the submission-time snapshot, then restore live RDRAM for VI.
    RendererSnapshotScope snapshot_scope(application_->core.RDRAM,
                                         application_->state->RDRAM,
                                         rdram_snapshot);
    dkr::runtime::presentation::TaskIdentityScope identity_scope(
        rdram_snapshot, task->t.data_ptr);
    // DKR authors a new visual state at 30 Hz. Deriving that source cadence
    // from delayed VI history creates a positive feedback loop under load:
    // one late workload is misread as 20/15 Hz, RT64 schedules three or four
    // renders to catch up, and the extra work makes the next workload later.
    // Modern interpolation must keep the source contract stable and may skip
    // an optional intermediate when a scene exceeds its budget. Accurate mode
    // retains RT64's original VI-history behaviour unchanged.
    if (ExperimentalInterpolationEnabled()) {
        application_->state->setRefreshRate(30);
    }
#if defined(__ANDROID__)
    if (dkr::runtime::graphics_health::failed()) return;
    try {
    dkr::runtime::graphics_health::TaskBoundary boundary;
#endif
    if (track_performance::enabled()) {
        const auto start = track_performance::Clock::now();
        f3ddkr_.process(*application_, *task);
        record_track_performance(rdram_snapshot, start);
    } else {
        track_capture_.count = 0; // A later opt-in capture must not span the idle gap.
        f3ddkr_.process(*application_, *task);
    }
#if defined(__ANDROID__)
    } catch (const dkr::runtime::graphics_health::TaskAborted&) {
        // fullSync may have scheduled CPU uploads before descriptor creation.
        // Drain them while the task snapshot and its draw buffers still live.
        application_->drawDataUploader->wait();
        application_->transformsUploader->wait();
        application_->tilesUploader->wait();
        application_->state->framebufferRenderer->waitForUploaders();
        // Return normally so the runtime still completes this task's DP edge.
        // The SDL thread reports the retained failure and requests shutdown.
        std::fprintf(stderr, "[android][graphics] stopped failed display-list task\n");
    }
#endif
}

void dkr::runtime::RT64Renderer::record_track_performance(
    std::uint8_t* snapshot, track_performance::Clock::time_point start) {
    using namespace track_performance;
    const auto now = Clock::now();
    const auto read_word = [&](std::uint32_t address) {
        std::uint32_t value = 0U;
        // Submission snapshot uses host-endian aligned N64 words. Read only
        // revision-mapped globals, never live simulation memory.
        std::memcpy(&value, snapshot + (address & 0x007FFFFCU), sizeof(value));
        return value;
    };
    const auto scene = presentation::task_scene_generation();
    const auto map = read_word(revision_addresses::CurrentMapId);
    const auto menu = read_word(revision_addresses::CurrentMenuId);
    auto& capture = track_capture_;
    if (capture.count == 0U || capture.scene != scene ||
        capture.map != map || capture.menu != menu) {
        capture.count = 0U;
        capture.started = start;
        capture.scene = scene;
        capture.map = map;
        capture.menu = menu;
    }
    capture.decode_ms[capture.count++] =
        std::chrono::duration<double, std::milli>(now - start).count();
    if (capture.count < kSamples) return;

    // The worker publishes completed timer history; never contend on the
    // renderer's long-held threadMutex or read its live vectors cross-thread.
    const auto published = water::read_history();
    const auto& history = published.values;
    const double history_age_ms = published.sequence ?
        std::chrono::duration<double, std::milli>(track_performance::Clock::now() - published.published).count() : -1.0;
    const bool available = published.sequence != 0U && history_age_ms < 2500.0;
    if (!available) ++capture.unavailable_windows;
    const auto decode = summarize(capture.decode_ms);
    const double seconds = std::chrono::duration<double>(now - capture.started).count();
    std::fprintf(stderr,
        "[perf][track] scene=%u map=%u menu=%u tasks=%zu seconds=%.3f "
        "task-hz=%.2f decode-ms(p50/p95/p99)=%.3f/%.3f/%.3f "
        "renderer-history-available=%u unavailable-windows=%llu history-sequence=%llu history-age-ms=%.3f\n",
        scene, map, menu, capture.count, seconds,
        seconds > 0.0 ? static_cast<double>(capture.count - 1U) / seconds : 0.0,
        decode.median, decode.p95, decode.p99, available ? 1U : 0U,
        static_cast<unsigned long long>(capture.unavailable_windows),
        static_cast<unsigned long long>(published.sequence), history_age_ms);
    const char* labels[] = {"matching", "render-cpu", "render-gpu", "workload"};
    if (available) {
        for (std::size_t i = 0U; i < history.size(); ++i) {
            const auto result = summarize(history[i]);
            std::fprintf(stderr,
                "[perf][track-history] %s n=%zu ms(p50/p95/p99)=%.3f/%.3f/%.3f\n",
                labels[i], result.count, result.median, result.p95, result.p99);
        }
    }
    // These are rolling renderer timings, not presented-frame intervals.
    // They can include the preceding scene until its history ages out.
    capture.count = 0U;
}

void dkr::runtime::RT64Renderer::update_screen() {
#if defined(__ANDROID__)
    if (dkr::runtime::graphics_health::failed()) return;
#endif
    std::scoped_lock presentation_lock(presentation_mutex_);
    if (application_ == nullptr) {
        return;
    }
    const bool keep_awake = render_power::gpu_keep_awake.load();
    if (application_->userConfig.idleWorkActive != keep_awake) {
        application_->userConfig.idleWorkActive = keep_awake;
        application_->updateUserConfig(false);
        std::fprintf(stderr, "[graphics][power] GPU keep-awake=%s\n", keep_awake ? "on" : "off");
    }
    dkr::runtime::telemetry::record_vi_present();
    RT64::MipWaitFeedbackScope mipFeedback([this] { present_mipmap_loading(*application_); });
    if (application_->sharedQueueResources != nullptr) {
        const std::uint64_t completed = application_->sharedQueueResources->
            totalPresentations.load(std::memory_order_relaxed);
        if (completed > 0 && !first_successful_presentation_reported_) {
            first_successful_presentation_reported_ = true;
#if defined(__ANDROID__)
            dkr::runtime::graphics_health::first_presentation.store(true);
#endif
            dkr::runtime::startup_performance::mark("first-successful-presentation");
        }
        dkr::runtime::telemetry::record_presented_frames(
            dkr::runtime::presentation_counter::consume_delta(
                completed, completed_presentations_));
        const std::uint64_t total = application_->sharedQueueResources->
            totalInterpolatedPresentations.load(std::memory_order_relaxed);
        std::uint64_t interpolated_delta = 0;
        if (total >= interpolated_present_count_) {
            interpolated_delta = total - interpolated_present_count_;
            dkr::runtime::telemetry::record_interpolated_presents(
                interpolated_delta);
        }
        interpolated_present_count_ = total;
    } else {
        dkr::runtime::telemetry::record_presented_frames(1U);
    }
    ++present_count_;
#if defined(__ANDROID__)
    constexpr bool report_performance = true;
#else
    static const bool report_performance = std::getenv("DKR_POWER_PROFILE") != nullptr;
#endif
    const auto report_now = std::chrono::steady_clock::now();
    if (report_performance && report_now - last_android_report_ >= std::chrono::seconds(5)) {
        last_android_report_ = report_now;
        performance_trace::report();
        const auto metrics = dkr::runtime::telemetry::metrics();
        std::fprintf(stderr,
            "[graphics][performance] presented=%.1f sim=%.1f graphics=%.1f "
            "vi=%.1f target=%d scale=%.1f\n",
            metrics.presented_fps, metrics.simulation_hz, metrics.graphics_hz,
            metrics.vi_hz, g_effective_refresh_target, get_resolution_scale());
    }
    if (present_count_ == 1 && !owned_mode_) {
        dkr::runtime::startup_performance::mark("first-vi-received");
        std::fprintf(stderr, "[boot] VI initialized; starting recompiled DKR entrypoint\n");
        recomp::start_game(kGameId);
    }
    // Replacement changes are consumed on RT64's presentation thread. This
    // keeps pack hot-swaps transactional with texture streaming and prevents
    // the settings UI from mutating renderer-owned caches concurrently.
    dkr::runtime::texture_packs::apply_pending(
        *application_, dkr::runtime::enhancements::modern_presentation_enabled());
    {
        CanonicalViPresentationScope vi_scope(*application_);
        application_->updateScreen();
    }
    if (present_count_ == 1) {
        dkr::runtime::startup_performance::mark(
            "first-vi-update-completed");
    }
    // Preserve DKR's proven VI/DP scheduling path exactly; constructing the
    // next overlay frame after the game present keeps UI work out of the
    // original graphics-completion critical section.
    dkr::runtime::ui::draw(*application_);
    last_wait_presentation_ = std::chrono::steady_clock::now();
    observed_wait_generation_ =
        dkr::runtime::netplay::online_wait_generation();
    observed_overlay_visible_ = dkr::runtime::ui::overlay_visible();
}

void dkr::runtime::RT64Renderer::service_online_wait_presentation() {
#if defined(__ANDROID__)
    if (dkr::runtime::graphics_health::failed()) return;
#endif
    constexpr auto kWaitPresentationInterval = std::chrono::milliseconds(33);
    if (!dkr::runtime::netplay::online_wait_active()) {
        wait_replay_deferred_logged_ = false;
        return;
    }

    std::unique_lock presentation_lock(presentation_mutex_, std::try_to_lock);
    if (!presentation_lock.owns_lock()) return;
    const bool queue_ready = application_ != nullptr &&
        application_->state != nullptr &&
        application_->workloadQueue != nullptr &&
        application_->presentQueue != nullptr &&
        application_->framebufferGraphicsWorker != nullptr &&
        application_->sharedQueueResources != nullptr;
    const std::uint64_t completed_presentations = queue_ready
        ? application_->sharedQueueResources->totalPresentations.load(
              std::memory_order_acquire)
        : 0U;
    if (!dkr::runtime::presentation_counter::can_repeat_last_present(
            present_count_, queue_ready, completed_presentations)) {
        if (!wait_replay_deferred_logged_) {
            std::fprintf(
                stderr,
                "[netplay][presentation] waiting for first completed frame "
                "before replay (vi=%llu completed=%llu queue=%s)\n",
                static_cast<unsigned long long>(present_count_),
                static_cast<unsigned long long>(completed_presentations),
                queue_ready ? "ready" : "not-ready");
            wait_replay_deferred_logged_ = true;
        }
        return;
    }
    if (wait_replay_deferred_logged_) {
        std::fprintf(
            stderr,
            "[netplay][presentation] completed frame available; wait replay "
            "is now armed (completed=%llu)\n",
            static_cast<unsigned long long>(completed_presentations));
        wait_replay_deferred_logged_ = false;
    }

    const auto now = std::chrono::steady_clock::now();
    const std::uint64_t wait_generation =
        dkr::runtime::netplay::online_wait_generation();
    const bool overlay_visible = dkr::runtime::ui::overlay_visible();
    const bool state_changed =
        observed_wait_generation_ != wait_generation ||
        observed_overlay_visible_ != overlay_visible;
    if (!state_changed &&
        last_wait_presentation_.time_since_epoch().count() != 0 &&
        now - last_wait_presentation_ < kWaitPresentationInterval) {
        return;
    }

    // Build only presentation-owned UI, then use RT64's synchronized paused
    // update path to replay it. PresentQueue::repeatLastPresent() is only a
    // cursor operation: calling it directly re-consumes a queue slot without
    // cloning its matching workload/present IDs and without marking the slot
    // paused. The present thread then advances the ring barrier a second time,
    // eventually corrupting live queue ownership during ordinary gameplay.
    //
    // State::updateScreen() already owns the complete safe replay sequence
    // used by RT64's debugger: wait for both queues, clone and advance the
    // matching workload and present as paused entries, then submit both. Its
    // paused branch returns before VI history or authored game state changes.
    dkr::runtime::ui::draw(*application_);
    bool has_inspector = false;
    {
        const std::unique_lock inspector_lock(
            application_->presentQueue->inspectorMutex, std::try_to_lock);
        if (!inspector_lock.owns_lock()) return;
        has_inspector = application_->presentQueue->inspector != nullptr;
    }
    if (has_inspector) {
        const bool previous_pause =
            application_->state->debuggerInspector.paused;
        application_->state->debuggerInspector.paused = true;
        const auto replay_started = std::chrono::steady_clock::now();
        application_->updateScreen();
        application_->state->debuggerInspector.paused = previous_pause;
        const auto replay_elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                      replay_started);
        if (replay_elapsed >= std::chrono::milliseconds(50) &&
            replay_elapsed > slowest_wait_replay_) {
            slowest_wait_replay_ = replay_elapsed;
            netplay::failure_recorder().record(
                netplay::FailureEventKind::ProgressWatchdog, 0U,
                static_cast<std::uint32_t>(replay_elapsed.count()), 0U,
                "slow synchronized wait presentation (milliseconds)");
        }
    }
    last_wait_presentation_ = now;
    observed_wait_generation_ = wait_generation;
    observed_overlay_visible_ = overlay_visible;
}

void dkr::runtime::RT64Renderer::shutdown() {
    {
        std::scoped_lock active_lock(g_active_renderer_mutex);
        if (g_active_renderer == this) g_active_renderer = nullptr;
    }
    std::scoped_lock presentation_lock(presentation_mutex_);
    if (application_ != nullptr) {
        dkr::runtime::ui::detach(*application_);
        application_->end();
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        owned_submissions_.clear(); // Actual workers joined, not just first-target notification.
        owned_registration_.reset(); // After the actual RT64 consumers joined.
#endif
        // end() releases RT64's backend resources but leaves the Application
        // object alive. Destroy it here so the next in-process game session
        // receives a genuinely fresh renderer and so the destructor cannot
        // detach from an already-ended application a second time.
        application_.reset();
    }
}

#if defined(DKR_EXPERIMENTAL_RACE_TEST)
void dkr::runtime::RT64Renderer::reap_owned() {
    if(!owned_registration_)return;
    while(!owned_submissions_.empty() && owned_registration_->completed(
            owned_submissions_.front().workload,owned_submissions_.front().present))
        owned_submissions_.pop_front();
}
bool dkr::runtime::RT64Renderer::owned_capacity_available() {
    reap_owned();
    return owned_submissions_.size()<2;
}
void dkr::runtime::RT64Renderer::present_owned(
    std::shared_ptr<const netplay::experimental::RenderSnapshot> lease,
    netplay::experimental::PresentationMailbox& mailbox) {
    if(!owned_mode_||!application_||!owned_registration_||!mailbox.is_current(lease))return;
    reap_owned();
    // The render loop leaves the latest mailbox image pending until capacity
    // returns; it never blocks on an unbounded completion wait or adds debt.
    // Defend the bound for any other caller without changing queue lifetimes.
    if(owned_submissions_.size()>=2)return;
    auto workspace=netplay::experimental::DecodeWorkspace::create(lease);
    if(!workspace)throw std::runtime_error("Owned frame already has a decode consumer.");
    // No native CPU/VI worker exists in this mode. RAM is read synchronously
    // by State/RSP/TMEM; async uploaders read copied DrawData/transform/tile
    // vectors, framebuffer storage and owned texture buffers, NOT guest RAM.
    // Their CPU completion remains mandatory. GPU queues consume those RT64
    // resources; retain the immutable generation lease until BOTH full queue
    // callbacks complete (not RT64's early first-target notification).
    const auto drain=[&] {
        netplay::experimental::performance::Scope timing(netplay::experimental::performance::Stage::Drain);
        auto& a=*application_;a.drawDataUploader->wait();a.transformsUploader->wait();a.tilesUploader->wait();
        a.state->framebufferRenderer->waitForUploaders();
        a.workloadQueue->waitForWorkloadId(a.state->workloadId);a.presentQueue->waitForPresentId(a.state->presentId);
        a.workloadQueue->waitForIdle();a.presentQueue->waitForIdle();
    };
    RendererSnapshotScope snapshot(application_->core.RDRAM,application_->state->RDRAM,workspace->bytes().data());
    bool queued=false;
    struct DrainOnExit {decltype(drain)& run;bool& queued;~DrainOnExit(){if(!queued)run();}} consumers{drain,queued};
    netplay::experimental::present_gate::Scope submission(*owned_registration_,lease);
    if(!mailbox.is_current(lease))return;
    const auto& d=lease->descriptor();OSTask task{};task.t.type=M_GFXTASK;task.t.data_ptr=d.display_start;task.t.data_size=d.display_end-d.display_start;
    // Local quality/aspect settings must never enter rollback RAM or hashes.
    // Interpret audited draw-site observations against the MUTABLE DECODE COPY
    // and the same typed marker consumer used by the normal Patch Pipeline.
    if(!owned_decode_scratch_)owned_decode_scratch_=std::make_unique<OwnedDecodeScratch>();
    owned_decode_scratch_->clear();
    auto& owned_markers=owned_decode_scratch_->markers;
    auto& owned_matrices=owned_decode_scratch_->matrices;
    auto& owned_shadows=owned_decode_scratch_->shadows;
    auto& covered_matrices=owned_decode_scratch_->covered_matrices;
    const bool modern=enhancements::modern_presentation_enabled();
    // One immutable local layout policy for this decode. Never consulted by
    // the rewindable CPU or included in peer/checkpoint fingerprints.
    const auto hud_mode=hud::mode();
    const bool expanded=enhancements::modern_presentation_enabled() &&
        ultramodern::renderer::get_graphics_config().ar_option==ultramodern::renderer::AspectRatio::Expand;
    int width=0,height=0;
    if(auto* window=static_cast<SDL_Window*>(platform::sdl_window()))SDL_GetWindowSize(window,&width,&height);
    const float cover=expanded && width>0 && height>0 ?
        (std::max)(1.0F,float(width)/float(height)/(4.0F/3.0F)):1.0F;
    const auto read_vertex=[&](std::uint32_t address) {
        std::int16_t value;
        std::memcpy(&value,workspace->bytes().data()+((address-0x80000000U)^2U),2);
        return value;
    };
    const auto write_vertex=[&](std::uint32_t address,float value) {
        if(!std::isfinite(value))throw std::runtime_error("Invalid owned background coordinate.");
        const auto clamped=std::int16_t(std::lround(std::clamp(value,-32768.0F,32767.0F)));
        std::memcpy(workspace->bytes().data()+((address-0x80000000U)^2U),&clamped,2);
    };
    std::uint32_t scene=0;
    {
    netplay::experimental::performance::Scope metadata_timing(netplay::experimental::performance::Stage::Metadata);
    for(const auto& event:lease->draw_events())if(event.kind==DKR_OWNED_FRAME_METADATA) {
        scene=presentation::normalise_identity(event.parameters[0]^presentation::mix_identity(std::uint32_t(lease->epoch()))^
            presentation::mix_identity(std::uint32_t(lease->generation())));
    }
    for(const auto& event:lease->draw_events()) {
        if(event.kind==DKR_OWNED_FRAME_METADATA||event.kind==DKR_OWNED_LOCAL_SCENERY)continue;
        if(event.kind==DKR_OWNED_MATRIX) {
            const auto flags=event.parameters[0];
            const auto identity=event.token?presentation::normalise_identity(event.token^presentation::mix_identity(scene)):0U;
            owned_matrices.push_back({event.address,{identity,(flags&1)!=0,(flags&2)!=0,(flags&4)!=0,(flags&8)!=0,std::uint8_t(event.parameters[3])},event.parameters[1]!=0});
            continue;
        }
        if(event.kind==DKR_OWNED_SKY_MATRIX || event.kind==DKR_OWNED_TRANSITION) {
            // Only skydome-produced combined matrices: mirror the legacy
            // row-vector projection-column cover, on the decode copy only.
            const bool already_covered=std::find(covered_matrices.begin(),covered_matrices.end(),event.address)!=covered_matrices.end();
            if(cover>1.0001F && !already_covered) {
            covered_matrices.push_back(event.address);
            for(unsigned row=0;row<4;++row)for(unsigned column=0;column<2;++column) {
                const auto p=event.address-0x80000000U+(row*4+column)*2;
                std::int16_t whole;std::uint16_t fraction;
                std::memcpy(&whole,workspace->bytes().data()+(p^2U),2);
                std::memcpy(&fraction,workspace->bytes().data()+((p+32)^2U),2);
                const double value=(double(whole)+double(fraction)/65536.0)*
                    (column==0||event.kind==DKR_OWNED_TRANSITION?cover:enhancements::sky_vertical_cover_scale(cover));
                if(!std::isfinite(value))continue;
                const auto fixed=std::int32_t(std::clamp(std::round(value*65536.0),double(INT32_MIN),double(INT32_MAX)));
                const auto integer=std::int16_t(std::uint32_t(fixed)>>16);
                const auto decimal=std::uint16_t(fixed);
                std::memcpy(workspace->bytes().data()+(p^2U),&integer,2);
                std::memcpy(workspace->bytes().data()+((p+32)^2U),&decimal,2);
            }
            }
            continue;
        }
        if(event.kind==DKR_OWNED_BACKGROUND_QUAD) {
            const auto half=std::clamp(std::lround(160.0F*cover),160L,32767L);
            for(unsigned v=0;v<4;++v) {
                const auto x=std::int16_t((v&1)?half:-half);
                std::memcpy(workspace->bytes().data()+((event.address-0x80000000U+v*10U)^2U),&x,2);
            }
            continue;
        }
        if(event.kind==DKR_OWNED_SPLIT_SKY_QUAD) {
            // Three/four-player CPU vertices remain canonical (cover=1).
            // Apply the accepted gradient-cover policy only to this image.
            const float horizontal=enhancements::split_sky_horizontal_cover_scale(cover,int(event.token));
            const float vertical=enhancements::split_sky_vertical_cover_scale(cover,int(event.token));
            for(unsigned v=0;v<4;++v) {
                const auto address=event.address+v*10U;
                write_vertex(address,float(read_vertex(address))*horizontal);
                write_vertex(address+2,float(read_vertex(address+2))*vertical);
            }
            continue;
        }
        if(event.kind==DKR_OWNED_SPLIT_VOID_QUAD) {
            float basis[4];std::memcpy(basis,event.parameters,sizeof(basis));
            const float lx=basis[0],lz=basis[1],cx=basis[2],cz=basis[3];
            const float length=lx*lx+lz*lz;
            // Same healthy-geometry guard as the legacy Patch Pipeline hook.
            if(!std::isfinite(lx)||!std::isfinite(lz)||!std::isfinite(cx)||!std::isfinite(cz)||
               length<0.5F||length>1.5F)continue;
            const float horizontal=enhancements::split_sky_horizontal_cover_scale(cover,int(event.token));
            for(unsigned v=0;v<4;++v) {
                const auto address=event.address+v*10U;
                const float x=read_vertex(address),z=read_vertex(address+4);
                const float expansion=((x-cx)*lx+(z-cz)*lz)/length*(horizontal-1.0F);
                write_vertex(address,x+expansion*lx);write_vertex(address+4,z+expansion*lz);
            }
            continue;
        }
        presentation::PresentationMarker marker{};marker.token=std::uint16_t(event.token);
        switch(event.kind) {
        case DKR_OWNED_GEOMETRY:
            if(!modern)continue;
            marker.mode=std::uint8_t(event.parameters[0]);marker.variant=std::uint8_t(event.parameters[1]);break;
        case DKR_OWNED_SHADOW: {
            if(!modern)continue;
            marker.mode=2;marker.variant=std::uint8_t(event.parameters[0]);
            const auto& p=event.parameters;
            const auto policy=presentation::rigid_shadow_owner_policy(std::uint16_t(p[1]),std::int8_t(p[3]),std::uint8_t(p[4]),std::uint16_t(p[2]));
            const float x=std::bit_cast<float>(p[5]),y=std::bit_cast<float>(p[6]),z=std::bit_cast<float>(p[7]);
            owned_shadows.push_back({std::uint16_t(event.token),policy,{{x,y,z},std::int16_t(p[8]),std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)}});break;
        }
        case DKR_OWNED_HUD_PASS:
            marker.kind=presentation::PresentationMarkerKind::HudPass;marker.mode=std::uint8_t(event.parameters[0]);marker.variant=std::uint8_t(event.token);
            marker.token=marker.mode?hud::encode_hud_viewport_cover(cover*4.0F/3.0F):0;
            break;
        case DKR_OWNED_HUD_RECT:
            marker.kind=presentation::PresentationMarkerKind::HudRect;marker.mode=std::uint8_t(event.token);marker.token=0;marker.variant=std::uint8_t(event.parameters[0]);break;
        case DKR_OWNED_HUD_WIDGET: {
            marker.kind=presentation::PresentationMarkerKind::HudWidget;marker.mode=std::uint8_t(event.token);marker.token=0;
            const auto& p=event.parameters;
            if(marker.mode&&modern) {
                if(p[10])marker.hud_transform.x=(p[10]==2?1:-1)*hud::fullscreen_gutter_authored(cover*4.0F/3.0F);
                else if(p[0]<=1&&p[1]<=1&&p[3]<=std::uint32_t(hud::Widget::TimerGlyphs)) {
                    const auto scenario=p[5]?hud::groups::Scenario::TimeTrial:p[4]==1?hud::groups::Scenario::Adventure:hud::groups::scenario_for(int(p[4]),false);
                    marker.hud_transform=hud::reference::transform(hud_mode,cover*4.0F/3.0F,
                        hud::reference::anchor(p[2],hud::Widget(p[3]),scenario,p[0]==1,p[6]!=0,std::bit_cast<float>(p[7])),float(std::int32_t(p[8])),p[9]!=0);
                }
            }
            break;
        }
        case DKR_OWNED_BACKGROUND_BEGIN: marker.kind=presentation::PresentationMarkerKind::BackgroundAspect;marker.mode=1;break;
        case DKR_OWNED_BACKGROUND_END: marker.kind=presentation::PresentationMarkerKind::BackgroundAspect;break;
        case DKR_OWNED_POSTRACE_FULL_VIEWPORT: marker.kind=presentation::PresentationMarkerKind::PostraceFullViewport;break;
        case DKR_OWNED_FRAMED_BEGIN: marker.kind=presentation::PresentationMarkerKind::FramedResults;marker.mode=1;break;
        case DKR_OWNED_FRAMED_END: marker.kind=presentation::PresentationMarkerKind::FramedResults;break;
        case DKR_OWNED_LENS_BEGIN: marker.kind=presentation::PresentationMarkerKind::TrackSelectLensFlare;marker.mode=1;break;
        case DKR_OWNED_LENS_END: marker.kind=presentation::PresentationMarkerKind::TrackSelectLensFlare;break;
        case DKR_OWNED_SPLIT_VIEWPORT:
            marker.kind=presentation::PresentationMarkerKind::SplitViewport;
            // Same 1/1024 cover encoding consumed by the legacy draw bridge.
            marker.token=std::uint16_t((std::clamp(std::lround(cover*1024.0F),0L,0x3FFFL)<<2U)|event.token);
            break;
        case DKR_OWNED_SPLIT_WORLD_BEGIN:
            marker.mode=interpolation::kAspectAdjustScopeMode;break;
        case DKR_OWNED_SPLIT_WORLD_END:
            marker.mode=0;break;
        default: throw std::runtime_error("Unsupported owned presentation observation.");
        }
        owned_markers.push_back({event.address,marker});
    }
    }
    owned_decode_scratch_->local_scenery.prepare(lease->local_scenery().get(),
        lease->draw_events(),workspace->bytes(),scene);
    const auto present_cursor=application_->presentQueue->writeCursor;
    {std::scoped_lock lock(presentation_mutex_);
        presentation::TaskIdentityScope identity{owned_markers,owned_matrices,owned_shadows,scene,modern};
        application_->state->setRefreshRate(30);
        {netplay::experimental::performance::Scope timing(netplay::experimental::performance::Stage::Process);
         f3ddkr_.process(*application_,task,&owned_decode_scratch_->local_scenery);}
        auto& c=application_->core;
        *c.VI_STATUS_REG=presentation::kRetailNtscViStatus;
        *c.VI_ORIGIN_REG=presentation::retail_ntsc_vi_origin(d.framebuffer);*c.VI_WIDTH_REG=320;
        *c.VI_INTR_REG=2;*c.VI_V_CURRENT_LINE_REG=0;*c.VI_TIMING_REG=0x03E52239;*c.VI_V_SYNC_REG=0x20D;
        *c.VI_H_SYNC_REG=0xC15;*c.VI_LEAP_REG=0x0C150C15;*c.VI_H_START_REG=d.black?0:0x006C02EC;
        *c.VI_V_START_REG=0x2501FF;*c.VI_V_BURST_REG=0xE0204;*c.VI_X_SCALE_REG=0x200;*c.VI_Y_SCALE_REG=0x400;
        application_->state->lastScreenVI=RT64::VI{};
    }
    {netplay::experimental::performance::Scope timing(netplay::experimental::performance::Stage::Present);
     update_screen();}
    {
        netplay::experimental::performance::Scope upload_timing(netplay::experimental::performance::Stage::UploadWait);
        application_->drawDataUploader->wait();application_->transformsUploader->wait();application_->tilesUploader->wait();
        application_->state->framebufferRenderer->waitForUploaders();
    }
    owned_submissions_.push_back({application_->state->workloadId,application_->state->presentId,lease});
    // Restore core/state RAM BEFORE recycling this one mutable workspace.
    // The snapshot scope destructor restores again harmlessly on every exit.
    snapshot.restore();
    workspace.reset();
    if(!mailbox.finish_decode(lease))throw std::runtime_error("Owned decoder lease could not retire its CPU readers.");
    queued=true;
    const auto now=std::chrono::steady_clock::now();
    if(now-last_owned_report_>=std::chrono::seconds(5)) {
        // Routine reporting MUST NOT synchronize WSI/GPU workers. Exact target
        // inspection is an explicitly intrusive developer diagnostic only.
        static const bool inspect_targets=[] {
            const char* flag=std::getenv("DKR_ROLLBACK_INSPECT_RENDER_TARGET");
            return flag && flag[0]=='1' && !flag[1];
        }();
        reap_owned();
        auto& resources=*application_->sharedQueueResources;
        if(inspect_targets) {
        drain();
        std::scoped_lock manager_lock(resources.managerMutex);
        const auto& present=application_->presentQueue->presents[present_cursor];
        const auto* framebuffer=resources.framebufferManager.find(present.screenVI.fbAddress());
        unsigned writes=0;
        for(const auto& operation:present.fbOperations)
            writes+=operation.type==RT64::FramebufferOperation::Type::WriteChanges;
        if(framebuffer) {
            auto& target=resources.renderTargetManager.get(RT64::RenderTargetKey(
                framebuffer->addressStart,framebuffer->width,framebuffer->siz,RT64::Framebuffer::Type::Color),true);
            std::fprintf(stderr,"[rollback][render-target] native=%ux%u size=%u target=%ux%u scale=%.1f/%.1f cpu-writes=%u interpolation=%u vi-base=%08X gamma=%.3f\n",
                framebuffer->width,framebuffer->height,framebuffer->siz,target.width,target.height,
                float(target.resolutionScale.x),float(target.resolutionScale.y),writes,framebuffer->interpolationEnabled,
                present.screenVI.fbAddress(),present.screenVI.gamma());
        } else {
            std::fprintf(stderr,"[rollback][render-target] native-RAM fallback framebuffer=%08X cpu-writes=%u\n",d.framebuffer,writes);
        }
        }
        const auto stats=owned_registration_->statistics();
        const auto completed=application_->sharedQueueResources->totalPresentations.load(std::memory_order_relaxed);
        const auto interpolated=application_->sharedQueueResources->totalInterpolatedPresentations.load(std::memory_order_relaxed);
        std::fprintf(stderr,"[rollback][render] epoch=%llu frame=%u black=%u framebuffer=%08X dl=%08X..%08X presented=%llu tagged=%llu accepted=%llu retired=%llu\n",
            static_cast<unsigned long long>(lease->epoch()),d.frame,d.black,d.framebuffer,d.display_start,d.display_end,
            static_cast<unsigned long long>(completed),static_cast<unsigned long long>(stats.tagged),
            static_cast<unsigned long long>(stats.accepted),static_cast<unsigned long long>(stats.rejected));
        if(last_owned_report_.time_since_epoch().count()) {
            const auto seconds=std::chrono::duration<double>(now-last_owned_report_).count();
            std::fprintf(stderr,"[rollback][fps] seconds=%.3f presents=%llu interpolated=%llu fps=%.2f interpolation-fps=%.2f\n",
                seconds,static_cast<unsigned long long>(completed-last_owned_presentations_),
                static_cast<unsigned long long>(interpolated-last_owned_interpolated_),
                double(completed-last_owned_presentations_)/seconds,double(interpolated-last_owned_interpolated_)/seconds);
        }
        last_owned_presentations_=completed;last_owned_interpolated_=interpolated;
        netplay::experimental::performance::report();
        std::fprintf(stderr,"[rollback][draw-budget] events=%zu matrices=%zu shadows=%zu dl-bytes=%u in-flight=%zu diagnostic-drain=%u\n",
            lease->draw_events().size(),owned_matrices.size(),owned_shadows.size(),d.display_end-d.display_start,
            owned_submissions_.size(),unsigned(inspect_targets));
        if(netplay::experimental::performance::enabled()) {
            // Read bounded allocation counters, never inspect worker-owned
            // vectors or synchronize the GPU for routine profiling.
            const auto buffers=mailbox.buffer_statistics();
            std::fprintf(stderr,"[rollback][buffers] images=%zu decoders=%zu metadata=%zu image-reuses=%zu decode-reuses=%zu metadata-reuses=%zu ram-cap-bytes=%zu metadata-cap-bytes=%zu\n",
                buffers.image_allocations,buffers.decode_allocations,buffers.metadata_allocations,
                buffers.image_reuses,buffers.decode_reuses,buffers.metadata_reuses,
                netplay::experimental::PresentationMailbox::kPeakDecodePayloadBytes,
                netplay::experimental::PresentationMailbox::kPeakDrawEventBytes);
        }
        last_owned_report_=now;
    }
}
void dkr::runtime::RT64Renderer::repeat_owned() {
    if(!owned_mode_||!application_)return;
    reap_owned();
    // Let an in-flight real presentation draw the fresh overlay. Resubmitting
    // a paused workload would stall/reset interpolation and race its slot.
    if(!owned_submissions_.empty())return;
    const auto now=std::chrono::steady_clock::now();
    if(now-last_wait_presentation_<std::chrono::milliseconds(33))return;
    std::scoped_lock lock(presentation_mutex_);
    ui::draw(*application_);
    if(!present_count_&&application_->presentQueue->inspector) {
        // No guest workload or RAM is borrowed here. A hidden VI submits only
        // the host's clear/overlay; force a fresh UI present during admission.
        application_->state->lastScreenVI=RT64::VI{};
        application_->updateScreen();
        application_->presentQueue->waitForPresentId(application_->state->presentId);
        application_->presentQueue->waitForIdle();
    } else if(present_count_&&application_->presentQueue->inspector) {
        const bool paused=application_->state->debuggerInspector.paused;
        application_->state->debuggerInspector.paused=true;application_->updateScreen();application_->state->debuggerInspector.paused=paused;
    }
    last_wait_presentation_=now;
}
#endif

std::uint32_t dkr::runtime::RT64Renderer::get_display_framerate() const {
    if (application_ == nullptr || application_->presentQueue == nullptr ||
        application_->presentQueue->ext.sharedResources == nullptr) {
        return 60;
    }
    return application_->presentQueue->ext.sharedResources->swapChainRate;
}

float dkr::runtime::RT64Renderer::get_resolution_scale() const {
    if (application_ == nullptr) {
        return 1.0F;
    }
    if (application_->userConfig.resolution ==
        RT64::UserConfiguration::Resolution::Manual) {
        return static_cast<float>(application_->userConfig.resolutionMultiplier);
    }
    constexpr std::uint32_t kReferenceHeight = 240;
    const std::uint32_t height = application_->sharedQueueResources->swapChainHeight;
    return height > 0
        ? static_cast<float>(std::max((height + kReferenceHeight - 1U) / kReferenceHeight, 1U))
        : 1.0F;
}

std::unique_ptr<ultramodern::renderer::RendererContext>
dkr::runtime::CreateRT64Renderer(
    std::uint8_t* rdram,
    ultramodern::renderer::WindowHandle window_handle,
    bool developer_mode) {
    return std::make_unique<RT64Renderer>(rdram, window_handle, developer_mode);
}

void dkr::runtime::service_online_wait_presentation() {
    std::unique_lock active_lock(g_active_renderer_mutex, std::try_to_lock);
    if (!active_lock.owns_lock()) return;
    if (g_active_renderer != nullptr) {
        g_active_renderer->service_online_wait_presentation();
    }
}
