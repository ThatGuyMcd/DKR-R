#include "runtime_ui.hpp"
#include "runtime_portable.hpp"
#include "render_power_policy.hpp"
#include "performance_trace.hpp"
#include "mobile_graphics_preset.hpp"
#if defined(__ANDROID__)
#include "../android/android_platform.hpp"
#include "../android/mobile_ui.hpp"
#endif
#include "runtime_mipmap_modal.hpp"

#include "game_registration.hpp"
#include "generated/jumpman_font.h"
#include "generated/racing_banana_font.h"
#include "generated/selawik_font.h"
#include "countdown_tone_policy.hpp"
#include "custom_tracks.hpp"
#include "launcher_render_policy.hpp"
#include "launcher_frame_policy.hpp"
#include "launcher_performance.hpp"
#include "launcher_solid_geometry.hpp"
#include "launcher_panel_cache.hpp"
#include "magic_code_policy.hpp"
#include "modern_camera_policy.hpp"
#include "rom_revision.hpp"
#include "mods/online_game_pak.hpp"
#include "mods/online_preparation_notice.hpp"
#include "mods/legacy_mod_library.hpp"
#include "mods/legacy_mod_launch.hpp"
#include "mods/legacy_mod_browser.hpp"
#include "mods/online_mod_runtime.hpp"
#include "netplay/online_mod_sync_link.hpp"
#include "runtime_enhancements.hpp"
#include "runtime_hud_layout.hpp"
#include "hud_layout_editor.hpp"
#include "hud_basic_settings.hpp"
#include "runtime_audio_controls.hpp"
#include "runtime_crt_overlay.hpp"
#include "runtime_input.hpp"
#include "runtime_magic_codes.hpp"
#include "runtime_netplay.hpp"
#include "netplay/friend_service.hpp"
#include "netplay/netplay_build_identity.hpp"
#include "netplay/experimental_runtime_admission.hpp"
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
#include "netplay/experimental_runtime.hpp"
#include "netplay/experimental_performance.hpp"
#include "netplay/secure_channel.hpp"
#endif
#include "netplay/netplay_pacing_policy.hpp"
#include "runtime_platform.hpp"
#include "runtime_support.hpp"
#include "startup_performance.hpp"
#include "runtime_telemetry.hpp"
#include "runtime_texture_packs.hpp"
#include "save_manager.hpp"
#include "runtime_save_routing.hpp"
#include "texture_pack_browser_policy.hpp"
#include "ui_notification_policy.hpp"
#include "virtual_pak.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Unknwn.h>
#include <oaidl.h>
#endif

#include "gui/rt64_inspector.h"
#include "hle/rt64_application.h"
#include "hle/rt64_present_queue.h"
#include "render/rt64_shader_library.h"
#include "render/rt64_generated_mip_config.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_sdl2_custom.h"
#include "imgui/backends/imgui_impl_sdlrenderer2.h"
#include "nfd.h"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"
#include "stb/stb_image.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// A single bounded checker texture replaces the old per-frame, per-strip
// tessellation. It lives in ImGui's existing font atlas, so the launcher and
// RT64 overlay use the same cached artwork without another renderer resource.
// Leave one full checker period for the font atlas packer's padded border.
// The atlas itself is 2048 px wide; 1984 is 31 exact 64 px pattern periods and
// therefore packs without a separate renderer texture or a partial tile.
constexpr int kRaceButtonAtlasWidth = 2048;
constexpr int kRaceButtonPatternWidth = 1984;
constexpr int kRaceButtonPatternHeight = 64;
constexpr int kRaceButtonPatternCell = 32;
int g_race_button_pattern_rect = -1;
// Scopes that restyle the shared button: the paddock's flat buttons (MODS /
// HACKS and its modals) and the sidebar rail's softer hover.
int g_race_button_flat = 0;
ImFont* g_race_button_flat_font = nullptr;
int g_race_button_sidebar = 0;

}  // namespace

namespace ImGui {

// Racing's periods nearly touch and a trailing "..." reads as an underscore.
// `spread` em of tracking goes after each of its periods, less the last
// (.play-ellipsis in the launcher study), after a .04 em lead-in.
bool RaceLabelHasEllipsis(const char* begin, const char* end) {
    return end - begin >= 3 && std::memcmp(end - 3, "...", 3U) == 0;
}

float RaceLabelWidth(ImFont* font, float size, const char* begin,
                     const char* end, float spread) {
    const float plain = font->CalcTextSizeA(size, FLT_MAX, 0.0F, begin, end).x;
    if (spread <= 0.0F || !RaceLabelHasEllipsis(begin, end)) return plain;
    return plain + size * (0.04F + spread * 2.0F);
}

void DrawRaceLabel(ImDrawList* draw, ImFont* font, float size, ImVec2 at,
                   ImU32 colour, const char* begin, const char* end,
                   float spread) {
    if (spread <= 0.0F || !RaceLabelHasEllipsis(begin, end)) {
        draw->AddText(font, size, at, colour, begin, end);
        return;
    }
    const char* dots = end - 3;
    draw->AddText(font, size, at, colour, begin, dots);
    float x = at.x + font->CalcTextSizeA(size, FLT_MAX, 0.0F, begin, dots).x +
              size * 0.04F;
    const float advance = font->CalcTextSizeA(size, FLT_MAX, 0.0F, dots, dots + 1).x;
    for (int dot = 0; dot < 3; ++dot) {
        draw->AddText(font, size, {x, at.y}, colour, dots, dots + 1);
        x += advance + size * spread;
    }
}

// The checker, border and label plate of a hovered or focused race button.
// `amount` fades the checker and plate in; `plate_scale` grows the plate.
// The label uses `font` at `font_size` when given, else the current font.
void DrawRaceButtonHover(const char* label, ImVec2 minimum, ImVec2 maximum,
                         float amount, float plate_scale,
                         ImFont* font = nullptr, float font_size = 0.0F,
                         float ellipsis_spread = 0.0F) {
    if (amount <= 0.0F) return;
    ImDrawList* draw = GetWindowDrawList();
    const float width = maximum.x - minimum.x;
    const float height = maximum.y - minimum.y;
    const float rounding = std::clamp(GetStyle().FrameRounding, 0.0F,
                                      std::min(width, height) * 0.5F);
    const auto faded = [amount](int r, int g, int b, int a) {
        return IM_COL32(r, g, b, static_cast<int>(std::lround(a * amount)));
    };
    // Keep the racing motif broad and calm. A dense black/cream grid creates
    // high-frequency noise behind the label (especially on handheld panels),
    // while two or three rows remain recognisably checkered without competing
    // with the text.
    const float cell = std::clamp(height * 0.62F, 20.0F, 34.0F);
    const float time = static_cast<float>(GetTime());
    const float pattern_period = cell * 2.0F;
    const float travel = std::fmod(time * 16.0F, pattern_period);
    ImFontAtlas* atlas = GetIO().Fonts;
    ImFontAtlasCustomRect* pattern =
        g_race_button_pattern_rect >= 0
            ? atlas->GetCustomRectByIndex(g_race_button_pattern_rect)
            : nullptr;
    if (pattern != nullptr && pattern->IsPacked() && atlas->TexID != nullptr &&
        atlas->TexWidth > 0 && atlas->TexHeight > 0) {
        const float source_scale =
            static_cast<float>(kRaceButtonPatternCell) / cell;
        const float source_phase = std::fmod(
            static_cast<float>(kRaceButtonPatternCell * 2) -
                travel * source_scale,
            static_cast<float>(kRaceButtonPatternCell * 2));
        const float source_width = std::min(
            width * source_scale,
            std::max(static_cast<float>(pattern->Width) - source_phase, 1.0F));
        const float source_height = std::min(
            height * source_scale, static_cast<float>(pattern->Height));
        const ImVec2 uv_min{
            (static_cast<float>(pattern->X) + source_phase) /
                static_cast<float>(atlas->TexWidth),
            static_cast<float>(pattern->Y) /
                static_cast<float>(atlas->TexHeight)};
        const ImVec2 uv_max{
            (static_cast<float>(pattern->X) + source_phase + source_width) /
                static_cast<float>(atlas->TexWidth),
            (static_cast<float>(pattern->Y) + source_height) /
                static_cast<float>(atlas->TexHeight)};
        draw->AddImageRounded(atlas->TexID, minimum, maximum, uv_min, uv_max,
                              faded(255, 255, 255, 255), rounding);
    } else {
        // Preserve a readable hover state if a constrained backend cannot
        // pack the optional checker artwork into its atlas.
        draw->AddRectFilled(minimum, maximum, faded(226, 112, 25, 255),
                            rounding);
    }
    draw->AddRect(minimum, maximum, faded(255, 218, 99, 245),
                  rounding, 0, 1.5F);
    const char* rendered_end = FindRenderedTextEnd(label);
    if (font == nullptr) font = GetFont();
    if (font_size <= 0.0F) font_size = GetFontSize();
    const ImVec2 text_size{
        RaceLabelWidth(font, font_size, label, rendered_end, ellipsis_spread),
        font->CalcTextSizeA(font_size, FLT_MAX, 0.0F, label, rendered_end).y};
    const ImVec2 text_position{
        minimum.x + (width - text_size.x) * GetStyle().ButtonTextAlign.x,
        minimum.y + (height - text_size.y) * GetStyle().ButtonTextAlign.y};
    const float plate_padding_x = std::clamp(height * 0.22F, 8.0F, 15.0F);
    const float plate_padding_y = std::clamp(height * 0.09F, 3.0F, 6.0F);
    ImVec2 plate_min{text_position.x - plate_padding_x,
                     text_position.y - plate_padding_y};
    ImVec2 plate_max{text_position.x + text_size.x + plate_padding_x,
                     text_position.y + text_size.y + plate_padding_y};
    plate_min.x = std::max(plate_min.x, minimum.x + 5.0F);
    plate_max.x = std::min(plate_max.x, maximum.x - 5.0F);
    plate_min.y = std::max(plate_min.y, minimum.y + 4.0F);
    plate_max.y = std::min(plate_max.y, maximum.y - 4.0F);
    if (plate_scale < 1.0F) {
        const ImVec2 centre{(plate_min.x + plate_max.x) * 0.5F,
                            (plate_min.y + plate_max.y) * 0.5F};
        plate_min = {centre.x + (plate_min.x - centre.x) * plate_scale,
                     centre.y + (plate_min.y - centre.y) * plate_scale};
        plate_max = {centre.x + (plate_max.x - centre.x) * plate_scale,
                     centre.y + (plate_max.y - centre.y) * plate_scale};
    }
    const float plate_rounding = std::min((plate_max.y - plate_min.y) * 0.5F,
                                          9.0F);

    // Give the label its own stable contrast surface. This deliberately does
    // not share the checker animation, so readability is identical at every
    // animation phase and for both mouse hover and controller focus.
    draw->AddRectFilled(plate_min, plate_max, faded(7, 28, 39, 242),
                        plate_rounding);
    draw->AddRect(plate_min, plate_max, faded(255, 204, 75, 225),
                  plate_rounding, 0, 1.0F);
    draw->PushClipRect({minimum.x + 5.0F, minimum.y + 4.0F},
                       {maximum.x - 5.0F, maximum.y - 4.0F}, true);
    DrawRaceLabel(draw, font, font_size,
                  {text_position.x + 1.0F, text_position.y + 1.0F},
                  IM_COL32(0, 0, 0, 185), label, rendered_end, ellipsis_spread);
    DrawRaceLabel(draw, font, font_size, text_position,
                  IM_COL32(255, 249, 222, 255), label, rendered_end,
                  ellipsis_spread);
    draw->PopClipRect();
}

// The sidebar's rail buttons: the checker fades in (and snaps out), and the
// buttons sit on a small drop shadow and sink slightly while pressed.
bool DkrSidebarRaceButton(const char* label, const ImVec2& size) {
    ImDrawList* draw = GetWindowDrawList();
    const ImGuiStyle& style = GetStyle();
    const char* rendered_end = FindRenderedTextEnd(label);
    const ImVec2 label_size = CalcTextSize(label, rendered_end, true);
    const ImVec2 origin = GetCursorScreenPos();
    const ImVec2 item_size = CalcItemSize(
        size, label_size.x + style.FramePadding.x * 2.0F,
        label_size.y + style.FramePadding.y * 2.0F);
    const float rounding = std::clamp(style.FrameRounding, 0.0F,
                                      std::min(item_size.x, item_size.y) * 0.5F);
    const int first_vertex = draw->VtxBuffer.Size;
    draw->AddRectFilled({origin.x, origin.y + 2.0F},
                        {origin.x + item_size.x, origin.y + item_size.y + 2.0F},
                        GetColorU32(IM_COL32(0, 0, 0, 89)), rounding);
    const ImVec4 resting = GetStyleColorVec4(ImGuiCol_Button);
    const ImVec4 text = GetStyleColorVec4(ImGuiCol_Text);
    PushStyleColor(ImGuiCol_ButtonHovered, resting);
    PushStyleColor(ImGuiCol_ButtonActive, resting);
    PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    const bool pressed = Button(label, size);
    PopStyleColor(3);
    const ImGuiID id = GetItemID();
    const ImVec2 minimum = GetItemRectMin();
    const ImVec2 maximum = GetItemRectMax();
    const bool lit = IsItemHovered() || (IsItemFocused() && GetIO().NavVisible);
    const bool held = IsItemActive() && IsItemHovered();

    // Soft in, instant out, like the launcher study's CSS transitions.
    ImGuiStorage* storage = GetStateStorage();
    const float step = GetIO().DeltaTime;
    float& hover = *storage->GetFloatRef(ImHashStr("sidebar-hover", 0, id), 0.0F);
    hover = lit ? std::min(hover + step / 0.12F, 1.0F) : 0.0F;
    float& edge = *storage->GetFloatRef(ImHashStr("sidebar-edge", 0, id), 0.0F);
    edge = lit ? std::min(edge + step / 0.15F, 1.0F)
               : std::max(edge - step / 0.15F, 0.0F);
    float& sink = *storage->GetFloatRef(ImHashStr("sidebar-sink", 0, id), 0.0F);
    sink = held ? std::min(sink + step / 0.15F, 1.0F)
                : std::max(sink - step / 0.15F, 0.0F);
    const auto ease = [](float t) {
        return 1.0F - (1.0F - t) * (1.0F - t) * (1.0F - t);
    };

    const float inset = rounding * 0.6F;
    draw->AddRectFilled({minimum.x + inset, minimum.y + 1.0F},
                        {maximum.x - inset, minimum.y + 2.0F},
                        GetColorU32(IM_COL32(255, 255, 255, 36)));
    if (edge > 0.0F) {
        draw->AddRect(minimum, maximum,
                      GetColorU32(IM_COL32(
                          255, 218, 99,
                          static_cast<int>(245.0F * ease(edge)))),
                      rounding, 0, 1.0F);
    }
    const float faded = ease(hover);
    if (lit) {
        DrawRaceButtonHover(label, minimum, maximum, faded,
                            0.9F + 0.1F * faded);
    } else {
        const ImVec2 text_position{
            minimum.x + (maximum.x - minimum.x - label_size.x) *
                            style.ButtonTextAlign.x,
            minimum.y + (maximum.y - minimum.y - label_size.y) *
                            style.ButtonTextAlign.y};
        draw->PushClipRect(minimum, maximum, true);
        draw->AddText({text_position.x, text_position.y + 2.0F},
                      GetColorU32(IM_COL32(0, 0, 0, 56)), label, rendered_end);
        draw->AddText(text_position, GetColorU32(text), label, rendered_end);
        draw->PopClipRect();
    }
    const float scale = 1.0F - 0.04F * ease(sink);
    if (scale < 0.9999F) {
        const ImVec2 centre{(minimum.x + maximum.x) * 0.5F,
                            (minimum.y + maximum.y) * 0.5F};
        for (int index = first_vertex; index < draw->VtxBuffer.Size; ++index) {
            ImVec2& position = draw->VtxBuffer[index].pos;
            position.x = centre.x + (position.x - centre.x) * scale;
            position.y = centre.y + (position.y - centre.y) * scale;
        }
    }
    return pressed;
}

// Keep every launcher and in-game overlay button on the same racing-themed
// interaction primitive. The checker layer is painted after the ordinary
// ImGui button so existing sizing, navigation, disabled-state and activation
// behaviour remain untouched.
bool DkrRaceButton(const char* label, const ImVec2& size = ImVec2(0.0F, 0.0F)) {
    // A paddock scope has already pushed its calm, flat button colours.
    if (g_race_button_flat > 0) {
        if (g_race_button_flat_font != nullptr) PushFont(g_race_button_flat_font);
        const bool pressed = Button(label, size);
        if (g_race_button_flat_font != nullptr) PopFont();
        return pressed;
    }
    if (g_race_button_sidebar > 0) return DkrSidebarRaceButton(label, size);
    const bool pressed = Button(label, size);
    if (!IsItemHovered() && !IsItemFocused()) return pressed;

    const ImVec2 minimum = GetItemRectMin();
    const ImVec2 maximum = GetItemRectMax();
    if (maximum.x <= minimum.x || maximum.y <= minimum.y) return pressed;
    DrawRaceButtonHover(label, minimum, maximum, 1.0F, 1.0F);
    return pressed;
}

}  // namespace ImGui

// Route existing ImGui::Button calls through the shared primitive without
// changing call sites or widget IDs.
#define Button DkrRaceButton

namespace {

using ultramodern::renderer::Antialiasing;
using ultramodern::renderer::AspectRatio;
using ultramodern::renderer::GraphicsApi;
using ultramodern::renderer::GraphicsConfig;
using ultramodern::renderer::HighPrecisionFramebuffer;
using ultramodern::renderer::HUDRatioMode;
using ultramodern::renderer::Resolution;
using ultramodern::renderer::RefreshRate;
using ultramodern::renderer::WindowMode;

std::filesystem::path g_config_directory;
dkr::mods::ModLibrary g_legacy_imports;
dkr::mods::ModLaunch g_mod_launch;
std::unique_ptr<dkr::runtime::netplay::DirectModSyncLink> g_online_mod_link;
std::unique_ptr<dkr::mods::online::ModSync> g_online_mod_sync;
dkr::mods::online::SyncSettings g_online_mod_settings;
std::filesystem::path g_online_mod_selected_rom;
dkr::runtime::rom::Identity g_online_mod_identity;
struct OnlineHostRequest {
    dkr::runtime::netplay::Rules rules;
    dkr::runtime::netplay::CompatibilityManifest manifest;
    dkr::runtime::saves::OnlineSaveSeedMode save_mode;
    std::string room,name;
};
std::optional<OnlineHostRequest> g_online_mod_host_request;
bool g_online_mod_watching=false;
dkr::mods::online::PreparationNotice g_online_mod_notice;
std::string g_legacy_import_status;
std::atomic<bool> g_overlay_visible{false};
// This is a one-shot navigation request, not a redraw flag. The overlay is
// drawn every presented frame while it is visible. Setting this for ordinary
// SDL events forces ImGui back onto the selected sidebar button after every
// mouse click or gamepad direction, which makes the content panel impossible
// to operate once the RT64 inspector owns the in-game UI.
std::atomic<bool> g_overlay_focus_requested{false};
std::atomic<dkr::runtime::ui::LifecycleRequest> g_lifecycle_request{
    dkr::runtime::ui::LifecycleRequest::None};
std::mutex g_inspector_guard;
RT64::Inspector* g_inspector = nullptr;
std::atomic<int> g_overlay_page{0};
std::atomic<int> g_overlay_last_rendered_page{-1};
// Shoulder buttons exclusively own this ten-entry rail. Entries 0-7 switch
// pages immediately; Restart and Exit require an explicit A press.
std::atomic<int> g_overlay_sidebar_selection{0};
std::atomic<int> g_overlay_sidebar_action{0};
ImFont* g_font_heading = nullptr;
ImFont* g_font_title = nullptr;
ImFont* g_font_controls = nullptr;
ImFont* g_font_fps = nullptr;
std::uint64_t g_font_generation = 0;
std::array<int, 2> g_brand_logo_rects{{-1, -1}};
constexpr int kOnlineGuideVersion = 1;
int g_online_guide_acknowledged_version = 0;
bool g_online_guide_opened_this_run = false;
// The launcher has its own monotonic animation clock. Its renderer is retired
// before game handoff; the existing SDL window ownership is unchanged.
double g_launcher_animation_seconds = -1.0;

double UiAnimationSeconds() {
    return g_launcher_animation_seconds >= 0.0
        ? g_launcher_animation_seconds
        : ImGui::GetTime();
}
bool g_online_guide_do_not_show_again = true;
bool g_online_guide_reopen_requested = false;
#if defined(__ANDROID__)
// Conservative first-use defaults only. The settings loader restores all
// existing choices, and explicit presets below disclose the quality tradeoff.
RefreshRate g_modern_refresh_mode = RefreshRate::Manual;
#else
RefreshRate g_modern_refresh_mode = RefreshRate::Display;
#endif
int g_modern_refresh_target = 60;
#if defined(__ANDROID__)
Resolution g_modern_resolution = Resolution::Original2x;
#else
Resolution g_modern_resolution = Resolution::Auto;
#endif
AspectRatio g_modern_aspect = AspectRatio::Expand;
Antialiasing g_modern_antialiasing = Antialiasing::None;
#if defined(__ANDROID__)
HighPrecisionFramebuffer g_modern_high_precision_fb = HighPrecisionFramebuffer::Off;
#else
HighPrecisionFramebuffer g_modern_high_precision_fb = HighPrecisionFramebuffer::On;
#endif
GraphicsApi g_modern_graphics_api = GraphicsApi::Auto;
int g_modern_downsample = 1;
bool g_fps_overlay_enabled = false;
int g_fps_overlay_position = 1;
int g_fps_overlay_detail = 1;
bool g_fps_overlay_single_row = false;
bool g_fps_custom_frame_time = true;
bool g_fps_custom_simulation = true;
bool g_fps_custom_graphics = false;
bool g_fps_custom_vi = false;
bool g_fps_custom_interpolation = false;
bool g_fps_custom_audio = false;
bool g_fps_custom_target = true;
bool g_fps_custom_resolution = false;
ImVec4 g_fps_fill_colour{0.96F, 0.18F, 0.10F, 1.0F};
ImVec4 g_fps_outline_colour{1.0F, 0.58F, 0.04F, 1.0F};
int g_fps_font_size = 24;
bool g_crt_enabled = false;
int g_crt_filter_index = 0;
int g_crt_scale_mode = 0;
float g_crt_strength = 0.35F;
std::string g_crt_status;
std::string g_texture_pack_status;
struct TextureImportState {
    bool running = false;
    bool finished = false;
    bool succeeded = false;
    bool commit_started = false;
    bool modal_visible = false;
    float progress = 0.0F;
    std::string stage;
    std::string result;
};
std::mutex g_texture_import_mutex;
std::atomic<bool> g_texture_import_cancel_requested{false};
TextureImportState g_texture_import_state;
// Keep the worker last so it is stopped and joined before the state, cancel
// flag, and mutex it can still touch are destroyed during application exit.
std::jthread g_texture_import_worker;
char g_texture_pack_search[160]{};
int g_texture_pack_sort = static_cast<int>(dkr::runtime::texture_browser::SortMode::Priority);
int g_texture_pack_state_filter = 0;
int g_texture_pack_compatibility_filter = 0;
int g_texture_pack_type_filter = 0;
int g_texture_pack_visibility_filter = 0;
std::string g_texture_pack_manage_id;
std::string g_texture_pack_remove_id;
std::string g_texture_pack_remove_name;
// Set when an action in Track Lab (import, arm, play) switched the presentation
// profile to Modern on the player's behalf. Shown in Track Lab and Graphics,
// cleared when the player changes the profile there by hand.
std::string g_track_lab_modern_notice;
// A Track Lab "Manage" button asks the shared single-pack modal to open.
bool g_texture_pack_manage_request = false;
std::string g_track_import_status;
struct ModBrowserState {
    char search[160]{};
    int sort=0,state=0,compatibility=0,visibility=0,format=0;
    std::string source,manage_id,remove_id,hide_id;
    std::shared_ptr<const dkr::mods::TrackCatalogView> snapshot;
    std::vector<dkr::mods::browser::Card> all;
    // Shown cards: >= 0 indexes `all`, < 0 the native tracks (-1 is the first).
    std::vector<int> shown;
    std::string filter_key,layout_key;
    std::vector<float> row_heights;
};
std::array<ModBrowserState,2> g_mod_browsers;
unsigned g_mod_browser_revision=0;
int g_online_offline_section = 0;
int g_online_active_section = 0;
char g_online_player_name[25] = "Racer";
char g_online_room_name[49] = "DKR-R Grand Prix";
char g_online_invite[1024]{};
char g_online_code_entry[6]{};
char g_online_profile_name[25] = "Racer";
char g_friend_code_entry[128]{};
char g_friend_search[64]{};
char g_friend_search_edit[64]{};
char g_friend_nickname[25]{};
enum class TextEntryTarget {
    None,
    RacerName,
    LobbyName,
    OnlineProfileName,
    FriendNickname,
    TexturePackSearch,
    CustomTrackSearch,
    CustomCharacterSearch,
    HudPresetName,
};
char g_hud_preset_name[49]{};
TextEntryTarget g_text_entry_target = TextEntryTarget::None;
char g_text_entry_edit[160]{};
bool g_text_entry_keyboard_pending = false;
bool g_friend_code_keyboard_pending = false;
bool g_friend_search_keyboard_pending = false;
int g_friend_invite_lifetime = 0;
int g_friend_invite_minutes = 60;
int g_friend_filter = 0;
int g_friend_sort = 0;
bool g_friend_online_notifications = true;
int g_friend_online_notification_position = 2;
std::string g_friend_action_identity;
bool g_friend_remove_pending = false;
bool g_friend_block_pending = false;
std::map<std::string,
         dkr::runtime::ui_notifications::FriendOnlineTransitionGate>
    g_friend_presence_notifications;
dkr::runtime::ui_notifications::Queue g_online_notifications;
std::set<std::uint64_t> g_seen_friend_lobby_invites;
std::optional<dkr::runtime::netplay::FriendLobbyInviteView> g_joining_friend_invite;
bool g_open_host_friend_invites = false;
bool g_online_code_keyboard_pending = false;
std::atomic<bool> g_online_code_keyboard_visible{false};
std::atomic<bool> g_online_code_keyboard_cancel_requested{false};
// The frame the Online lobby panel drew the start countdown itself.
int g_online_countdown_panel_frame = -1;
int g_online_host_control = static_cast<int>(
    dkr::runtime::netplay::HostControlPolicy::GuidedUntilCharacterSelect);
int g_online_maximum_players = 2;
int g_online_synchronization = static_cast<int>(
    dkr::runtime::netplay::SynchronizationMode::Rollback);
bool g_online_experimental_rollback = false;
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
std::filesystem::path g_online_experimental_rom;
#if defined(DKR_EXPERIMENTAL_FULL_SCENES)
bool g_online_experimental_adventure = false;
#endif
#endif
dkr::runtime::netplay::SynchronizationMode CurrentOnlineSynchronization() {
    return dkr::runtime::netplay::experimental::selected_mode(
        g_online_synchronization, g_online_experimental_rollback);
}
int g_online_rollback_window = 10;
bool g_online_automatic_delay = true;
int g_online_manual_delay = 2;
bool g_online_record_replay = true;
int g_online_save_seed_mode = static_cast<int>(
    dkr::runtime::saves::OnlineSaveSeedMode::CopySinglePlayer);
int g_online_input_profile = 0;
std::string g_online_action_status;
std::string g_online_error_notification;
std::string g_last_online_error;
bool g_online_error_was_active = false;
std::chrono::steady_clock::time_point g_online_error_started{};
dkr::runtime::netplay::OnlineFailure g_online_failure_modal{};
std::string g_online_failure_modal_message;
bool g_online_failure_modal_requested = false;
bool g_online_failure_modal_active = false;
bool g_network_overlay_enabled = false;
int g_network_overlay_position = 0;
int g_network_overlay_detail = 1;
bool g_network_overlay_single_row = false;
bool g_controller_input_overlay_enabled = false;
int g_controller_input_overlay_position = 2;
ImVec2 g_fps_overlay_extent{};
ImVec2 g_network_overlay_extent{};
std::future<dkr::runtime::support::SystemSummary> g_support_summary_future;
std::optional<dkr::runtime::support::SystemSummary> g_support_summary;
bool g_support_summary_requested = false;
std::string g_support_action_status;
bool g_patch_notes_requested = false;

constexpr int kPagePlay = 0;
constexpr int kPageGraphics = 1;
constexpr int kPageSound = 2;
constexpr int kPageControls = 3;
constexpr int kPageSaveManager = 4;
constexpr int kPageOnlineMp = 5;
constexpr int kPageModsHacks = 6;
constexpr int kPageAbout = 7;
constexpr int kPageTextures = 8;
constexpr int kMenuPageCount = 9;
// Stable page IDs, explicit visual order for both pointer and controller use.
constexpr std::array<int,kMenuPageCount+2> kSidebarOrder{0,5,6,1,2,3,4,8,7,9,10};
int StepSidebarSelection(int current,int direction) {
    const auto found=std::find(kSidebarOrder.begin(),kSidebarOrder.end(),current);
    const int index=found==kSidebarOrder.end()?0:static_cast<int>(found-kSidebarOrder.begin());
    return kSidebarOrder[(index+direction+static_cast<int>(kSidebarOrder.size()))%kSidebarOrder.size()];
}

struct CrtFilterEntry {
    std::string label;
    std::filesystem::path path;
    bool built_in = false;
};

std::vector<CrtFilterEntry> g_crt_filters;

#ifndef DKR_RELEASE_VERSION
#define DKR_RELEASE_VERSION "development"
#endif
#ifndef DKR_NETWORK_RELEASE_VERSION
#define DKR_NETWORK_RELEASE_VERSION "development"
#endif
enum class CaptureDevice { None, Keyboard, Controller };
CaptureDevice g_capture_device = CaptureDevice::None;
int g_capture_action = -1;
bool g_capture_popup_pending = false;
bool g_capture_finished = false;
std::size_t g_selected_player = 0;
int g_controls_section = 0;
constexpr int kShortcutCaptureAction = -2;
constexpr int kAssignControllerCaptureAction = -3;
constexpr std::size_t kShortcutActionCount = static_cast<std::size_t>(
    dkr::runtime::input::ShortcutAction::Count);
constexpr std::array<const char*, kShortcutActionCount> kShortcutSettingNames{
    "quick_restart", "toggle_overlay", "toggle_texture_pack",
    "toggle_fullscreen", "recenter_gyro"};
bool g_capture_secondary_controller = false;
dkr::runtime::input::ShortcutAction g_capture_shortcut_action =
    dkr::runtime::input::ShortcutAction::QuickRestart;
std::array<int, 2> g_shortcut_capture_sources{
    dkr::runtime::input::kUnbound, dkr::runtime::input::kUnbound};
int g_shortcut_capture_count = 0;
std::chrono::steady_clock::time_point g_shortcut_capture_deadline{};
std::string g_save_manager_status;
bool g_play_offline_after_leave = false;
struct SaveManagerViewCache {
    bool valid = false;
    std::chrono::steady_clock::time_point next_refresh{};
    dkr::runtime::saves::SaveInfo adventure;
    std::array<dkr::runtime::saves::SaveInfo,
               dkr::runtime::saves::kControllerPakCount> controller_paks{};
    std::vector<std::filesystem::path> adventure_backups;
    std::string preview;
};
SaveManagerViewCache g_save_manager_view_cache;

void InvalidateSaveManagerViewCache() {
    g_save_manager_view_cache.valid = false;
}

const SaveManagerViewCache& CachedSaveManagerView() {
    const auto now = std::chrono::steady_clock::now();
    if (g_save_manager_view_cache.valid &&
        now < g_save_manager_view_cache.next_refresh) {
        return g_save_manager_view_cache;
    }
    g_save_manager_view_cache.adventure =
        dkr::runtime::saves::adventure_info();
    g_save_manager_view_cache.preview.clear();
    dkr::runtime::saves::codec::SaveImage summary;
    std::string summary_error;
    if (g_save_manager_view_cache.adventure.valid && dkr::runtime::saves::load_adventure(summary, summary_error)) {
        for (std::size_t slot = 0; slot < summary.slots.size(); ++slot) {
            if (slot) g_save_manager_view_cache.preview += " | ";
            const auto& saved = summary.slots[slot];
            g_save_manager_view_cache.preview += "Slot " + std::to_string(slot + 1) + ": " +
                (saved.name.empty() ? "Empty" : saved.name) + " (" + std::to_string(saved.balloons[0]) + " balloons)";
        }
    }
    for (int channel = 0;
         channel < dkr::runtime::saves::kControllerPakCount; ++channel) {
        g_save_manager_view_cache.controller_paks[
            static_cast<std::size_t>(channel)] =
            dkr::runtime::saves::controller_pak_info(channel);
    }
    g_save_manager_view_cache.adventure_backups =
        dkr::runtime::saves::adventure_backups();
    g_save_manager_view_cache.next_refresh =
        now + std::chrono::milliseconds{500};
    g_save_manager_view_cache.valid = true;
    return g_save_manager_view_cache;
}

std::string g_magic_codes_status;
bool g_online_compatibility_sync_requested = false;
std::string g_controller_mapping_status;
bool g_controller_mapping_popup_pending = false;
bool g_controller_mapping_completion_saved = false;
std::optional<dkr::runtime::saves::codec::SaveImage> g_save_builder_image;

bool HandleInputCaptureEvent(SDL_Event* event);

struct BrowserEntry {
    std::filesystem::path path;
    bool directory = false;
};

struct RomCatalogEntry {
    std::filesystem::path path;
    std::string label;
    std::string key;
};

struct RomBrowserState {
    bool open = false;
    bool close_requested = false;
    bool focus_first_entry = false;
    std::filesystem::path directory;
    std::vector<BrowserEntry> entries;
    std::string message;
};

RomBrowserState g_rom_browser;

constexpr ImVec4 kBackground{0.02F, 0.20F, 0.45F, 1.0F};
constexpr ImVec4 kPanel{0.025F, 0.20F, 0.43F, 1.0F};
constexpr ImVec4 kPanelSoft{0.05F, 0.34F, 0.62F, 1.0F};
constexpr ImVec4 kText{1.0F, 0.965F, 0.855F, 1.0F};
constexpr ImVec4 kMuted{0.69F, 0.79F, 0.80F, 1.0F};
constexpr ImVec4 kAccent{0.10F, 0.76F, 0.64F, 1.0F};
constexpr ImVec4 kWarm{1.0F, 0.67F, 0.08F, 1.0F};
constexpr ImVec4 kRaceRed{0.91F, 0.18F, 0.13F, 1.0F};
constexpr ImVec4 kRaceBlue{0.04F, 0.43F, 0.63F, 1.0F};
constexpr ImVec4 kCream{1.0F, 0.94F, 0.76F, 1.0F};
// One visual shell for startup and the in-game menu. The backdrop remains
// surface-specific; panel readability and borders must not drift apart.
constexpr ImVec4 kMenuNavSurface{0.025F, 0.105F, 0.15F, 0.88F};
constexpr ImVec4 kMenuNavBorder{1.0F, 0.67F, 0.08F, 0.92F};
constexpr ImVec4 kMenuContentSurface{0.035F, 0.085F, 0.12F, 0.96F};
constexpr ImVec4 kMenuContentBorder{0.12F, 0.62F, 0.58F, 0.88F};

#include "runtime_ui_paddock.inl"

// A page asks the launcher or overlay to show another page (-1: none).
int g_page_navigation_request = -1;

std::string PathUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

std::filesystem::path RuntimeAssetPath(const std::filesystem::path& relative) {
    if (char* base = SDL_GetBasePath(); base != nullptr) {
        const std::filesystem::path candidate =
            std::filesystem::path(base) / relative;
        SDL_free(base);
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    const std::filesystem::path candidate =
        std::filesystem::current_path() / relative;
    std::error_code error;
    return std::filesystem::is_regular_file(candidate, error)
        ? candidate : std::filesystem::path{};
}

std::array<std::filesystem::path, 2> BrandLogoPaths() {
    return {
        RuntimeAssetPath("assets/ui/Icons/DKR-R-Spinning-Icon.png"),
        RuntimeAssetPath("assets/ui/Icons/DKR-R-Short-Logo.png"),
    };
}

std::filesystem::path LauncherBackgroundPath() {
    return RuntimeAssetPath(
        "assets/ui/Backgrounds/DKR-R-Launcher-Background.png");
}

struct LauncherBackgroundTexture {
    SDL_Texture* texture = nullptr;
    SDL_Texture* mirrored = nullptr;
    SDL_Surface* source = nullptr;
    std::array<SDL_Texture*, 2> scaled{};
    int attempted_width = 0;
    int attempted_height = 0;
    std::uint64_t cache_rebuilds = 0;
    std::uint64_t tiles_submitted = 0;
    int width = 0;
    int height = 0;
};

SDL_Texture* MirroredBackgroundTexture(SDL_Renderer* renderer, SDL_Surface* source) {
    SDL_Surface* mirror = SDL_CreateRGBSurfaceWithFormat(
        0, source->w, source->h, 32, SDL_PIXELFORMAT_RGBA32);
    if (mirror == nullptr) return nullptr;
    for (int y = 0; y < source->h; ++y) {
        const auto* input = static_cast<const Uint32*>(source->pixels) +
            y * (source->pitch / 4);
        auto* output = static_cast<Uint32*>(mirror->pixels) + y * (mirror->pitch / 4);
        std::reverse_copy(input, input + source->w, output);
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, mirror);
    if (texture != nullptr) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    SDL_FreeSurface(mirror);
    return texture;
}

void ReleaseLauncherBackground(LauncherBackgroundTexture& background) {
    for (SDL_Texture* texture : background.scaled) {
        if (texture != nullptr) SDL_DestroyTexture(texture);
    }
    if (background.mirrored != nullptr) SDL_DestroyTexture(background.mirrored);
    if (background.texture != nullptr) SDL_DestroyTexture(background.texture);
    if (background.source != nullptr) SDL_FreeSurface(background.source);
    background = {};
}

void PrepareLauncherBackground(LauncherBackgroundTexture& background,
                               SDL_Renderer* renderer, int width, int height) {
    if (background.source == nullptr || width <= 0 || height <= 0 ||
        (width == background.attempted_width && height == background.attempted_height)) return;
    background.attempted_width = width;
    background.attempted_height = height;
    // Bound memory on huge/docked displays. The original-size positive-UV
    // textures remain a safe fallback; never retry a failed allocation per frame.
    for (SDL_Texture*& texture : background.scaled) {
        if (texture != nullptr) SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    if (static_cast<std::uint64_t>(width) * height > 16U * 1024U * 1024U) return;
    SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(
        0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (scaled == nullptr) return;
    if (SDL_BlitScaled(background.source, nullptr, scaled, nullptr) == 0) {
        background.scaled[0] = SDL_CreateTextureFromSurface(renderer, scaled);
        background.scaled[1] = MirroredBackgroundTexture(renderer, scaled);
        if (background.scaled[0] != nullptr && background.scaled[1] != nullptr) {
            SDL_SetTextureBlendMode(background.scaled[0], SDL_BLENDMODE_NONE);
            ++background.cache_rebuilds;
        } else {
            for (SDL_Texture*& texture : background.scaled) {
                if (texture != nullptr) SDL_DestroyTexture(texture);
                texture = nullptr;
            }
        }
    }
    SDL_FreeSurface(scaled);
}

LauncherBackgroundTexture LoadLauncherBackground(SDL_Renderer* renderer,
                                                  bool software_return = false) {
    LauncherBackgroundTexture result{};
    if (renderer == nullptr) return result;

    const std::filesystem::path path = LauncherBackgroundPath();
    if (path.empty()) {
        std::fprintf(stderr,
                     "[boot][ui] DKR-R launcher background was not found beside the runtime\n");
        return result;
    }

    int channels = 0;
    stbi_uc* pixels = stbi_load(PathUtf8(path).c_str(), &result.width,
                                &result.height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr || result.width <= 0 || result.height <= 0) {
        std::fprintf(stderr,
                     "[boot][ui] launcher background could not be loaded: %s\n",
                     stbi_failure_reason() != nullptr ? stbi_failure_reason()
                                                      : "unknown image error");
        if (pixels != nullptr) stbi_image_free(pixels);
        result.width = 0;
        result.height = 0;
        return result;
    }

    result.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STATIC, result.width,
                                       result.height);
    if (result.texture == nullptr ||
        SDL_UpdateTexture(result.texture, nullptr, pixels,
                          result.width * STBI_rgb_alpha) != 0) {
        std::fprintf(stderr,
                     "[boot][ui] launcher background texture creation failed: %s\n",
                     SDL_GetError());
        if (result.texture != nullptr) SDL_DestroyTexture(result.texture);
        result.texture = nullptr;
        result.width = 0;
        result.height = 0;
    } else {
        SDL_SetTextureBlendMode(result.texture, SDL_BLENDMODE_NONE);
#if !defined(__linux__)
        if (software_return) {
#endif
        result.source = SDL_CreateRGBSurfaceWithFormat(
            0, result.width, result.height, 32, SDL_PIXELFORMAT_RGBA32);
        if (result.source != nullptr) {
            for (int y = 0; y < result.height; ++y) {
                std::memcpy(static_cast<Uint8*>(result.source->pixels) + y * result.source->pitch,
                            pixels + static_cast<std::size_t>(y) * result.width * 4U,
                            static_cast<std::size_t>(result.width) * 4U);
            }
            SDL_SetSurfaceBlendMode(result.source, SDL_BLENDMODE_NONE);
            result.mirrored = MirroredBackgroundTexture(renderer, result.source);
        }
#if !defined(__linux__)
        }
#endif
    }
    stbi_image_free(pixels);
    return result;
}

void RefreshCrtFilters() {
    static constexpr std::array<std::pair<const char*, const char*>, 6>
        built_ins{{
            {"Soft scanlines", "scanlines-soft.png"},
            {"Shadow mask", "shadow-mask.png"},
            {"Aperture grille", "aperture-grille.png"},
            {"Perfect CRT 240p Bright", "Perfect_CRT-240p-BRT.png"},
            {"Perfect CRT 240p", "Perfect_CRT-240p.png"},
            {"Perfect CRT", "Perfect_CRT.png"},
        }};
    g_crt_filters.clear();
    for (const auto& [label, file] : built_ins) {
        g_crt_filters.push_back(
            {label, RuntimeAssetPath(std::filesystem::path("assets/filters") / file),
             true});
    }

    const std::filesystem::path custom_directory =
        g_config_directory / "filters";
    std::error_code error;
    std::filesystem::create_directories(custom_directory, error);
    std::vector<std::filesystem::path> custom_paths;
    if (!error) {
        for (const auto& entry :
             std::filesystem::directory_iterator(custom_directory, error)) {
            if (error) break;
            if (!entry.is_regular_file(error)) continue;
            std::string extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                           });
            if (extension == ".png") custom_paths.push_back(entry.path());
        }
    }
    std::sort(custom_paths.begin(), custom_paths.end());
    for (const auto& path : custom_paths) {
        g_crt_filters.push_back({path.stem().string() + " (Custom)", path, false});
    }
    if (g_crt_filters.empty()) {
        g_crt_filter_index = 0;
    } else {
        g_crt_filter_index = std::clamp(
            g_crt_filter_index, 0, static_cast<int>(g_crt_filters.size()) - 1);
    }
}

void LoadBrandLogoIntoAtlas() {
    g_brand_logo_rects = {{-1, -1}};
    g_race_button_pattern_rect = -1;
    struct LoadedLogo {
        stbi_uc* pixels = nullptr;
        int width = 0;
        int height = 0;
        int rect_index = -1;
    };
    std::array<LoadedLogo, 2> logos{};
    const auto paths = BrandLogoPaths();
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->TexDesiredWidth = std::max(atlas->TexDesiredWidth,
                                      kRaceButtonAtlasWidth);
    for (std::size_t index = 0; index < logos.size(); ++index) {
        if (paths[index].empty()) continue;
        int channels = 0;
        logos[index].pixels = stbi_load(PathUtf8(paths[index]).c_str(),
                                        &logos[index].width,
                                        &logos[index].height, &channels,
                                        STBI_rgb_alpha);
        if (logos[index].pixels == nullptr || logos[index].width <= 0 ||
            logos[index].height <= 0) {
            std::fprintf(stderr,
                         "[boot][ui] launcher logo face %zu could not be loaded: %s\n",
                         index,
                         stbi_failure_reason() != nullptr
                             ? stbi_failure_reason()
                             : "unknown image error");
            if (logos[index].pixels != nullptr) {
                stbi_image_free(logos[index].pixels);
                logos[index].pixels = nullptr;
            }
            continue;
        }
        logos[index].rect_index = atlas->AddCustomRectRegular(
            logos[index].width, logos[index].height);
    }
    const int pattern_rect_index = atlas->AddCustomRectRegular(
        kRaceButtonPatternWidth, kRaceButtonPatternHeight);

    unsigned char* pixels = nullptr;
    int atlas_width = 0;
    int atlas_height = 0;
    atlas->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);
    for (std::size_t index = 0; index < logos.size(); ++index) {
        LoadedLogo& logo = logos[index];
        ImFontAtlasCustomRect* rect = logo.rect_index >= 0
            ? atlas->GetCustomRectByIndex(logo.rect_index)
            : nullptr;
        const bool fits = pixels != nullptr && logo.pixels != nullptr &&
            rect != nullptr && rect->IsPacked() &&
            rect->X + rect->Width <= atlas_width &&
            rect->Y + rect->Height <= atlas_height;
        if (!fits) {
            if (logo.pixels != nullptr) {
                std::fprintf(stderr,
                             "[boot][ui] launcher logo face %zu would not fit the font atlas\n",
                             index);
            }
        } else {
            for (int row = 0; row < logo.height; ++row) {
                const auto* source_row = logo.pixels +
                    static_cast<std::size_t>(row) * logo.width * 4U;
                auto* destination_row = pixels +
                    (static_cast<std::size_t>(rect->Y + row) * atlas_width +
                     rect->X) * 4U;
                std::memcpy(destination_row, source_row,
                            static_cast<std::size_t>(logo.width) * 4U);
            }
            g_brand_logo_rects[index] = logo.rect_index;
        }
        if (logo.pixels != nullptr) stbi_image_free(logo.pixels);
    }
    ImFontAtlasCustomRect* pattern = pattern_rect_index >= 0
        ? atlas->GetCustomRectByIndex(pattern_rect_index)
        : nullptr;
    const bool pattern_fits = pixels != nullptr && pattern != nullptr &&
        pattern->IsPacked() && pattern->X + pattern->Width <= atlas_width &&
        pattern->Y + pattern->Height <= atlas_height;
    if (pattern_fits) {
        constexpr std::array<unsigned char, 4> kCheckerDark{{176, 67, 22, 255}};
        constexpr std::array<unsigned char, 4> kCheckerLight{{244, 145, 31, 255}};
        for (int row = 0; row < pattern->Height; ++row) {
            for (int column = 0; column < pattern->Width; ++column) {
                const bool dark =
                    (((column / kRaceButtonPatternCell) +
                      (row / kRaceButtonPatternCell)) & 1) != 0;
                const auto& colour = dark ? kCheckerDark : kCheckerLight;
                auto* destination = pixels +
                    (static_cast<std::size_t>(pattern->Y + row) * atlas_width +
                     pattern->X + column) * 4U;
                std::memcpy(destination, colour.data(), colour.size());
            }
        }
        g_race_button_pattern_rect = pattern_rect_index;
    } else {
        std::fprintf(stderr,
                     "[boot][ui] racing button pattern would not fit the font atlas\n");
    }
    atlas->TexPixelsUseColors = true;
}

std::filesystem::path SettingsPath() {
    return g_config_directory / "dkr-port-settings.ini";
}

std::filesystem::path LastRomPath() {
    return g_config_directory / "last-rom.txt";
}

std::filesystem::path RomCatalogPath() {
    return g_config_directory / "rom-catalog.txt";
}

bool ReplaceSettingsFile(const std::filesystem::path& temporary,
                         const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(temporary.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

void ApplyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0F;
    style.ChildRounding = 18.0F;
    style.FrameRounding = 12.0F;
    style.PopupRounding = 16.0F;
    style.ScrollbarRounding = 12.0F;
    style.GrabRounding = 12.0F;
    style.WindowPadding = {0.0F, 0.0F};
    style.FramePadding = {16.0F, 11.0F};
    style.ItemSpacing = {12.0F, 13.0F};
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 2.0F;
    style.FrameBorderSize = 1.0F;
    style.Colors[ImGuiCol_WindowBg] = kBackground;
    style.Colors[ImGuiCol_ChildBg] = kPanel;
    style.Colors[ImGuiCol_Border] = {0.16F, 0.42F, 0.44F, 1.0F};
    style.Colors[ImGuiCol_Text] = kText;
    style.Colors[ImGuiCol_TextDisabled] = kMuted;
    style.Colors[ImGuiCol_FrameBg] = kPanelSoft;
    style.Colors[ImGuiCol_FrameBgHovered] = {0.09F, 0.28F, 0.33F, 1.0F};
    style.Colors[ImGuiCol_FrameBgActive] = {0.10F, 0.36F, 0.38F, 1.0F};
    style.Colors[ImGuiCol_Button] = kRaceBlue;
    style.Colors[ImGuiCol_ButtonHovered] = {0.98F, 0.43F, 0.08F, 1.0F};
    style.Colors[ImGuiCol_ButtonActive] = kRaceRed;
    style.Colors[ImGuiCol_CheckMark] = kAccent;
    style.Colors[ImGuiCol_SliderGrab] = kWarm;
    style.Colors[ImGuiCol_SliderGrabActive] = kRaceRed;
    style.Colors[ImGuiCol_Header] = {0.04F, 0.38F, 0.43F, 1.0F};
    style.Colors[ImGuiCol_HeaderHovered] = {0.98F, 0.43F, 0.08F, 1.0F};
    style.Colors[ImGuiCol_HeaderActive] = kRaceRed;
    style.Colors[ImGuiCol_Separator] = {0.95F, 0.55F, 0.08F, 0.75F};
    // Controller navigation must read as a deliberate selection, even over
    // the blue half of the racing backdrop. Warm yellow is shared with the
    // start lights and remains distinct from every card and button colour.
    style.Colors[ImGuiCol_NavHighlight] = {1.0F, 0.82F, 0.12F, 1.0F};
}

void LoadLauncherFonts() {
    ++g_font_generation;
    ImGuiIO& io = ImGui::GetIO();
    static constexpr ImWchar kLauncherGlyphRanges[] = {
        0x0020, 0x00FF, // Basic Latin and Latin-1 Supplement.
        0,
    };
    static constexpr ImWchar kLauncherNonDigitGlyphRanges[] = {
        0x0020, 0x002F,
        0x003A, 0x00FF,
        0,
    };
    static constexpr ImWchar kJumpmanDigitGlyphRanges[] = {
        0x0030, 0x0039,
        0,
    };

    const auto merge_fallback = [&](float size, const ImWchar* ranges) {
        ImFontConfig fallback_config{};
        fallback_config.MergeMode = true;
        fallback_config.PixelSnapH = true;
        fallback_config.OversampleH = 1;
        fallback_config.OversampleV = 1;
        fallback_config.SizePixels = size;
        fallback_config.RasterizerMultiply = 1.35F;
        fallback_config.GlyphRanges = ranges;
        io.Fonts->AddFontDefault(&fallback_config);
    };
    const auto merge_jumpman_digits = [&](float size) -> bool {
        // Jumpman's glyphs occupy substantially less of their em square than
        // Racing Banana. Merge them from a larger source size so digits have
        // the same visual cap height as the surrounding launcher text.
        constexpr float kJumpmanVisualScale = 1.42F;
        ImFontConfig config{};
        config.FontDataOwnedByAtlas = false;
        config.MergeMode = true;
        config.PixelSnapH = true;
        config.OversampleH = 2;
        config.OversampleV = 2;
        config.GlyphRanges = kJumpmanDigitGlyphRanges;
        return io.Fonts->AddFontFromMemoryTTF(
                   const_cast<unsigned char*>(dkr_jumpman_font),
                   static_cast<int>(dkr_jumpman_font_size),
                   size * kJumpmanVisualScale, &config,
                   kJumpmanDigitGlyphRanges) != nullptr;
    };
    const auto add_racing_font = [&](float size) -> ImFont* {
        ImFontConfig config{};
        config.FontDataOwnedByAtlas = false;
        config.OversampleH = 2;
        config.OversampleV = 2;
        config.GlyphRanges = kLauncherNonDigitGlyphRanges;
        ImFont* font = io.Fonts->AddFontFromMemoryTTF(
            const_cast<char*>(dkr_racing_banana_font),
            static_cast<int>(dkr_racing_banana_font_size), size, &config,
            kLauncherNonDigitGlyphRanges);
        if (font != nullptr) {
            if (!merge_jumpman_digits(size)) {
                merge_fallback(size, kJumpmanDigitGlyphRanges);
            }
            merge_fallback(size, kLauncherNonDigitGlyphRanges);
        }
        return font;
    };

    // Controls retain the highly legible default face for words and symbols,
    // while Jumpman owns every digit. Building the base without 0-9 is
    // important because ImGui deliberately refuses to overwrite an existing
    // glyph during a MergeMode font addition.
    ImFontConfig control_config{};
    control_config.SizePixels = 21.0F;
    control_config.OversampleH = 2;
    control_config.OversampleV = 2;
    control_config.PixelSnapH = true;
    control_config.RasterizerMultiply = 1.20F;
    control_config.GlyphRanges = kLauncherNonDigitGlyphRanges;
    g_font_controls = io.Fonts->AddFontDefault(&control_config);
    if (g_font_controls != nullptr && !merge_jumpman_digits(21.0F)) {
        merge_fallback(21.0F, kJumpmanDigitGlyphRanges);
    }

    ImFont* body_font = add_racing_font(19.0F);
    if (body_font == nullptr) {
        ImFontConfig fallback_config{};
        fallback_config.SizePixels = 19.0F;
        fallback_config.GlyphRanges = kLauncherNonDigitGlyphRanges;
        body_font = io.Fonts->AddFontDefault(&fallback_config);
        if (body_font != nullptr && !merge_jumpman_digits(19.0F)) {
            merge_fallback(19.0F, kJumpmanDigitGlyphRanges);
        }
        std::fprintf(stderr,
                     "[boot][ui] Racing Banana body font could not be loaded; using fallback\n");
    }
    io.FontDefault = body_font;
    if (g_font_controls == nullptr) {
        g_font_controls = body_font;
    }
    for (std::size_t index = 0U; index < kPaddockSignSizes.size(); ++index) {
        g_paddock_sign[index] = add_racing_font(kPaddockSignSizes[index]);
    }
    LoadPaddockReadingFonts(io.Fonts);
    LoadPaddockMonoFonts(io.Fonts);

    const auto add_jumpman_font = [&](float size) -> ImFont* {
        ImFontConfig config{};
        config.FontDataOwnedByAtlas = false;
        config.OversampleH = 2;
        config.OversampleV = 2;
        config.GlyphRanges = kLauncherGlyphRanges;
        ImFont* font = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(dkr_jumpman_font),
            static_cast<int>(dkr_jumpman_font_size), size, &config,
            kLauncherGlyphRanges);
        if (font != nullptr) {
            merge_fallback(size, kLauncherGlyphRanges);
        }
        return font;
    };
    g_font_heading = add_jumpman_font(58.0F);
    g_font_title = add_jumpman_font(68.0F);
    g_font_fps = add_jumpman_font(24.0F);
    if (g_font_heading == nullptr || g_font_title == nullptr) {
        std::fprintf(stderr,
                     "[boot][ui] Jumpman heading font could not be loaded; using body font\n");
        g_font_heading = body_font;
        g_font_title = body_font;
    }
    if (g_font_fps == nullptr) {
        g_font_fps = g_font_controls != nullptr ? g_font_controls : body_font;
    }
    LoadBrandLogoIntoAtlas();
#if defined(__ANDROID__)
    dkr::runtime::mobile::install_context();
#endif
}

class ControlFontScope {
public:
    explicit ControlFontScope(bool pad_popup = false)
        : font_pushed_(g_font_controls != nullptr),
          padding_pushed_(pad_popup) {
        if (font_pushed_) {
            ImGui::PushFont(g_font_controls);
        }
        if (padding_pushed_) {
            // WindowPadding is sampled when BeginCombo creates its popup; it
            // does not disturb the already-open launcher card. This leaves a
            // comfortable cap above the first row and below the last row.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                ImVec2(16.0F, 12.0F));
        }
    }

    ~ControlFontScope() {
        if (padding_pushed_) {
            ImGui::PopStyleVar();
        }
        if (font_pushed_) {
            ImGui::PopFont();
        }
    }

    ControlFontScope(const ControlFontScope&) = delete;
    ControlFontScope& operator=(const ControlFontScope&) = delete;

private:
    bool font_pushed_ = false;
    bool padding_pushed_ = false;
};

bool ControlCombo(const char* label, int* current_item,
                  const char* items_separated_by_zeros,
                  int popup_max_height_in_items = -1) {
    const ControlFontScope scope(true);
    return ImGui::Combo(label, current_item, items_separated_by_zeros,
                        popup_max_height_in_items);
}

bool ControlCombo(const char* label, int* current_item,
                  bool (*items_getter)(void*, int, const char**), void* data,
                  int items_count, int popup_max_height_in_items = -1) {
    const ControlFontScope scope(true);
    return ImGui::Combo(label, current_item, items_getter, data, items_count,
                        popup_max_height_in_items);
}

bool ControlSliderInt(const char* label, int* value, int minimum, int maximum,
                      const char* format = "%d",
                      ImGuiSliderFlags flags = ImGuiSliderFlags_None) {
    const ControlFontScope scope;
    return ImGui::SliderInt(label, value, minimum, maximum, format, flags);
}

void PushHeadingFont(bool title = false) {
    ImFont* font = title ? g_font_title : g_font_heading;
    if (font != nullptr) {
        ImGui::PushFont(font);
    }
}

void PopHeadingFont(bool title = false) {
    if ((title ? g_font_title : g_font_heading) != nullptr) {
        ImGui::PopFont();
    }
}

float TrackedTextWidth(ImFont* font, float size, const char* text,
                       float tracking) {
    if (font == nullptr || text == nullptr || *text == '\0') return 0.0F;
    const float scale = size / std::max(font->FontSize, 1.0F);
    float width = 0.0F;
    const std::size_t length = std::strlen(text);
    for (std::size_t index = 0; index < length; ++index) {
        const auto character = static_cast<unsigned char>(text[index]);
        const ImFontGlyph* glyph = font->FindGlyph(character);
        if (glyph != nullptr) width += glyph->AdvanceX * scale;
        if (index + 1U < length) width += tracking;
    }
    return width;
}

void AddTrackedText(ImDrawList* draw, ImFont* font, float size,
                    ImVec2 position, ImU32 colour, const char* text,
                    float tracking) {
    if (draw == nullptr || font == nullptr || text == nullptr) return;
    const float scale = size / std::max(font->FontSize, 1.0F);
    const std::size_t length = std::strlen(text);
    for (std::size_t index = 0; index < length; ++index) {
        const char glyph_text[2]{text[index], '\0'};
        draw->AddText(font, size, position, colour, glyph_text,
                      glyph_text + 1);
        const ImFontGlyph* glyph = font->FindGlyph(
            static_cast<unsigned char>(text[index]));
        position.x += glyph != nullptr ? glyph->AdvanceX * scale : 0.0F;
        if (index + 1U < length) position.x += tracking;
    }
}

void DrawPageHeading(const char* text, bool title = false,
                     float size_override = 0.0F) {
    ImFont* font = title ? g_font_title : g_font_heading;
    if (font == nullptr || text == nullptr) {
        ImGui::TextUnformatted(text != nullptr ? text : "");
        return;
    }
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float available_width =
        std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    float size = size_override > 0.0F ? size_override : font->FontSize;
#if defined(__ANDROID__)
    size = std::min(size, 36.0F);
#endif
    float tracking = size * (title ? 0.03F : 0.04F);
    float width = TrackedTextWidth(font, size, text, tracking);
    if (width > available_width) {
        const float fit = std::clamp(available_width / width, 0.62F, 1.0F);
        size *= fit;
        tracking *= fit;
        width = TrackedTextWidth(font, size, text, tracking);
    }
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0F, text);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float outline = std::clamp(size * 0.055F, 2.0F, 4.0F);
    const ImU32 orange = ImGui::ColorConvertFloat4ToU32(kWarm);
    const ImU32 red = ImGui::ColorConvertFloat4ToU32(kRaceRed);
    constexpr std::array<ImVec2, 8> directions{{
        {-1.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, -1.0F}, {0.0F, 1.0F},
        {-0.72F, -0.72F}, {0.72F, -0.72F},
        {-0.72F, 0.72F}, {0.72F, 0.72F},
    }};
    for (const ImVec2 direction : directions) {
        AddTrackedText(draw, font, size,
            {position.x + direction.x * outline,
             position.y + direction.y * outline},
            orange, text, tracking);
    }
    AddTrackedText(draw, font, size, position, red, text, tracking);
    ImGui::Dummy({std::min(width, available_width), extent.y + outline + 2.0F});
}

void RememberModernGraphics(const GraphicsConfig& config) {
    g_modern_resolution = config.res_option;
    g_modern_aspect = config.ar_option == AspectRatio::Original
        ? AspectRatio::Original
        : AspectRatio::Expand;
    g_modern_antialiasing = config.msaa_option;
    g_modern_high_precision_fb = config.hpfb_option;
    g_modern_graphics_api = config.api_option;
    g_modern_downsample = std::clamp(config.ds_option, 1, 8);
    if (config.rr_option == RefreshRate::Display ||
        config.rr_option == RefreshRate::Manual) {
        g_modern_refresh_mode = config.rr_option;
        g_modern_refresh_target =
            dkr::runtime::enhancements::clamp_presentation_rate(
                config.rr_manual_value);
    }
}

void ApplyProfileGraphics(GraphicsConfig& config,
                          dkr::runtime::enhancements::PresentationProfile profile) {
    if (profile == dkr::runtime::enhancements::PresentationProfile::Modern) {
        config.res_option = g_modern_resolution;
        config.ar_option = g_modern_aspect;
        config.msaa_option = g_modern_antialiasing;
        config.hpfb_option = g_modern_high_precision_fb;
        config.api_option = g_modern_graphics_api;
        config.hr_option = HUDRatioMode::Original;
        config.ds_option = g_modern_downsample;
        config.rr_option = g_modern_refresh_mode;
        config.rr_manual_value = g_modern_refresh_target;
        return;
    }

    // Accurate is enforced again inside the renderer. These launcher values
    // make the effective state explicit and ensure stale Modern preferences do
    // not leak into a pre-launch configuration snapshot.
    config.res_option = Resolution::Auto;
    config.ar_option = AspectRatio::Original;
    config.msaa_option = Antialiasing::None;
    config.hpfb_option = HighPrecisionFramebuffer::Auto;
    config.api_option = GraphicsApi::Auto;
    config.hr_option = HUDRatioMode::Original;
    config.ds_option = 1;
    config.rr_option = RefreshRate::Original;
    config.rr_manual_value = 30;
#if defined(__ANDROID__)
    // Accurate retains original timing/HUD, not desktop-resolution GPU cost.
    dkr::runtime::mobile_graphics::apply_accurate_budget(config);
#endif
}

// The Sound page's volumes: Master, then the four-part island mix. A muted
// channel plays at 0 while its level is kept for the slider and the settings
// file (sound_muted), so unmuting brings it straight back.
constexpr std::size_t kSoundChannelCount = 5U;
std::array<bool, kSoundChannelCount> g_sound_muted{};
std::array<float, kSoundChannelCount> g_sound_kept_level{1.0F, 1.0F, 1.0F, 1.0F, 1.0F};
// The last hand-made EQ curve in whole dB, kept so trying a preset never
// loses it (eq_custom).
std::optional<std::array<int, 3>> g_eq_custom;

float SoundChannelVolume(std::size_t channel) {
    switch (channel) {
        case 0: return dkr::runtime::platform::master_volume();
        case 1: return dkr::runtime::audio::music_volume();
        case 2: return dkr::runtime::audio::sound_effects_volume();
        case 3: return dkr::runtime::audio::vehicle_volume();
        default: return dkr::runtime::audio::nature_volume();
    }
}

void SetSoundChannelVolume(std::size_t channel, float volume) {
    switch (channel) {
        case 0: dkr::runtime::platform::set_master_volume(volume); break;
        case 1: dkr::runtime::audio::set_music_volume(volume); break;
        case 2: dkr::runtime::audio::set_sound_effects_volume(volume); break;
        case 3: dkr::runtime::audio::set_vehicle_volume(volume); break;
        default: dkr::runtime::audio::set_nature_volume(volume); break;
    }
}

float SoundLevel(std::size_t channel) {
    return g_sound_muted[channel] ? g_sound_kept_level[channel]
                                  : SoundChannelVolume(channel);
}

// Moving a muted slider unmutes it, like the system mixer.
void SetSoundLevel(std::size_t channel, float level) {
    g_sound_muted[channel] = false;
    SetSoundChannelVolume(channel, level);
}

void SetSoundMuted(std::size_t channel, bool muted) {
    if (g_sound_muted[channel] == muted) return;
    if (muted) {
        g_sound_kept_level[channel] = SoundChannelVolume(channel);
        SetSoundChannelVolume(channel, 0.0F);
    } else {
        SetSoundChannelVolume(channel, g_sound_kept_level[channel]);
    }
    g_sound_muted[channel] = muted;
}

void SaveSettings() {
    std::error_code error;
    std::filesystem::create_directories(g_config_directory, error);
    if (error) {
        std::fprintf(stderr, "[boot][settings] failed to create settings directory: %s\n",
                     error.message().c_str());
        return;
    }
    const auto& config = ultramodern::renderer::get_graphics_config();
    const std::filesystem::path settings_path = SettingsPath();
    const std::filesystem::path temporary_path = settings_path.string() + ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::trunc);
        if (!output) {
            std::fprintf(stderr, "[boot][settings] failed to open temporary settings file\n");
            return;
        }
        output << "settings_version="
               << dkr::runtime::enhancements::kCurrentSettingsVersion << '\n';
        output << "presentation_profile="
               << static_cast<int>(dkr::runtime::enhancements::presentation_profile()) << '\n';
        output << "window_mode=" << static_cast<int>(config.wm_option) << '\n';
        output << "resolution=" << static_cast<int>(config.res_option) << '\n';
        output << "aspect=" << static_cast<int>(config.ar_option) << '\n';
        output << "antialiasing=" << static_cast<int>(config.msaa_option) << '\n';
        output << "high_precision_fb=" << static_cast<int>(config.hpfb_option) << '\n';
        output << "graphics_api=" << static_cast<int>(config.api_option) << '\n';
        output << "refresh_rate=" << static_cast<int>(config.rr_option) << '\n';
        output << "refresh_rate_target="
               << dkr::runtime::enhancements::clamp_presentation_rate(
                      config.rr_manual_value) << '\n';
        output << "modern_refresh_rate=" << static_cast<int>(g_modern_refresh_mode) << '\n';
        output << "modern_refresh_target="
               << dkr::runtime::enhancements::clamp_presentation_rate(
                      g_modern_refresh_target) << '\n';
        output << "modern_resolution=" << static_cast<int>(g_modern_resolution) << '\n';
        output << "modern_aspect=" << static_cast<int>(g_modern_aspect) << '\n';
        output << "modern_antialiasing="
               << static_cast<int>(g_modern_antialiasing) << '\n';
        output << "modern_high_precision_fb="
               << static_cast<int>(g_modern_high_precision_fb) << '\n';
        output << "modern_graphics_api="
               << static_cast<int>(g_modern_graphics_api) << '\n';
        output << "modern_downsample=" << g_modern_downsample << '\n';
        output << "modern_anisotropy="
               << dkr::runtime::enhancements::anisotropy_level() << '\n';
        output << "modern_texture_lod_bias_hundredths="
               << dkr::runtime::enhancements::texture_lod_bias_hundredths()
               << '\n';
        output << "modern_generate_texture_mipmaps="
               << (dkr::runtime::enhancements::generated_mipmaps_requested() ? 1 : 0) << '\n';
        output << "gpu_keep_awake=" << (dkr::runtime::render_power::gpu_keep_awake.load() ? 1 : 0) << '\n';
        output << "fps_overlay_enabled=" << (g_fps_overlay_enabled ? 1 : 0) << '\n';
        output << "fps_overlay_position=" << g_fps_overlay_position << '\n';
        output << "fps_overlay_detail=" << g_fps_overlay_detail << '\n';
        output << "fps_overlay_single_row=" << (g_fps_overlay_single_row ? 1 : 0) << '\n';
        output << "fps_custom_frame_time=" << (g_fps_custom_frame_time ? 1 : 0) << '\n';
        output << "fps_custom_simulation=" << (g_fps_custom_simulation ? 1 : 0) << '\n';
        output << "fps_custom_graphics=" << (g_fps_custom_graphics ? 1 : 0) << '\n';
        output << "fps_custom_vi=" << (g_fps_custom_vi ? 1 : 0) << '\n';
        output << "fps_custom_interpolation=" << (g_fps_custom_interpolation ? 1 : 0) << '\n';
        output << "fps_custom_audio=" << (g_fps_custom_audio ? 1 : 0) << '\n';
        output << "fps_custom_target=" << (g_fps_custom_target ? 1 : 0) << '\n';
        output << "fps_custom_resolution=" << (g_fps_custom_resolution ? 1 : 0) << '\n';
        output << "fps_font_size=" << g_fps_font_size << '\n';
        output << "fps_fill_r=" << static_cast<int>(g_fps_fill_colour.x * 255.0F) << '\n';
        output << "fps_fill_g=" << static_cast<int>(g_fps_fill_colour.y * 255.0F) << '\n';
        output << "fps_fill_b=" << static_cast<int>(g_fps_fill_colour.z * 255.0F) << '\n';
        output << "fps_fill_a=" << static_cast<int>(g_fps_fill_colour.w * 255.0F) << '\n';
        output << "fps_outline_r=" << static_cast<int>(g_fps_outline_colour.x * 255.0F) << '\n';
        output << "fps_outline_g=" << static_cast<int>(g_fps_outline_colour.y * 255.0F) << '\n';
        output << "fps_outline_b=" << static_cast<int>(g_fps_outline_colour.z * 255.0F) << '\n';
        output << "fps_outline_a=" << static_cast<int>(g_fps_outline_colour.w * 255.0F) << '\n';
        output << "network_overlay_enabled="
               << (g_network_overlay_enabled ? 1 : 0) << '\n';
        output << "network_overlay_position=" << g_network_overlay_position << '\n';
        output << "network_overlay_detail=" << g_network_overlay_detail << '\n';
        output << "network_overlay_single_row="
               << (g_network_overlay_single_row ? 1 : 0) << '\n';
        output << "controller_input_overlay_enabled="
               << (g_controller_input_overlay_enabled ? 1 : 0) << '\n';
        output << "controller_input_overlay_position="
               << g_controller_input_overlay_position << '\n';
        output << "crt_enabled=" << (g_crt_enabled ? 1 : 0) << '\n';
        output << "crt_filter_index=" << g_crt_filter_index << '\n';
        output << "crt_scale_mode=" << g_crt_scale_mode << '\n';
        output << "crt_strength="
               << static_cast<int>(std::clamp(g_crt_strength, 0.0F, 1.0F) *
                                   1000.0F)
               << '\n';
        output << "master_volume=" << SoundLevel(0U) << '\n';
        output << "music_volume=" << SoundLevel(1U) << '\n';
        output << "sound_effects_volume=" << SoundLevel(2U) << '\n';
        output << "vehicle_volume=" << SoundLevel(3U) << '\n';
        output << "nature_volume=" << SoundLevel(4U) << '\n';
        unsigned sound_muted = 0U;
        for (std::size_t channel = 0; channel < kSoundChannelCount; ++channel) {
            if (g_sound_muted[channel]) sound_muted |= 1U << channel;
        }
        output << "sound_muted=" << sound_muted << '\n';
        if (g_eq_custom) {
            output << "eq_custom=" << (*g_eq_custom)[0] << ',' << (*g_eq_custom)[1]
                   << ',' << (*g_eq_custom)[2] << '\n';
        }
        output << "modern_multiplayer_race_music="
               << (dkr::runtime::enhancements::multiplayer_race_music_requested()
                       ? 1 : 0)
               << '\n';
        output << "eq_bass=" << dkr::runtime::platform::bass_gain() << '\n';
        output << "eq_mid=" << dkr::runtime::platform::mid_gain() << '\n';
        output << "eq_treble=" << dkr::runtime::platform::treble_gain() << '\n';
        output << "maximum_detail="
               << (dkr::runtime::enhancements::maximum_detail_requested() ? 1 : 0) << '\n';
        output << "modern_fov_offset="
               << dkr::runtime::enhancements::fov_offset() << '\n';
        output << "magic_codes_persistent="
               << dkr::runtime::magic_codes::persistent_mask() << '\n';
        output << "modern_view_distance_multiplier="
               << dkr::runtime::enhancements::view_distance_multiplier() << '\n';
        output << "modern_scenery_settings_version=2\n";
        output << "modern_keep_hub_scenery="
               << (dkr::runtime::enhancements::keep_hub_scenery_requested() ? 1 : 0)
               << '\n';
        output << "modern_keep_track_scenery="
               << (dkr::runtime::enhancements::keep_track_scenery_requested() ? 1 : 0)
               << '\n';
        output << "modern_keep_minigame_scenery="
               << (dkr::runtime::enhancements::keep_minigame_scenery_requested() ? 1 : 0)
               << '\n';
        output << "modern_scenery_retention_mode="
               << static_cast<int>(
                      dkr::runtime::enhancements::scenery_retention_mode())
               << '\n';
        output << "modern_animated_scenery_distance_multiplier="
               << dkr::runtime::enhancements::animated_scenery_distance_multiplier()
               << '\n';
        output << "modern_billboard_effect_distance_multiplier="
               << dkr::runtime::enhancements::billboard_effect_distance_multiplier()
               << '\n';
        output << "modern_water_lava_detail_multiplier="
               << dkr::runtime::enhancements::water_lava_detail_multiplier()
               << '\n';
        output << "modern_extended_culling="
               << (dkr::runtime::enhancements::extended_culling_requested() ? 1 : 0)
               << '\n';
        output << "modern_frustum_guard_percent="
               << dkr::runtime::enhancements::frustum_guard_percent() << '\n';
        output << "memory_pak=" << (dkr::runtime::pak::enabled() ? 1 : 0) << '\n';
        output << "rumble=" << (dkr::runtime::platform::rumble_enabled() ? 1 : 0) << '\n';
        output << "rumble_strength=" << dkr::runtime::platform::rumble_strength() << '\n';
        output << "stick_deadzone=" << dkr::runtime::input::stick_deadzone() << '\n';
        output << "stick_anti_deadzone=" << dkr::runtime::input::stick_anti_deadzone() << '\n';
        output << "stick_sensitivity=" << dkr::runtime::input::stick_sensitivity() << '\n';
        output << "stick_curve=" << dkr::runtime::input::stick_curve() << '\n';
        output << "stick_x_inverted=" << (dkr::runtime::input::stick_x_inverted() ? 1 : 0) << '\n';
        output << "stick_y_inverted=" << (dkr::runtime::input::stick_y_inverted() ? 1 : 0) << '\n';
        constexpr std::array<const char*, 3> vehicle_names{
            "car", "hovercraft", "plane"};
        for (std::size_t index = 0; index < vehicle_names.size(); ++index) {
            const auto vehicle = static_cast<
                dkr::runtime::input::VehicleClass>(index);
            output << "stick_x_inverted_" << vehicle_names[index] << '='
                   << (dkr::runtime::input::vehicle_stick_x_inverted(vehicle)
                           ? 1 : 0) << '\n';
            output << "stick_y_inverted_" << vehicle_names[index] << '='
                   << (dkr::runtime::input::vehicle_stick_y_inverted(vehicle)
                           ? 1 : 0) << '\n';
        }
        output << "trigger_threshold=" << dkr::runtime::input::trigger_threshold() << '\n';
        const auto quick_keyboard =
            dkr::runtime::input::quick_restart_keyboard_binding();
        const auto quick_controller =
            dkr::runtime::input::quick_restart_controller_binding();
        output << "quick_restart_enabled="
               << (dkr::runtime::input::quick_restart_enabled() ? 1 : 0) << '\n';
        output << "quick_restart_keyboard_primary=" << quick_keyboard.primary << '\n';
        output << "quick_restart_keyboard_secondary=" << quick_keyboard.secondary << '\n';
        output << "quick_restart_controller_primary=" << quick_controller.primary << '\n';
        output << "quick_restart_controller_secondary=" << quick_controller.secondary << '\n';
        for (std::size_t index = 0; index < kShortcutSettingNames.size(); ++index) {
            const auto action = static_cast<
                dkr::runtime::input::ShortcutAction>(index);
            const auto keyboard =
                dkr::runtime::input::shortcut_keyboard_binding(action);
            const auto controller =
                dkr::runtime::input::shortcut_controller_binding(action);
            output << "shortcut." << kShortcutSettingNames[index]
                   << ".keyboard_primary=" << keyboard.primary << '\n';
            output << "shortcut." << kShortcutSettingNames[index]
                   << ".keyboard_secondary=" << keyboard.secondary << '\n';
            output << "shortcut." << kShortcutSettingNames[index]
                   << ".controller_primary=" << controller.primary << '\n';
            output << "shortcut." << kShortcutSettingNames[index]
                   << ".controller_secondary=" << controller.secondary << '\n';
        }
        output << "gyro_enabled=" << (dkr::runtime::input::gyro_enabled() ? 1 : 0) << '\n';
        output << "gyro_sensitivity=" << dkr::runtime::input::gyro_sensitivity() << '\n';
        output << "gyro_y_sensitivity=" << dkr::runtime::input::gyro_y_sensitivity() << '\n';
        output << "gyro_deadzone=" << dkr::runtime::input::gyro_deadzone() << '\n';
        output << "gyro_inverted=" << (dkr::runtime::input::gyro_inverted() ? 1 : 0) << '\n';
        output << "gyro_y_inverted=" << (dkr::runtime::input::gyro_y_inverted() ? 1 : 0) << '\n';
        output << "gyro_axis=" << static_cast<int>(dkr::runtime::input::gyro_axis()) << '\n';
        for (std::size_t player = 0;
             player < dkr::runtime::input::kPlayerCount; ++player) {
            const std::string prefix = "player" + std::to_string(player + 1U) +
                                       ".gyro_";
            output << prefix << "enabled="
                   << (dkr::runtime::input::gyro_enabled(player) ? 1 : 0) << '\n';
            output << prefix << "sensitivity="
                   << dkr::runtime::input::gyro_sensitivity(player) << '\n';
            output << prefix << "y_sensitivity="
                   << dkr::runtime::input::gyro_y_sensitivity(player) << '\n';
            output << prefix << "deadzone="
                   << dkr::runtime::input::gyro_deadzone(player) << '\n';
            output << prefix << "inverted="
                   << (dkr::runtime::input::gyro_inverted(player) ? 1 : 0) << '\n';
            output << prefix << "y_inverted="
                   << (dkr::runtime::input::gyro_y_inverted(player) ? 1 : 0) << '\n';
            output << prefix << "axis="
                   << static_cast<int>(dkr::runtime::input::gyro_axis(player))
                   << '\n';
        }
        output << "online_player_name=" << g_online_player_name << '\n';
        output << "online_room_name=" << g_online_room_name << '\n';
        output << "online_host_control=" << g_online_host_control << '\n';
        output << "online_maximum_players=" << g_online_maximum_players << '\n';
        output << "online_synchronization=" << g_online_synchronization << '\n';
        output << "online_experimental_rollback=" << (g_online_experimental_rollback ? 1 : 0) << '\n';
#if defined(DKR_EXPERIMENTAL_FULL_SCENES)
        output << "online_experimental_adventure=" << (g_online_experimental_adventure ? 1 : 0) << '\n';
#endif
        output << "online_rollback_window=" << g_online_rollback_window << '\n';
        output << "online_automatic_delay=" << (g_online_automatic_delay ? 1 : 0) << '\n';
        output << "online_manual_delay=" << g_online_manual_delay << '\n';
        output << "online_record_replay=" << (g_online_record_replay ? 1 : 0) << '\n';
        output << "online_save_seed_mode=" << g_online_save_seed_mode << '\n';
        output << "online_input_profile=" << g_online_input_profile << '\n';
        output << "friend_online_notifications="
               << (g_friend_online_notifications ? 1 : 0) << '\n';
        output << "friend_online_notification_position="
               << g_friend_online_notification_position << '\n';
        output << "online_guide_acknowledged_version="
               << g_online_guide_acknowledged_version << '\n';
        output << "input_backend=" << static_cast<int>(
            dkr::runtime::platform::requested_input_backend()) << '\n';
        output << "controller_assignment_mode=" << static_cast<int>(
            dkr::runtime::platform::controller_assignment_mode()) << '\n';
        output << "keyboard_player=" << dkr::runtime::input::keyboard_player() << '\n';
        const auto controller_assignments =
            dkr::runtime::platform::controller_assignment_keys();
        for (std::size_t player = 0; player < dkr::runtime::input::kPlayerCount;
             ++player) {
            output << "player" << (player + 1U) << ".controller_key="
                   << controller_assignments[player] << '\n';
            output << "player" << (player + 1U)
                   << ".allow_background_inputs="
                   << (dkr::runtime::input::background_input_enabled(player)
                           ? 1
                           : 0)
                   << '\n';
            for (std::size_t index = 0;
                 index < dkr::runtime::input::action_count(); ++index) {
                const auto action = static_cast<dkr::runtime::input::Action>(index);
                output << "player" << (player + 1U) << ".keyboard_binding."
                       << dkr::runtime::input::action_identifier(action) << '='
                       << dkr::runtime::input::keyboard_binding(player, action) << '\n';
                output << "player" << (player + 1U) << ".controller_binding."
                       << dkr::runtime::input::action_identifier(action) << '='
                       << dkr::runtime::input::controller_binding(player, action) << '\n';
                output << "player" << (player + 1U)
                       << ".controller_binding_secondary."
                       << dkr::runtime::input::action_identifier(action) << '='
                       << dkr::runtime::input::secondary_controller_binding(
                              player, action) << '\n';
            }
        }
        // This must be the final record. A truncated settings file is never allowed
        // to reactivate experimental presentation features.
        output << "settings_complete=1\n";
        output.flush();
        if (!output) {
            std::fprintf(stderr, "[boot][settings] failed while writing settings\n");
            output.close();
            std::filesystem::remove(temporary_path, error);
            return;
        }
    }
    if (!ReplaceSettingsFile(temporary_path, settings_path)) {
        std::fprintf(stderr, "[boot][settings] failed to replace settings file\n");
        std::filesystem::remove(temporary_path, error);
    }
}

// Custom tracks and their HD textures are Modern-only by policy. An action that
// needs Modern - importing a track, arming one, playing one - switches to it
// live rather than sending the player to the Graphics page, and leaves a note
// saying what changed and how to undo it. Opening Track Lab changes nothing;
// only these actions do. Returns true when it actually switched.
bool EnsureModernForTracks() {
    using dkr::runtime::enhancements::PresentationProfile;
    if (dkr::runtime::enhancements::presentation_profile() ==
        PresentationProfile::Modern) {
        return false;
    }
    GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    dkr::runtime::enhancements::set_presentation_profile(
        PresentationProfile::Modern);
    ApplyProfileGraphics(config, PresentationProfile::Modern);
    ultramodern::renderer::set_graphics_config(config);
    SaveSettings();
    g_track_lab_modern_notice =
        "Switched to Modern - custom tracks and their HD textures need it. "
        "Change back in Graphics.";
    std::fprintf(stderr,
                 "[track-lab] presentation switched to Modern for custom "
                 "tracks\n");
    return true;
}

bool LoadPlayerBindingSetting(const std::string& key, int number) {
    if (key.rfind("player", 0) != 0) {
        return false;
    }

    for (std::size_t player = 0;
         player < dkr::runtime::input::kPlayerCount; ++player) {
        const std::string player_prefix =
            "player" + std::to_string(player + 1U);
        const std::string background_input_key =
            player_prefix + ".allow_background_inputs";
        if (key == background_input_key) {
            dkr::runtime::input::set_background_input_enabled(
                player, number != 0);
            return true;
        }

        const std::string keyboard_prefix =
            player_prefix + ".keyboard_binding.";
        const std::string controller_prefix =
            player_prefix + ".controller_binding.";
        const std::string controller_secondary_prefix =
            player_prefix + ".controller_binding_secondary.";
        const bool keyboard = key.rfind(keyboard_prefix, 0) == 0;
        const bool controller = key.rfind(controller_prefix, 0) == 0;
        const bool controller_secondary =
            key.rfind(controller_secondary_prefix, 0) == 0;
        if (!keyboard && !controller && !controller_secondary) {
            continue;
        }

        const std::string identifier = key.substr(
            keyboard ? keyboard_prefix.size()
                     : controller_secondary
                         ? controller_secondary_prefix.size()
                         : controller_prefix.size());
        for (std::size_t index = 0;
             index < dkr::runtime::input::action_count(); ++index) {
            const auto action =
                static_cast<dkr::runtime::input::Action>(index);
            if (identifier != dkr::runtime::input::action_identifier(action)) {
                continue;
            }
            if (keyboard) {
                dkr::runtime::input::set_keyboard_binding(player, action, number);
            } else if (controller_secondary) {
                dkr::runtime::input::set_secondary_controller_binding(
                    player, action, number);
            } else {
                dkr::runtime::input::set_controller_binding(player, action, number);
            }
            return true;
        }
        return true;
    }
    return true;
}

bool LoadLegacyBindingSetting(const std::string& key, int number) {
    const bool keyboard = key.rfind("keyboard_binding.", 0) == 0;
    const bool controller = key.rfind("controller_binding.", 0) == 0;
    if (!keyboard && !controller) {
        return false;
    }

    const std::size_t prefix = keyboard ? 17U : 19U;
    const std::string identifier = key.substr(prefix);
    for (std::size_t index = 0;
         index < dkr::runtime::input::action_count(); ++index) {
        const auto action = static_cast<dkr::runtime::input::Action>(index);
        if (identifier != dkr::runtime::input::action_identifier(action)) {
            continue;
        }
        if (keyboard) {
            dkr::runtime::input::set_keyboard_binding(action, number);
        } else {
            dkr::runtime::input::set_controller_binding(action, number);
        }
        break;
    }
    return true;
}

void LoadSettings() {
    GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    int settings_version = 0;
    bool settings_complete = false;
    auto profile = dkr::runtime::enhancements::PresentationProfile::Accurate;
    bool profile_value_valid = true;
    bool migrated = false;
    bool modern_scenery_settings_v2 = false;
    std::array<dkr::runtime::input::ShortcutBinding, kShortcutActionCount>
        shortcut_keyboard{};
    std::array<dkr::runtime::input::ShortcutBinding, kShortcutActionCount>
        shortcut_controller{};
    for (std::size_t index = 0; index < shortcut_keyboard.size(); ++index) {
        const auto action = static_cast<
            dkr::runtime::input::ShortcutAction>(index);
        shortcut_keyboard[index] =
            dkr::runtime::input::shortcut_keyboard_binding(action);
        shortcut_controller[index] =
            dkr::runtime::input::shortcut_controller_binding(action);
    }
    auto assignment_mode = dkr::runtime::platform::controller_assignment_mode();
    auto controller_assignments =
        dkr::runtime::platform::controller_assignment_keys();
    // Volumes load as levels; muted channels go silent once all are read.
    unsigned sound_muted = 0U;
    for (std::size_t channel = 0; channel < kSoundChannelCount; ++channel) {
        SetSoundMuted(channel, false);
    }
    g_eq_custom.reset();
    std::ifstream input(SettingsPath());
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        bool controller_key = false;
        for (std::size_t player = 0; player < dkr::runtime::input::kPlayerCount;
             ++player) {
            const std::string expected = "player" + std::to_string(player + 1U) +
                                         ".controller_key";
            if (key == expected) {
                controller_assignments[player] = value;
                controller_key = true;
                break;
            }
        }
        if (controller_key) {
            continue;
        }
        const auto copy_online_text = [](char* destination, std::size_t capacity,
                                         const std::string& text) {
            const std::size_t length = std::min(text.size(), capacity - 1U);
            std::memcpy(destination, text.data(), length);
            destination[length] = '\0';
        };
        if (key == "online_player_name") {
            copy_online_text(g_online_player_name, sizeof(g_online_player_name), value);
            continue;
        }
        if (key == "online_room_name") {
            copy_online_text(g_online_room_name, sizeof(g_online_room_name), value);
            continue;
        }
        // Outside the chain below, which is already at MSVC's nesting limit.
        if (key == "sound_muted") {
            sound_muted = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
            continue;
        }
        if (key == "eq_custom") {
            std::array<int, 3> curve{};
            char comma_a = 0;
            char comma_b = 0;
            std::istringstream fields(value);
            if (fields >> curve[0] >> comma_a >> curve[1] >> comma_b >> curve[2] &&
                comma_a == ',' && comma_b == ',') {
                for (int& band : curve) band = std::clamp(band, -12, 12);
                g_eq_custom = curve;
            }
            continue;
        }
        bool player_gyro_key = false;
        for (std::size_t player = 0;
             player < dkr::runtime::input::kPlayerCount; ++player) {
            const std::string prefix = "player" + std::to_string(player + 1U) +
                                       ".gyro_";
            if (!key.starts_with(prefix)) continue;
            const std::string field = key.substr(prefix.size());
            try {
                if (field == "enabled") {
                    dkr::runtime::input::set_gyro_enabled(std::stoi(value) != 0,
                                                          player);
                } else if (field == "sensitivity") {
                    dkr::runtime::input::set_gyro_sensitivity(std::stof(value),
                                                              player);
                } else if (field == "y_sensitivity") {
                    dkr::runtime::input::set_gyro_y_sensitivity(std::stof(value),
                                                                player);
                } else if (field == "deadzone") {
                    dkr::runtime::input::set_gyro_deadzone(std::stof(value), player);
                } else if (field == "inverted") {
                    dkr::runtime::input::set_gyro_inverted(std::stoi(value) != 0,
                                                           player);
                } else if (field == "y_inverted") {
                    dkr::runtime::input::set_gyro_y_inverted(std::stoi(value) != 0,
                                                             player);
                } else if (field == "axis") {
                    dkr::runtime::input::set_gyro_axis(
                        std::stoi(value) == static_cast<int>(
                            dkr::runtime::input::GyroAxis::Yaw)
                            ? dkr::runtime::input::GyroAxis::Yaw
                            : dkr::runtime::input::GyroAxis::Roll,
                        player);
                }
            } catch (...) {
                std::fprintf(stderr,
                             "[boot][settings] ignored malformed per-player gyro setting %s\n",
                             key.c_str());
            }
            player_gyro_key = true;
            break;
        }
        if (player_gyro_key) continue;
        try {
            const int number = std::stoi(value);
            // Keep the opt-in independent of the legacy 0/1 setting (and of
            // the long compatibility parser below).
            if (key == "online_experimental_rollback") {
                g_online_experimental_rollback = number == 1;
                continue;
            }
#if defined(DKR_EXPERIMENTAL_FULL_SCENES)
            if (key == "online_experimental_adventure") {
                g_online_experimental_adventure = number == 1;
                continue;
            }
#endif
            bool shortcut_setting = false;
            for (std::size_t index = 0; index < kShortcutSettingNames.size(); ++index) {
                const std::string prefix =
                    "shortcut." + std::string(kShortcutSettingNames[index]) + ".";
                if (key == prefix + "keyboard_primary") {
                    shortcut_keyboard[index].primary = number;
                } else if (key == prefix + "keyboard_secondary") {
                    shortcut_keyboard[index].secondary = number;
                } else if (key == prefix + "controller_primary") {
                    shortcut_controller[index].primary = number;
                } else if (key == prefix + "controller_secondary") {
                    shortcut_controller[index].secondary = number;
                } else {
                    continue;
                }
                shortcut_setting = true;
                break;
            }
            if (shortcut_setting) continue;
            if (key == "gpu_keep_awake") {
                dkr::runtime::render_power::gpu_keep_awake.store(number != 0);
                continue;
            }
            if (key == "settings_version") {
                settings_version = number;
            } else if (key == "settings_complete") {
                settings_complete = number == 1;
            } else if (key == "presentation_profile") {
                profile = dkr::runtime::enhancements::normalise_presentation_profile(number);
                profile_value_valid =
                    number == static_cast<int>(
                        dkr::runtime::enhancements::PresentationProfile::Accurate) ||
                    number == static_cast<int>(
                        dkr::runtime::enhancements::PresentationProfile::Modern);
            } else if (key == "window_mode" && number >= 0 && number < 2) {
                config.wm_option = static_cast<WindowMode>(number);
            } else if (key == "resolution" && number >= 0 && number < 3) {
                config.res_option = static_cast<Resolution>(number);
            } else if (key == "aspect" && number >= 0 && number < 2) {
                config.ar_option = static_cast<AspectRatio>(number);
            } else if (key == "antialiasing" && number >= 0 && number < 4) {
                config.msaa_option = static_cast<Antialiasing>(number);
            } else if (key == "high_precision_fb" && number >= 0 && number < 3) {
                config.hpfb_option = static_cast<HighPrecisionFramebuffer>(number);
            } else if (key == "graphics_api" && number >= 0 && number < 4) {
                config.api_option = static_cast<GraphicsApi>(number);
            } else if (key == "refresh_rate" && number >= 0 && number < 3) {
                config.rr_option = static_cast<RefreshRate>(number);
            } else if (key == "refresh_rate_target") {
                config.rr_manual_value =
                    dkr::runtime::enhancements::clamp_presentation_rate(number);
            } else if (key == "modern_refresh_rate" &&
                       number >= static_cast<int>(RefreshRate::Display) &&
                       number <= static_cast<int>(RefreshRate::Manual)) {
                g_modern_refresh_mode = static_cast<RefreshRate>(number);
            } else if (key == "modern_refresh_target") {
                g_modern_refresh_target =
                    dkr::runtime::enhancements::clamp_presentation_rate(number);
            } else if (key == "modern_resolution" && number >= 0 && number < 3) {
                g_modern_resolution = static_cast<Resolution>(number);
            } else if (key == "modern_aspect" && number >= 0 && number < 2) {
                g_modern_aspect = static_cast<AspectRatio>(number);
            } else if (key == "modern_antialiasing" && number >= 0 && number < 4) {
                g_modern_antialiasing = static_cast<Antialiasing>(number);
            } else if (key == "modern_high_precision_fb" && number >= 0 && number < 3) {
                g_modern_high_precision_fb =
                    static_cast<HighPrecisionFramebuffer>(number);
            } else if (key == "modern_graphics_api" && number >= 0 && number < 4) {
                g_modern_graphics_api = static_cast<GraphicsApi>(number);
            } else if (key == "modern_downsample") {
                g_modern_downsample = std::clamp(number, 1, 8);
            } else if (key == "online_host_control") {
                g_online_host_control = std::clamp(number, 0, 2);
            } else if (key == "online_maximum_players") {
                g_online_maximum_players = std::clamp(number, 2,
                    static_cast<int>(dkr::runtime::netplay::kSupportedOnlinePlayers));
            } else if (key == "online_synchronization") {
                g_online_synchronization = std::clamp(number, 0, 1);
            } else if (key == "online_rollback_window") {
                g_online_rollback_window = std::clamp(number, 2, 20);
            } else if (key == "online_automatic_delay") {
                g_online_automatic_delay = number != 0;
            } else if (key == "online_manual_delay") {
                g_online_manual_delay = std::clamp(number, 0, 9);
            } else if (key == "online_record_replay") {
                g_online_record_replay = number != 0;
            } else if (key == "online_save_seed_mode") {
                g_online_save_seed_mode = std::clamp(number, 0, 2);
            } else if (key == "online_input_profile") {
                g_online_input_profile = std::clamp(number, 0, 3);
                dkr::runtime::platform::set_online_input_profile(
                    static_cast<std::size_t>(g_online_input_profile));
            } else if (key == "friend_online_notifications") {
                g_friend_online_notifications = number != 0;
            } else if (key == "friend_online_notification_position") {
                g_friend_online_notification_position = std::clamp(number, 0, 3);
            } else if (key == "online_guide_acknowledged_version") {
                g_online_guide_acknowledged_version = std::clamp(
                    number, 0, kOnlineGuideVersion);
            } else if (key == "modern_anisotropy") {
                dkr::runtime::enhancements::set_anisotropy_level(number);
            } else if (key == "modern_texture_lod_bias_hundredths") {
                dkr::runtime::enhancements::set_texture_lod_bias_hundredths(
                    number);
            } else if (key == "modern_generate_texture_mipmaps") {
                dkr::runtime::enhancements::set_generated_mipmaps_requested(number != 0);
            } else if (key == "fps_overlay_enabled") {
                g_fps_overlay_enabled = number != 0;
            } else if (key == "fps_overlay_position") {
                g_fps_overlay_position = std::clamp(number, 0, 3);
            } else if (key == "fps_overlay_detail") {
                g_fps_overlay_detail = std::clamp(number, 0, 3);
            } else if (key == "fps_overlay_single_row") {
                g_fps_overlay_single_row = number != 0;
            } else if (key == "fps_custom_frame_time") {
                g_fps_custom_frame_time = number != 0;
            } else if (key == "fps_custom_simulation") {
                g_fps_custom_simulation = number != 0;
            } else if (key == "fps_custom_graphics") {
                g_fps_custom_graphics = number != 0;
            } else if (key == "fps_custom_vi") {
                g_fps_custom_vi = number != 0;
            } else if (key == "fps_custom_interpolation") {
                g_fps_custom_interpolation = number != 0;
            } else if (key == "fps_custom_audio") {
                g_fps_custom_audio = number != 0;
            } else if (key == "fps_custom_target") {
                g_fps_custom_target = number != 0;
            } else if (key == "fps_custom_resolution") {
                g_fps_custom_resolution = number != 0;
            } else if (key == "fps_font_size") {
                g_fps_font_size = std::clamp(number, 16, 64);
            } else if (key == "fps_fill_r") {
                g_fps_fill_colour.x = std::clamp(number, 0, 255) / 255.0F;
            } else if (key == "fps_fill_g") {
                g_fps_fill_colour.y = std::clamp(number, 0, 255) / 255.0F;
            } else if (key == "fps_fill_b") {
                g_fps_fill_colour.z = std::clamp(number, 0, 255) / 255.0F;
            } else if (key == "fps_fill_a") {
                g_fps_fill_colour.w = std::clamp(number, 0, 255) / 255.0F;
            } else if (key == "fps_outline_r") {
                g_fps_outline_colour.x = std::clamp(number, 0, 255) / 255.0F;
            } else if (key == "fps_outline_g") {
                g_fps_outline_colour.y = std::clamp(number, 0, 255) / 255.0F;
            } else if (key == "fps_outline_b") {
                g_fps_outline_colour.z = std::clamp(number, 0, 255) / 255.0F;
            } else if (key == "fps_outline_a") {
                g_fps_outline_colour.w = std::clamp(number, 0, 255) / 255.0F;
            } else if (key == "network_overlay_enabled") {
                g_network_overlay_enabled = number != 0;
            } else if (key == "network_overlay_position") {
                g_network_overlay_position = std::clamp(number, 0, 3);
            } else if (key == "network_overlay_detail") {
                g_network_overlay_detail = std::clamp(number, 0, 2);
            } else if (key == "network_overlay_single_row") {
                g_network_overlay_single_row = number != 0;
            } else if (key == "controller_input_overlay_enabled") {
                g_controller_input_overlay_enabled = number != 0;
            } else if (key == "controller_input_overlay_position") {
                g_controller_input_overlay_position = std::clamp(number, 0, 3);
            } else if (key == "crt_enabled") {
                g_crt_enabled = number != 0;
            } else if (key == "crt_filter_index") {
                g_crt_filter_index = std::max(number, 0);
            } else if (key == "crt_scale_mode") {
                g_crt_scale_mode = std::clamp(number, 0, 1);
            } else if (key == "crt_strength") {
                g_crt_strength = std::clamp(number, 0, 1000) / 1000.0F;
            } else if (key == "master_volume") {
                dkr::runtime::platform::set_master_volume(std::stof(value));
            } else if (key == "music_volume") {
                dkr::runtime::audio::set_music_volume(std::stof(value));
            } else if (key == "sound_effects_volume") {
                dkr::runtime::audio::set_sound_effects_volume(std::stof(value));
            } else if (key == "vehicle_volume") {
                dkr::runtime::audio::set_vehicle_volume(std::stof(value));
            } else if (key == "nature_volume") {
                dkr::runtime::audio::set_nature_volume(std::stof(value));
            } else if (key == "modern_multiplayer_race_music") {
                dkr::runtime::enhancements::set_multiplayer_race_music_enabled(
                    number != 0);
            } else if (key == "eq_bass") {
                dkr::runtime::platform::set_bass_gain(std::stof(value));
            } else if (key == "eq_mid") {
                dkr::runtime::platform::set_mid_gain(std::stof(value));
            } else if (key == "eq_treble") {
                dkr::runtime::platform::set_treble_gain(std::stof(value));
            } else if (key == "maximum_detail") {
                dkr::runtime::enhancements::set_maximum_detail_enabled(number != 0);
            } else if (key == "modern_fov_offset") {
                dkr::runtime::enhancements::set_fov_offset(number);
            } else if (key == "magic_codes_persistent") {
                dkr::runtime::magic_codes::set_persistent_mask(
                    static_cast<std::uint32_t>(std::max(number, 0)));
            } else if (key == "modern_view_distance_multiplier") {
                dkr::runtime::enhancements::set_view_distance_multiplier(number);
            } else if (key == "modern_scenery_settings_version") {
                modern_scenery_settings_v2 = number >= 2;
            } else if (key == "modern_keep_hub_scenery") {
                dkr::runtime::enhancements::set_keep_hub_scenery_enabled(number != 0);
                if (number != 0 && !modern_scenery_settings_v2) {
                    // This key meant "keep all scenery rendered" before the
                    // granular controls existed. Preserve that intent once,
                    // then persist the scoped v2 representation.
                    dkr::runtime::enhancements::set_keep_track_scenery_enabled(true);
                    dkr::runtime::enhancements::set_keep_minigame_scenery_enabled(true);
                    dkr::runtime::enhancements::set_scenery_retention_mode(
                        dkr::runtime::enhancements::SceneryRetentionMode::
                            VisibleAndAdjacent);
                    dkr::runtime::enhancements::set_water_lava_detail_multiplier(
                        dkr::runtime::enhancements::
                            kMaximumWaterLavaDetailMultiplier);
                    migrated = true;
                }
            } else if (key == "modern_keep_track_scenery") {
                dkr::runtime::enhancements::set_keep_track_scenery_enabled(number != 0);
            } else if (key == "modern_keep_minigame_scenery") {
                dkr::runtime::enhancements::set_keep_minigame_scenery_enabled(number != 0);
            } else if (key == "modern_scenery_retention_mode") {
                dkr::runtime::enhancements::set_scenery_retention_mode(
                    dkr::runtime::enhancements::normalise_scenery_retention_mode(
                        number));
            } else if (key == "modern_animated_scenery_distance_multiplier") {
                dkr::runtime::enhancements::
                    set_animated_scenery_distance_multiplier(number);
            } else if (key == "modern_billboard_effect_distance_multiplier") {
                dkr::runtime::enhancements::
                    set_billboard_effect_distance_multiplier(number);
            } else if (key == "modern_water_lava_detail_multiplier") {
                dkr::runtime::enhancements::set_water_lava_detail_multiplier(number);
            } else if (key == "modern_extended_culling") {
                dkr::runtime::enhancements::set_extended_culling_enabled(number != 0);
            } else if (key == "modern_frustum_guard_percent") {
                dkr::runtime::enhancements::set_frustum_guard_percent(number);
            } else if (key == "memory_pak") {
                dkr::runtime::pak::set_enabled(number != 0);
            } else if (key == "rumble") {
                dkr::runtime::platform::set_rumble_enabled(number != 0);
            } else if (key == "rumble_strength") {
                dkr::runtime::platform::set_rumble_strength(std::stof(value));
            } else if (key == "stick_deadzone") {
                dkr::runtime::input::set_stick_deadzone(std::stof(value));
            } else if (key == "stick_anti_deadzone") {
                dkr::runtime::input::set_stick_anti_deadzone(std::stof(value));
            } else if (key == "stick_sensitivity") {
                dkr::runtime::input::set_stick_sensitivity(std::stof(value));
            } else if (key == "stick_curve") {
                dkr::runtime::input::set_stick_curve(std::stof(value));
            } else if (key == "stick_x_inverted") {
                dkr::runtime::input::set_stick_x_inverted(number != 0);
            } else if (key == "stick_y_inverted") {
                dkr::runtime::input::set_stick_y_inverted(number != 0);
            } else if (key == "stick_x_inverted_car") {
                dkr::runtime::input::set_vehicle_stick_x_inverted(
                    dkr::runtime::input::VehicleClass::Car, number != 0);
            } else if (key == "stick_x_inverted_hovercraft") {
                dkr::runtime::input::set_vehicle_stick_x_inverted(
                    dkr::runtime::input::VehicleClass::Hovercraft, number != 0);
            } else if (key == "stick_x_inverted_plane") {
                dkr::runtime::input::set_vehicle_stick_x_inverted(
                    dkr::runtime::input::VehicleClass::Plane, number != 0);
            } else if (key == "stick_y_inverted_car") {
                dkr::runtime::input::set_vehicle_stick_y_inverted(
                    dkr::runtime::input::VehicleClass::Car, number != 0);
            } else if (key == "stick_y_inverted_hovercraft") {
                dkr::runtime::input::set_vehicle_stick_y_inverted(
                    dkr::runtime::input::VehicleClass::Hovercraft, number != 0);
            } else if (key == "stick_y_inverted_plane") {
                dkr::runtime::input::set_vehicle_stick_y_inverted(
                    dkr::runtime::input::VehicleClass::Plane, number != 0);
            } else if (key == "trigger_threshold") {
                dkr::runtime::input::set_trigger_threshold(std::stof(value));
            } else if (key == "quick_restart_enabled") {
                dkr::runtime::input::set_quick_restart_enabled(number != 0);
            } else if (key == "quick_restart_keyboard_primary") {
                shortcut_keyboard[0].primary = number;
            } else if (key == "quick_restart_keyboard_secondary") {
                shortcut_keyboard[0].secondary = number;
            } else if (key == "quick_restart_controller_primary") {
                shortcut_controller[0].primary = number;
            } else if (key == "quick_restart_controller_secondary") {
                shortcut_controller[0].secondary = number;
            } else if (key == "gyro_enabled") {
                dkr::runtime::input::set_gyro_enabled(number != 0);
            } else if (key == "gyro_sensitivity") {
                dkr::runtime::input::set_gyro_sensitivity(std::stof(value));
            } else if (key == "gyro_y_sensitivity") {
                dkr::runtime::input::set_gyro_y_sensitivity(std::stof(value));
            } else if (key == "gyro_deadzone") {
                dkr::runtime::input::set_gyro_deadzone(std::stof(value));
            } else if (key == "gyro_inverted") {
                dkr::runtime::input::set_gyro_inverted(number != 0);
            } else if (key == "gyro_y_inverted") {
                dkr::runtime::input::set_gyro_y_inverted(number != 0);
            } else if (key == "gyro_axis") {
                dkr::runtime::input::set_gyro_axis(
                    number == static_cast<int>(dkr::runtime::input::GyroAxis::Yaw)
                        ? dkr::runtime::input::GyroAxis::Yaw
                        : dkr::runtime::input::GyroAxis::Roll);
            } else if (key == "input_backend") {
                const auto backend =
                    number == static_cast<int>(
                        dkr::runtime::platform::InputBackend::SDL2Compatibility)
                        ? dkr::runtime::platform::InputBackend::SDL2Compatibility
                    : number == static_cast<int>(
                        dkr::runtime::platform::InputBackend::SDL3Native)
                        ? dkr::runtime::platform::InputBackend::SDL3Native
                        : dkr::runtime::platform::InputBackend::Automatic;
                dkr::runtime::platform::set_requested_input_backend(backend);
            } else if (key == "controller_assignment_mode") {
                assignment_mode = number == static_cast<int>(
                    dkr::runtime::controllers::AssignmentMode::Manual)
                    ? dkr::runtime::controllers::AssignmentMode::Manual
                    : dkr::runtime::controllers::AssignmentMode::Automatic;
            } else if (key == "keyboard_player") {
                dkr::runtime::input::set_keyboard_player(number);
            } else if (LoadPlayerBindingSetting(key, number)) {
            } else if (LoadLegacyBindingSetting(key, number)) {
            }
        } catch (...) {
            std::fprintf(stderr, "[boot][settings] ignored malformed setting %s\n", key.c_str());
        }
    }
    // Windows cannot atomically replace the settings file while this reader
    // still owns an open handle. Migration may call SaveSettings below, so
    // release the read handle before attempting that replacement.
    input.close();
    for (std::size_t channel = 0; channel < kSoundChannelCount; ++channel) {
        SetSoundMuted(channel, (sound_muted & (1U << channel)) != 0U);
    }
    if (settings_version < 8) {
        // The former global gamepad map becomes the starting point for every
        // local player. Keyboard ownership remains Player 1, matching all
        // existing single-player settings and avoiding duplicate key input.
        for (std::size_t player = 1; player < dkr::runtime::input::kPlayerCount;
             ++player) {
            dkr::runtime::input::copy_bindings(0U, player);
        }
        dkr::runtime::input::set_keyboard_player(0);
        assignment_mode = dkr::runtime::controllers::AssignmentMode::Automatic;
        migrated = true;
    }
    if (settings_version == 8 && settings_complete && profile_value_valid) {
        // Version 9 adds secondary controller bindings, per-vehicle axis
        // direction, configurable shortcuts, and online overlay preferences.
        // Every addition is opt-in or begins from the established v8 value.
        migrated = true;
    }
    dkr::runtime::platform::restore_controller_assignments(
        assignment_mode, controller_assignments);
    for (std::size_t index = 0; index < shortcut_keyboard.size(); ++index) {
        const auto action = static_cast<
            dkr::runtime::input::ShortcutAction>(index);
        dkr::runtime::input::set_shortcut_keyboard_binding(
            action, shortcut_keyboard[index]);
        dkr::runtime::input::set_shortcut_controller_binding(
            action, shortcut_controller[index]);
    }
    // Early launcher builds defaulted to 8x MSAA, which can turn busy races
    // GPU-bound at high desktop resolutions. Migrate that one legacy default
    // to 2x; users can still explicitly choose 4x or 8x afterwards.
    if (settings_version < 2 &&
        config.msaa_option == Antialiasing::MSAA8X) {
        config.msaa_option = Antialiasing::MSAA2X;
        migrated = true;
        std::fprintf(stderr, "[boot][settings] migrated legacy MSAA 8x default to 2x\n");
    }
    const auto resolved_profile =
        dkr::runtime::enhancements::resolve_settings_profile(
            settings_version, settings_complete, profile);
    if (settings_version <
            dkr::runtime::enhancements::kOldestCompatibleSettingsVersion ||
        settings_version > dkr::runtime::enhancements::kCurrentSettingsVersion ||
        !settings_complete || !profile_value_valid) {
        // Every settings file from before the hardened profile boundary, and
        // every truncated v4 write, becomes Accurate. Preserve the old refresh
        // preference as Modern's remembered value without activating it.
        if (config.rr_option == RefreshRate::Display ||
            config.rr_option == RefreshRate::Manual) {
            g_modern_refresh_mode = config.rr_option;
            g_modern_refresh_target =
                dkr::runtime::enhancements::clamp_presentation_rate(
                    config.rr_manual_value);
        }
        RememberModernGraphics(config);
        profile = resolved_profile;
        migrated = true;
        if (settings_version >= 4 && !settings_complete) {
            std::fprintf(stderr,
                         "[boot][settings] incomplete settings file; "
                         "falling back to Accurate\n");
        }
    }
    if (settings_version == 6 && settings_complete && profile_value_valid) {
        // Version 7 adds only opt-in controls. Preserve the proven v6 profile
        // and initialise the new shortcut as disabled.
        dkr::runtime::input::set_quick_restart_enabled(false);
        migrated = true;
    }
    profile = resolved_profile;
    dkr::runtime::enhancements::set_presentation_profile(profile);
    ApplyProfileGraphics(config, profile);
    config.developer_mode = false;
    ultramodern::renderer::set_graphics_config(config);
    if (migrated) {
        SaveSettings();
    }
}

void SaveLastRom(const std::filesystem::path& path) {
    std::ofstream output(LastRomPath(), std::ios::trunc);
    output << PathUtf8(path);
}

std::optional<std::filesystem::path> LoadLastRom() {
    std::ifstream input(LastRomPath());
    std::string path;
    std::getline(input, path);
    if (path.empty()) {
        return std::nullopt;
    }
    return std::filesystem::u8path(path);
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::filesystem::path NormalizeRomPath(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path normalized =
        std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return normalized;
    }
    error.clear();
    normalized = std::filesystem::absolute(path, error);
    return error ? path.lexically_normal() : normalized.lexically_normal();
}

std::string RomPathKey(const std::filesystem::path& path) {
    std::string key = PathUtf8(NormalizeRomPath(path));
#if defined(_WIN32)
    key = Lowercase(std::move(key));
#endif
    return key;
}

std::string RomCatalogLabel(const dkr::runtime::rom::Identity& identity) {
    switch (identity.revision) {
    case dkr::runtime::rom::Revision::UsV77:
        return "Diddy Kong Racing v 1.0";
    case dkr::runtime::rom::Revision::UsV80:
        return identity.byte_order == dkr::runtime::rom::ByteOrder::BigEndian
            ? "Diddy Kong Racing v 1.1"
            : "Diddy Kong Racing v 1.1 (byte-swapped)";
    default:
        return "Unsupported Diddy Kong Racing revision";
    }
}

bool SaveRomCatalog(const std::vector<RomCatalogEntry>& catalog) {
    std::error_code error;
    std::filesystem::create_directories(g_config_directory, error);
    if (error) {
        return false;
    }
    const std::filesystem::path destination = RomCatalogPath();
    std::filesystem::path temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            return false;
        }
        for (const RomCatalogEntry& entry : catalog) {
            output << PathUtf8(entry.path) << '\n';
        }
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    if (!ReplaceSettingsFile(temporary, destination)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::vector<RomCatalogEntry> LoadRomCatalog() {
    std::vector<RomCatalogEntry> catalog;
    std::ifstream input(RomCatalogPath());
    if (!input) {
        return catalog;
    }
    bool cleaned = false;
    std::string encoded_path;
    while (std::getline(input, encoded_path)) {
        if (!encoded_path.empty() && encoded_path.back() == '\r') {
            encoded_path.pop_back();
        }
        if (encoded_path.empty()) {
            cleaned = true;
            continue;
        }
        const std::filesystem::path path =
            NormalizeRomPath(std::filesystem::u8path(encoded_path));
        const std::string key = RomPathKey(path);
        if (std::any_of(catalog.begin(), catalog.end(),
                        [&](const RomCatalogEntry& entry) {
                            return entry.key == key;
                        })) {
            cleaned = true;
            continue;
        }
        std::error_code file_error;
        if (!std::filesystem::is_regular_file(path, file_error) || file_error) {
            cleaned = true;
            continue;
        }
        const auto identity = dkr::runtime::rom::cached_identity(path);
        const std::string label = identity.has_value()
            ? RomCatalogLabel(*identity)
            : "Imported Game Pak - " + PathUtf8(path.filename());
        catalog.push_back({path, label, key});
    }
    if (cleaned) {
        SaveRomCatalog(catalog);
    }
    return catalog;
}

void RememberRomInCatalog(const std::filesystem::path& path,
                          const dkr::runtime::rom::Identity& identity,
                          std::vector<RomCatalogEntry>& catalog) {
    const std::filesystem::path normalized = NormalizeRomPath(path);
    const std::string key = RomPathKey(normalized);
    const std::string label = RomCatalogLabel(identity);
    const auto existing = std::find_if(
        catalog.begin(), catalog.end(), [&](const RomCatalogEntry& entry) {
            return entry.key == key;
        });
    if (existing == catalog.end()) {
        catalog.push_back({normalized, label, key});
        SaveRomCatalog(catalog);
        return;
    }
    if (existing->path != normalized || existing->label != label) {
        existing->path = normalized;
        existing->label = label;
        existing->key = key;
        SaveRomCatalog(catalog);
    }
}

void CommitRomSelection(const std::filesystem::path& path,
                        const dkr::runtime::rom::Identity& identity,
                        std::filesystem::path& selected,
                        std::vector<RomCatalogEntry>& catalog,
                        std::string& status) {
    selected = NormalizeRomPath(path);
    g_mod_browser_revision=identity.revision==dkr::runtime::rom::Revision::UsV80?2U:identity.revision==dkr::runtime::rom::Revision::UsV77?1U:0U;
    SaveLastRom(selected);
    RememberRomInCatalog(selected, identity, catalog);
    status = dkr::runtime::rom::describe(identity) + ". Ready to race!";
}

bool SelectCatalogRom(const RomCatalogEntry& entry,
                      std::filesystem::path& selected,
                      std::vector<RomCatalogEntry>& catalog,
                      std::string& status) {
    std::string error;
    dkr::runtime::rom::Identity identity{};
    if (!dkr::runtime::ValidateRomForLauncher(entry.path, identity, error)) {
        status = error;
        const std::string invalid_key = RomPathKey(entry.path);
        std::erase_if(catalog, [&](const RomCatalogEntry& candidate) {
            return candidate.key == invalid_key;
        });
        SaveRomCatalog(catalog);
        return false;
    }
    CommitRomSelection(entry.path, identity, selected, catalog, status);
    return true;
}

bool IsRomFile(const std::filesystem::path& path) {
    const std::string extension = Lowercase(PathUtf8(path.extension()));
    return extension == ".z64" || extension == ".v64" || extension == ".n64";
}

std::filesystem::path DefaultBrowserDirectory(const std::filesystem::path& selected) {
    std::error_code error;
    if (!selected.empty()) {
        const auto parent = selected.parent_path();
        if (std::filesystem::is_directory(parent, error)) {
            return parent;
        }
    }
#if defined(_WIN32)
    if (const char* profile = std::getenv("USERPROFILE"); profile != nullptr) {
        const auto downloads = std::filesystem::path(profile) / "Downloads";
        if (std::filesystem::is_directory(downloads, error)) {
            return downloads;
        }
        return std::filesystem::path(profile);
    }
#else
    if (const char* home = std::getenv("HOME"); home != nullptr) {
        const auto downloads = std::filesystem::path(home) / "Downloads";
        if (std::filesystem::is_directory(downloads, error)) {
            return downloads;
        }
        return std::filesystem::path(home);
    }
#endif
    return std::filesystem::current_path(error);
}

void RefreshRomBrowser() {
    g_rom_browser.entries.clear();
    g_rom_browser.message.clear();
    std::error_code error;
    if (g_rom_browser.directory.empty()) {
#if defined(_WIN32)
        const DWORD drive_mask = GetLogicalDrives();
        for (int drive = 0; drive < 26; ++drive) {
            if ((drive_mask & (1UL << drive)) != 0) {
                std::string root = "A:\\";
                root[0] = static_cast<char>('A' + drive);
                g_rom_browser.entries.push_back({std::filesystem::path(root), true});
            }
        }
#else
        g_rom_browser.entries.push_back({std::filesystem::path("/"), true});
        if (const char* home = std::getenv("HOME"); home != nullptr) {
            g_rom_browser.entries.push_back({std::filesystem::path(home), true});
        }
#endif
    } else {
        std::filesystem::directory_iterator iterator(
            g_rom_browser.directory,
            std::filesystem::directory_options::skip_permission_denied, error);
        if (error) {
            g_rom_browser.message = "T.T. could not read this location: " + error.message();
        } else {
            for (const auto& item : iterator) {
                const bool directory = item.is_directory(error);
                error.clear();
                if (directory || (item.is_regular_file(error) && IsRomFile(item.path()))) {
                    g_rom_browser.entries.push_back({item.path(), directory});
                }
                error.clear();
            }
        }
    }
    std::sort(g_rom_browser.entries.begin(), g_rom_browser.entries.end(),
              [](const BrowserEntry& left, const BrowserEntry& right) {
        if (left.directory != right.directory) {
            return left.directory > right.directory;
        }
        return Lowercase(PathUtf8(left.path.filename())) <
               Lowercase(PathUtf8(right.path.filename()));
    });
    g_rom_browser.focus_first_entry = true;
}

void OpenRomBrowser(const std::filesystem::path& selected) {
    g_rom_browser.open = true;
    g_rom_browser.close_requested = false;
    g_rom_browser.directory = DefaultBrowserDirectory(selected);
    RefreshRomBrowser();
}

void RomBrowserBack() {
    if (!g_rom_browser.open) {
        return;
    }
    if (g_rom_browser.directory.empty()) {
        g_rom_browser.close_requested = true;
        return;
    }
    const auto parent = g_rom_browser.directory.parent_path();
    if (parent.empty() || parent == g_rom_browser.directory) {
        g_rom_browser.directory.clear();
    } else {
        g_rom_browser.directory = parent;
    }
    RefreshRomBrowser();
}

bool AcceptRom(const std::filesystem::path& path, std::filesystem::path& selected,
               std::vector<RomCatalogEntry>& catalog, std::string& status) {
    std::string error;
    dkr::runtime::rom::Identity identity{};
    if (!dkr::runtime::ValidateRomForLauncher(path, identity, error)) {
        status = error;
        g_rom_browser.message = error;
        return false;
    }
    CommitRomSelection(path, identity, selected, catalog, status);
    g_rom_browser.open = false;
    g_rom_browser.close_requested = false;
    return true;
}

bool SelectRomWithDialog(std::filesystem::path& selected,
                         std::vector<RomCatalogEntry>& catalog,
                         std::string& status) {
    if (NFD_Init() != NFD_OKAY) {
        status = "The system file picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"Nintendo 64 ROM", "z64,v64,n64"}};
    const nfdresult_t dialog = NFD_OpenDialogU8(&result, filters, 1, nullptr);
    if (dialog == NFD_OKAY) {
        const std::filesystem::path candidate = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        std::string error;
        dkr::runtime::rom::Identity identity{};
        if (!dkr::runtime::ValidateRomForLauncher(candidate, identity, error)) {
            status = error;
            NFD_Quit();
            return false;
        }
        CommitRomSelection(candidate, identity, selected, catalog, status);
        NFD_Quit();
        return true;
    }
    if (dialog == NFD_ERROR) {
        status = NFD_GetError();
    }
    NFD_Quit();
    return false;
}

bool ImportCrtFilterWithDialog() {
    // The Vulkan Inspector pool reserves one descriptor for the font atlas and
    // provides 63 image descriptors. Six are used by the built-in masks, so
    // cap the imported catalogue before a live session can exhaust the pool.
    // Uploaded masks are deliberately retained until renderer shutdown to
    // avoid freeing an image that an in-flight presentation still references.
    constexpr std::size_t maximum_crt_filters = 63;
    if (g_crt_filters.size() >= maximum_crt_filters) {
        g_crt_status =
            "The CRT filter library is full. Remove a custom filter from the "
            "local filters folder before importing another.";
        return false;
    }
    if (NFD_Init() != NFD_OKAY) {
        g_crt_status = "The system file picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"PNG image", "png"}};
    const nfdresult_t dialog = NFD_OpenDialogU8(&result, filters, 1, nullptr);
    if (dialog != NFD_OKAY) {
        if (dialog == NFD_ERROR) g_crt_status = NFD_GetError();
        NFD_Quit();
        return false;
    }
    const std::filesystem::path source = std::filesystem::u8path(result);
    NFD_FreePathU8(result);
    NFD_Quit();

    std::error_code error;
    constexpr std::uintmax_t maximum_filter_bytes = 64U * 1024U * 1024U;
    const std::uintmax_t size = std::filesystem::file_size(source, error);
    if (error || size == 0 || size > maximum_filter_bytes) {
        g_crt_status = "Choose a valid PNG filter no larger than 64 MB.";
        return false;
    }
    const std::filesystem::path custom_directory = g_config_directory / "filters";
    std::filesystem::create_directories(custom_directory, error);
    if (error) {
        g_crt_status = "The custom filter folder could not be created.";
        return false;
    }
    std::string stem = source.stem().string();
    if (stem.empty()) stem = "custom-filter";
    std::filesystem::path destination = custom_directory / (stem + ".png");
    for (int suffix = 2; std::filesystem::exists(destination, error) && suffix < 1000;
         ++suffix) {
        destination = custom_directory /
            (stem + "-" + std::to_string(suffix) + ".png");
    }
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::none, error);
    if (error) {
        g_crt_status = "The custom filter could not be imported: " + error.message();
        return false;
    }
    RefreshCrtFilters();
    for (std::size_t index = 0; index < g_crt_filters.size(); ++index) {
        if (g_crt_filters[index].path == destination) {
            g_crt_filter_index = static_cast<int>(index);
            break;
        }
    }
    g_crt_enabled = true;
    g_crt_status = "Imported " + destination.filename().string() + ".";
    SaveSettings();
    return true;
}

bool TexturePackImportRunning() {
    std::scoped_lock lock(g_texture_import_mutex);
    return g_texture_import_state.running;
}

bool StartTexturePackImport(
    const std::filesystem::path& source,
    const dkr::runtime::texture_packs::TrackPackOwner* owner = nullptr,
    const std::filesystem::path& import_temp = {}) {
    {
        std::scoped_lock lock(g_texture_import_mutex);
        if (g_texture_import_state.running) return false;
    }
    std::optional<dkr::runtime::texture_packs::TrackPackOwner> owner_copy;
    if (owner != nullptr) {
        owner_copy = *owner;
    }
    const std::filesystem::path import_temp_copy = import_temp;

    // A completed jthread remains joinable until joined. Retire it before
    // assigning the next worker, outside the state mutex so its final update
    // can never deadlock against this thread.
    if (g_texture_import_worker.joinable()) {
        g_texture_import_worker.join();
    }

    g_texture_import_cancel_requested.store(false, std::memory_order_release);
    {
        std::scoped_lock lock(g_texture_import_mutex);
        g_texture_import_state = {};
        g_texture_import_state.running = true;
        g_texture_import_state.modal_visible = true;
        g_texture_import_state.progress = 0.0F;
        g_texture_import_state.stage = "Preparing texture-pack import";
    }

    g_texture_import_worker = std::jthread(
        [source, owner_copy, import_temp_copy](std::stop_token stop_token) {
            std::string status;
            const auto progress = [stop_token](
                                      const dkr::runtime::texture_packs::ImportProgress& update) {
                {
                    std::scoped_lock lock(g_texture_import_mutex);
                    g_texture_import_state.progress = update.fraction;
                    g_texture_import_state.stage = update.stage;
                    g_texture_import_state.commit_started = update.commit_started;
                }
                if (update.commit_started) return true;
                return !stop_token.stop_requested() &&
                    !g_texture_import_cancel_requested.load(
                        std::memory_order_acquire);
            };
            bool imported = false;
            try {
                imported = dkr::runtime::texture_packs::import_archive(
                    source, status, progress,
                    owner_copy ? &*owner_copy : nullptr);
            } catch (const std::exception& exception) {
                status = "Texture-pack import stopped safely: ";
                status += exception.what();
            } catch (...) {
                status = "Texture-pack import stopped safely because archive "
                         "processing raised an unknown error.";
            }
            if (!import_temp_copy.empty()) {
                dkr::runtime::custom_tracks::discard_install_temp(
                    import_temp_copy);
            }
            std::scoped_lock lock(g_texture_import_mutex);
            g_texture_import_state.running = false;
            g_texture_import_state.finished = true;
            g_texture_import_state.succeeded = imported;
            g_texture_import_state.result = status;
            if (imported) {
                g_texture_import_state.progress = 1.0F;
                g_texture_import_state.stage = "Texture pack imported";
            } else if (g_texture_import_state.stage.empty()) {
                g_texture_import_state.stage = "Texture-pack import stopped";
            }
        });
    return true;
}

// A native file or folder picker is modal: it pumps its own message loop for as
// long as the player browses. The in-game overlay is drawn on the graphics
// thread, inside update_screen, so a picker opened there stalls every graphics
// task in flight - the game's scheduler then drops the late completion and
// main_game_loop waits on it forever. Pickers, and the file work that follows
// them, therefore run on their own thread; the job hands back a completion
// that PumpDialogJob runs on the UI thread on a later frame.
std::mutex g_dialog_job_mutex;
bool g_dialog_job_running = false;
std::function<void()> g_dialog_job_finish;
// Keep the worker last so it is joined before the state above is destroyed.
std::jthread g_dialog_job_worker;

bool DialogJobRunning() {
    std::scoped_lock lock(g_dialog_job_mutex);
    return g_dialog_job_running;
}

bool StartDialogJob(std::function<std::function<void()>()> job) {
    {
        std::scoped_lock lock(g_dialog_job_mutex);
        if (g_dialog_job_running) return false;
        g_dialog_job_running = true;
    }
    // A finished worker stays joinable until joined; retire it first.
    if (g_dialog_job_worker.joinable()) g_dialog_job_worker.join();
    g_dialog_job_worker = std::jthread([job = std::move(job)](std::stop_token) {
        std::function<void()> finish;
        try {
            finish = job();
        } catch (const std::exception& exception) {
            const std::string message =
                std::string("The picker stopped safely: ") + exception.what();
            finish = [message] {
                g_track_import_status = message;
                g_texture_pack_status = message;
            };
        } catch (...) {
            finish = [] {
                g_track_import_status = "The picker stopped safely.";
                g_texture_pack_status = "The picker stopped safely.";
            };
        }
        std::scoped_lock lock(g_dialog_job_mutex);
        g_dialog_job_finish = std::move(finish);
        g_dialog_job_running = false;
    });
    return true;
}

// Runs a finished job's completion. Called once per frame on the UI thread.
void PumpDialogJob() {
    std::function<void()> finish;
    {
        std::scoped_lock lock(g_dialog_job_mutex);
        finish = std::move(g_dialog_job_finish);
        g_dialog_job_finish = nullptr;
    }
    if (finish) finish();
}

// A folder picker, for the dialog-job thread (which owns its own COM
// initialization). False with an empty error means the player cancelled.
bool PickFolder(std::filesystem::path& chosen, std::string& error) {
    if (NFD_Init() != NFD_OKAY) {
        error = "The system folder picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const nfdresult_t dialog = NFD_PickFolderU8(&result, nullptr);
    if (dialog == NFD_OKAY) {
        chosen = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
    } else if (dialog == NFD_ERROR) {
        error = NFD_GetError();
    }
    NFD_Quit();
    return dialog == NFD_OKAY;
}

bool ImportTexturePackWithDialog() {
    if (TexturePackImportRunning()) return false;
    return StartDialogJob([]() -> std::function<void()> {
        if (NFD_Init() != NFD_OKAY) {
            return [] {
                g_texture_pack_status =
                    "The system file picker could not be initialized.";
            };
        }
        nfdu8char_t* result = nullptr;
        const nfdfilteritem_t filters[] = {
            {"Texture-pack archive", "zip,rtz"},
        };
        const nfdresult_t dialog =
            NFD_OpenDialogU8(&result, filters, 1, nullptr);
        std::filesystem::path source;
        std::string error;
        if (dialog == NFD_OKAY) {
            source = std::filesystem::u8path(result);
            NFD_FreePathU8(result);
        } else if (dialog == NFD_ERROR) {
            error = NFD_GetError();
        }
        NFD_Quit();
        if (source.empty()) {
            return [error] {
                if (!error.empty()) g_texture_pack_status = error;
            };
        }
        return [source] { StartTexturePackImport(source); };
    });
}

void ImportLegacyModWithDialog() {
    if (g_legacy_imports.snapshot().busy) return;
    if (NFD_Init() != NFD_OKAY) {
        g_legacy_import_status = "The system file picker could not be initialized.";
        return;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"Legacy DKR patch or archive", "xdelta,zip"}};
    const auto dialog = NFD_OpenDialogU8(&result, filters, 1, nullptr);
    std::filesystem::path source;
    if (dialog == NFD_OKAY) {
        source = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
    } else if (dialog == NFD_ERROR) {
        g_legacy_import_status = NFD_GetError();
    }
    NFD_Quit();
    if (source.empty()) return;
    std::vector<std::filesystem::path> roms;
    for (const auto& entry : LoadRomCatalog()) {
        if (roms.size() == 8) break;
        roms.push_back(entry.path);
    }
    g_legacy_import_status.clear();
    g_legacy_imports.import_file(std::move(source), std::move(roms));
}

bool ImportAdventureWithDialog() {
    if (NFD_Init() != NFD_OKAY) {
        g_save_manager_status = "The system file picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"DKR Adventure save", "bin"}};
    const nfdresult_t dialog = NFD_OpenDialogU8(&result, filters, 1, nullptr);
    bool imported = false;
    if (dialog == NFD_OKAY) {
        const auto source = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        std::string error;
        imported = dkr::runtime::saves::import_adventure(source, error);
        if (imported) {
            InvalidateSaveManagerViewCache();
            g_save_builder_image.reset();
        }
        g_save_manager_status = imported
            ? "Adventure save imported. The previous save was backed up first."
            : error;
    } else if (dialog == NFD_ERROR) {
        g_save_manager_status = NFD_GetError();
    }
    NFD_Quit();
    return imported;
}

bool ExportAdventureWithDialog(const std::filesystem::path& source = {},
    dkr::runtime::saves::StoredSaveKind kind = dkr::runtime::saves::StoredSaveKind::Adventure) {
    if (NFD_Init() != NFD_OKAY) {
        g_save_manager_status = "The system file picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const bool pak = kind == dkr::runtime::saves::StoredSaveKind::ControllerPak;
    const nfdfilteritem_t filters[] = {{pak ? "DKR Controller Pak" : "DKR stored save", pak ? "mpk" : "bin"}};
    const nfdresult_t dialog = NFD_SaveDialogU8(
        &result, filters, 1, nullptr, pak ? "dkr-controller-pak.mpk" :
            kind == dkr::runtime::saves::StoredSaveKind::ExperimentalPaks ? "dkr-online-paks.bin" : "dkr-adventure-save.bin");
    bool exported = false;
    if (dialog == NFD_OKAY) {
        auto destination = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        if (destination.extension().empty()) {
            destination += pak ? ".mpk" : ".bin";
        }
        std::string error;
        exported = dkr::runtime::saves::export_stored_save(source.empty()
            ? dkr::runtime::saves::adventure_info().path : source, destination, kind, error);
        g_save_manager_status = exported
            ? "Stored save exported successfully. The original was not changed."
            : error;
    } else if (dialog == NFD_ERROR) {
        g_save_manager_status = NFD_GetError();
    }
    NFD_Quit();
    return exported;
}

bool ImportSaveBundleWithDialog() {
    if (NFD_Init() != NFD_OKAY) {
        g_save_manager_status = "The system file picker could not be initialized.";
        return false;
    }
    nfdchar_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"DKR-R save bundle", "dkrsave"}};
    const nfdresult_t dialog = NFD_OpenDialogU8(&result, filters, 1, nullptr);
    bool imported = false;
    if (dialog == NFD_OKAY) {
        const auto source = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        std::string error;
        imported = dkr::runtime::saves::import_bundle(source, error);
        if (imported) {
            InvalidateSaveManagerViewCache();
            g_save_builder_image.reset();
        }
        g_save_manager_status = imported
            ? "Adventure and Controller Pak saves returned safely to T.T.'s garage."
            : error;
    } else if (dialog == NFD_ERROR) {
        g_save_manager_status = NFD_GetError();
    }
    NFD_Quit();
    return imported;
}

bool ExportSaveBundleWithDialog() {
    if (NFD_Init() != NFD_OKAY) {
        g_save_manager_status = "The system file picker could not be initialized.";
        return false;
    }
    nfdchar_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"DKR-R save bundle", "dkrsave"}};
    const nfdresult_t dialog = NFD_SaveDialogU8(
        &result, filters, 1, nullptr, "dkr-port-save-garage.dkrsave");
    bool exported = false;
    if (dialog == NFD_OKAY) {
        auto destination = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        if (destination.extension().empty()) {
            destination += ".dkrsave";
        }
        std::string error;
        exported = dkr::runtime::saves::export_bundle(destination, error);
        g_save_manager_status = exported
            ? "Complete save garage exported successfully."
            : error;
    } else if (dialog == NFD_ERROR) {
        g_save_manager_status = NFD_GetError();
    }
    NFD_Quit();
    return exported;
}

bool ImportControllerMappingsWithDialog() {
    if (NFD_Init() != NFD_OKAY) {
        g_controller_mapping_status =
            "The system file picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {
        {"SDL controller mappings", "txt,db"},
    };
    const nfdresult_t dialog = NFD_OpenDialogU8(&result, filters, 1, nullptr);
    bool imported = false;
    if (dialog == NFD_OKAY) {
        const std::filesystem::path source = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        imported = dkr::runtime::platform::import_controller_mappings(
            source, g_controller_mapping_status);
    } else if (dialog == NFD_ERROR) {
        g_controller_mapping_status = NFD_GetError();
    }
    NFD_Quit();
    return imported;
}

bool ExportControllerMappingsWithDialog() {
    if (NFD_Init() != NFD_OKAY) {
        g_controller_mapping_status =
            "The system file picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {
        {"SDL controller mappings", "txt"},
    };
    const nfdresult_t dialog = NFD_SaveDialogU8(
        &result, filters, 1, nullptr, "dkr-r-controller-mappings.txt");
    bool exported = false;
    if (dialog == NFD_OKAY) {
        std::filesystem::path destination = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
        if (destination.extension().empty()) {
            destination += ".txt";
        }
        exported = dkr::runtime::platform::export_controller_mappings(
            destination, g_controller_mapping_status);
    } else if (dialog == NFD_ERROR) {
        g_controller_mapping_status = NFD_GetError();
    }
    NFD_Quit();
    return exported;
}

void DrawRaceBadge(const char* label, const ImVec4& color, float width = 0.0F);

bool BeginPaddedChild(const char* id, const ImVec2& size, bool border = true,
                      ImGuiWindowFlags flags = 0,
                      const ImVec2& padding = {18.0F, 16.0F}) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    const bool visible = ImGui::BeginChild(id, size, border, flags);
    ImGui::PopStyleVar();
    return visible;
}

void DrawColoredWrapped(const ImVec4& color, std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%.*s", static_cast<int>(text.size()), text.data());
    ImGui::PopStyleColor();
}

bool BeginPaddedModal(const char* name, ImGuiWindowFlags flags = 0,
                      const ImVec2& padding = {26.0F, 24.0F}) {
#if defined(__ANDROID__)
    dkr::runtime::mobile::constrain_modal();
    flags &= ~(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (g_paddock_modal_windows > 0) return BeginPaddockModalWindow(name, flags);
#else
    if (g_paddock_modal_windows > 0) {
        return BeginPaddockModalWindow(
            name, flags | ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);
    }
#endif
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0F);
    // Modal actions must remain fully visible. Long pages may scroll behind a
    // modal, and specialised children such as the ROM file list may scroll,
    // but the modal window itself must never grow its own scrollbar.
#if !defined(__ANDROID__)
    flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
#endif
    const bool visible = ImGui::BeginPopupModal(name, nullptr, flags);
    ImGui::PopStyleVar(2);
    return visible;
}

void DrawTexturePackImportModal() {
    TextureImportState state;
    {
        std::scoped_lock lock(g_texture_import_mutex);
        state = g_texture_import_state;
    }
    if (!state.modal_visible) return;

    constexpr const char* popup_name = "Importing texture pack";
    ImGui::OpenPopup(popup_name);
    ImGui::SetNextWindowSize({540.0F, 0.0F}, ImGuiCond_Appearing);
    if (!BeginPaddedModal(popup_name, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted(state.running ? "PREPARING YOUR TEXTURE PACK"
                                         : "TEXTURE-PACK IMPORT COMPLETE");
    ImGui::Dummy({0.0F, 8.0F});
    ImGui::TextWrapped("%s", state.stage.empty()
        ? "Inspecting the selected archive..."
        : state.stage.c_str());
    ImGui::Dummy({0.0F, 8.0F});
    ImGui::ProgressBar(std::clamp(state.progress, 0.0F, 1.0F),
                       {ImGui::GetContentRegionAvail().x, 28.0F});

    if (state.running) {
        ImGui::Dummy({0.0F, 10.0F});
        if (state.commit_started) {
            ImGui::TextDisabled(
                "Finishing the atomic install. DKR-R will not interrupt this step.");
        } else if (ImGui::Button("CANCEL IMPORT", {170.0F, 42.0F})) {
            g_texture_import_cancel_requested.store(true,
                                                     std::memory_order_release);
        }
    } else {
        ImGui::Dummy({0.0F, 10.0F});
        if (!state.result.empty()) ImGui::TextWrapped("%s", state.result.c_str());
        ImGui::Dummy({0.0F, 8.0F});
        if (ImGui::Button(state.succeeded ? "READY TO RACE" : "CLOSE",
                          {170.0F, 42.0F})) {
            g_texture_pack_status = state.result;
            {
                std::scoped_lock lock(g_texture_import_mutex);
                g_texture_import_state.modal_visible = false;
            }
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

void DrawLegacyModImportModal() {
    g_legacy_imports.tick();
    const auto state = g_legacy_imports.snapshot();
    if (!state.modal) return;
    constexpr const char* name = "Import and manage mods";
    ImGui::OpenPopup(name);
    ImGui::SetNextWindowSize({std::min(600.0F, ImGui::GetIO().DisplaySize.x - 48.0F), 0.0F}, ImGuiCond_Appearing);
    if (!BeginPaddedModal(name, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(state.busy ? "PREPARING MODS" : "MOD LIBRARY");
    ImGui::Dummy({0.0F, 8.0F});
    if (state.busy) {
        const auto cursor = ImGui::GetCursorScreenPos();
        const float angle = static_cast<float>(ImGui::GetTime() * 4.0);
        auto* draw = ImGui::GetWindowDrawList();
        draw->PathArcTo({cursor.x + 12.0F, cursor.y + 12.0F}, 9.0F, angle, angle + 4.5F, 24);
        draw->PathStroke(ImGui::GetColorU32(kWarm), 0, 3.0F);
        ImGui::Dummy({28.0F, 26.0F});
        ImGui::TextWrapped("%s", state.stage.c_str());
        if (state.total) ImGui::Text("Patch %u of %u", state.completed, state.total);
        ImGui::TextWrapped("Library work runs in the background. Original Game Paks and saves are not modified. Cancellation stops before commit; committed changes finish safely.");
        ImGui::Dummy({0.0F, 10.0F});
        if (ImGui::Button("CANCEL OPERATION", {ImGui::GetContentRegionAvail().x, 42.0F})) g_legacy_imports.cancel();
    } else {
        ImGui::TextWrapped("%s", state.result.c_str());
        ImGui::Dummy({0.0F, 10.0F});
        if (ImGui::Button("DONE", {ImGui::GetContentRegionAvail().x, 42.0F})) {
            g_legacy_imports.dismiss();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

void DrawPaddockLegacyModImportModal() {
    const PaddockFlatScope paddock;
    ++g_paddock_modal_windows;
    DrawLegacyModImportModal();
    --g_paddock_modal_windows;
}

void DrawModLaunchModal() {
    const auto state=g_mod_launch.snapshot();
    if(!state.modal)return;
    constexpr const char* name="Preparing custom game";
    ImGui::OpenPopup(name);
    ImGui::SetNextWindowSize({std::min(600.0F,ImGui::GetIO().DisplaySize.x-48.0F),0},ImGuiCond_Appearing);
    if(!BeginPaddedModal(name,ImGuiWindowFlags_AlwaysAutoResize))return;
    if(state.busy) {
        const auto cursor=ImGui::GetCursorScreenPos();const float angle=static_cast<float>(ImGui::GetTime()*4.0);
        auto* draw=ImGui::GetWindowDrawList();
        draw->PathArcTo({cursor.x+12,cursor.y+12},9,angle,angle+4.5F,24);
        draw->PathStroke(ImGui::GetColorU32(kWarm),0,3);ImGui::Dummy({28,26});
        ImGui::TextWrapped("%s",state.stage.c_str());
        ImGui::TextWrapped("Checking prepared assets and separate modded saves. The launcher remains responsive.");
        if(ImGui::Button("CANCEL LAUNCH",{ImGui::GetContentRegionAvail().x,42})) {
            g_mod_launch.cancel();ImGui::CloseCurrentPopup();
        }
    } else {
        DrawColoredWrapped(kWarm,state.error);
        ImGui::TextWrapped("The game is stopped. Check Mods / Hacks or disable the affected mods and try again.");
        if(ImGui::Button("BACK TO LAUNCHER",{ImGui::GetContentRegionAvail().x,42})) {
            g_mod_launch.dismiss();ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

// Diagnostic markers execute in draw-list order, not while building the UI.
// They leave renderer state untouched and exist only with profiling enabled.
struct LauncherDrawProfileRange {
    SDL_Renderer* renderer;
    dkr::runtime::launcher::Profile* profile;
    dkr::runtime::launcher::Profile::Phase phase;
    std::chrono::steady_clock::time_point started{};
    static void begin(const ImDrawList*, const ImDrawCmd* command) {
        auto& range = *static_cast<LauncherDrawProfileRange*>(command->UserCallbackData);
        SDL_RenderFlush(range.renderer);
        range.started = std::chrono::steady_clock::now();
    }
    static void end(const ImDrawList*, const ImDrawCmd* command) {
        auto& range = *static_cast<LauncherDrawProfileRange*>(command->UserCallbackData);
        SDL_RenderFlush(range.renderer);
        range.profile->add(range.phase, range.started, std::chrono::steady_clock::now());
    }
};

struct CachedLauncherPanelDraw {
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_Rect bounds, clip;
    static void draw(const ImDrawList*, const ImDrawCmd* command) {
        const auto& panel = *static_cast<CachedLauncherPanelDraw*>(command->UserCallbackData);
        SDL_Rect previous{};
        const bool clipped = SDL_RenderIsClipEnabled(panel.renderer) == SDL_TRUE;
        SDL_RenderGetClipRect(panel.renderer, &previous);
        SDL_RenderSetClipRect(panel.renderer, &panel.clip);
        SDL_RenderCopy(panel.renderer, panel.texture, nullptr, &panel.bounds);
        SDL_RenderSetClipRect(panel.renderer, clipped ? &previous : nullptr);
    }
};

void RenderRecoveryDrawData(SDL_Renderer* renderer,ImDrawData* data) {
    // SDL 2.26.3's software triangle sampler stores texture_coordinate*area
    // in a signed 32-bit SDL_Point BEFORE its 64-bit interpolation. A large
    // cloud/atlas triangle at high resolution can overflow and read outside
    // the texture during SDL_RenderPresent. Bound the input geometry here;
    // never modify the dependency or the normal accelerated launcher.
    if(!data || data->DisplaySize.x<=0 || data->DisplaySize.y<=0)return;
    SDL_Rect previous_view{},previous_clip{};
    SDL_RenderGetViewport(renderer,&previous_view);SDL_RenderGetClipRect(renderer,&previous_clip);
    const bool clipped=SDL_RenderIsClipEnabled(renderer)==SDL_TRUE;
    SDL_BlendMode previous_blend{};SDL_GetRenderDrawBlendMode(renderer,&previous_blend);
    SDL_RenderSetViewport(renderer,nullptr);SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    struct Vertex {SDL_FPoint position;SDL_Color color;SDL_FPoint uv;};
    // Axis-aligned images are copies, not a reason to rasterize thousands of
    // tiny triangles. Validate BOTH triangles and their shared diagonal before
    // taking this path; glyphs, gradients and rotated images remain geometry.
    const auto copy_quad=[&](SDL_Texture* texture,int tw,int th,const std::array<Vertex,6>& v,
                            auto&& flush_geometry) {
        if(!texture || tw<=0 || th<=0)return false;
        float left=v[0].position.x,right=left,top=v[0].position.y,bottom=top;
        for(const auto& vertex:v) {
            left=(std::min)(left,vertex.position.x);right=(std::max)(right,vertex.position.x);
            top=(std::min)(top,vertex.position.y);bottom=(std::max)(bottom,vertex.position.y);
            if(std::memcmp(&vertex.color,&v[0].color,sizeof(SDL_Color))!=0)return false;
        }
        if(right<=left || bottom<=top)return false;
        std::array<SDL_FPoint,4> uv{};unsigned corners=0,triangles[2]{};
        for(unsigned i=0;i<v.size();++i) {
            const auto p=v[i].position;
            if((p.x!=left && p.x!=right) || (p.y!=top && p.y!=bottom))return false;
            const unsigned corner=p.y==top?(p.x==left?0:1):(p.x==right?2:3);
            const unsigned bit=1U<<corner;
            if(triangles[i/3]&bit)return false;
            triangles[i/3]|=bit;
            if(corners&bit) {
                if(uv[corner].x!=v[i].uv.x || uv[corner].y!=v[i].uv.y)return false;
            } else uv[corner]=v[i].uv;
            corners|=bit;
        }
        const unsigned diagonal=triangles[0]&triangles[1];
        if(corners!=15 || (diagonal!=5 && diagonal!=10) ||
           uv[0].x!=uv[3].x || uv[1].x!=uv[2].x ||
           uv[0].y!=uv[1].y || uv[2].y!=uv[3].y)return false;
        const float u0=(std::min)(uv[0].x,uv[1].x),u1=(std::max)(uv[0].x,uv[1].x);
        const float v0=(std::min)(uv[0].y,uv[3].y),v1=(std::max)(uv[0].y,uv[3].y);
        const int x0=int(std::round(u0*tw)),x1=int(std::round(u1*tw));
        const int y0=int(std::round(v0*th)),y1=int(std::round(v1*th));
        // Copy sampling has integer source texels, like SDL's ordinary software
        // image-copy path. Backgrounds are pre-scaled to window resolution, so
        // clipping rounds by at most half a screen pixel (no resolution drop).
        // The font atlas never uses this path: keep sub-texel glyph sampling.
        if(x0<0 || x1>tw || y0<0 || y1>th || x1<=x0 || y1<=y0)return false;
        SDL_Rect source{x0,y0,x1-x0,y1-y0};SDL_FRect destination{left,top,right-left,bottom-top};
        Uint8 r=255,g=255,b=255,a=255;
        if(SDL_GetTextureColorMod(texture,&r,&g,&b)!=0 || SDL_GetTextureAlphaMod(texture,&a)!=0)return false;
        flush_geometry();
        const auto color=v[0].color;
        SDL_SetTextureColorMod(texture,color.r,color.g,color.b);SDL_SetTextureAlphaMod(texture,color.a);
        const auto flip=static_cast<SDL_RendererFlip>((uv[0].x>uv[1].x?SDL_FLIP_HORIZONTAL:0) |
                                                     (uv[0].y>uv[3].y?SDL_FLIP_VERTICAL:0));
        const bool copied=(flip==SDL_FLIP_NONE
            ? SDL_RenderCopyF(renderer,texture,&source,&destination)
            : SDL_RenderCopyExF(renderer,texture,&source,&destination,0,nullptr,flip))==0;
        SDL_SetTextureColorMod(texture,r,g,b);SDL_SetTextureAlphaMod(texture,a);
        return copied;
    };
    const auto midpoint=[](const Vertex& a,const Vertex& b) {
        return Vertex{{(a.position.x+b.position.x)*0.5F,(a.position.y+b.position.y)*0.5F},
            {Uint8((unsigned(a.color.r)+b.color.r)/2),Uint8((unsigned(a.color.g)+b.color.g)/2),
             Uint8((unsigned(a.color.b)+b.color.b)/2),Uint8((unsigned(a.color.a)+b.color.a)/2)},
            {(a.uv.x+b.uv.x)*0.5F,(a.uv.y+b.uv.y)*0.5F}};
    };
    std::vector<Vertex> batch;batch.reserve(1536);
    for(int n=0;n<data->CmdListsCount;++n) {
        const auto* list=data->CmdLists[n];
        for(const auto& command:list->CmdBuffer) {
            if(command.UserCallback) {
                if(command.UserCallback==ImDrawCallback_ResetRenderState) {
                    SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetClipRect(renderer,nullptr);
                } else command.UserCallback(list,&command);
                continue;
            }
            if(command.IdxOffset>unsigned(list->IdxBuffer.Size) ||
               command.ElemCount>unsigned(list->IdxBuffer.Size)-command.IdxOffset || command.ElemCount%3)continue;
            const auto scale=data->FramebufferScale,origin=data->DisplayPos;
            const float left=(std::max)(0.0F,(command.ClipRect.x-origin.x)*scale.x);
            const float top=(std::max)(0.0F,(command.ClipRect.y-origin.y)*scale.y);
            const float right=(std::min)(data->DisplaySize.x*scale.x,(command.ClipRect.z-origin.x)*scale.x);
            const float bottom=(std::min)(data->DisplaySize.y*scale.y,(command.ClipRect.w-origin.y)*scale.y);
            if(right<=left || bottom<=top)continue;
            SDL_Rect clip{int(left),int(top),int(right-left),int(bottom-top)};SDL_RenderSetClipRect(renderer,&clip);
            auto* texture=reinterpret_cast<SDL_Texture*>(command.TextureId);
            int tw=0,th=0;
            if(texture && (SDL_QueryTexture(texture,nullptr,nullptr,&tw,&th)!=0 || tw>65535 || th>65535))continue;
            const auto flush=[&] {
                if(batch.empty())return;
                SDL_RenderGeometryRaw(renderer,texture,&batch[0].position.x,sizeof(Vertex),
                    &batch[0].color,sizeof(Vertex),&batch[0].uv.x,sizeof(Vertex),int(batch.size()),nullptr,0,0);
                batch.clear();
            };
            const auto emit=[&](auto&& self,Vertex a,Vertex b,Vertex c,unsigned depth)->void {
                const float span=(std::max)({std::abs(a.position.x-b.position.x),std::abs(a.position.y-b.position.y),
                    std::abs(a.position.x-c.position.x),std::abs(a.position.y-c.position.y),
                    std::abs(b.position.x-c.position.x),std::abs(b.position.y-c.position.y)});
                if(texture && span>64.0F) {
                    if(depth==12)return; // Bounded failure, never an unsafe triangle.
                    const auto ab=midpoint(a,b),bc=midpoint(b,c),ca=midpoint(c,a);
                    self(self,a,ab,ca,depth+1);self(self,ab,b,bc,depth+1);
                    self(self,ca,bc,c,depth+1);self(self,ab,bc,ca,depth+1);return;
                }
                batch.push_back(a);batch.push_back(b);batch.push_back(c);
                if(batch.size()>=1536)flush();
            };
            for(unsigned i=0;i<command.ElemCount;i+=3) {
                if(texture && command.TextureId!=ImGui::GetIO().Fonts->TexID && command.ElemCount-i>=6) {
                    std::array<Vertex,6> quad{};bool valid=true;
                    for(unsigned j=0;j<quad.size();++j) {
                        const auto index=std::uint64_t(list->IdxBuffer[command.IdxOffset+i+j])+command.VtxOffset;
                        if(index>=unsigned(list->VtxBuffer.Size)) {valid=false;break;}
                        const auto& source=list->VtxBuffer[int(index)];
                        quad[j]={{(source.pos.x-origin.x)*scale.x,(source.pos.y-origin.y)*scale.y},
                            {Uint8(source.col),Uint8(source.col>>8),Uint8(source.col>>16),Uint8(source.col>>24)},
                            {source.uv.x,source.uv.y}};
                        valid=valid && std::isfinite(quad[j].position.x) && std::isfinite(quad[j].position.y) &&
                            std::abs(quad[j].position.x)<32768 && std::abs(quad[j].position.y)<32768 &&
                            std::isfinite(quad[j].uv.x) && std::isfinite(quad[j].uv.y) &&
                            quad[j].uv.x>=0 && quad[j].uv.x<=1 && quad[j].uv.y>=0 && quad[j].uv.y<=1;
                    }
                    // Flush earlier geometry first: image copies must keep the
                    // original ImGui ordering, clipping and alpha composition.
                    if(valid && copy_quad(texture,tw,th,quad,flush)) {i+=3;continue;}
                }
                Vertex v[3];bool valid=true;
                for(unsigned j=0;j<3;++j) {
                    const auto index=std::uint64_t(list->IdxBuffer[command.IdxOffset+i+j])+command.VtxOffset;
                    if(index>=unsigned(list->VtxBuffer.Size)) {valid=false;break;}
                    const auto& source=list->VtxBuffer[int(index)];
                    v[j]={{(source.pos.x-origin.x)*scale.x,(source.pos.y-origin.y)*scale.y},
                        {Uint8(source.col),Uint8(source.col>>8),Uint8(source.col>>16),Uint8(source.col>>24)},
                        {source.uv.x,source.uv.y}};
                    valid=valid && std::isfinite(v[j].position.x) && std::isfinite(v[j].position.y) &&
                        std::abs(v[j].position.x)<32768 && std::abs(v[j].position.y)<32768 &&
                        std::isfinite(v[j].uv.x) && std::isfinite(v[j].uv.y) &&
                        (!texture || (v[j].uv.x>=0 && v[j].uv.x<=1 && v[j].uv.y>=0 && v[j].uv.y<=1));
                }
                if(valid)emit(emit,v[0],v[1],v[2],0);
            }
            flush();
        }
    }
    SDL_RenderSetViewport(renderer,&previous_view);SDL_RenderSetClipRect(renderer,clipped?&previous_clip:nullptr);
    SDL_SetRenderDrawBlendMode(renderer,previous_blend);
}

void RenderLauncherDrawData(SDL_Renderer* renderer, ImDrawData* data,
                           dkr::runtime::launcher::Profile& profile,
                           dkr::runtime::launcher::PanelCache& panel_cache,
                           bool software_failure_recovery=false) {
#if defined(__linux__) || defined(_WIN32)
#if defined(_WIN32)
    // Share the proven software solid/panel adapter only on the returned
    // recovery launcher. Initial/ordinary Windows launchers stay accelerated.
    if(!software_failure_recovery) {
        ImGui_ImplSDLRenderer2_RenderDrawData(data);return;
    }
#endif
    // SDL supports untextured geometry with the same per-vertex colours and
    // alpha. Avoid the software texture sampler for ImGui's solid shapes.
    // This adapter is launcher-only; the shared backend and RT64 stay intact.
    if (data == nullptr) return;
    const auto font_texture = ImGui::GetIO().Fonts->TexID;
    const auto white_uv = ImGui::GetIO().Fonts->TexUvWhitePixel;
    panel_cache.next_frame();
    float scale_x = 1, scale_y = 1;
    SDL_RenderGetScale(renderer, &scale_x, &scale_y);
    const bool cache_coordinates_supported = scale_x == 1 && scale_y == 1 &&
        data->FramebufferScale.x == 1 && data->FramebufferScale.y == 1 &&
        data->DisplayPos.x == 0 && data->DisplayPos.y == 0;
    std::deque<CachedLauncherPanelDraw> panel_draws;
    std::vector<ImVector<ImDrawCmd>> original_commands(data->CmdListsCount);
    std::vector<LauncherDrawProfileRange> ranges;
    if (profile.enabled()) ranges.resize(data->CmdListsCount);
    for (int list_index = 0; list_index < data->CmdListsCount; ++list_index) {
        auto* list = data->CmdLists[list_index];
        ImVector<ImDrawCmd> replacement;
        replacement.reserve(list->CmdBuffer.Size);
        if (profile.enabled()) {
            using P = dkr::runtime::launcher::Profile;
            const std::string_view name = list->_OwnerName != nullptr ? list->_OwnerName : "";
            const auto phase = name == "DKR-R Startup" ? P::Root
                : name.find("launcher-nav") != std::string_view::npos ? P::Sidebar
                : name.find("launcher-content") != std::string_view::npos ? P::Content
                : P::OtherLists;
            ranges[list_index] = {renderer, &profile, phase};
            ImDrawCmd marker;
            marker.UserCallback = LauncherDrawProfileRange::begin;
            marker.UserCallbackData = &ranges[list_index];
            replacement.push_back(marker);
        }
        for (const auto& command : list->CmdBuffer) {
            if (command.UserCallback != nullptr || command.TextureId != font_texture ||
                command.ElemCount % 3 != 0 ||
                command.IdxOffset > static_cast<unsigned>(list->IdxBuffer.Size) ||
                command.ElemCount > static_cast<unsigned>(list->IdxBuffer.Size) - command.IdxOffset) {
                replacement.push_back(command);
                continue;
            }
            for (unsigned offset = 0; offset < command.ElemCount;) {
                const auto solid_at = [&](unsigned i) {
                    return dkr::runtime::launcher::is_solid_triangle(
                        list->VtxBuffer.Data, static_cast<std::size_t>(list->VtxBuffer.Size),
                        list->IdxBuffer.Data + command.IdxOffset + i,
                        command.VtxOffset, white_uv);
                };
                const bool solid = solid_at(offset);
                const unsigned begin = offset;
                do { offset += 3; } while (offset < command.ElemCount && solid_at(offset) == solid);
                auto run = command;
                run.IdxOffset += begin;
                run.ElemCount = offset - begin;
                if (solid) run.TextureId = 0;
                if (solid && cache_coordinates_supported && run.ElemCount <= 4096) {
                    std::vector<SDL_Vertex> vertices;
                    vertices.reserve(run.ElemCount);
                    for (unsigned i = 0; i < run.ElemCount; ++i) {
                        const auto& source = list->VtxBuffer[run.VtxOffset + list->IdxBuffer[run.IdxOffset + i]];
                        SDL_Vertex vertex{};
                        vertex.position = {source.pos.x, source.pos.y};
                        std::memcpy(&vertex.color, &source.col, sizeof(vertex.color));
                        vertices.push_back(vertex);
                    }
                    if (auto* cached = panel_cache.find(renderer, std::move(vertices))) {
                        const float left = std::max(run.ClipRect.x, 0.0F);
                        const float top = std::max(run.ClipRect.y, 0.0F);
                        const float right = std::min(run.ClipRect.z, data->DisplaySize.x);
                        const float bottom = std::min(run.ClipRect.w, data->DisplaySize.y);
                        if (right > left && bottom > top) {
                            panel_draws.push_back({renderer, cached->texture, cached->bounds,
                                {static_cast<int>(left), static_cast<int>(top),
                                 static_cast<int>(right-left), static_cast<int>(bottom-top)}});
                            run.UserCallback = CachedLauncherPanelDraw::draw;
                            run.UserCallbackData = &panel_draws.back();
                        }
                    }
                }
                replacement.push_back(run);
            }
        }
        if (profile.enabled()) {
            ImDrawCmd marker;
            marker.UserCallback = LauncherDrawProfileRange::end;
            marker.UserCallbackData = &ranges[list_index];
            replacement.push_back(marker);
        }
        original_commands[list_index].swap(list->CmdBuffer);
        list->CmdBuffer.swap(replacement);
    }
    SDL_BlendMode old_blend{};
    SDL_GetRenderDrawBlendMode(renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if(software_failure_recovery)RenderRecoveryDrawData(renderer,data);
    else ImGui_ImplSDLRenderer2_RenderDrawData(data);
    SDL_SetRenderDrawBlendMode(renderer, old_blend);
    for (int i = 0; i < data->CmdListsCount; ++i) {
        data->CmdLists[i]->CmdBuffer.swap(original_commands[i]);
    }
#else
    (void)renderer;
    (void)profile;
    (void)panel_cache;
    if(software_failure_recovery)RenderRecoveryDrawData(renderer,data);
    else ImGui_ImplSDLRenderer2_RenderDrawData(data);
#endif
}

void DrawLinuxSoftwarePanelUnderlay(const ImVec2& size,
                                    const ImVec4& color) {
#if defined(__linux__)
    // Keep this underlay on Linux as a guard for software-renderer fallback and
    // fractional scaling. A simple axis-aligned rounded rectangle beneath the
    // normal ImGui child prevents raster seams from exposing or folding the
    // checkerboard through the content panel.
    if (size.x <= 0.0F || size.y <= 0.0F) return;
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const ImVec2 maximum{minimum.x + size.x, minimum.y + size.y};
    const float radius = std::min(
        18.0F, std::max(std::min(size.x, size.y) * 0.5F, 0.0F));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(color);
    draw->AddRectFilled(
        {minimum.x + radius, minimum.y},
        {maximum.x - radius, maximum.y}, fill);
    draw->AddRectFilled(
        {minimum.x, minimum.y + radius},
        {maximum.x, maximum.y - radius}, fill);
    const float half = radius * 0.5F;
    draw->AddRectFilled(
        {minimum.x + half, minimum.y + half},
        {maximum.x - half, maximum.y - half}, fill);
#else
    (void)size;
    (void)color;
#endif
}

void DrawRomBrowser(std::filesystem::path& selected, std::string& status,
                    bool& rom_ready,
                    std::vector<RomCatalogEntry>& catalog) {
    constexpr const char* kPopupName = "Select Diddy Kong Racing ROM";
    if (g_rom_browser.open && !ImGui::IsPopupOpen(kPopupName)) {
        ImGui::OpenPopup(kPopupName);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(
        {std::clamp(viewport->Size.x * 0.72F, 680.0F, 1040.0F),
         std::clamp(viewport->Size.y * 0.78F, 560.0F, 760.0F)},
        ImGuiCond_Appearing);
    const bool popup_visible = BeginPaddedModal(
        kPopupName, ImGuiWindowFlags_NoSavedSettings);
    if (!popup_visible) {
        return;
    }
    if (g_rom_browser.close_requested) {
        g_rom_browser.open = false;
        g_rom_browser.close_requested = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    PushHeadingFont();
    ImGui::TextUnformatted("SELECT DIDDY KONG RACING ROM");
    PopHeadingFont();
    ImGui::TextDisabled("Folders first, then .z64, .v64 and .n64 files. Nothing leaves this PC.");
    ImGui::Dummy({0.0F, 8.0F});

    const std::string location = g_rom_browser.directory.empty()
        ? "This PC"
        : PathUtf8(g_rom_browser.directory);
    ImGui::TextWrapped("Current folder: %s", location.c_str());
    if (ImGui::Button("UP ONE LEVEL", {170.0F, 42.0F})) {
        RomBrowserBack();
    }
    ImGui::SameLine();
    if (ImGui::Button("THIS PC", {130.0F, 42.0F})) {
        g_rom_browser.directory.clear();
        RefreshRomBrowser();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.02F, 0.12F, 0.23F, 0.98F});
    BeginPaddedChild("game-pak-files", {0.0F, -118.0F}, true,
                     ImGuiWindowFlags_NavFlattened, {14.0F, 12.0F});
    if (!g_rom_browser.message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
        ImGui::TextWrapped("%s", g_rom_browser.message.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    if (g_rom_browser.entries.empty() && g_rom_browser.message.empty()) {
        ImGui::TextDisabled("No supported ROM files or folders were found here.");
    }
    for (std::size_t index = 0; index < g_rom_browser.entries.size(); ++index) {
        const BrowserEntry& entry = g_rom_browser.entries[index];
        std::string name = PathUtf8(entry.path.filename());
        if (name.empty()) {
            name = PathUtf8(entry.path);
        }
        const std::string label = entry.directory
            ? "[FOLDER]  " + name
            : "[ROM]     " + name;
        if (ImGui::Selectable(label.c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns, {0.0F, 38.0F})) {
            if (entry.directory) {
                g_rom_browser.directory = entry.path;
                RefreshRomBrowser();
                break;
            }
            if (AcceptRom(entry.path, selected, catalog, status)) {
                rom_ready = true;
                ImGui::CloseCurrentPopup();
                break;
            }
        }
        if (g_rom_browser.focus_first_entry && index == 0) {
            ImGui::SetItemDefaultFocus();
            g_rom_browser.focus_first_entry = false;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::TextDisabled("A / Cross  SELECT     B / Circle  BACK     D-pad / Stick  MOVE");
    if (ImGui::Button("CANCEL", {130.0F, 42.0F})) {
        g_rom_browser.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("SYSTEM FILE PICKER", {220.0F, 42.0F})) {
        if (SelectRomWithDialog(selected, catalog, status)) {
            rom_ready = true;
            g_rom_browser.open = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

bool BeginMainWindow(const char* name, ImGuiWindowFlags extra = 0) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    return ImGui::Begin(name, nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | extra);
}

void DrawLauncherBackdrop(LauncherBackgroundTexture& background,
                          SDL_Renderer* renderer, double scroll_distance,
                          bool software_return = false) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 viewport_max{origin.x + size.x, origin.y + size.y};

    // A quiet fallback keeps development builds usable if the packaged asset
    // is missing. Release packages always stage the cloud texture.
    if (background.texture == nullptr || background.width <= 0 ||
        background.height <= 0 || size.x <= 0.0F || size.y <= 0.0F) {
        draw->AddRectFilledMultiColor(
            origin, viewport_max, IM_COL32(28, 127, 224, 255),
            IM_COL32(62, 165, 239, 255), IM_COL32(93, 188, 242, 255),
            IM_COL32(55, 150, 231, 255));
        return;
    }

    // Scale uniformly from the viewport height. This fills every aspect ratio
    // without distorting the artwork; wide screens reveal additional mirrored
    // tiles while narrow screens naturally crop the sides.
    constexpr float kCoverOverscan = 1.03F;
    const float scale = (size.y / static_cast<float>(background.height)) *
                        kCoverOverscan;
    const float tile_width = static_cast<float>(background.width) * scale;
    const float tile_height = static_cast<float>(background.height) * scale;
    if (tile_width <= 0.0F || tile_height <= 0.0F) return;

#if !defined(__linux__)
    if (software_return) {
#endif
    const ImVec2 pixel_scale = ImGui::GetIO().DisplayFramebufferScale;
    PrepareLauncherBackground(background, renderer,
        static_cast<int>(std::ceil(tile_width * std::max(pixel_scale.x, 1.0F))),
        static_cast<int>(std::ceil(tile_height * std::max(pixel_scale.y, 1.0F))));
#if !defined(__linux__)
    }
#endif
    draw->PushClipRect(origin, viewport_max, true);
    for (const auto& tile : dkr::runtime::launcher::background_tiles(
             size.x, size.y, tile_width, tile_height, scroll_distance)) {
        SDL_Texture* texture = background.texture;
        bool flip_uv = tile.mirrored;
        if (background.scaled[0] != nullptr) {
            texture = background.scaled[tile.mirrored ? 1 : 0];
            flip_uv = false;
        } else if (tile.mirrored && background.mirrored != nullptr) {
            texture = background.mirrored;
            flip_uv = false;
        }
        draw->AddImage(reinterpret_cast<ImTextureID>(texture),
            {origin.x + tile.left, origin.y + tile.top},
            {origin.x + tile.right, origin.y + tile.bottom},
            {flip_uv ? 1.0F - tile.u0 : tile.u0, tile.v0},
            {flip_uv ? 1.0F - tile.u1 : tile.u1, tile.v1});
        ++background.tiles_submitted;
    }
    draw->PopClipRect();
}

void DrawRaceBadge(const char* label, const ImVec4& color, float width) {
    // Badges communicate status; they are deliberately not ImGui buttons so
    // keyboard/gamepad navigation never wastes a stop on non-actions.
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 badge_size{
        width > 0.0F ? width : text_size.x + 20.0F,
        30.0F};
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(position,
        {position.x + badge_size.x, position.y + badge_size.y},
        ImGui::ColorConvertFloat4ToU32(color), 15.0F);
    draw->AddText({position.x + std::max((badge_size.x - text_size.x) * 0.5F, 0.0F),
                   position.y + std::max((badge_size.y - text_size.y) * 0.5F, 0.0F)},
                  ImGui::GetColorU32(ImGuiCol_Text), label);
    ImGui::Dummy(badge_size);
}

void DrawStartingLights(bool ready) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    draw->AddRectFilled(cursor, {cursor.x + 42.0F, cursor.y + 112.0F},
                        IM_COL32(12, 25, 34, 255), 21.0F);
    const ImU32 off = IM_COL32(78, 91, 91, 255);
    draw->AddCircleFilled({cursor.x + 21.0F, cursor.y + 24.0F}, 9.0F,
                          ready ? off : IM_COL32(236, 54, 39, 255));
    draw->AddCircleFilled({cursor.x + 21.0F, cursor.y + 56.0F}, 9.0F,
                          ready ? off : IM_COL32(255, 174, 24, 255));
    draw->AddCircleFilled({cursor.x + 21.0F, cursor.y + 88.0F}, 9.0F,
                          ready ? IM_COL32(32, 218, 129, 255) : off);
    ImGui::Dummy({42.0F, 112.0F});
}

void BrandBlock(float available_width, float maximum_size = 230.0F,
                bool animate_as_coin = true) {
    const float phase = animate_as_coin
        ? static_cast<float>(UiAnimationSeconds()) *
              (2.0F * 3.14159265358979323846F / 4.8F)
        : 0.0F;
    const float facing = animate_as_coin ? std::cos(phase) : 1.0F;
    // Swap faces only while the coin is edge-on, so the change is hidden by
    // the deliberately narrow silhouette rather than flashing mid-rotation.
    const std::size_t face = animate_as_coin && facing < 0.0F ? 1U : 0U;
    if (g_brand_logo_rects[face] >= 0) {
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        const ImFontAtlasCustomRect* rect =
            atlas->GetCustomRectByIndex(g_brand_logo_rects[face]);
        if (rect != nullptr && rect->IsPacked() && atlas->TexID != nullptr &&
            atlas->TexWidth > 0 && atlas->TexHeight > 0) {
            const float block_size = std::max(
                std::min(available_width, maximum_size), 1.0F);
            const float natural_aspect = static_cast<float>(rect->Width) /
                static_cast<float>(rect->Height);
            float image_width = block_size;
            float image_height = image_width / natural_aspect;
            if (image_height > block_size) {
                image_height = block_size;
                image_width = image_height * natural_aspect;
            }
            const float face_width = image_width *
                (animate_as_coin ? 0.06F + 0.94F * std::abs(facing) : 1.0F);
            const float indent = std::max(
                (available_width - block_size) * 0.5F, 0.0F);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
            const ImVec2 block_min = ImGui::GetCursorScreenPos();
            const float image_left = block_min.x +
                (block_size - face_width) * 0.5F;
            const float image_top = block_min.y +
                (block_size - image_height) * 0.5F;
            ImVec2 uv_min{};
            ImVec2 uv_max{};
            atlas->CalcCustomRectUV(rect, &uv_min, &uv_max);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float perspective_tilt = animate_as_coin
                ? std::sin(phase) * image_height * 0.022F
                : 0.0F;
            const float brightness = animate_as_coin
                ? 0.72F + 0.28F * std::abs(facing)
                : 1.0F;
            const int tint = static_cast<int>(std::round(brightness * 255.0F));
            draw->AddImageQuad(
                atlas->TexID,
                {image_left, image_top + perspective_tilt},
                {image_left + face_width, image_top - perspective_tilt},
                {image_left + face_width,
                 image_top + image_height + perspective_tilt},
                {image_left, image_top + image_height - perspective_tilt},
                uv_min, {uv_max.x, uv_min.y}, uv_max,
                {uv_min.x, uv_max.y}, IM_COL32(tint, tint, tint, 255));
            ImGui::Dummy({block_size, block_size});
            return;
        }
    }

    // Keep startup usable when a development tree has not staged its visual
    // assets yet; packaged builds always carry both coin faces beside the runtime.
    ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
    PushHeadingFont();
    ImGui::TextUnformatted("DKR-R");
    PopHeadingFont();
    ImGui::PopStyleColor();
    PushHeadingFont(true);
    ImGui::TextWrapped("DIDDY KONG RACING RECOMPILED");
    PopHeadingFont(true);
}

struct MenuPanelLayout {
    float margin;
    float height;
    float sidebar_width;
    float content_x;
    float content_width;
    float padding;
    float inner_width;
};

MenuPanelLayout CalculateMenuPanelLayout(ImVec2 available) {
    const float width = std::floor(available.x);
    const float height = std::floor(available.y);
    const float margin = std::round(std::clamp(width * 0.022F, 16.0F, 34.0F));
    const float gap = std::round(std::clamp(width * 0.018F, 14.0F, 28.0F));
    const float minimum_sidebar = available.x < 980.0F ? 190.0F : 230.0F;
    const float sidebar = std::round(std::clamp(width * 0.235F, minimum_sidebar,
        std::max(minimum_sidebar, std::min(340.0F, width * 0.34F))));
    const float content_x = margin + sidebar + gap;
    const float content_width = std::max(std::floor(width - content_x - margin), 320.0F);
    const float padding = std::round(std::clamp(content_width * 0.045F, 20.0F, 44.0F));
    return {margin, std::max(std::floor(height - margin * 2.0F), 1.0F),
            sidebar, content_x, content_width, padding,
            std::max(content_width - padding * 2.0F, 1.0F)};
}

struct SidebarLayout {
    float padding = 24.0F;
    float logo_size = 190.0F;
    float button_height = 46.0F;
    float brand_gap = 18.0F;
    float action_section_gap = 16.0F;
    float action_gap = 8.0F;
    float item_spacing_y = 8.0F;
};

SidebarLayout CalculateSidebarLayout(float panel_height, float panel_width) {
    SidebarLayout result{};
    const bool compact = panel_height < 840.0F;
    result.padding = std::clamp(panel_height * 0.020F, 10.0F,
                                compact ? 16.0F : 24.0F);
    result.item_spacing_y = compact ? 4.0F : 8.0F;
    result.brand_gap = compact ? 10.0F : 18.0F;
    result.action_section_gap = compact ? 10.0F : 16.0F;
    result.action_gap = compact ? 4.0F : 8.0F;

    constexpr float kMinimumLogoSize = 64.0F;
    constexpr float kMinimumButtonHeight = 26.0F;
    constexpr float kButtonCount = static_cast<float>(kMenuPageCount + 2);
    constexpr float kItemSpacingCount = kButtonCount + 3.0F;
    const float desired_logo = std::min(
        compact ? 112.0F : 218.0F,
        std::max(panel_width - result.padding * 2.0F, kMinimumLogoSize));
    const float desired_button = compact ? 40.0F : 46.0F;
    const float fixed_height = result.padding * 2.0F + result.brand_gap +
        result.action_section_gap + result.action_gap +
        result.item_spacing_y * kItemSpacingCount;
    const float content_budget = std::max(panel_height - fixed_height, 1.0F);

    result.logo_size = std::min(
        desired_logo,
        std::max(kMinimumLogoSize,
                 content_budget - kButtonCount * kMinimumButtonHeight));
    result.button_height = std::min(
        desired_button,
        std::max(kMinimumButtonHeight,
                 (content_budget - result.logo_size) / kButtonCount));

    // If an unusually short window forces both controls to their minimum,
    // give the buttons priority and use the remaining height for the logo.
    const float used_height = result.logo_size +
        result.button_height * kButtonCount;
    if (used_height > content_budget) {
        result.logo_size = std::max(
            1.0F, content_budget - result.button_height * kButtonCount);
    }
    return result;
}

// The gap before RESTART / EXIT, with a faint hairline across its middle.
void SidebarActionGap(float width, float gap) {
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float previous_bottom = cursor.y - spacing;
    const float next_top = cursor.y + gap + spacing;
    const float y = std::floor((previous_bottom + next_top) * 0.5F);
    ImGui::GetWindowDrawList()->AddRectFilled(
        {cursor.x + 6.0F, y}, {cursor.x + width - 6.0F, y + 1.0F},
        ImGui::GetColorU32(IM_COL32(255, 171, 20, 89)));
    ImGui::Dummy({0.0F, gap});
}

// The current page's rail button: PLAY keeps its green, the others turn red,
// and a cream stripe marks the inner left edge without moving the label
// (.sidebar .race-button[aria-current="page"]::after).
void PushSidebarButtonColour(int page, bool current) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          page == 0 ? kAccent : current ? kRaceRed : kRaceBlue);
}

void DrawSidebarCurrentStripe() {
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float rounding = std::clamp(ImGui::GetStyle().FrameRounding, 0.0F,
                                      (maximum.y - minimum.y) * 0.5F);
    const ImVec2 inner_min{minimum.x + 1.0F, minimum.y + 1.0F};
    const ImVec2 inner_max{maximum.x - 1.0F, maximum.y - 1.0F};
    draw->PushClipRect(inner_min, {inner_min.x + 5.0F, inner_max.y}, true);
    draw->AddRectFilled(inner_min, inner_max, IM_COL32(7, 28, 39, 217),
                        std::max(rounding - 1.0F, 0.0F));
    draw->PopClipRect();
    draw->PushClipRect(inner_min, {inner_min.x + 3.0F, inner_max.y}, true);
    draw->AddRectFilled(inner_min, inner_max, ImGui::GetColorU32(kCream),
                        std::max(rounding - 1.0F, 0.0F));
    draw->PopClipRect();
}

bool SidebarButton(const char* label, int page, int& sidebar_selection,
                   float width = -1.0F, float height = 46.0F) {
    const bool current = sidebar_selection == page;
    PushSidebarButtonColour(page, current);
    const bool pressed = ImGui::Button(label, {width, height});
    ImGui::PopStyleColor();
    if (current) DrawSidebarCurrentStripe();
    if (pressed) {
        g_overlay_page = page;
        sidebar_selection = page;
        g_overlay_sidebar_selection.store(page, std::memory_order_release);
    }
    return pressed;
}

bool LauncherSidebarButton(const char* label, int target_page, int& page,
                           int& sidebar_selection, float width,
                           float height = 46.0F) {
    const bool current = sidebar_selection == target_page;
    PushSidebarButtonColour(target_page, current);
    const bool pressed = ImGui::Button(label, {width, height});
    ImGui::PopStyleColor();
    if (current) DrawSidebarCurrentStripe();
    if (pressed) {
        page = target_page;
        sidebar_selection = target_page;
    }
    return pressed;
}

const char* SupportPresentationName() {
    return dkr::runtime::enhancements::presentation_profile() ==
                   dkr::runtime::enhancements::PresentationProfile::Modern
               ? "Modern"
               : "Accurate";
}

const char* SupportGraphicsApiName(GraphicsApi api) {
    if (api == GraphicsApi::D3D12) return "Direct3D 12";
    if (api == GraphicsApi::Vulkan) return "Vulkan";
#if defined(__APPLE__)
    if (api != GraphicsApi::Auto) return "Metal";
#endif
    return "Automatic";
}

std::size_t EnabledTexturePackCount() {
    static std::uint64_t cached_generation = 0U;
    static std::size_t cached_count = 0U;
    const std::uint64_t current_generation =
        dkr::runtime::texture_packs::generation();
    if (current_generation != cached_generation) {
        const auto packs = dkr::runtime::texture_packs::snapshot(true);
        cached_count = static_cast<std::size_t>(std::count_if(
            packs.begin(), packs.end(),
            [](const dkr::runtime::texture_packs::PackInfo& pack) {
                return pack.enabled;
            }));
        cached_generation = current_generation;
    }
    return cached_count;
}

void PollSupportSystemSummary() {
    if (!g_support_summary_requested) {
        g_support_summary_requested = true;
        g_support_summary_future = std::async(
            std::launch::async,
            [] { return dkr::runtime::support::collect_system_summary(); });
    }
    if (g_support_summary || !g_support_summary_future.valid() ||
        g_support_summary_future.wait_for(std::chrono::milliseconds(0)) !=
            std::future_status::ready) {
        return;
    }
    try {
        g_support_summary = g_support_summary_future.get();
    } catch (...) {
        g_support_action_status =
            "System details could not be collected. Settings can still be exported.";
    }
}

std::string BuildSupportReport() {
    const GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    const bool modern = dkr::runtime::enhancements::presentation_profile() ==
                        dkr::runtime::enhancements::PresentationProfile::Modern;
    std::ostringstream report;
    report << "DKR-R Support Summary\n"
           << "=====================\n"
           << "Release: " << DKR_RELEASE_VERSION << '\n'
           << "Presentation style: " << SupportPresentationName() << '\n'
           << "Window mode: "
           << (static_cast<int>(config.wm_option) == 1 ? "Fullscreen"
                                                       : "Windowed")
           << '\n'
           << "Aspect ratio: "
           << (static_cast<int>(config.ar_option) == 0 ? "Original 4:3"
                                                       : "Fit to window")
           << '\n'
           << "Graphics API: " << SupportGraphicsApiName(config.api_option)
           << '\n'
           << "Presentation target: ";
    if (!modern) {
        report << "Original 30 FPS\n";
    } else if (config.rr_option == RefreshRate::Manual) {
        report << std::clamp(config.rr_manual_value, 30, 500) << " FPS\n";
    } else {
        report << "Match display\n";
    }
    report << "HUD size: "
           << std::lround(dkr::runtime::hud::global_scale() * 100.0F) << "%\n"
           << "Enabled texture packs: " << EnabledTexturePackCount() << '\n'
           << "Online synchronization: "
           << dkr::runtime::netplay::synchronization_name(CurrentOnlineSynchronization())
           << '\n'
           << "Diagnostic logging: "
           << (dkr::runtime::support::diagnostic_logging_enabled() ? "On"
                                                                   : "Off")
           << '\n'
           << "Crash dumps: "
           << (dkr::runtime::support::crash_dumps_enabled() ? "On" : "Off")
           << "\n\nSystem\n------\n";
    if (g_support_summary) {
        report << "Operating system: " << g_support_summary->operating_system
               << '\n'
               << "CPU: " << g_support_summary->cpu << '\n'
               << "Memory: " << g_support_summary->memory << '\n'
               << "GPU: " << g_support_summary->gpu << '\n'
               << "Boot drive type: " << g_support_summary->boot_drive << '\n'
               << "DKR-R drive type: "
               << g_support_summary->application_drive << '\n';
    } else {
        report << "System details are still being collected.\n";
    }
    report << "\nPrivacy\n-------\n"
           << "This report intentionally excludes Game Pak paths, save data, "
              "usernames, device identifiers, friend codes and lobby codes.\n";
    return report.str();
}

void DrawComingSoonPage(const char* heading, const char* card_id,
                        const char* description, const char* workshop_note,
                        float width) {
    DrawPageHeading(heading);
    ImGui::TextDisabled("A new route is being prepared for DKR-R.");
    ImGui::Dummy({0.0F, 16.0F});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.045F, 0.18F, 0.25F, 0.96F});
    BeginPaddedChild(card_id, {width, 286.0F}, true,
                     ImGuiWindowFlags_NoScrollbar, {24.0F, 22.0F});
    ImGui::PushTextWrapPos(std::max(width - 24.0F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
    PushHeadingFont(true);
    ImGui::TextUnformatted("COMING SOON");
    PopHeadingFont(true);
    ImGui::PopStyleColor();
    ImGui::Dummy({0.0F, 10.0F});
    ImGui::TextWrapped("%s", description);
    ImGui::Dummy({0.0F, 14.0F});
    ImGui::Separator();
    ImGui::Dummy({0.0F, 10.0F});
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", workshop_note);
    ImGui::TextWrapped("This feature is not finished cooking yet, so it stays safely parked for this release.");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

dkr::runtime::netplay::CompatibilityManifest BuildNetplayManifest(
    const dkr::runtime::rom::Identity& identity) {
    using namespace dkr::runtime::netplay;
    CompatibilityManifest manifest{};
    // Network compatibility comes from the repository VERSION and network ABI,
    // not a platform-specific package/RC label. The human-facing launcher can
    // still display DKR_RELEASE_VERSION independently.
    manifest.release_version = canonical_network_release(
        DKR_NETWORK_RELEASE_VERSION);
    manifest.build_fingerprint = canonical_network_build_fingerprint(
        DKR_NETWORK_RELEASE_VERSION);
    manifest.revision = identity.revision == dkr::runtime::rom::Revision::UsV80
        ? Revision::UsV80 : Revision::UsV77;
    manifest.canonical_rom_hash = identity.canonical_xxh3;
    // CMake derives this digest directly from the checked Patch Pipeline
    // policy used for this revision. Admission therefore fails before launch
    // whenever authored hook or instruction policy differs between builds.
    manifest.patch_policy_hash = stable_hash(DKR_PATCH_POLICY_SHA256);
    manifest.magic_codes_hash = dkr::runtime::magic_codes::selected_mask();
    manifest.gameplay_settings_hash = stable_hash(
        std::to_string(manifest.magic_codes_hash) + ":retail-simulation-30");
    std::vector<std::uint8_t> canonical_save;
    std::string save_error;
    if (dkr::runtime::saves::canonical_adventure_bytes(
            canonical_save, save_error)) {
        manifest.session_save_hash = stable_hash(std::string_view(
            reinterpret_cast<const char*>(canonical_save.data()),
            canonical_save.size()));
    }
    manifest.simulation_rate = 30U;
#if defined(_M_X64) || defined(__x86_64__)
    manifest.architecture = "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    manifest.architecture = "arm64";
#else
    manifest.architecture = "unknown";
#endif
    manifest.floating_point_mode = "strict-ieee754-v1";
    return manifest;
}

const char* OnlineRouteName(dkr::runtime::netplay::Route route) {
    using dkr::runtime::netplay::Route;
    switch (route) {
    case Route::Lan: return "LAN";
    case Route::Direct: return "DIRECT";
    case Route::Relay: return "RELAY";
    default: return "MEASURING";
    }
}

std::string NormalizeOnlineInvite(std::string_view invite) {
    std::string unquoted;
    unquoted.reserve(invite.size());
    for (const char character : invite) {
        if (!std::isspace(static_cast<unsigned char>(character))) {
            unquoted.push_back(character);
        }
    }
    if (unquoted.size() >= 2U &&
        ((unquoted.front() == '"' && unquoted.back() == '"') ||
         (unquoted.front() == '\'' && unquoted.back() == '\''))) {
        unquoted = unquoted.substr(1U, unquoted.size() - 2U);
    }

    std::string normalized;
    normalized.reserve(unquoted.size());
    for (const char character : unquoted) {
        if (character == '-') continue;
        normalized.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(character))));
    }
    return normalized;
}

std::string OnlineInviteLobbyToken(std::string_view invite) {
    const std::string normalized = NormalizeOnlineInvite(invite);
    return dkr::runtime::netplay::valid_quick_join_code(normalized)
        ? normalized : std::string{};
}

bool SetOnlineInvite(std::string_view invite) {
    const std::string normalized = NormalizeOnlineInvite(invite);
    if (normalized.empty() || normalized.size() >= sizeof(g_online_invite) ||
        !dkr::runtime::netplay::valid_quick_join_code(normalized)) {
        g_online_invite[0] = '\0';
        return false;
    }
    const std::size_t length = normalized.size();
    std::memcpy(g_online_invite, normalized.data(), length);
    g_online_invite[length] = '\0';
    return true;
}

void RequestOnlineCodeKeyboard() {
    constexpr std::string_view kCodeAlphabet =
        "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    const std::string normalized = NormalizeOnlineInvite(g_online_invite);
    std::size_t length = 0U;
    for (const char character : normalized) {
        if (length >= sizeof(g_online_code_entry) - 1U) break;
        if (kCodeAlphabet.find(character) != std::string_view::npos) {
            g_online_code_entry[length++] = character;
        }
    }
    g_online_code_entry[length] = '\0';
    g_online_code_keyboard_pending = true;
}

int FilterOnlineCodeCharacter(ImGuiInputTextCallbackData* data) {
    constexpr std::string_view kCodeAlphabet =
        "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    if (data == nullptr || data->EventChar > 0x7FU) return 1;
    const char character = static_cast<char>(std::toupper(
        static_cast<unsigned char>(data->EventChar)));
    if (kCodeAlphabet.find(character) == std::string_view::npos) return 1;
    data->EventChar = static_cast<ImWchar>(character);
    return 0;
}

void AppendOnlineCodeCharacter(char character) {
    const std::size_t length = std::strlen(g_online_code_entry);
    if (length >= sizeof(g_online_code_entry) - 1U) return;
    g_online_code_entry[length] = character;
    g_online_code_entry[length + 1U] = '\0';
}

void BackspaceOnlineCodeCharacter() {
    const std::size_t length = std::strlen(g_online_code_entry);
    if (length > 0U) g_online_code_entry[length - 1U] = '\0';
}

bool CopyOnlineInviteToClipboard(std::string_view invite) {
    const std::string normalized = NormalizeOnlineInvite(invite);
    if (normalized.empty() || SDL_SetClipboardText(normalized.c_str()) != 0) {
        g_online_action_status = std::string("The Quick Join code was not copied: ") +
            SDL_GetError();
        return false;
    }
    char* clipboard = SDL_GetClipboardText();
    const std::string copied = clipboard != nullptr
        ? NormalizeOnlineInvite(clipboard) : std::string{};
    if (clipboard != nullptr) SDL_free(clipboard);
    if (copied != normalized) {
        g_online_action_status =
            "The system clipboard did not retain the Quick Join code. Nothing was shared; copy it again.";
        return false;
    }
    g_online_action_status = "Quick Join code copied and verified for lobby " +
        OnlineInviteLobbyToken(normalized) + ".";
    return true;
}

bool PasteOnlineInviteFromClipboard() {
    char* clipboard = SDL_GetClipboardText();
    const std::string pasted = clipboard != nullptr
        ? NormalizeOnlineInvite(clipboard) : std::string{};
    if (clipboard != nullptr) SDL_free(clipboard);
    if (!SetOnlineInvite(pasted)) {
        g_online_action_status =
            "The clipboard does not contain a valid five-character Quick Join code.";
        return false;
    }
    g_online_action_status = "Quick Join code " +
        OnlineInviteLobbyToken(g_online_invite) +
        " verified. Request host approval when ready.";
    return true;
}

bool FinishOnlineLobby(const OnlineHostRequest& request,std::string mod_digest,std::uint32_t manifest_bytes) {
    using namespace dkr::runtime::netplay;
    const auto& rules=request.rules;
    // Fail before creating/changing an online save directory. A new label
    // must never silently start the old implementation under an experiment.
    if (const char* reason = experimental::runtime_admission_error(rules.synchronization,session().owned_backend_available())) {
        g_online_action_status = reason;
        return false;
    }
    std::string error;
    std::vector<std::uint8_t> online_save;
    auto manifest=request.manifest;manifest.mod_manifest_hash=mod_digest;
    session().configure_manifest(manifest);
    if(!session().configure_mod_admission(std::move(mod_digest),manifest_bytes)) {
        g_online_action_status="The lobby changed while host mods were being prepared. Nothing was activated.";return false;
    }
    const auto save_mode = request.save_mode;
    if (!dkr::runtime::saves::prepare_host_online_adventure(
            save_mode, online_save, error,manifest.mod_manifest_hash)) {
        g_online_action_status = error;
        return false;
    }
    session().configure_session_save(
        std::move(online_save),
        dkr::runtime::saves::install_synchronized_online_adventure,
        dkr::runtime::saves::read_online_adventure);
    if (!session().host(0U, {}, request.room,
                        ConnectionMethod::QuickJoin, request.name,
                        rules, error)) {
        dkr::runtime::saves::discard_staged_host_online_adventure();
        g_online_action_status = error;
        return false;
    }
    if(!dkr::runtime::saves::bind_staged_host_online_adventure(session().view().match_id,error)) {
        session().disconnect(error);dkr::runtime::saves::discard_staged_host_online_adventure();
        g_online_action_status=error;return false;
    }
    return true;
}

#include "runtime_online_mods_ui.inl"

struct TextEntrySpec {
    char* value = nullptr;
    std::size_t capacity = 0U;
    const char* heading = "ENTER TEXT";
    const char* hint = "TEXT";
    const char* accept = "SAVE";
    bool allow_empty = false;
};

TextEntrySpec GetTextEntrySpec(TextEntryTarget target) {
    switch (target) {
    case TextEntryTarget::HudPresetName:
        return {g_hud_preset_name, sizeof(g_hud_preset_name),
                "NAME HUD PRESET", "PRESET NAME", "SAVE PRESET", false};
    case TextEntryTarget::RacerName:
        return {g_online_player_name, sizeof(g_online_player_name),
                "ENTER RACER NAME", "RACER NAME", "USE RACER NAME", false};
    case TextEntryTarget::LobbyName:
        return {g_online_room_name, sizeof(g_online_room_name),
                "ENTER LOBBY NAME", "LOBBY NAME", "USE LOBBY NAME", false};
    case TextEntryTarget::OnlineProfileName:
        return {g_online_profile_name, sizeof(g_online_profile_name),
                "ENTER DISPLAY NAME", "DISPLAY NAME", "USE DISPLAY NAME", false};
    case TextEntryTarget::FriendNickname:
        return {g_friend_nickname, sizeof(g_friend_nickname),
                "SET FRIEND NICKNAME", "FRIEND NICKNAME", "SAVE NICKNAME", true};
    case TextEntryTarget::TexturePackSearch:
        return {g_texture_pack_search, sizeof(g_texture_pack_search),
                "SEARCH TEXTURE PACKS", "TEXTURE PACK NAME", "APPLY SEARCH", true};
    case TextEntryTarget::CustomTrackSearch:
        return {g_mod_browsers[0].search,sizeof(g_mod_browsers[0].search),
                "SEARCH CUSTOM TRACKS","TRACK OR PACK NAME","APPLY SEARCH",true};
    case TextEntryTarget::CustomCharacterSearch:
        return {g_mod_browsers[1].search,sizeof(g_mod_browsers[1].search),
                "SEARCH CUSTOM CHARACTERS","CHARACTER OR PACK NAME","APPLY SEARCH",true};
    case TextEntryTarget::None:
        break;
    }
    return {};
}

void RequestTextEntryKeyboard(TextEntryTarget target) {
    const TextEntrySpec spec = GetTextEntrySpec(target);
    if (spec.value == nullptr || spec.capacity == 0U) return;
    const std::size_t length = std::min(
        std::strlen(spec.value), sizeof(g_text_entry_edit) - 1U);
    std::memcpy(g_text_entry_edit, spec.value, length);
    g_text_entry_edit[length] = '\0';
    g_text_entry_target = target;
    g_text_entry_keyboard_pending = true;
}

void CommitTextEntry() {
    const TextEntryTarget target = g_text_entry_target;
    const TextEntrySpec spec = GetTextEntrySpec(target);
    if (spec.value == nullptr || spec.capacity == 0U) return;
    const std::size_t length = std::min(
        std::strlen(g_text_entry_edit), spec.capacity - 1U);
    std::memcpy(spec.value, g_text_entry_edit, length);
    spec.value[length] = '\0';

    if (target == TextEntryTarget::HudPresetName)
        dkr::runtime::hud::editor::accept_preset_name(g_hud_preset_name);

    if (target == TextEntryTarget::FriendNickname) {
        std::string error;
        if (!dkr::runtime::netplay::friend_service().set_friend_nickname(
                g_friend_action_identity, g_friend_nickname, error)) {
            g_online_action_status = error;
        } else {
            g_online_action_status = "Friend nickname saved.";
        }
    } else if (target == TextEntryTarget::RacerName ||
               target == TextEntryTarget::LobbyName) {
        SaveSettings();
    }
}

void DrawTextEntryKeyboard() {
    constexpr const char* kPopupName = "ENTER DKR-R TEXT";
    constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.\'";
    constexpr int kColumns = 8;

    if (g_text_entry_keyboard_pending) {
        ImGui::OpenPopup(kPopupName);
        g_text_entry_keyboard_pending = false;
    }
    if (g_text_entry_target == TextEntryTarget::None &&
        !ImGui::IsPopupOpen(kPopupName)) {
        return;
    }

    const TextEntrySpec spec = GetTextEntrySpec(g_text_entry_target);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool compact = display.y < 700.0F;
    ImGui::SetNextWindowSize(
        {std::min(760.0F, std::max(display.x - 32.0F, 1.0F)),
         std::min(690.0F, std::max(display.y - 32.0F, 1.0F))},
        ImGuiCond_Appearing);
    if (!BeginPaddedModal(kPopupName,
                          ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    PushHeadingFont();
    ImGui::TextUnformatted(spec.heading);
    PopHeadingFont();
    ImGui::TextWrapped(
        "Use the D-pad and A button to enter text. Every letter and number "
        "is available; spaces and common name characters are included too.");
    ImGui::Dummy({0.0F, 8.0F});
    ImGui::SetNextItemWidth(-1.0F);
    {
        const ControlFontScope scope;
        ImGui::InputTextWithHint("##general-text-entry", spec.hint,
                                 g_text_entry_edit,
                                 sizeof(g_text_entry_edit));
    }
    ImGui::Dummy({0.0F, 8.0F});

    const float spacing = 8.0F;
    const float key_width = std::max(
        (ImGui::GetContentRegionAvail().x -
         spacing * static_cast<float>(kColumns - 1)) /
            static_cast<float>(kColumns),
        1.0F);
    const float key_height = compact ? 34.0F : 42.0F;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {spacing, spacing});
    for (std::size_t index = 0U; index < kAlphabet.size(); ++index) {
        char key_label[2]{kAlphabet[index], '\0'};
        if (ImGui::Button(key_label, {key_width, key_height})) {
            const std::size_t length = std::strlen(g_text_entry_edit);
            if (length < sizeof(g_text_entry_edit) - 1U) {
                g_text_entry_edit[length] = kAlphabet[index];
                g_text_entry_edit[length + 1U] = '\0';
            }
        }
        if (index == 0U && ImGui::IsWindowAppearing()) {
            ImGui::SetItemDefaultFocus();
        }
        if ((index + 1U) % static_cast<std::size_t>(kColumns) != 0U) {
            ImGui::SameLine();
        }
    }
    ImGui::PopStyleVar();

    ImGui::Dummy({0.0F, 8.0F});
    const float action_gap = ImGui::GetStyle().ItemSpacing.x;
    const float action_region_width = ImGui::GetContentRegionAvail().x;
    const float edit_action_width = std::max(
        (action_region_width - action_gap * 2.0F) / 3.0F, 1.0F);
    const float finish_action_width = std::max(
        (action_region_width - action_gap) * 0.5F, 1.0F);
    const float action_height = compact ? 38.0F : 44.0F;
    if (ImGui::Button("SPACE", {edit_action_width, action_height})) {
        const std::size_t length = std::strlen(g_text_entry_edit);
        if (length < sizeof(g_text_entry_edit) - 1U) {
            g_text_entry_edit[length] = ' ';
            g_text_entry_edit[length + 1U] = '\0';
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("BACKSPACE", {edit_action_width, action_height})) {
        const std::size_t length = std::strlen(g_text_entry_edit);
        if (length > 0U) g_text_entry_edit[length - 1U] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("CLEAR", {edit_action_width, action_height})) {
        g_text_entry_edit[0] = '\0';
    }
    ImGui::Dummy({0.0F, 4.0F});
    if (ImGui::Button("CANCEL", {finish_action_width, action_height})) {
        g_text_entry_target = TextEntryTarget::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    const bool can_accept = spec.allow_empty || g_text_entry_edit[0] != '\0';
    ImGui::BeginDisabled(!can_accept);
    ImGui::PushStyleColor(ImGuiCol_Button, kRaceRed);
    if (ImGui::Button(spec.accept, {finish_action_width, action_height})) {
        CommitTextEntry();
        g_text_entry_target = TextEntryTarget::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void DrawOnlineCodeKeyboard() {
    constexpr const char* kPopupName = "ENTER QUICK JOIN CODE";
    constexpr std::string_view kCodeAlphabet =
        "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    constexpr int kColumns = 8;

    if (g_online_code_keyboard_pending) {
        ImGui::OpenPopup(kPopupName);
        g_online_code_keyboard_pending = false;
    }

    const ImVec2 display_size = ImGui::GetIO().DisplaySize;
    const bool compact_keyboard = display_size.y < 700.0F;
    ImGui::SetNextWindowSize(
        {std::min(700.0F, std::max(display_size.x - 32.0F, 1.0F)),
         std::min(640.0F, std::max(display_size.y - 32.0F, 1.0F))},
        ImGuiCond_Appearing);
    if (!BeginPaddedModal(
            kPopupName,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (!ImGui::IsPopupOpen(kPopupName)) {
            g_online_code_keyboard_visible.store(false,
                                                  std::memory_order_release);
        }
        return;
    }

    g_online_code_keyboard_visible.store(true, std::memory_order_release);
    if (g_online_code_keyboard_cancel_requested.exchange(
            false, std::memory_order_acq_rel)) {
        ImGui::CloseCurrentPopup();
        g_online_code_keyboard_visible.store(false,
                                              std::memory_order_release);
        ImGui::EndPopup();
        return;
    }

    PushHeadingFont();
    ImGui::TextUnformatted("ENTER QUICK JOIN CODE");
    PopHeadingFont();
    ImGui::TextWrapped(
        "Use the D-pad to choose each character and press A to enter it. "
        "Quick Join avoids I, O, 1 and 0 so codes are easy to read.");
    ImGui::Dummy({0.0F, 8.0F});

    ImGui::SetNextItemWidth(-1.0F);
    {
        const ControlFontScope scope;
        ImGui::InputTextWithHint(
            "##quick-join-code-entry", "FIVE CHARACTERS",
            g_online_code_entry, sizeof(g_online_code_entry),
            ImGuiInputTextFlags_CharsUppercase |
                ImGuiInputTextFlags_CallbackCharFilter,
            FilterOnlineCodeCharacter);
    }
    ImGui::Dummy({0.0F, 8.0F});

    const float spacing = 8.0F;
    const float keyboard_width = ImGui::GetContentRegionAvail().x;
    const float key_width = std::max(
        (keyboard_width - spacing * static_cast<float>(kColumns - 1)) /
            static_cast<float>(kColumns),
        1.0F);
    const float key_height = compact_keyboard ? 34.0F : 44.0F;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {spacing, spacing});
    bool first_key = true;
    for (std::size_t index = 0U; index < kCodeAlphabet.size(); ++index) {
        char label[2]{kCodeAlphabet[index], '\0'};
        if (ImGui::Button(label, {key_width, key_height})) {
            AppendOnlineCodeCharacter(kCodeAlphabet[index]);
        }
        if (first_key && ImGui::IsWindowAppearing()) {
            ImGui::SetItemDefaultFocus();
            first_key = false;
        }
        if ((index + 1U) % static_cast<std::size_t>(kColumns) != 0U) {
            ImGui::SameLine();
        }
    }
    ImGui::PopStyleVar();

    ImGui::Dummy({0.0F, 8.0F});
    constexpr const char* backspace_label = "BACKSPACE";
    constexpr const char* clear_label = "CLEAR";
    constexpr const char* cancel_label = "CANCEL";
    constexpr const char* accept_label = "USE CODE";
    const float action_spacing = ImGui::GetStyle().ItemSpacing.x;
    const float action_width = std::max(
        (ImGui::GetContentRegionAvail().x - action_spacing * 3.0F) * 0.25F,
        1.0F);
    const float action_height = compact_keyboard ? 38.0F : 44.0F;
    if (ImGui::Button(backspace_label, {action_width, action_height})) {
        BackspaceOnlineCodeCharacter();
    }
    ImGui::SameLine();
    if (ImGui::Button(clear_label, {action_width, action_height})) {
        g_online_code_entry[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button(cancel_label, {action_width, action_height})) {
        ImGui::CloseCurrentPopup();
        g_online_code_keyboard_visible.store(false,
                                              std::memory_order_release);
    }
    ImGui::SameLine();
    const bool complete = dkr::runtime::netplay::valid_quick_join_code(
        g_online_code_entry);
    ImGui::BeginDisabled(!complete);
    ImGui::PushStyleColor(ImGuiCol_Button, kRaceRed);
    if (ImGui::Button(accept_label, {action_width, action_height})) {
        SetOnlineInvite(g_online_code_entry);
        ImGui::CloseCurrentPopup();
        g_online_code_keyboard_visible.store(false,
                                              std::memory_order_release);
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void CopyFriendCodeToClipboard(std::string_view code) {
    const std::string normalized =
        dkr::runtime::netplay::normalize_friend_code(code);
    const std::string clipboard_text(code);
    if (normalized.empty() || SDL_SetClipboardText(clipboard_text.c_str()) != 0) {
        g_online_action_status = "The Friend Code could not be copied.";
        return;
    }
    g_online_action_status = "Friend Code copied. Share it only with racers you trust.";
}

void PasteFriendCodeFromClipboard() {
    char* clipboard = SDL_GetClipboardText();
    if (clipboard == nullptr) {
        g_online_action_status = "The clipboard does not contain a Friend Code.";
        return;
    }
    const std::string normalized =
        dkr::runtime::netplay::normalize_friend_code(clipboard);
    SDL_free(clipboard);
    if (normalized.empty() || normalized.size() >= sizeof(g_friend_code_entry)) {
        g_online_action_status = "The clipboard does not contain a valid Friend Code.";
        return;
    }
    if (normalized.size() == 8U) {
        const std::string formatted = "DKR-" + normalized;
        std::memcpy(g_friend_code_entry, formatted.data(), formatted.size());
        g_friend_code_entry[formatted.size()] = '\0';
    } else {
        // Legacy codes remain pasteable even though new codes use the short
        // controller-friendly format.
        std::memcpy(g_friend_code_entry, normalized.data(), normalized.size());
        g_friend_code_entry[normalized.size()] = '\0';
    }
}

void DrawFriendCodeKeyboard() {
    constexpr const char* kPopupName = "ADD A DKR-R FRIEND";
    constexpr std::string_view kAlphabet =
        "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    constexpr int kColumns = 8;
    if (g_friend_code_keyboard_pending) {
        if (g_friend_code_entry[0] == '\0') {
            std::memcpy(g_friend_code_entry, "DKR-", 5U);
        }
        ImGui::OpenPopup(kPopupName);
        g_friend_code_keyboard_pending = false;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(
        {std::min(760.0F, std::max(display.x - 32.0F, 1.0F)),
         std::min(690.0F, std::max(display.y - 32.0F, 1.0F))},
        ImGuiCond_Appearing);
    if (!BeginPaddedModal(kPopupName,
                          ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    PushHeadingFont();
    ImGui::TextUnformatted("ADD A DKR-R FRIEND");
    PopHeadingFont();
    ImGui::TextWrapped(
        "Paste the secure Friend Code, or enter its eight characters with "
        "the controller. DKR-R supplies the DKR- prefix automatically. "
        "The request is delivered when both racers are online.");
    ImGui::SetNextItemWidth(-1.0F);
    {
        const ControlFontScope scope;
        ImGui::InputText("##friend-code", g_friend_code_entry,
                         sizeof(g_friend_code_entry),
                         ImGuiInputTextFlags_CharsUppercase);
    }
    const float spacing = 8.0F;
    const float key_width = std::max(
        (ImGui::GetContentRegionAvail().x - spacing * (kColumns - 1)) /
            static_cast<float>(kColumns),
        1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {spacing, spacing});
    for (std::size_t index = 0U; index < kAlphabet.size(); ++index) {
        char label[2]{kAlphabet[index], '\0'};
        if (ImGui::Button(label, {key_width, 42.0F})) {
            const std::size_t length = std::strlen(g_friend_code_entry);
            const bool short_entry = std::string_view(g_friend_code_entry)
                                         .starts_with("DKR-");
            if (length + 1U < sizeof(g_friend_code_entry) &&
                (!short_entry || length < 12U)) {
                g_friend_code_entry[length] = kAlphabet[index];
                g_friend_code_entry[length + 1U] = '\0';
            }
        }
        if (index == 0U && ImGui::IsWindowAppearing()) {
            ImGui::SetItemDefaultFocus();
        }
        if ((index + 1U) % kColumns != 0U) ImGui::SameLine();
    }
    ImGui::PopStyleVar();
    ImGui::Dummy({0.0F, 6.0F});
    const float action_gap = ImGui::GetStyle().ItemSpacing.x;
    const float action_width = std::max(
        (ImGui::GetContentRegionAvail().x - action_gap * 3.0F) * 0.25F,
        1.0F);
    if (ImGui::Button("PASTE", {action_width, 42.0F})) {
        PasteFriendCodeFromClipboard();
    }
    ImGui::SameLine();
    if (ImGui::Button("BACKSPACE", {action_width, 42.0F})) {
        const std::size_t length = std::strlen(g_friend_code_entry);
        const std::size_t minimum =
            std::string_view(g_friend_code_entry).starts_with("DKR-") ? 4U : 0U;
        if (length > minimum) g_friend_code_entry[length - 1U] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("CANCEL", {action_width, 42.0F})) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    const bool valid = dkr::runtime::netplay::valid_friend_code(
        g_friend_code_entry);
    ImGui::BeginDisabled(!valid);
    ImGui::PushStyleColor(ImGuiCol_Button, kRaceRed);
    if (ImGui::Button("SEND REQUEST", {action_width, 42.0F})) {
        std::string error;
        if (dkr::runtime::netplay::friend_service().submit_friend_code(
                g_friend_code_entry, error)) {
            g_friend_code_entry[0] = '\0';
            g_online_action_status =
                "Friend request saved locally. Its status will confirm when the racer receives it.";
            ImGui::CloseCurrentPopup();
        } else {
            g_online_action_status = error;
        }
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void RequestFriendSearchKeyboard() {
    std::memcpy(g_friend_search_edit, g_friend_search,
                sizeof(g_friend_search_edit));
    g_friend_search_edit[sizeof(g_friend_search_edit) - 1U] = '\0';
    g_friend_search_keyboard_pending = true;
}

void AppendFriendSearchCharacter(char character) {
    const std::size_t length = std::strlen(g_friend_search_edit);
    if (length >= sizeof(g_friend_search_edit) - 1U) return;
    g_friend_search_edit[length] = character;
    g_friend_search_edit[length + 1U] = '\0';
}

void DrawFriendSearchKeyboard() {
    constexpr const char* kPopupName = "SEARCH DKR-R FRIENDS";
    constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.'";
    constexpr int kColumns = 8;

    if (g_friend_search_keyboard_pending) {
        ImGui::OpenPopup(kPopupName);
        g_friend_search_keyboard_pending = false;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool compact = display.y < 700.0F;
    ImGui::SetNextWindowSize(
        {std::min(760.0F, std::max(display.x - 32.0F, 1.0F)),
         std::min(690.0F, std::max(display.y - 32.0F, 1.0F))},
        ImGuiCond_Appearing);
    if (!BeginPaddedModal(kPopupName,
                          ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    PushHeadingFont();
    ImGui::TextUnformatted("SEARCH DKR-R FRIENDS");
    PopHeadingFont();
    ImGui::TextWrapped(
        "Use the D-pad and A button to enter a racer's name or nickname. "
        "Every letter and number is available; spaces and common name "
        "characters are included too.");
    ImGui::Dummy({0.0F, 8.0F});
    ImGui::SetNextItemWidth(-1.0F);
    {
        const ControlFontScope scope;
        ImGui::InputTextWithHint("##friend-search-edit", "FRIEND NAME",
                                 g_friend_search_edit,
                                 sizeof(g_friend_search_edit));
    }
    ImGui::Dummy({0.0F, 8.0F});

    const float spacing = 8.0F;
    const float key_width = std::max(
        (ImGui::GetContentRegionAvail().x -
         spacing * static_cast<float>(kColumns - 1)) /
            static_cast<float>(kColumns),
        1.0F);
    const float key_height = compact ? 34.0F : 42.0F;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {spacing, spacing});
    for (std::size_t index = 0U; index < kAlphabet.size(); ++index) {
        char label[2]{kAlphabet[index], '\0'};
        if (ImGui::Button(label, {key_width, key_height})) {
            AppendFriendSearchCharacter(kAlphabet[index]);
        }
        if (index == 0U && ImGui::IsWindowAppearing()) {
            ImGui::SetItemDefaultFocus();
        }
        if ((index + 1U) % static_cast<std::size_t>(kColumns) != 0U) {
            ImGui::SameLine();
        }
    }
    ImGui::PopStyleVar();

    ImGui::Dummy({0.0F, 8.0F});
    constexpr const char* space_label = "SPACE";
    constexpr const char* backspace_label = "BACKSPACE";
    constexpr const char* clear_label = "CLEAR";
    constexpr const char* cancel_label = "CANCEL";
    constexpr const char* search_label = "SEARCH";
    const float action_gap = ImGui::GetStyle().ItemSpacing.x;
    const float action_width = std::max(
        (ImGui::GetContentRegionAvail().x - action_gap * 4.0F) * 0.20F,
        1.0F);
    const float action_height = compact ? 38.0F : 44.0F;
    if (ImGui::Button(space_label, {action_width, action_height})) {
        AppendFriendSearchCharacter(' ');
    }
    ImGui::SameLine();
    if (ImGui::Button(backspace_label, {action_width, action_height})) {
        const std::size_t length = std::strlen(g_friend_search_edit);
        if (length > 0U) g_friend_search_edit[length - 1U] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button(clear_label, {action_width, action_height})) {
        g_friend_search_edit[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button(cancel_label, {action_width, action_height})) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, kRaceRed);
    if (ImGui::Button(search_label, {action_width, action_height})) {
        std::memcpy(g_friend_search, g_friend_search_edit,
                    sizeof(g_friend_search));
        g_friend_search[sizeof(g_friend_search) - 1U] = '\0';
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor();
    ImGui::EndPopup();
}

std::string FriendDisplayLabel(const dkr::runtime::netplay::FriendView& racer) {
    return racer.nickname.empty() ? racer.display_name : racer.nickname;
}

std::string LowerAscii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](char value) {
        return static_cast<char>(std::tolower(
            static_cast<unsigned char>(value)));
    });
    return result;
}

const char* FriendLobbyInviteStatusLabel(
    dkr::runtime::netplay::FriendLobbyInviteStatus status) {
    using dkr::runtime::netplay::FriendLobbyInviteStatus;
    switch (status) {
        case FriendLobbyInviteStatus::Sent: return "SENT";
        case FriendLobbyInviteStatus::Delivered: return "DELIVERED";
        case FriendLobbyInviteStatus::Accepted: return "ACCEPTED";
        case FriendLobbyInviteStatus::Declined: return "DECLINED";
        case FriendLobbyInviteStatus::Expired: return "EXPIRED";
        case FriendLobbyInviteStatus::Cancelled: return "CANCELLED";
    }
    return "UNKNOWN";
}

dkr::runtime::netplay::FriendLobbyAdvertisement FriendLobbyFromSession(
    const dkr::runtime::netplay::SessionView& view) {
    using namespace dkr::runtime::netplay;
    FriendLobbyAdvertisement advertisement{};
    advertisement.hosting = session().presentation_active() && view.host &&
        (view.state == ConnectionState::Hosting ||
         view.state == ConnectionState::Lobby) &&
        !view.lobby_locked && valid_quick_join_code(view.invite);
    advertisement.quick_join_code = advertisement.hosting
        ? view.invite : std::string{};
    advertisement.maximum_players = view.room.rules.maximum_players;
    advertisement.synchronization =
        view.room.rules.synchronization == SynchronizationMode::ExperimentalRollback
            ? "Experimental rollback"
            : (view.room.rules.synchronization == SynchronizationMode::Rollback ? "Rollback" : "Lockstep");
    advertisement.compatibility = DKR_NETWORK_RELEASE_VERSION;
    for (const Player& player : view.room.players) {
        if (player.occupied) ++advertisement.players;
    }
    return advertisement;
}

void UpdateFriendPresenceNotification() {
    const auto snapshot = dkr::runtime::netplay::friend_service().snapshot();
    const auto& racers = snapshot->friends;
    const auto now = std::chrono::steady_clock::now();
    std::set<std::string> current;
    for (const auto& racer : racers) {
        if (racer.blocked) continue;
        current.insert(racer.identity);
        auto [entry, inserted] =
            g_friend_presence_notifications.try_emplace(racer.identity);
        (void)inserted;
        if (entry->second.update(racer.online, now) &&
            g_friend_online_notifications) {
            g_online_notifications.push(
                dkr::runtime::ui_notifications::Kind::FriendOnline,
                "FRIEND ONLINE",
                FriendDisplayLabel(racer) + " is online and ready to race.",
                now);
        }
    }
    for (auto iterator = g_friend_presence_notifications.begin();
         iterator != g_friend_presence_notifications.end();) {
        if (!current.contains(iterator->first)) {
            iterator = g_friend_presence_notifications.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void PumpFriendPresence() {
    // Presence has its own worker and does not need to be rebuilt at display
    // refresh rate. A 100 ms frontend cadence keeps notifications responsive
    // while avoiding lock/copy work on every launcher or overlay frame.
    static auto next_pump = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now < next_pump) return;
    next_pump = now + std::chrono::milliseconds{100};

    using namespace dkr::runtime::netplay;
    const SessionView view = session().presentation_view();
    if (g_joining_friend_invite && view.invite == g_joining_friend_invite->lobby_code) {
        if (view.state == ConnectionState::Lobby || view.state == ConnectionState::Loading || view.state == ConnectionState::Running) {
            FriendLobbyInviteView confirmed;
            std::string error;
            if (!friend_service().respond_lobby_invite(g_joining_friend_invite->invite_id, true, confirmed, error))
                g_online_action_status = error;
            g_joining_friend_invite.reset();
        }
    } else if (g_joining_friend_invite && view.state != ConnectionState::Offline) {
        g_joining_friend_invite.reset();
    }
    const FriendLobbyAdvertisement advertisement =
        FriendLobbyFromSession(view);
    friend_service().pump(advertisement);
    UpdateFriendPresenceNotification();
    const auto snapshot = friend_service().snapshot();
    std::set<std::uint64_t> current_invites;
    for (const FriendLobbyInviteView& invite :
         snapshot->incoming_lobby_invites) {
        current_invites.insert(invite.invite_id);
        if (invite.status != FriendLobbyInviteStatus::Delivered ||
            g_seen_friend_lobby_invites.contains(invite.invite_id)) {
            continue;
        }
        if (g_online_notifications.push(
            dkr::runtime::ui_notifications::Kind::LobbyInvite,
            "LOBBY INVITE",
            invite.friend_display_name +
                " invited you to race. Open DKR-R ONLINE - OPEN LOBBIES to accept.",
            std::chrono::steady_clock::now()))
            g_seen_friend_lobby_invites.insert(invite.invite_id);
    }
    std::erase_if(g_seen_friend_lobby_invites,
        [&](auto id) { return !current_invites.contains(id); });
}

void PumpDirectSessionIfDue() {
    // DirectSession's network worker already services the transport on its own
    // bounded wait. This call is only a low-latency wake hint, so issuing it at
    // frame rate merely causes needless mutex traffic while the launcher idles.
    static auto next_pump = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now < next_pump) return;
    next_pump = now + std::chrono::milliseconds{50};
    dkr::runtime::netplay::session().pump();
}

void DrawFpsOverlay(RT64::Application&) {
    if (!g_fps_overlay_enabled ||
        !dkr::runtime::enhancements::modern_presentation_enabled()) {
        g_fps_overlay_extent = {};
        return;
    }
    const auto measured = dkr::runtime::telemetry::metrics();
    std::vector<std::string> fields;
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), "%.0f FPS", measured.presented_fps);
    fields.emplace_back(buffer);

    bool frame_time = false;
    bool simulation = false;
    bool graphics = false;
    bool vi = false;
    bool interpolation = false;
    bool audio = false;
    bool target = false;
    bool resolution = false;
    if (g_fps_overlay_detail == 1) {
        frame_time = simulation = target = true;
    } else if (g_fps_overlay_detail == 2) {
        frame_time = simulation = graphics = vi = interpolation = audio =
            target = resolution = true;
    } else if (g_fps_overlay_detail == 3) {
        frame_time = g_fps_custom_frame_time;
        simulation = g_fps_custom_simulation;
        graphics = g_fps_custom_graphics;
        vi = g_fps_custom_vi;
        interpolation = g_fps_custom_interpolation;
        audio = g_fps_custom_audio;
        target = g_fps_custom_target;
        resolution = g_fps_custom_resolution;
    }
    const auto add_rate = [&](bool enabled, const char* format, double value) {
        if (!enabled) return;
        std::snprintf(buffer, sizeof(buffer), format, value);
        fields.emplace_back(buffer);
    };
    add_rate(frame_time, "%.2f MS", measured.frame_time_ms);
    add_rate(simulation, "SIM %.1f HZ", measured.simulation_hz);
    add_rate(graphics, "GFX %.1f HZ", measured.graphics_hz);
    add_rate(vi, "VI %.1f HZ", measured.vi_hz);
    add_rate(interpolation, "INTERP %.1f HZ", measured.interpolated_hz);
    add_rate(audio, "AUDIO %.0f HZ", measured.audio_frames_per_second);
    if (target) {
        const auto& config = ultramodern::renderer::get_graphics_config();
        const int rate = config.rr_option == RefreshRate::Manual
            ? config.rr_manual_value
            : static_cast<int>(ultramodern::get_display_refresh_rate());
        std::snprintf(buffer, sizeof(buffer), "TARGET %d FPS", rate);
        fields.emplace_back(buffer);
    }
    if (resolution) {
        int width = 0;
        int height = 0;
        if (auto* window = static_cast<SDL_Window*>(
                dkr::runtime::platform::sdl_window()); window != nullptr) {
            SDL_GetWindowSize(window, &width, &height);
        }
        std::snprintf(buffer, sizeof(buffer), "%d X %d", width, height);
        fields.emplace_back(buffer);
    }
    if (g_fps_overlay_single_row && fields.size() > 1U) {
        std::string row = fields.front();
        for (std::size_t index = 1; index < fields.size(); ++index) {
            row += "  |  ";
            row += fields[index];
        }
        fields.assign(1U, std::move(row));
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    constexpr float margin = 18.0F;
    ImVec2 position{margin, margin};
    ImVec2 pivot{0.0F, 0.0F};
    if (g_fps_overlay_position == 1 || g_fps_overlay_position == 3) {
        position.x = display.x - margin;
        pivot.x = 1.0F;
    }
    if (g_fps_overlay_position >= 2) {
        position.y = display.y - margin;
        pivot.y = 1.0F;
    }
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.70F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0F, 10.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.025F, 0.08F, 0.12F, 0.82F});
    ImGui::PushStyleColor(ImGuiCol_Border, kWarm);
    if (ImGui::Begin("##dkr-r-fps-overlay", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings)) {
        ImFont* font = g_font_fps != nullptr ? g_font_fps : ImGui::GetFont();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 fill = ImGui::ColorConvertFloat4ToU32(g_fps_fill_colour);
        const ImU32 outline =
            ImGui::ColorConvertFloat4ToU32(g_fps_outline_colour);
        const float font_size = static_cast<float>(
            std::clamp(g_fps_font_size, 16, 64));
        const float edge = std::clamp(font_size * 0.0625F, 1.0F, 4.0F);
        for (const std::string& field : fields) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const ImVec2 extent = font->CalcTextSizeA(
                font_size, FLT_MAX, 0.0F, field.c_str());
            draw->AddText(font, font_size, {at.x - edge, at.y}, outline,
                          field.c_str());
            draw->AddText(font, font_size, {at.x + edge, at.y}, outline,
                          field.c_str());
            draw->AddText(font, font_size, {at.x, at.y - edge}, outline,
                          field.c_str());
            draw->AddText(font, font_size, {at.x, at.y + edge}, outline,
                          field.c_str());
            draw->AddText(font, font_size, at, fill, field.c_str());
            ImGui::Dummy({extent.x, extent.y + 2.0F});
        }
        g_fps_overlay_extent = ImGui::GetWindowSize();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

float OverlayStackOffset(int position, bool include_fps,
                         bool include_network) {
    float result = 0.0F;
    if (include_fps && g_fps_overlay_enabled &&
        g_fps_overlay_position == position && g_fps_overlay_extent.y > 0.0F) {
        result += g_fps_overlay_extent.y + 8.0F;
    }
    if (include_network && g_network_overlay_enabled &&
        g_network_overlay_position == position &&
        g_network_overlay_extent.y > 0.0F) {
        result += g_network_overlay_extent.y + 8.0F;
    }
    return result;
}

void DrawStatusOverlay(const char* id, std::vector<std::string> fields,
                       int position, float stack_offset, ImVec4 fill_colour,
                       ImVec4 outline_colour, ImVec2& extent) {
    if (fields.empty()) {
        extent = {};
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    constexpr float margin = 18.0F;
    ImVec2 at{margin, margin};
    ImVec2 pivot{0.0F, 0.0F};
    if (position == 1 || position == 3) {
        at.x = display.x - margin;
        pivot.x = 1.0F;
    }
    if (position >= 2) {
        at.y = display.y - margin - stack_offset;
        pivot.y = 1.0F;
    } else {
        at.y += stack_offset;
    }
    ImGui::SetNextWindowPos(at, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.76F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0F, 10.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.025F, 0.08F, 0.12F, 0.86F});
    ImGui::PushStyleColor(ImGuiCol_Border, kWarm);
    if (ImGui::Begin(id, nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoSavedSettings)) {
        ImFont* font = g_font_fps != nullptr ? g_font_fps : ImGui::GetFont();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 fill = ImGui::ColorConvertFloat4ToU32(fill_colour);
        const ImU32 outline = ImGui::ColorConvertFloat4ToU32(outline_colour);
        constexpr float font_size = 20.0F;
        constexpr float edge = 1.5F;
        for (const std::string& field : fields) {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const ImVec2 text_extent = font->CalcTextSizeA(
                font_size, FLT_MAX, 0.0F, field.c_str());
            draw->AddText(font, font_size, {cursor.x - edge, cursor.y},
                          outline, field.c_str());
            draw->AddText(font, font_size, {cursor.x + edge, cursor.y},
                          outline, field.c_str());
            draw->AddText(font, font_size, {cursor.x, cursor.y - edge},
                          outline, field.c_str());
            draw->AddText(font, font_size, {cursor.x, cursor.y + edge},
                          outline, field.c_str());
            draw->AddText(font, font_size, cursor, fill, field.c_str());
            ImGui::Dummy({text_extent.x, text_extent.y + 2.0F});
        }
        extent = ImGui::GetWindowSize();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void DrawNetworkOverlay() {
    using namespace dkr::runtime::netplay;
    if (!g_network_overlay_enabled || !session().presentation_active()) {
        g_network_overlay_extent = {};
        return;
    }
    const SessionView view = session().presentation_view();
    std::vector<std::string> fields;
    char buffer[192]{};
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    const auto owned = experimental::runtime_view();
    if (owned.active) {
        std::snprintf(buffer, sizeof(buffer), "ONLINE EXPERIMENTAL  %u MS  DELAY %u",
            view.network_rtt_ms, view.input_delay_frames);
        fields.emplace_back(buffer);
        if (g_network_overlay_detail >= 1) {
            std::snprintf(buffer, sizeof(buffer), "SCENE EPOCH %llu  FRAME %u  CONFIRMED %u",
                static_cast<unsigned long long>(owned.epoch), owned.frame, owned.confirmed);
            fields.emplace_back(buffer);
        }
        if (g_network_overlay_detail >= 2) {
            std::snprintf(buffer, sizeof(buffer), "ROLLBACKS %u  REPLAYED %u  WAIT %d",
                owned.corrections, owned.replayed, int(owned.wait));
            fields.emplace_back(buffer);
        }
    } else {
#endif
    std::snprintf(buffer, sizeof(buffer), "ONLINE %s  %u MS",
        view.room.rules.synchronization == SynchronizationMode::ExperimentalRollback
            ? "EXPERIMENTAL"
            : (view.room.rules.synchronization == SynchronizationMode::Rollback ? "ROLLBACK" : "LOCKSTEP"),
        view.network_rtt_ms);
    fields.emplace_back(buffer);
    if (g_network_overlay_detail >= 1) {
        std::snprintf(buffer, sizeof(buffer),
            "JITTER %u MS  LOSS %.1f%%  DELAY %u",
            view.network_jitter_ms, view.network_loss_percent,
            view.input_delay_frames);
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "AUTH %u R%u  PRED 0X%02X",
            view.authoritative_input_frame,
            view.authoritative_input_revision,
            view.authoritative_predicted_mask);
        fields.emplace_back(buffer);
    }
    if (g_network_overlay_detail >= 2) {
        const auto rollback = rollback_metrics();
        std::snprintf(buffer, sizeof(buffer),
            "ROLLBACKS %u  REPLAYED %u  MAX %u",
            rollback.rollback_count, rollback.replayed_frames,
            rollback.largest_rollback);
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "CORRECTIONS %u  STALLS %u  QUEUE %zu/%zu",
            view.input_corrections, view.input_stalls,
            view.outbound_queue_high_water, view.rollback_queue_high_water);
        fields.emplace_back(buffer);
        const auto phase_name = [](FrameDebtPhase phase) {
            switch (phase) {
            case FrameDebtPhase::Frontend: return "FRONTEND";
            case FrameDebtPhase::LoadingBarrier: return "LOADING";
            case FrameDebtPhase::GameplayStartBarrier: return "START GATE";
            case FrameDebtPhase::Gameplay: return "GAMEPLAY";
            case FrameDebtPhase::RecoveryBarrier: return "RECOVERY";
            case FrameDebtPhase::FinishBarrier: return "FINISH GATE";
            case FrameDebtPhase::PostRace: return "RESULTS";
            case FrameDebtPhase::Inactive: break;
            }
            return "INACTIVE";
        };
        const FrameDebtSample& debt = rollback.frame_debt;
        if (debt.valid) {
            std::snprintf(buffer, sizeof(buffer),
                "FRAME DEBT %u/%u  %s E%u/S%u  PACE %u HZ  SCTP C/A/R/S %zu/%zu/%zu/%zu",
                debt.debt, rollback.target_frame_debt,
                phase_name(debt.phase), rollback.input_epoch,
                rollback.scene_epoch, rollback.pacing_target_hz,
                view.control_transport_buffered_bytes,
                view.authority_transport_buffered_bytes,
                view.realtime_transport_buffered_bytes,
                view.replica_transport_buffered_bytes);
        } else if (debt.intentionally_parked) {
            std::snprintf(buffer, sizeof(buffer),
                "FRAME DEBT HELD  %s  SCTP C/A/R/S %zu/%zu/%zu/%zu",
                phase_name(debt.phase), view.control_transport_buffered_bytes,
                view.authority_transport_buffered_bytes,
                view.realtime_transport_buffered_bytes,
                view.replica_transport_buffered_bytes);
        } else {
            std::snprintf(buffer, sizeof(buffer),
                "FRAME DEBT N/A  %s  SCTP C/A/R/S %zu/%zu/%zu/%zu",
                phase_name(debt.phase), view.control_transport_buffered_bytes,
                view.authority_transport_buffered_bytes,
                view.realtime_transport_buffered_bytes,
                view.replica_transport_buffered_bytes);
        }
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "PEER DEBT %u  RECOVERING %u  HOST HOLDS %llu  PROGRESS %s/%u MS",
            view.maximum_peer_frame_debt, view.recovering_peer_count,
            static_cast<unsigned long long>(view.host_backpressure_events),
            view.peer_progress_known ? "LIVE" : "N/A",
            view.oldest_peer_progress_age_ms);
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "QUEUED %zu B / %u MS  CHECKPOINT %zu B  PUMP MAX %llu US",
            view.pending_outbound_bytes, view.oldest_outbound_age_ms,
            view.checkpoint_transport_buffered_bytes,
            static_cast<unsigned long long>(view.maximum_network_pump_us));
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "REPAIR TX/RX/BATCH %llu/%llu/%llu  LATE INPUT %llu",
            static_cast<unsigned long long>(view.commit_repair_requests_sent),
            static_cast<unsigned long long>(view.commit_repair_requests_received),
            static_cast<unsigned long long>(view.commit_repair_batches_sent),
            static_cast<unsigned long long>(view.late_inputs_discarded));
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "INPUT ECHO/CONSUME %u/%u MS (%llu SAMPLES)  LIMIT WAITS %llu",
            view.local_input_echo_ms, view.local_input_consume_ms,
            static_cast<unsigned long long>(view.measured_input_echoes),
            static_cast<unsigned long long>(view.prediction_limit_waits));
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "LOCAL CONSUME P50/95/99/MAX %u/%u/%u/%u MS (%llu)",
            view.input_consume_latency.p50_ms, view.input_consume_latency.p95_ms,
            view.input_consume_latency.p99_ms, view.input_consume_latency.maximum_ms,
            static_cast<unsigned long long>(view.input_consume_latency.samples));
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "HOST ECHO P95 %u MS  ENQUEUE P95 %u MS  PREDICTED/MISMATCH/SUPERSEDED %llu/%llu/%llu",
            view.input_echo_latency.p95_ms, view.input_enqueue_latency.p95_ms,
            static_cast<unsigned long long>(view.predicted_local_samples),
            static_cast<unsigned long long>(view.mismatched_local_samples),
            static_cast<unsigned long long>(view.superseded_local_samples));
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "INPUT REFRESH / OLD-DUP REVISION %llu / %llu",
            static_cast<unsigned long long>(view.refreshed_input_samples),
            static_cast<unsigned long long>(view.stale_input_revisions));
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "REPLICA REQUEST/MISS/WINDOW/DECODE %llu/%llu/%llu/%llu",
            static_cast<unsigned long long>(view.live_replica_requests_sent),
            static_cast<unsigned long long>(view.live_replica_request_misses),
            static_cast<unsigned long long>(view.live_replica_window_rejections),
            static_cast<unsigned long long>(view.live_replica_decode_failures));
        fields.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer), "PACKETS %llu / %llu",
            static_cast<unsigned long long>(view.packets_sent),
            static_cast<unsigned long long>(view.packets_received));
        fields.emplace_back(buffer);
    }
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    }
#endif
    if (g_network_overlay_single_row && fields.size() > 1U) {
        std::string row = fields.front();
        for (std::size_t index = 1; index < fields.size(); ++index) {
            row += "  |  ";
            row += fields[index];
        }
        fields.assign(1U, std::move(row));
    }
    DrawStatusOverlay("##dkr-r-network-overlay", std::move(fields),
        g_network_overlay_position,
        OverlayStackOffset(g_network_overlay_position, true, false),
        kCream, kRaceBlue, g_network_overlay_extent);
}

std::string InputButtonSummary(std::uint16_t buttons) {
    struct Label { std::uint16_t mask; const char* text; };
    constexpr std::array<Label, 14> labels{{
        {0x8000, "A"}, {0x4000, "B"}, {0x2000, "Z"},
        {0x1000, "START"}, {0x0800, "DU"}, {0x0400, "DD"},
        {0x0200, "DL"}, {0x0100, "DR"}, {0x0020, "L"},
        {0x0010, "R"}, {0x0008, "CU"}, {0x0004, "CD"},
        {0x0002, "CL"}, {0x0001, "CR"}}};
    std::string result;
    for (const Label& label : labels) {
        if ((buttons & label.mask) == 0U) continue;
        if (!result.empty()) result += '+';
        result += label.text;
    }
    return result.empty() ? "-" : result;
}

void DrawControllerInputOverlay() {
    using namespace dkr::runtime::netplay;
    static ImVec2 extent{};
    if (!g_controller_input_overlay_enabled || !session().presentation_active()) {
        extent = {};
        return;
    }
    const SessionView view = session().presentation_view();
    if (view.state != ConnectionState::Running ||
        view.local_slot >= view.room.players.size()) {
        extent = {};
        return;
    }
    std::vector<std::string> fields;
    char buffer[224]{};
    const PackedInput& local = view.local_input_submitted;
    std::snprintf(buffer, sizeof(buffer),
        "LOCAL P%u  X%+d Y%+d  %s",
        static_cast<unsigned>(view.local_slot + 1U),
        static_cast<int>(local.stick_x), static_cast<int>(local.stick_y),
        InputButtonSummary(local.buttons).c_str());
    fields.emplace_back(buffer);
    if (view.authoritative_inputs_valid) {
        for (std::size_t slot = 0; slot < view.room.players.size(); ++slot) {
            if (!view.room.players[slot].occupied) continue;
            const PackedInput& input = view.authoritative_inputs[slot];
            std::snprintf(buffer, sizeof(buffer),
                "HOST P%zu  X%+d Y%+d  %s",
                slot + 1U, static_cast<int>(input.stick_x),
                static_cast<int>(input.stick_y),
                InputButtonSummary(input.buttons).c_str());
            fields.emplace_back(buffer);
        }
    } else {
        fields.emplace_back("WAITING FOR PLAYER 1 COMMIT");
    }
    DrawStatusOverlay("##dkr-r-controller-input-overlay",
        std::move(fields), g_controller_input_overlay_position,
        OverlayStackOffset(g_controller_input_overlay_position, true, true),
        kCream, kAccent, extent);
}

bool UpdateOnlineErrorNotification() {
    using dkr::runtime::netplay::ConnectionState;
    const auto view = dkr::runtime::netplay::session().presentation_view();
    const bool failed = view.state == ConnectionState::Failed &&
                        !view.status.empty();
    if (failed && (!g_online_error_was_active ||
                   view.status != g_last_online_error)) {
        g_online_error_notification =
            dkr::runtime::netplay::online_failure_display_message(view.status);
        g_last_online_error = view.status;
        g_online_error_started = std::chrono::steady_clock::now();
        g_online_failure_modal =
            dkr::runtime::netplay::classify_online_failure(view.status);
        g_online_failure_modal_message = g_online_error_notification;
        g_online_failure_modal_requested = true;
    }
    g_online_error_was_active = failed;
    if (!failed) g_last_online_error.clear();
    if (g_online_error_notification.empty() ||
        g_online_error_started.time_since_epoch().count() == 0) {
        return false;
    }
    return std::chrono::steady_clock::now() - g_online_error_started <
           std::chrono::seconds(10);
}

const char* OnlineFailureRecoveryText(
    dkr::runtime::netplay::OnlineFailureCode code) {
    using dkr::runtime::netplay::OnlineFailureCode;
    switch (code) {
        case OnlineFailureCode::ProtocolMismatch:
        case OnlineFailureCode::BuildMismatch:
        case OnlineFailureCode::PatchPolicyMismatch:
            return "Install the same DKR-R build on every machine, then restart DKR-R before reconnecting.";
        case OnlineFailureCode::GamePakMismatch:
            return "Select the same supported Game Pak revision as player 1, then reconnect to the lobby.";
        case OnlineFailureCode::GameplaySettingsMismatch:
            return "Match the host's gameplay-affecting settings, then leave and rejoin the lobby.";
        case OnlineFailureCode::MagicCodesMismatch:
            return "Match the host's enabled Magic Codes in MODS / HACKS, then reconnect. The exact differences are listed below.";
        case OnlineFailureCode::SaveMismatch:
            return "Leave and rejoin the lobby to receive the host's isolated online save again. If it repeats, the host can check their Online save in Save Manager. Do not replace clients' offline saves to fix a lobby mismatch.";
        case OnlineFailureCode::SimulationRateMismatch:
            return "Use the same presentation preset and simulation rate on every machine, then reconnect.";
        case OnlineFailureCode::PlatformMismatch:
            return "This platform combination did not pass deterministic compatibility. Confirm both builds are from the same release package.";
        case OnlineFailureCode::InvitationExpired:
        case OnlineFailureCode::InvitationInvalid:
            return "Ask the host for a fresh Quick Join code and enter it again.";
        case OnlineFailureCode::HostUnreachable:
            return "Confirm the host is still running, both racers are online, and DKR-R is allowed through each firewall. Then retry Quick Join.";
        case OnlineFailureCode::RequestRejected:
            return "The host declined this request. Ask the host to approve a new join request before retrying.";
        case OnlineFailureCode::Blocked:
            return "This racer is blocked by the host. The host must unblock them before another request can be accepted.";
        case OnlineFailureCode::LobbyFull:
            return "This two-racer lobby is full. Wait for Player 2's slot to open, then reconnect.";
        case OnlineFailureCode::LobbyLocked:
            return "The lobby is already launching or racing. Wait until the host returns to the lobby, then reconnect.";
        case OnlineFailureCode::BaselineFailure:
            return "Return every racer to the lobby and let player 1 start the event again. If it repeats, save the diagnostic log before restarting DKR-R.";
        case OnlineFailureCode::DeterminismFailure:
            return "The session stopped before divergent gameplay could continue. Return to the lobby, confirm matching settings and Game Paks, then retry.";
        case OnlineFailureCode::RecoveryFailure:
        case OnlineFailureCode::FinishBarrierTimeout:
            return "The connection could not recover safely. Return to the lobby and retry after the connection has stabilized.";
        case OnlineFailureCode::TransportFailure:
            return "The network connection was interrupted. Check the connection, then reconnect through Quick Join.";
        case OnlineFailureCode::None:
        case OnlineFailureCode::Unknown:
        default:
            return "Return to the DKR-R Online tab for the persistent error. Retry after confirming matching builds, Game Paks, settings and a stable connection.";
    }
}

void DrawMagicCodeDifferences(
    const dkr::runtime::netplay::OnlineFailure& failure) {
    if (!failure.has_magic_code_details) return;
    const std::uint64_t host_only =
        failure.expected_magic_codes & ~failure.candidate_magic_codes;
    const std::uint64_t racer_only =
        failure.candidate_magic_codes & ~failure.expected_magic_codes;
    if (host_only == 0U && racer_only == 0U) return;

    ImGui::Separator();
    ImGui::TextUnformatted("MAGIC CODE DIFFERENCES");
    for (const auto& definition :
         dkr::runtime::magic_codes::kMagicCodeDefinitions) {
        const std::uint64_t bit =
            dkr::runtime::magic_codes::magic_code_bit(
                definition.internal_index);
        if ((host_only & bit) != 0U) {
            ImGui::BulletText("HOST ON / THIS RACER OFF: %s - %s",
                              definition.phrase, definition.effect);
        } else if ((racer_only & bit) != 0U) {
            ImGui::BulletText("HOST OFF / THIS RACER ON: %s - %s",
                              definition.phrase, definition.effect);
        }
    }
}

void DrawOnlineFailureModal() {
    constexpr const char* kPopupName = "DKR-R Online needs attention";
    if (g_online_failure_modal_requested) {
        ImGui::OpenPopup(kPopupName);
        g_online_failure_modal_requested = false;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 size{
        std::clamp(display.x * 0.72F, 500.0F, 880.0F),
        std::clamp(display.y * 0.68F, 360.0F, 650.0F)};
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    if (!BeginPaddedModal(kPopupName)) {
        g_online_failure_modal_active = false;
        return;
    }
    g_online_failure_modal_active = true;

    if (g_font_title != nullptr) ImGui::PushFont(g_font_title);
    ImGui::TextColored(kRaceRed, "ONLINE SESSION HALTED");
    if (g_font_title != nullptr) ImGui::PopFont();
    ImGui::Dummy({0.0F, 4.0F});

    const float footer_height = 58.0F;
    if (BeginPaddedChild("##online-failure-help",
                         {0.0F, -(footer_height + ImGui::GetStyle().ItemSpacing.y)},
                         false, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextWrapped("%s", g_online_failure_modal_message.c_str());
        ImGui::Dummy({0.0F, 8.0F});
        ImGui::TextColored(kWarm, "HOW TO RECOVER");
        ImGui::TextWrapped("%s", OnlineFailureRecoveryText(
            g_online_failure_modal.code));
        DrawMagicCodeDifferences(g_online_failure_modal);
        const auto session_view = dkr::runtime::netplay::session().presentation_view();
        if (session_view.compatibility_sync_offer.has_value()) {
            ImGui::Separator();
            ImGui::TextColored(kAccent, "ONE-CLICK HOST SYNC AVAILABLE");
            ImGui::TextWrapped(
                "DKR-R can select the host's Game Pak only if that exact supported ROM is already in your library, mirror the host's Magic Codes, verify the resulting manifest, and retry this invitation. No ROM data is transferred.");
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    const auto session_view = dkr::runtime::netplay::session().presentation_view();
    if (session_view.compatibility_sync_offer.has_value()) {
        if (ImGui::Button("SYNC WITH HOST & RETRY", {280.0F, 42.0F})) {
            g_online_compatibility_sync_requested = true;
            g_online_failure_modal_active = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
    }
    const float button_width = 150.0F;
    if (ImGui::Button("GOT IT", {button_width, 42.0F})) {
        g_online_failure_modal_active = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawOnlineErrorNotification() {
    const auto now = std::chrono::steady_clock::now();
    const float elapsed = std::chrono::duration<float>(
        now - g_online_error_started).count();
    if (elapsed < 0.0F || elapsed >= 10.0F ||
        g_online_error_notification.empty()) {
        return;
    }

    // Remain fully legible for eight seconds, then fade smoothly during the
    // final two. This is a non-modal warning: the persistent error remains in
    // Online MP, while gameplay/overlay controls stay usable underneath it.
    const float alpha = elapsed <= 8.0F
        ? 1.0F : std::clamp((10.0F - elapsed) / 2.0F, 0.0F, 1.0F);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::clamp(display.x * 0.70F, 340.0F, 900.0F);
    ImGui::SetNextWindowPos({display.x * 0.5F, display.y - 24.0F},
                            ImGuiCond_Always, {0.5F, 1.0F});
    ImGui::SetNextWindowSize({width, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94F * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18.0F, 14.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.12F, 0.025F, 0.025F, 0.96F});
    ImGui::PushStyleColor(ImGuiCol_Border, kRaceRed);
    if (ImGui::Begin("##dkr-r-online-error-notification", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
        if (g_font_title != nullptr) ImGui::PushFont(g_font_title);
        ImGui::TextUnformatted("ONLINE SESSION HALTED");
        if (g_font_title != nullptr) ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::Dummy({0.0F, 4.0F});
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 36.0F);
        ImGui::TextWrapped("%s", g_online_error_notification.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void DrawOnlineStartCountdown() {
    const auto view = dkr::runtime::netplay::session().presentation_view();
    static std::uint32_t observed_generation = 0U;
    static std::uint32_t sounded_second = 0U;
    if (!view.launch_countdown_active ||
        view.launch_countdown_remaining_ms == 0U) {
        sounded_second = 0U;
        return;
    }

    if (observed_generation != view.launch_countdown_generation) {
        observed_generation = view.launch_countdown_generation;
        sounded_second = 0U;
    }
    const std::uint32_t second = std::clamp(
        (view.launch_countdown_remaining_ms + 999U) / 1000U, 1U, 5U);
    if (second != sounded_second) {
        sounded_second = second;
        const auto tone = dkr::runtime::ui_cues::online_launch_tone(second);
        if (tone) {
            dkr::runtime::platform::request_ui_tone(
                tone.frequency_hz, tone.duration_ms);
        }
    }

    if (g_online_countdown_panel_frame == ImGui::GetFrameCount()) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float maximum_width = std::max(280.0F, display.x - 32.0F);
    const float width = std::min(
        std::clamp(display.x * 0.58F, 460.0F, 900.0F), maximum_width);
    ImGui::SetNextWindowPos({display.x * 0.5F, display.y * 0.42F},
                            ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowSize({width, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {24.0F, 20.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.025F, 0.08F, 0.12F, 0.97F});
    ImGui::PushStyleColor(ImGuiCol_Border, kAccent);
    if (ImGui::Begin("##dkr-r-online-start-countdown", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings)) {
        const char* heading = "DKR-R ONLINE STARTING IN...";
        if (g_font_title != nullptr) ImGui::PushFont(g_font_title);
        ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
        const float heading_width = ImGui::CalcTextSize(heading).x;
        const float heading_available = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(
            0.0F, (heading_available - heading_width) * 0.5F));
        ImGui::TextUnformatted(heading);
        ImGui::PopStyleColor();
        if (g_font_title != nullptr) ImGui::PopFont();
        ImGui::Dummy({0.0F, 8.0F});
        char number[2]{static_cast<char>('0' + second), '\0'};
        if (g_font_title != nullptr) ImGui::PushFont(g_font_title);
        ImGui::SetWindowFontScale(3.6F);
        const float number_width = ImGui::CalcTextSize(number).x;
        const float number_available = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(
            0.0F, (number_available - number_width) * 0.5F));
        ImGui::PushStyleColor(ImGuiCol_Text, kRaceRed);
        ImGui::TextUnformatted(number);
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0F);
        if (g_font_title != nullptr) ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

const char* OnlineWaitReasonLabel(
    dkr::runtime::netplay::OnlineWaitReason reason) {
    using dkr::runtime::netplay::OnlineWaitReason;
    switch (reason) {
    case OnlineWaitReason::Loading:
        return "LOADING THE SAME GAME STATE";
    case OnlineWaitReason::RaceStart:
        return "SYNCHRONIZING RACE START";
    case OnlineWaitReason::Transition:
        return "SYNCHRONIZING THE NEXT SCREEN";
    case OnlineWaitReason::RaceFinish:
        return "SYNCHRONIZING RACE RESULTS";
    case OnlineWaitReason::Recovery:
        return "RECOVERING THE SHARED TIMELINE";
    case OnlineWaitReason::ClientCatchUp:
        return "A RACER IS CATCHING UP";
    case OnlineWaitReason::Cutscene:
        return "SYNCHRONIZING CUTSCENE";
    case OnlineWaitReason::None:
    default:
        return nullptr;
    }
}

bool OnlineWaitingActive(
    const dkr::runtime::netplay::SessionView& view) {
    const bool launch_wait =
        view.launch_stage == dkr::runtime::netplay::LaunchStage::Preparing ||
        view.launch_stage == dkr::runtime::netplay::LaunchStage::Committing ||
        view.launch_stage == dkr::runtime::netplay::LaunchStage::Releasing ||
        (view.launch_countdown_active &&
         view.launch_countdown_remaining_ms == 0U);
    return launch_wait ||
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        (dkr::runtime::netplay::experimental::runtime_view().active &&
         dkr::runtime::netplay::experimental::runtime_view().wait!=dkr::runtime::netplay::experimental::PumpWait::None &&
         dkr::runtime::netplay::experimental::runtime_view().wait!=dkr::runtime::netplay::experimental::PumpWait::FramePacing) ||
#endif
        dkr::runtime::netplay::online_wait_reason() !=
            dkr::runtime::netplay::OnlineWaitReason::None;
}

void DrawMipmapLoadingModal() {
    dkr::runtime::ui::draw_mipmap_loading_modal();
}

void DrawOnlineWaitingNotification(
    const dkr::runtime::netplay::SessionView& view) {
    using dkr::runtime::netplay::LaunchStage;
    const auto runtime_reason = dkr::runtime::netplay::online_wait_reason();
    const std::uint64_t runtime_generation =
        dkr::runtime::netplay::online_wait_episode();
    const char* reason = nullptr;
    bool launch_wait = true;
    if (view.launch_countdown_active &&
        view.launch_countdown_remaining_ms == 0U) {
        reason = "WAITING FOR PLAYER 1'S SYNCHRONIZED START SIGNAL";
    } else {
        switch (view.launch_stage) {
        case LaunchStage::Preparing:
            reason = "VALIDATING EVERY RACER'S STARTING GRID";
            break;
        case LaunchStage::Committing:
            reason = "ARMING THE SAME LAUNCH FOR EVERY RACER";
            break;
        case LaunchStage::Releasing:
            reason = "RELEASING THE SYNCHRONIZED START";
            break;
        default:
            launch_wait = false;
            reason = OnlineWaitReasonLabel(runtime_reason);
            break;
        }
    }

#if defined(DKR_EXPERIMENTAL_RACE_TEST)
    const auto owned=dkr::runtime::netplay::experimental::runtime_view();
    if(owned.active) {
        using dkr::runtime::netplay::experimental::PumpWait;
        if(owned.initial_admission) {
            reason=owned.status.c_str();
            launch_wait=true;
        } else switch(owned.wait) {
        case PumpWait::OwnerInput: case PumpWait::Confirmation:reason="WAITING FOR CONFIRMED RACER INPUT";break;
        case PumpWait::ScenePeers:reason="WAITING FOR RACERS AT THE SAME SCENE";break;
        case PumpWait::ScenePreparation:reason="PREPARING THE SYNCHRONIZED SCENE";break;
        case PumpWait::PresentationDrain:reason="RETIRING THE CORRECTED FRAME";break;
        default:reason=nullptr;break;
        }
    }
#endif
    static auto pending_since = std::chrono::steady_clock::time_point{};
    static dkr::runtime::netplay::OnlineWaitReason observed_reason =
        dkr::runtime::netplay::OnlineWaitReason::None;
    static std::uint64_t observed_generation = 0U;
    const auto now = std::chrono::steady_clock::now();
    if (reason == nullptr) {
        pending_since = {};
        observed_reason = dkr::runtime::netplay::OnlineWaitReason::None;
        observed_generation = runtime_generation;
        return;
    }
    if (!launch_wait &&
        (pending_since.time_since_epoch().count() == 0 ||
         observed_generation != runtime_generation)) {
        observed_reason = runtime_reason;
        observed_generation = runtime_generation;
        pending_since = now;
    }
    if (!launch_wait && pending_since.time_since_epoch().count() != 0 &&
        now - pending_since < std::chrono::milliseconds(300)) {
        return;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(
        std::clamp(display.x * 0.46F, 360.0F, 680.0F),
        std::max(280.0F, display.x - 32.0F));
    ImGui::SetNextWindowPos({display.x * 0.5F, display.y - 28.0F},
                            ImGuiCond_Always, {0.5F, 1.0F});
    ImGui::SetNextWindowSize({width, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18.0F, 14.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.025F, 0.08F, 0.12F, 0.97F});
    ImGui::PushStyleColor(ImGuiCol_Border, kAccent);
    if (ImGui::Begin("##dkr-r-online-waiting", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings)) {
        const ImVec2 spinner_center{
            ImGui::GetCursorScreenPos().x + 11.0F,
            ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5F};
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float angle = static_cast<float>(ImGui::GetTime() * 4.5);
        draw->PathArcTo(spinner_center, 8.0F, angle,
                        angle + 4.7F, 18);
        draw->PathStroke(ImGui::ColorConvertFloat4ToU32(kWarm),
                         false, 3.0F);
        ImGui::Dummy({24.0F, 1.0F});
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
        ImGui::TextUnformatted("WAITING FOR RACERS");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("%s", reason);
        ImGui::PopStyleColor();
        ImGui::EndGroup();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

bool OnlineNotificationActive() {
    return g_online_notifications.current(std::chrono::steady_clock::now()) !=
           nullptr;
}

void DrawOnlineNotification() {
    const auto now = std::chrono::steady_clock::now();
    const auto* notification = g_online_notifications.current(now);
    if (notification == nullptr) return;
    const float alpha =
        dkr::runtime::ui_notifications::Queue::alpha(*notification, now);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::clamp(display.x * 0.30F, 300.0F, 460.0F);
    const int position = std::clamp(g_friend_online_notification_position, 0, 3);
    const float stack_offset = OverlayStackOffset(position, true, true);
    constexpr float margin = 18.0F;
    ImVec2 at{margin, margin + stack_offset};
    ImVec2 pivot{0.0F, 0.0F};
    if (position == 1 || position == 3) {
        at.x = display.x - margin;
        pivot.x = 1.0F;
    }
    if (position >= 2) {
        at.y = display.y - margin - stack_offset;
        pivot.y = 1.0F;
    }
    ImGui::SetNextWindowPos(at, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowSize({width, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94F * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18.0F, 14.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.025F, 0.12F, 0.12F, 0.96F});
    ImGui::PushStyleColor(ImGuiCol_Border, kAccent);
    if (ImGui::Begin("##dkr-r-online-notification", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarm);
        if (g_font_title != nullptr) ImGui::PushFont(g_font_title);
        ImGui::TextUnformatted(notification->title.c_str());
        if (g_font_title != nullptr) ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::Dummy({0.0F, 4.0F});
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 36.0F);
        ImGui::TextWrapped("%s", notification->message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void DrawTexturePackRemovalModal() {
    const float display_width = std::max(ImGui::GetIO().DisplaySize.x, 1.0F);
    const float minimum_width = std::min(480.0F, display_width - 36.0F);
    ImGui::SetNextWindowSizeConstraints(
        {std::max(minimum_width, 280.0F), 0.0F},
        {std::min(680.0F, display_width - 18.0F), FLT_MAX});
    if (!BeginPaddedModal("Remove texture pack?",
                          ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted("REMOVE TEXTURE PACK?");
    ImGui::Separator();
    ImGui::TextWrapped("%s", g_texture_pack_remove_name.c_str());
    ImGui::Dummy({0.0F, 6.0F});
    ImGui::TextWrapped(
        "HIDE FROM LIST keeps DKR-R's managed copy on disk. Choose Hidden "
        "from the Visibility filter to restore it later.");
    ImGui::Dummy({0.0F, 8.0F});
    ImGui::PushStyleColor(ImGuiCol_Text, kRaceRed);
    ImGui::TextUnformatted("WARNING - PERMANENT DELETION CANNOT BE UNDONE");
    ImGui::PopStyleColor();
    ImGui::TextWrapped(
        "DELETE COMPLETELY removes DKR-R's managed archive or converted "
        "texture cache. The original source archive outside DKR-R is never touched.");
    ImGui::Dummy({0.0F, 12.0F});
    const float available = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const bool compact = available < 560.0F;
    const float action_width = compact ? available :
        std::max((available - gap * 2.0F) / 3.0F, 1.0F);
    if (ImGui::Button("CANCEL", {action_width, 44.0F})) {
        ImGui::CloseCurrentPopup();
    }
    if (!compact) ImGui::SameLine();
    if (ImGui::Button("HIDE FROM LIST", {action_width, 44.0F})) {
        dkr::runtime::texture_packs::set_hidden(
            g_texture_pack_remove_id, true, g_texture_pack_status);
        ImGui::CloseCurrentPopup();
    }
    if (!compact) ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, {0.45F, 0.09F, 0.10F, 1.0F});
    if (ImGui::Button("DELETE COMPLETELY", {action_width, 44.0F})) {
        dkr::runtime::texture_packs::delete_managed(
            g_texture_pack_remove_id, g_texture_pack_status);
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor();
    ImGui::EndPopup();
}

std::string FormatManagedTexturePackSize(std::uintmax_t bytes) {
    constexpr std::array<const char*, 5> units{{"B", "KB", "MB", "GB", "TB"}};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0U;
    while (value >= 1024.0 && unit + 1U < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    char result[64]{};
    std::snprintf(result, sizeof(result),
                  unit == 0U ? "%.0f %s" : "%.1f %s", value, units[unit]);
    return result;
}

bool DrawTexturePackManagementModal(
    const std::vector<dkr::runtime::texture_packs::PackInfo>& packs) {
    constexpr const char* kPopupName = "Manage texture pack";
    const float display_width = std::max(ImGui::GetIO().DisplaySize.x, 1.0F);
    ImGui::SetNextWindowSizeConstraints(
        {std::min(540.0F, display_width - 36.0F), 0.0F},
        {std::min(720.0F, display_width - 18.0F), FLT_MAX});
    if (!BeginPaddedModal(kPopupName, ImGuiWindowFlags_AlwaysAutoResize)) {
        return false;
    }

    const auto match = std::find_if(
        packs.begin(), packs.end(), [](const auto& pack) {
            return pack.id == g_texture_pack_manage_id;
        });
    if (match == packs.end()) {
        ImGui::TextUnformatted("MANAGE TEXTURE PACK");
        ImGui::Separator();
        ImGui::TextWrapped(
            "This texture pack is no longer present in DKR-R's managed library.");
        if (ImGui::Button("CLOSE", {150.0F, 42.0F})) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return false;
    }

    const auto& pack = *match;
    ImGui::TextUnformatted("MANAGE TEXTURE PACK");
    ImGui::Separator();
    ImGui::TextWrapped("%s", pack.name.c_str());
    ImGui::Dummy({0.0F, 5.0F});

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        pack.compatible && !pack.hidden ? kAccent : kWarm);
    ImGui::TextUnformatted(pack.hidden
        ? "HIDDEN"
        : (pack.enabled ? "ACTIVE" : "INACTIVE"));
    ImGui::SameLine();
    ImGui::TextUnformatted("  -  ");
    ImGui::SameLine();
    ImGui::TextUnformatted(pack.compatible ? "COMPATIBLE" : "INCOMPATIBLE");
    ImGui::PopStyleColor();

    if (ImGui::BeginTable("texture-pack-information", 2,
                          ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerH)) {
        const auto detail_row = [](const char* label,
                                   const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", value.c_str());
        };
        detail_row("TYPE", dkr::runtime::texture_packs::format_name(pack.format));
        detail_row("PRIORITY", std::to_string(pack.priority) +
                   (pack.priority == 1U ? " (highest)" : " (lower number wins)"));
        detail_row("MANAGED SIZE",
                   FormatManagedTexturePackSize(pack.managed_size_bytes));
        detail_row("TEXTURES", std::to_string(pack.image_count));
        detail_row("VISIBILITY", pack.hidden ? "Hidden" : "Visible");
        ImGui::EndTable();
    }

    ImGui::TextDisabled("MANAGED LOCATION");
    ImGui::TextWrapped("%s", pack.path.string().c_str());
    if (!pack.detail.empty()) {
        ImGui::Dummy({0.0F, 3.0F});
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("%s", pack.detail.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Dummy({0.0F, 8.0F});
    const float available = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    // The normal 540px modal has room for a two-column action grid. Only stack
    // on genuinely narrow viewports; this keeps every option visible within a
    // 1280x800 Steam Deck window without shrinking the labels.
    const bool compact = available < 440.0F;
    const float action_width = compact ? available :
        std::max((available - gap) * 0.5F, 1.0F);

    ImGui::BeginDisabled(pack.hidden || !pack.compatible);
    if (ImGui::Button(pack.enabled ? "DEACTIVATE PACK" : "ACTIVATE PACK",
                      {action_width, 42.0F})) {
        dkr::runtime::texture_packs::set_enabled(pack.id, !pack.enabled);
    }
    ImGui::EndDisabled();
    if (!compact) ImGui::SameLine();
    if (ImGui::Button(pack.hidden ? "RESTORE TO LIBRARY" : "HIDE FROM LIBRARY",
                      {action_width, 42.0F})) {
        dkr::runtime::texture_packs::set_hidden(
            pack.id, !pack.hidden, g_texture_pack_status);
    }

    std::error_code path_error;
    const std::filesystem::path managed_location =
        std::filesystem::is_directory(pack.path, path_error)
            ? pack.path
            : pack.path.parent_path();
    if (ImGui::Button("OPEN MANAGED LOCATION", {action_width, 42.0F})) {
        if (dkr::runtime::support::open_directory(
                managed_location, g_texture_pack_status)) {
            g_texture_pack_status = "Opened the managed texture-pack location.";
        }
    }
    if (!compact) ImGui::SameLine();
    if (ImGui::Button("REFRESH PACK DETAILS", {action_width, 42.0F})) {
        dkr::runtime::texture_packs::refresh();
        g_texture_pack_status = "Texture-pack details refreshed.";
    }

    bool request_removal = false;
    ImGui::PushStyleColor(ImGuiCol_Button, {0.45F, 0.09F, 0.10F, 1.0F});
    if (ImGui::Button("REMOVE PACK...", {action_width, 42.0F})) {
        g_texture_pack_remove_id = pack.id;
        g_texture_pack_remove_name = pack.name;
        request_removal = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor();
    if (!compact) ImGui::SameLine();
    if (ImGui::Button("CLOSE", {action_width, 42.0F})) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return request_removal;
}


#include "runtime_mod_library_ui.inl"

// A Magic Code as a painted sign: its phrase on the header board, then what it
// does and where it applies (.im-magic-cell in the launcher study).
struct MagicCodeCard {
    const dkr::runtime::magic_codes::MagicCodeDefinition* definition = nullptr;
    bool enabled = false;
};

float MeasureMagicCodeCard(const MagicCodeCard& card, float width) {
    const PaddockType label = PaddockSign(20.0F, 1.25F);
    const PaddockType effect = PaddockReading(14.0F, false, 1.55F);
    const PaddockType where = PaddockReading(11.0F, false, 1.55F);
    const float label_height = std::max(
        PaddockTextHeight(label, card.definition->phrase, width - 28.0F - 34.0F), 24.0F);
    const float header = std::max(66.0F, 16.0F + label_height + 13.0F + 2.0F);
    const float body = 16.0F +
        PaddockTextHeight(effect, card.definition->effect, width - 32.0F) + 14.0F +
        1.0F + 10.0F + PaddockTextHeight(where, card.definition->availability, width - 32.0F) +
        12.0F;
    return std::ceil(2.0F + header + body + 2.0F);
}

void DrawMagicCodeCard(const MagicCodeCard& card, ImVec2 origin, float width,
                       float height, bool diagnostics) {
    using namespace dkr::runtime::magic_codes;
    const MagicCodeDefinition& definition = *card.definition;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::PushID(static_cast<int>(definition.internal_index));
    const float on = PaddockEase(PaddockTween(ImGui::GetID("magic-on"), card.enabled, 0.16F, 0.16F));
    const ImVec2 end{origin.x + width, origin.y + height};
    PaddockPanelStyle panel;
    panel.radii = {10.0F, 22.0F, 10.0F, 10.0F};
    panel.fill = PaddockRgb(0x0C3047);
    panel.drop = PaddockRgb(0x041822);
    panel.drop_offset = 5.0F;
    panel.ring = PaddockRgb(0x071E2D);
    panel.ring_width = 2.0F;
    panel.border_width = 2.0F;
    PaddockPanel(draw, origin, end, panel);

    const PaddockType label = PaddockSign(20.0F, 1.25F);
    const float label_width = width - 28.0F - 34.0F;
    const float label_height = std::max(PaddockTextHeight(label, definition.phrase, label_width), 24.0F);
    const float header = std::max(66.0F, 16.0F + label_height + 13.0F + 2.0F);
    const ImVec2 board_min{origin.x + 2.0F, origin.y + 2.0F};
    const ImVec2 board_max{end.x - 2.0F, origin.y + 2.0F + header};
    PaddockFill(draw, board_min, board_max, {8.0F, 20.0F, 0.0F, 0.0F},
                PaddockApply(PaddockMix(PaddockRgb(0x085B78), PaddockRgb(0x79601C), on)));
    draw->AddRectFilled({board_min.x, board_max.y - 2.0F}, board_max, PaddockCol(0x042338));
    const bool checker_on = card.enabled;
    PaddockChecker(draw, {board_min.x + 12.0F, board_min.y}, 8, 6.0F,
                   checker_on ? 0x675016U : 0x074961U, checker_on ? 0xFFE29AU : 0xA5DCE5U);

    // The whole board is the checkbox.
    const float content_top = board_min.y + 16.0F;
    const float content_height = header - 16.0F - 13.0F - 2.0F;
    const float check_height = std::max({42.0F, label_height, 24.0F});
    ImGui::SetCursorScreenPos({board_min.x + 12.0F,
                               content_top + (content_height - check_height) * 0.5F});
    bool enabled = card.enabled;
    if (PaddockCheckbox("##magic", definition.phrase, &enabled, width - 28.0F,
                        PaddockCheckKind::Magic, &label, 0xFFF3CB)) {
        std::string error;
        if (set_enabled(definition.internal_index, enabled, error)) {
            SaveSettings();
            g_magic_codes_status = enabled
                ? std::string(definition.phrase) +
                      (magic_code_is_one_shot(definition)
                           ? " queued until its native action runs."
                           : " will be active on launch.")
                : std::string(definition.phrase) + " disabled.";
        } else {
            g_magic_codes_status = error;
        }
    }
    const bool focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;

    const float left = origin.x + 16.0F;
    const float inner = width - 32.0F;
    const PaddockType effect = PaddockReading(14.0F, false, 1.55F);
    const PaddockType where = PaddockReading(11.0F, false, 1.55F);
    PaddockTextStyle effect_style;
    effect_style.colour = PaddockCol(diagnostics ? 0xFFBBA6U : 0xDAE8EEU);
    PaddockTextAt(draw, effect, {left, board_max.y + 16.0F}, inner, definition.effect, effect_style);
    const float where_height = PaddockTextHeight(where, definition.availability, inner);
    const float where_top = end.y - 2.0F - 12.0F - where_height;
    PaddockDashes(draw, {left, where_top - 11.0F}, inner, PaddockCol(0x356078), 1.0F);
    PaddockTextStyle where_style;
    where_style.colour = PaddockCol(diagnostics ? 0xFFBBA6U : 0xADC8D6U);
    PaddockTextAt(draw, where, {left, where_top}, inner, definition.availability, where_style);

    const ImU32 border = PaddockMix(PaddockRgb(0x387E9A), PaddockRgb(0xFFC453), on);
    PaddockStroke(draw, origin, end, panel.radii, PaddockApply(border), 2.0F);
    if (focused) {
        draw->AddRect({origin.x - 4.0F, origin.y - 4.0F}, {end.x + 4.0F, end.y + 4.0F},
                      PaddockCol(0xFFE293), 14.0F, 0, 2.0F);
    }
    ImGui::PopID();
}

void DrawMagicCodeGrid(float width, bool diagnostics, int enter_index) {
    using namespace dkr::runtime::magic_codes;
    const auto mask = selected_mask();
    std::vector<MagicCodeCard> cards;
    for (const auto& definition : kMagicCodeDefinitions) {
        if (magic_code_is_diagnostic(definition) != diagnostics) continue;
        cards.push_back({&definition, magic_code_enabled(mask, definition.internal_index)});
    }
    if (cards.empty()) return;
    constexpr float kGap = 18.0F;
    const int fit = std::max(static_cast<int>((width + kGap) / (260.0F + kGap)), 1);
    const int columns = std::min(fit, static_cast<int>(cards.size()));
    const float card_width = std::floor((width - kGap * (columns - 1)) / columns);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    float y = origin.y;
    for (std::size_t first = 0U; first < cards.size(); first += columns) {
        const std::size_t last = std::min(first + columns, cards.size());
        float height = 0.0F;
        for (std::size_t index = first; index < last; ++index) {
            height = std::max(height, MeasureMagicCodeCard(cards[index], card_width));
        }
        if (ImGui::IsRectVisible({origin.x, y}, {origin.x + width, y + height + 5.0F})) {
            for (std::size_t index = first; index < last; ++index) {
                PaddockEnter enter(enter_index + static_cast<int>(index));
                DrawMagicCodeCard(cards[index],
                                  {origin.x + (card_width + kGap) * (index - first), y},
                                  card_width, height, diagnostics);
            }
        }
        y += height + kGap;
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, y - kGap - origin.y + 5.0F});
}

void DrawMagicCodesSection(float width, bool lobby_active) {
    using namespace dkr::runtime::magic_codes;
    const auto mask = selected_mask();
    std::size_t selected = 0U;
    for (const auto& definition : kMagicCodeDefinitions) {
        if (magic_code_enabled(mask, definition.internal_index)) ++selected;
    }
    {
        PaddockEnter enter(2);
        PaddockInlineNote(std::to_string(selected) + (selected == 1U ? " code selected" : " codes selected"),
                          "Conflicting codes are turned off automatically.", width);
        PaddockGap(18.0F);
    }
    {
        PaddockEnter enter(3);
        const bool was_open = g_mods_page.disclosures.contains("code-rules");
        bool open = was_open;
        PaddockDisclosure disclosure("##code-rules", "When and where do codes apply?", open, width);
        if (disclosure.Open()) {
            const PaddockType body = PaddockReading(13.0F, false, 1.55F);
            PaddockText(body, PaddockRgb(0xABC0CC),
                        "Changes apply on the next launch. Each code lists its supported modes. "
                        "Tracks-only codes do not affect Adventure.", disclosure.Inner());
            PaddockGap(13.0F);
            PaddockText(body, PaddockRgb(0xABC0CC),
                        "One-time actions stay queued until used. After granting a Golden Balloon, "
                        "let the game save before quitting.", disclosure.Inner());
        }
        disclosure.End();
        if (open != was_open) {
            if (open) g_mods_page.disclosures.insert("code-rules");
            else g_mods_page.disclosures.erase("code-rules");
        }
        PaddockGap(16.0F);
    }
    if (lobby_active) {
        PaddockText(PaddockReading(14.0F, false, 1.55F), PaddockRgb(0xFFF6DA),
                    "Leave the online lobby before changing Magic Codes. This session uses the codes "
                    "agreed with the host. Credits remain queued for offline play.", width);
        PaddockGap(13.0F);
    }
    PaddockGap(8.0F);
    ImGui::BeginDisabled(lobby_active);
    PaddockGap(24.0F);
    {
        PaddockEnter enter(4);
        PaddockSeparatorText("RACE MODIFIERS", width);
    }
    PaddockGap(16.0F);
    DrawMagicCodeGrid(width, false, 5);
    PaddockGap(13.0F);
    {
        PaddockEnter enter(7);
        const bool was_open = g_mods_page.disclosures.contains("diagnostics");
        bool open = was_open;
        PaddockDisclosure disclosure("##diagnostics", "Advanced: diagnostic codes", open, width);
        if (disclosure.Open()) DrawMagicCodeGrid(disclosure.Inner(), true, 7);
        disclosure.End();
        if (open != was_open) {
            if (open) g_mods_page.disclosures.insert("diagnostics");
            else g_mods_page.disclosures.erase("diagnostics");
        }
        PaddockGap(16.0F);
        PaddockGap(6.0F);
        if (PaddockButton("CLEAR ALL MAGIC CODES", PaddockButtonKind::Flat, width)) {
            std::string error;
            if (clear_all(error)) {
                SaveSettings();
                g_magic_codes_status = "All launch Magic Codes cleared.";
            } else {
                g_magic_codes_status = error;
            }
        }
    }
    ImGui::EndDisabled();
    const auto pending_queue_error = queue_error();
    if (!pending_queue_error.empty()) {
        PaddockGap(13.0F);
        PaddockText(PaddockReading(13.0F, false, 1.55F), PaddockRgb(0xFFBBA6),
                    "Magic Code queue could not be saved: " + pending_queue_error +
                        ". A completed action may still be queued on the next launch.", width);
    }
    PaddockFeedback(g_magic_codes_status, width);
}

// The player points the folder picker at the track's .dkrmap, at its own
// folder, or at the folder that holds both it and its <track>-hd.zip. This
// turns any of those into the thing install() takes: a .dkrmap directory or a
// .zip. Empty return means nothing usable was there.
std::filesystem::path ResolveTrackSource(const std::filesystem::path& chosen) {
    namespace fs = std::filesystem;
    std::error_code code;
    const std::string extension =
        dkr::runtime::texture_browser::lower_ascii(chosen.extension().string());
    if (extension == ".dkrmap" || extension == ".zip") {
        return chosen;
    }
    if (fs::is_regular_file(chosen / "manifest.json", code)) {
        return chosen;   // a .dkrmap folder that is not named one
    }
    // A parent folder: take the single .dkrmap inside, else a lone track .zip.
    fs::path dkrmap;
    fs::path zip;
    int dkrmap_count = 0;
    for (const auto& item : fs::directory_iterator(chosen, code)) {
        if (item.is_directory(code) &&
            dkr::runtime::texture_browser::lower_ascii(
                item.path().extension().string()) == ".dkrmap") {
            dkrmap = item.path();
            ++dkrmap_count;
        } else if (zip.empty() && item.is_regular_file(code) &&
                   dkr::runtime::texture_browser::lower_ascii(
                       item.path().extension().string()) == ".zip") {
            zip = item.path();
        }
    }
    if (dkrmap_count == 1) {
        return dkrmap;
    }
    if (dkrmap_count == 0 && !zip.empty()) {
        return zip;
    }
    return {};
}

// The second half of a track import, back on the UI thread: hand any HD pack to
// the texture-pack worker, report, and switch to Modern when it has to.
void FinishTrackImport(
    const dkr::runtime::custom_tracks::InstallOutcome& outcome) {
    namespace tracks_ns = dkr::runtime::custom_tracks;
    if (!outcome.hd_pack_archive.empty()) {
        const dkr::runtime::texture_packs::TrackPackOwner owner{
            outcome.track_id, outcome.hd_pack_digest};
        // The worker imports the pack (born enabled) and clears the temp.
        // Importing inline instead would convert the pack on the graphics
        // thread and stall it exactly as a picker does, so when a texture-pack
        // import is already running, skip; the player can import the folder
        // again once it is free.
        if (StartTexturePackImport(outcome.hd_pack_archive, &owner,
                                   outcome.temp_root)) {
            g_track_import_status =
                "Installed " + outcome.track_id +
                " and its HD textures. They load the next time the game starts.";
        } else {
            tracks_ns::discard_install_temp(outcome.temp_root);
            g_track_import_status =
                "Installed " + outcome.track_id + ". Another texture-pack "
                "import is already running - import this folder again in a "
                "moment to pick up its HD textures.";
        }
    } else {
        tracks_ns::discard_install_temp(outcome.temp_root);
        g_track_import_status = outcome.hd_pack_mismatch
            ? "Installed " + outcome.track_id +
                  ". Its -hd.zip is from a different export and was left out - "
                  "re-export the pair together."
            : "Installed " + outcome.track_id + ".";
    }

    EnsureModernForTracks();
    g_mods_page.section = kModsSectionLibrary;
    g_mods_page.category = 0;
    g_mods_page.entered_at = PaddockClock();
    ResetLibraryFilters(g_mod_browsers[0]);
}

// A track is a folder or a zip, so this is a folder picker rather than a file
// picker. The picker and the copy run on the dialog-job thread - see
// StartDialogJob for why they must never run on the graphics thread - and
// FinishTrackImport completes the import back on the UI thread.
void ImportTrackWithDialog() {
    const bool started = StartDialogJob([]() -> std::function<void()> {
        namespace tracks_ns = dkr::runtime::custom_tracks;
        std::filesystem::path chosen;
        std::string error;
        if (!PickFolder(chosen, error)) {
            return [error] { g_track_import_status = error; };
        }
        const std::filesystem::path source = ResolveTrackSource(chosen);
        if (source.empty()) {
            return [] {
                g_track_import_status =
                    "Pick the track's .dkrmap, its folder, or the folder that "
                    "holds both it and its -hd.zip.";
            };
        }
        tracks_ns::InstallOutcome outcome;
        if (!tracks_ns::install(source, error, &outcome)) {
            return [error] { g_track_import_status = error; };
        }
        return [outcome] { FinishTrackImport(outcome); };
    });
    g_track_import_status = started
        ? "Choose the track in the folder picker..."
        : "A picker is already open.";
}

void ChooseWorkingFolderWithDialog() {
    const bool started = StartDialogJob([]() -> std::function<void()> {
        std::filesystem::path chosen;
        std::string error;
        if (!PickFolder(chosen, error)) {
            return [error] { g_track_import_status = error; };
        }
        // The folder holds .dkrmap directories; it is not one itself. Accept
        // either, so picking the track folder by mistake still works.
        dkr::runtime::custom_tracks::set_working_directory(
            chosen.extension() == ".dkrmap" ? chosen.parent_path() : chosen);
        const std::size_t found =
            dkr::runtime::custom_tracks::tracks().size();
        return [found] {
            g_track_import_status =
                found == 0 ? "No .dkrmap folders found there yet."
                           : std::to_string(found) + " track(s) loaded.";
        };
    });
    g_track_import_status = started
        ? "Choose the folder in the folder picker..."
        : "A picker is already open.";
}

// A track shipped as a ZIP takes the same install path as a folder; only the
// picker differs. Like every picker it runs on the dialog-job thread.
void ImportTrackZipWithDialog() {
    const bool started = StartDialogJob([]() -> std::function<void()> {
        namespace tracks_ns = dkr::runtime::custom_tracks;
        if (NFD_Init() != NFD_OKAY) {
            return [] {
                g_track_import_status =
                    "The system file picker could not be initialized.";
            };
        }
        nfdu8char_t* result = nullptr;
        const nfdfilteritem_t filters[] = {{"DKR-R track", "zip"}};
        const nfdresult_t dialog =
            NFD_OpenDialogU8(&result, filters, 1, nullptr);
        std::filesystem::path source;
        std::string error;
        if (dialog == NFD_OKAY) {
            source = std::filesystem::u8path(result);
            NFD_FreePathU8(result);
        } else if (dialog == NFD_ERROR) {
            error = NFD_GetError();
        }
        NFD_Quit();
        if (source.empty()) {
            return [error] { g_track_import_status = error; };
        }
        tracks_ns::InstallOutcome outcome;
        if (!tracks_ns::install(source, error, &outcome)) {
            return [error] { g_track_import_status = error; };
        }
        return [outcome] { FinishTrackImport(outcome); };
    });
    g_track_import_status = started
        ? "Choose the track ZIP in the file picker..."
        : "A picker is already open.";
}

// A working-folder track's <track>-hd.zip goes to the texture-pack importer,
// filed against the track like an imported copy's pack.
void InstallWorkingHdPack(const dkr::runtime::custom_tracks::Track& track,
                          const dkr::runtime::custom_tracks::HdPack& hd) {
    const dkr::runtime::texture_packs::TrackPackOwner owner{track.id, hd.digest};
    if (StartTexturePackImport(hd.sibling_archive, &owner)) {
        g_track_import_status = "Installing HD textures for " + track.name +
            ". The track stays in your working folder.";
        EnsureModernForTracks();
    } else {
        g_track_import_status =
            "Another texture-pack import is already running. Try again in a moment.";
    }
}

std::string PathLeaf(const std::filesystem::path& path) {
    return PathUtf8(path.filename());
}

bool IsWorkingFolderTrack(const dkr::runtime::custom_tracks::Track& track,
                          const std::filesystem::path& working) {
    if (working.empty()) return false;
    std::error_code error;
    return std::filesystem::equivalent(track.source.parent_path(), working, error) ||
           track.source.parent_path() == working;
}

std::vector<DkrLibraryTrack> BuildDkrLibrary(
    const std::vector<dkr::runtime::custom_tracks::Track>& tracks) {
    namespace tracks_ns = dkr::runtime::custom_tracks;
    namespace packs_ns = dkr::runtime::texture_packs;
    std::vector<DkrLibraryTrack> library;
    library.reserve(tracks.size());
    for (const tracks_ns::Track& track : tracks) {
        DkrLibraryTrack entry;
        entry.id = track.id;
        entry.name = track.name.empty() ? track.id : track.name;
        entry.author = track.author;
        entry.source = PathLeaf(track.source);
        entry.installed = tracks_ns::is_installed(track);
        for (const auto& payload : track.entries) entry.bytes += payload.bytes.size();
        entry.enabled = track.enabled;
        entry.kept_online = track.kept_online;
        if (!track.hd_pack_file.empty()) {
            const packs_ns::TrackPackState pack =
                packs_ns::track_pack_state(track.id, track.hd_pack_digest);
            entry.hd_textures = pack.installed && pack.enabled;
            if (pack.installed) entry.hd_pack_id = pack.pack_id;
        }
        library.push_back(std::move(entry));
    }
    return library;
}

// A plain paddock card that grows with what is drawn inside it.
template <typename Content>
void DrawTaskCard(float width, bool testing, Content&& content) {
    PaddockBox box(width, {20.0F, 20.0F});
    content(box.Inner());
    box.End([testing](ImDrawList* draw, ImVec2 a, ImVec2 b) {
        PaddockPanelStyle panel;
        panel.radii = PaddockRound(14.0F);
        panel.fill = PaddockRgb(0x122A37);
        panel.ring = testing ? PaddockRgb(0x54C9AD, 128U) : PaddockRgb(0xFFFFFF, 18U);
        panel.ring_width = 1.0F;
        PaddockPanel(draw, a, b, panel);
    });
}

void DrawTaskHeading(std::string_view text, float width) {
    PaddockHeading(text, 23.0F, 0xFFF0C2, width);
    PaddockGap(8.0F);
}

void DrawTaskParagraph(std::string_view text, float width) {
    PaddockText(PaddockReading(13.0F, false, 1.6F), PaddockRgb(0xABC0CC), text, width);
    PaddockGap(13.0F);
}

void DrawToolIntro(std::string_view text, float width) {
    PaddockText(PaddockReading(14.0F, false, 1.55F), PaddockRgb(0xABC0CC), text, width);
    PaddockGap(24.0F);
}

// A heading with a link on the right (.mods-library-heading).
bool DrawHeadingWithLink(std::string_view heading, const char* link, bool link_disabled,
                         float width) {
    const PaddockType type = PaddockSign(23.0F, 1.25F);
    const float link_width = PaddockButtonWidth(link, PaddockButtonKind::Link);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = std::max(type.line, 42.0F);
    PaddockTextStyle style;
    style.colour = PaddockCol(0xFFF0C2);
    style.shadow = PaddockCol(0x031623);
    PaddockTextAt(ImGui::GetWindowDrawList(), type, {at.x, at.y + (height - type.line) * 0.5F},
                  width - link_width - 12.0F, heading, style);
    ImGui::SetCursorScreenPos({at.x + width - link_width, at.y + (height - 42.0F) * 0.5F});
    ImGui::BeginDisabled(link_disabled);
    const bool pressed = PaddockButton(link, PaddockButtonKind::Link);
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, height});
    return pressed;
}

void DrawTrackLabTrack(const dkr::runtime::custom_tracks::Track& track, float width,
                       const std::string& armed, bool locked, bool in_game) {
    namespace tracks_ns = dkr::runtime::custom_tracks;
    namespace packs_ns = dkr::runtime::texture_packs;
    const bool is_armed = !armed.empty() && armed == track.id;
    const std::int32_t level = tracks_ns::resolved_level_id(track.id);
    const tracks_ns::HdPack hd = tracks_ns::hd_pack(track.id);
    const bool declares_pack = !hd.file.empty();
    const packs_ns::TrackPackState pack_state = declares_pack
        ? packs_ns::track_pack_state(track.id, hd.digest)
        : packs_ns::TrackPackState{};
    const bool pack_ready = pack_state.installed && pack_state.enabled;
    const bool published = tracks_ns::track_textures_published(track.id);
    const bool needs_relaunch = pack_ready && !published;

    ImGui::PushID(track.id.c_str());
    DrawTaskCard(width, is_armed, [&](float inner) {
        const char* action = is_armed ? "STOP TESTING"
            : declares_pack && pack_ready ? "PLAY IN HD" : "RACE THIS";
        const PaddockButtonKind kind = is_armed ? PaddockButtonKind::Stop : PaddockButtonKind::Flat;
        const float action_width = PaddockButtonWidth(action, kind);
        const float info_width = std::max(inner - action_width - 20.0F, 1.0F);

        // The name, who made it and what artwork it brings.
        const ImVec2 row = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        PaddockHeading(track.name, 23.0F, 0xFFF0C2, info_width);
        PaddockGap(5.0F);
        const PaddockType muted = PaddockReading(14.0F, false, 1.55F);
        const std::string author = track.author.empty() ? "unknown author" : track.author;
        PaddockText(muted, PaddockRgb(0xABC0CC),
                    level >= 0 ? author + "  -  level " + std::to_string(level)
                               : author + "  -  level assigned at launch",
                    info_width);
        const tracks_ns::ArtworkSummary art = tracks_ns::artwork(track.id);
        if (art.textures > 0U || art.minimap) {
            std::string line;
            if (art.textures > 0U) {
                line = std::to_string(art.textures) +
                       (art.textures == 1U ? " texture" : " textures");
                if (art.translucent > 0U) line += ", " + std::to_string(art.translucent) + " see-through";
                if (art.animated > 0U) line += ", " + std::to_string(art.animated) + " animated";
            }
            if (art.minimap) line += line.empty() ? "own minimap" : ", own minimap";
            PaddockGap(5.0F);
            PaddockText(muted, PaddockRgb(0xABC0CC), line, info_width);
        }
        ImGui::EndGroup();
        const float info_height = ImGui::GetItemRectSize().y;
        ImGui::SetCursorScreenPos({row.x + inner - action_width,
                                   row.y + std::max((info_height - 42.0F) * 0.5F, 0.0F)});
        ImGui::BeginDisabled(locked);
        if (PaddockButton(action, kind)) {
            if (is_armed) {
                tracks_ns::arm_track_override(std::string{});
            } else {
                tracks_ns::arm_track_override(track.id);
                EnsureModernForTracks();
                // From the launcher there is no process to relaunch, so skipping
                // the menus on the coming boot is how the track's textures load.
                if (needs_relaunch && !in_game) tracks_ns::set_auto_boot(true);
            }
        }
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(row);
        ImGui::Dummy({inner, std::max(info_height, 42.0F)});

        // One status line for the track's HD pack.
        if (declares_pack) {
            const char* hd_line = "HD textures: pack not installed";
            unsigned colour = 0xABC0CC;
            if (pack_ready && published) {
                hd_line = "HD textures: ready";
                colour = 0x1AC2A3;
            } else if (pack_ready) {
                hd_line = "HD textures: restart to load";
                colour = 0xFFAB14;
            } else if (hd.sibling_mismatch ||
                       (pack_state.installed && !pack_state.digest_matches)) {
                hd_line = "HD textures: pack does not match this export";
                colour = 0xFFAB14;
            } else if (pack_state.installed) {
                hd_line = "HD textures: pack disabled";
            }
            PaddockGap(10.0F);
            const ImVec2 line = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(line, {line.x + inner, line.y + 1.0F}, PaddockCol(0xFFFFFF, 18U));
            const bool can_install = !hd.sibling_archive.empty() &&
                (!pack_state.installed || !pack_state.digest_matches);
            const char* install = pack_state.installed ? "Update HD textures" : "Install HD textures";
            float buttons = 0.0F;
            if (pack_state.installed) buttons += PaddockButtonWidth("Manage", PaddockButtonKind::Flat) + 12.0F;
            if (can_install) buttons += PaddockButtonWidth(install, PaddockButtonKind::Flat) + 12.0F;
            const PaddockType status_type = PaddockReading(12.0F, false, 1.5F);
            const float top = line.y + 11.0F;
            const float row_height = buttons > 0.0F ? 42.0F : status_type.line;
            PaddockTextStyle style;
            style.colour = PaddockCol(colour);
            PaddockTextAt(draw, status_type, {line.x, top + (row_height - status_type.line) * 0.5F},
                          inner - buttons, hd_line, style);
            float x = line.x + inner;
            ImGui::BeginDisabled(locked);
            if (can_install) {
                x -= PaddockButtonWidth(install, PaddockButtonKind::Flat);
                ImGui::SetCursorScreenPos({x, top});
                if (PaddockButton(install, PaddockButtonKind::Flat)) {
                    InstallWorkingHdPack(track, hd);
                }
                x -= 12.0F;
            }
            ImGui::EndDisabled();
            if (pack_state.installed) {
                x -= PaddockButtonWidth("Manage", PaddockButtonKind::Flat);
                ImGui::SetCursorScreenPos({x, top});
                if (PaddockButton("Manage", PaddockButtonKind::Flat)) {
                    g_texture_pack_manage_id = pack_state.pack_id;
                    g_texture_pack_manage_request = true;
                }
            }
            ImGui::SetCursorScreenPos(line);
            ImGui::Dummy({inner, 11.0F + row_height});
        }

        // The 3D texture table is published once at boot, so a track added
        // since needs a relaunch. In game that is one click.
        if (needs_relaunch && in_game) {
            PaddockGap(12.0F);
            if (PaddockButton("RESTART & PLAY IN HD", PaddockButtonKind::Primary, inner)) {
                tracks_ns::arm_track_override(track.id);
                EnsureModernForTracks();
                tracks_ns::set_auto_boot(true);
                SaveSettings();
                g_lifecycle_request.store(dkr::runtime::ui::LifecycleRequest::Restart,
                                          std::memory_order_release);
                g_overlay_visible.store(false, std::memory_order_release);
            }
        }
    });
    ImGui::PopID();
}

// Track Lab: author tools for .dkrmap tracks read in place from a working
// folder. Arming a track sends every race the player starts to it.
void DrawTrackLabSection(float width, bool locked,
                         const std::vector<dkr::runtime::custom_tracks::Track>& tracks) {
    namespace tracks_ns = dkr::runtime::custom_tracks;
    const std::filesystem::path working = tracks_ns::working_directory();
    const std::string armed = tracks_ns::armed_track_id();
    const bool in_game = g_overlay_visible.load(std::memory_order_acquire);
    const bool picking = DialogJobRunning();
    const bool lab_locked = locked || picking;
    std::vector<const tracks_ns::Track*> watched;
    for (const auto& track : tracks) {
        if (IsWorkingFolderTrack(track, working)) watched.push_back(&track);
    }
    const bool auto_boot = tracks_ns::auto_boot_enabled();

    {
        PaddockEnter enter(2);
        // Track Lab draws in Accurate too - the list and arming. Every arm path
        // goes through Modern first, so Accurate never loads a custom track.
        if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
            PaddockGap(14.0F);
            PaddockAlert("Testing a track uses the Modern graphics profile. Selecting a test "
                         "track switches to it automatically.", width);
            PaddockGap(14.0F);
        }
        PaddockFeedback(g_track_lab_modern_notice, width);
    }
    {
        PaddockEnter enter(3);
        PaddockGap(20.0F);
        DrawTaskCard(width, false, [&](float inner) {
            DrawTaskHeading("Work on a track", inner);
            DrawTaskParagraph("Keep your .dkrmap tracks in one working folder. Re-export in place, "
                              "then rescan to find updates and new tracks. Remove a track from this "
                              "folder to remove it from the list.", inner);
            DrawTaskParagraph("Track files stay in this folder. HD texture packs are installed "
                              "separately in AppData.", inner);
            if (!working.empty()) PaddockFeedback("Watching: " + PathUtf8(working), inner);
            ImGui::BeginDisabled(lab_locked);
            if (PaddockButton(working.empty() ? "Choose working folder" : "Change folder",
                              PaddockButtonKind::Settings)) {
                ChooseWorkingFolderWithDialog();
            }
            if (!working.empty()) {
                ImGui::SameLine(0.0F, 10.0F);
                if (PaddockButton("Stop watching", PaddockButtonKind::Settings)) {
                    tracks_ns::set_working_directory({});
                    tracks_ns::arm_track_override(std::string{});
                    tracks_ns::set_auto_boot(false);
                }
            }
            ImGui::EndDisabled();
        });
        PaddockGap(26.0F);
    }
    {
        PaddockEnter enter(4);
        PaddockFeedback(g_track_import_status, width);
        const std::string heading = "Test tracks (" + std::to_string(watched.size()) + ")";
        if (DrawHeadingWithLink(heading, "Rescan tracks", lab_locked || working.empty(), width)) {
            tracks_ns::reload();
            const std::size_t found = tracks_ns::tracks().size();
            g_track_import_status = std::to_string(found) + " track(s) after rescan.";
        }
        PaddockGap(20.0F);
    }
    {
        PaddockEnter enter(5);
        if (watched.empty()) {
            PaddockBox box(width, {24.0F, 48.0F});
            const PaddockType heading = PaddockSign(23.0F, 1.25F);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            PaddockTextStyle heading_style;
            heading_style.colour = PaddockCol(0xFFF0C2);
            heading_style.shadow = PaddockCol(0x031623);
            heading_style.centre = true;
            float y = PaddockTextAt(ImGui::GetWindowDrawList(), heading, at, box.Inner(),
                                    "No test tracks yet", heading_style, true) + 13.0F;
            PaddockTextStyle body_style;
            body_style.colour = PaddockCol(0xABC0CC);
            body_style.centre = true;
            y += PaddockTextAt(ImGui::GetWindowDrawList(), PaddockReading(14.0F, false, 1.55F),
                               {at.x, at.y + y}, box.Inner(),
                               "Choose the folder containing your .dkrmap tracks to start testing.",
                               body_style) + 13.0F;
            ImGui::Dummy({box.Inner(), y});
            box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
                PaddockPanelStyle panel;
                panel.radii = PaddockRound(14.0F);
                panel.fill = PaddockRgb(0xFFFFFF, 4U);
                panel.ring = PaddockRgb(0xFFFFFF, 13U);
                panel.ring_width = 1.0F;
                PaddockPanel(draw, a, b, panel);
            });
        } else {
            DrawToolIntro("Select a track, then launch the game. Races load that track until you "
                          "stop testing or choose a course in Track Select.", width);
            for (std::size_t index = 0U; index < watched.size(); ++index) {
                if (index != 0U) PaddockGap(12.0F);
                DrawTrackLabTrack(*watched[index], width, armed, lab_locked, in_game);
            }
        }
    }
    // Shown whenever it could matter, so a setting left over from a removed
    // track can always be turned off.
    if (!watched.empty() || auto_boot || !armed.empty()) {
        PaddockEnter enter(6);
        PaddockGap(22.0F);
        PaddockBox box(width, {18.0F, 18.0F});
        bool boot = auto_boot;
        ImGui::BeginDisabled(lab_locked);
        if (PaddockCheckbox("##auto-boot", "Skip the menus on every launch", &boot, box.Inner())) {
            tracks_ns::set_auto_boot(boot);
        }
        ImGui::EndDisabled();
        PaddockGap(6.0F);
        PaddockText(PaddockReading(13.0F, false, 1.55F), PaddockRgb(0xABC0CC),
                    "Start directly in the selected test track as Diddy, in single player. Stays on "
                    "until you turn it off. Quit a race to reach the menus; L+Z reloads the track.",
                    box.Inner());
        if (!armed.empty()) {
            const auto found = std::find_if(tracks.begin(), tracks.end(),
                [&armed](const tracks_ns::Track& track) { return track.id == armed; });
            PaddockFeedback("Selected for testing: " +
                                (found != tracks.end() ? found->name : armed),
                            box.Inner());
        }
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockFill(draw, a, b, PaddockRound(12.0F), PaddockCol(0xFFFFFF, 4U));
        });
    }
}

// One shared single-pack modal, reachable from a texture browser card's
// MANAGE button and from a Track Lab "Manage" line. Both pages render it, and
// it is only touched when it might be open.
void DrawSharedTexturePackModal() {
    if (!g_texture_pack_manage_request &&
        !ImGui::IsPopupOpen("Manage texture pack") &&
        !ImGui::IsPopupOpen("Remove texture pack?")) {
        return;
    }
    if (g_texture_pack_manage_request) {
        ImGui::OpenPopup("Manage texture pack");
        g_texture_pack_manage_request = false;
    }
    const bool request_remove_modal = DrawTexturePackManagementModal(
        dkr::runtime::texture_packs::snapshot(true));
    if (request_remove_modal) ImGui::OpenPopup("Remove texture pack?");
    DrawTexturePackRemovalModal();
}

// A disclosure whose open state lives with the page.
template <typename Content>
void DrawPageDisclosure(const std::string& key, const std::string& label, float width,
                        Content&& content) {
    const bool was_open = g_mods_page.disclosures.contains(key);
    bool open = was_open;
    PaddockDisclosure disclosure(("##" + key).c_str(), label.c_str(), open, width);
    if (disclosure.Open()) content(disclosure.Inner());
    disclosure.End();
    if (open != was_open) {
        if (open) g_mods_page.disclosures.insert(key);
        else g_mods_page.disclosures.erase(key);
    }
}

void DrawModHelpSection(float width, const dkr::mods::ModLibraryView& mods, bool locked) {
    using dkr::mods::TrackCatalog;
    const PaddockType note = PaddockReading(13.0F, false, 1.6F);
    const bool library_locked = locked || mods.busy;
    {
        PaddockEnter enter(2);
        PaddockGap(20.0F);
        const bool two_columns = width >= 640.0F;
        const float card_width = two_columns ? std::floor((width - 16.0F) * 0.5F) : width;
        const float text_width = card_width - 40.0F;
        const PaddockType heading = PaddockSign(23.0F, 1.25F);
        constexpr std::array<std::string_view, 3> steps{{
            "Use Import mods to choose a legacy patch or a DKR track.",
            "Load your original ROM in Play. Legacy patches need it for preparation; DKR tracks can be "
            "imported before this step.",
            "Enable legacy tracks for Track Select. Installed DKR tracks appear automatically in the "
            "in-game track menu.",
        }};
        constexpr std::array<std::string_view, 2> safety{{
            "Each enabled legacy mod set uses separate Adventure saves and Controller Paks.",
            "Hiding a legacy mod turns it off and keeps its files. Removing it keeps your saves and "
            "original files.",
        }};
        const auto list_height = [&] {
            float height = 0.0F;
            for (std::size_t index = 0U; index < steps.size(); ++index) {
                if (index != 0U) height += 10.0F;
                height += PaddockTextHeight(note, steps[index], text_width - 18.0F);
            }
            return height;
        };
        const float heading_a = PaddockTextHeight(heading, "From import to race", text_width, true);
        const float heading_b = PaddockTextHeight(heading, "Your progress stays safe", text_width, true);
        const float card_a = 40.0F + heading_a + 8.0F + list_height();
        const float card_b = 40.0F + heading_b + 8.0F +
            PaddockTextHeight(note, safety[0], text_width) + 13.0F +
            PaddockTextHeight(note, safety[1], text_width);
        const float shared = two_columns ? std::max(card_a, card_b) : 0.0F;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const auto card = [&](ImVec2 at, float min_height, const auto& content) {
            ImGui::SetCursorScreenPos(at);
            PaddockBox box(card_width, {20.0F, 20.0F}, min_height);
            content(box.Inner());
            return box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
                PaddockPanelStyle panel;
                panel.radii = PaddockRound(14.0F);
                panel.fill = PaddockRgb(0x122A37);
                panel.ring = PaddockRgb(0xFFFFFF, 18U);
                panel.ring_width = 1.0F;
                PaddockPanel(draw, a, b, panel);
            });
        };
        const float first = card(origin, shared, [&](float inner) {
            DrawTaskHeading("From import to race", inner);
            for (std::size_t index = 0U; index < steps.size(); ++index) {
                if (index != 0U) PaddockGap(10.0F);
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const std::string number = std::to_string(index + 1U) + ". ";
                const float number_width = PaddockMeasure(note, number);
                PaddockDrawRun(ImGui::GetWindowDrawList(), note, {at.x + 18.0F - number_width, at.y},
                               PaddockCol(0xABC0CC), number.data(), number.data() + number.size());
                ImGui::SetCursorScreenPos({at.x + 18.0F, at.y});
                PaddockText(note, PaddockRgb(0xABC0CC), steps[index], inner - 18.0F);
                ImGui::SetCursorScreenPos({at.x, ImGui::GetCursorScreenPos().y});
                ImGui::Dummy({0.0F, 0.0F});
            }
        });
        const ImVec2 second_at = two_columns ? ImVec2{origin.x + card_width + 16.0F, origin.y}
                                             : ImVec2{origin.x, origin.y + first + 16.0F};
        const float second = card(second_at, shared, [&](float inner) {
            DrawTaskHeading("Your progress stays safe", inner);
            DrawTaskParagraph(safety[0], inner);
            PaddockText(note, PaddockRgb(0xABC0CC), safety[1], inner);
        });
        const float total = two_columns ? std::max(first, second) : first + 16.0F + second;
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy({width, total});
        PaddockGap(26.0F);
    }
    {
        PaddockEnter enter(3);
        DrawPageDisclosure("versions", "ROM versions and compatibility", width, [](float inner) {
            PaddockText(PaddockReading(13.0F, false, 1.55F), PaddockRgb(0xABC0CC),
                        "Legacy patches need their exact source ROM. Tracks are prepared for your "
                        "imported US v1.0 or v1.1 ROMs; characters are validated for either version. "
                        "Native DKR tracks do not need patch preparation.", inner);
        });
        PaddockGap(16.0F);
    }
    {
        PaddockEnter enter(4);
        ImGui::BeginDisabled(library_locked);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        float x = at.x;
        float y = at.y;
        constexpr std::array<const char*, 3> actions{{
            "Refresh library", "Turn off legacy tracks", "Turn off all characters"}};
        for (std::size_t index = 0U; index < actions.size(); ++index) {
            const float button = PaddockButtonWidth(actions[index], PaddockButtonKind::Settings);
            if (x > at.x && x + button > at.x + width) {
                x = at.x;
                y += 52.0F;
            }
            ImGui::SetCursorScreenPos({x, y});
            if (PaddockButton(actions[index], PaddockButtonKind::Settings)) {
                if (index == 0U) {
                    g_legacy_imports.refresh();
                } else {
                    g_legacy_imports.disable_all(index == 1U ? TrackCatalog::Kind::Track
                                                             : TrackCatalog::Kind::Character);
                }
            }
            x += button + 10.0F;
        }
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({width, y + 42.0F - at.y});
        PaddockGap(14.0F);
        if (!mods.result.empty()) PaddockFeedback(mods.result, width);
    }
    {
        PaddockEnter enter(5);
        PaddockGap(16.0F);
        PaddockHeading("Legacy import history", 23.0F, 0xFFF0C2, width);
        PaddockGap(8.0F);
        DrawToolIntro("Preparing an import again adds support for your current ROM and reinstalls "
                      "removed entries. Hidden mods stay hidden.", width);
    }
    PaddockEnter enter(6);
    if (mods.imports == nullptr || mods.imports->items.empty()) {
        PaddockText(PaddockReading(14.0F, false, 1.55F), PaddockRgb(0xFFF6DA),
                    "No imports yet. Start with Import mods.", width);
        return;
    }
    // Group every item under its review, in first-seen order.
    std::vector<std::string> order;
    std::map<std::string, std::vector<const dkr::mods::ImportReviewItem*>> groups;
    for (const auto& item : mods.imports->items) {
        auto [found, added] = groups.try_emplace(item.review);
        if (added) order.push_back(item.review);
        found->second.push_back(&item);
    }
    for (std::size_t group = 0U; group < order.size(); ++group) {
        const auto& items = groups[order[group]];
        std::string title;
        for (const auto* item : items) {
            if (!title.empty()) title += ", ";
            title += item->name;
        }
        ImGui::PushID(static_cast<int>(group));
        DrawPageDisclosure("import-" + order[group], title, width, [&](float inner) {
            for (const auto* item : items) {
                PaddockGap(12.0F);
                PaddockText(PaddockReading(14.0F, true, 1.55F), PaddockRgb(0xFFF6DA),
                            item->name + " / " + item->kind, inner);
                for (std::string warning : item->warnings) {
                    for (std::size_t at = warning.find("Game Pak"); at != std::string::npos;
                         at = warning.find("Game Pak", at)) {
                        warning.replace(at, 8U, "ROM");
                    }
                    PaddockGap(4.0F);
                    PaddockText(PaddockReading(13.0F, false, 1.55F), PaddockRgb(0xFFCF83),
                                warning, inner);
                }
                PaddockGap(12.0F);
            }
            ImGui::BeginDisabled(library_locked || g_mod_browser_revision == 0U);
            if (PaddockButton("Prepare this import again", PaddockButtonKind::Settings)) {
                g_legacy_imports.prepare_review(order[group], ModReviewRoms());
            }
            ImGui::EndDisabled();
        });
        ImGui::PopID();
        PaddockGap(16.0F);
    }
}

void DrawModsLibrarySection(float width, const dkr::mods::ModLibraryView& mods,
                            bool mods_locked,
                            const std::vector<DkrLibraryTrack>& dkr_tracks) {
    const bool locked = mods_locked || mods.busy;
    const std::size_t track_count = g_mod_browsers[0].all.size() + dkr_tracks.size();
    const std::size_t character_count = g_mod_browsers[1].all.size();
    {
        PaddockEnter enter(0);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const int chosen = DrawLibraryCategories(g_mods_page.category, track_count, character_count);
        const float row = ImGui::GetItemRectSize().y;
        if (chosen != g_mods_page.category) {
            g_mods_page.category = chosen;
            g_mods_page.entered_at = PaddockClock();
        }
        const float refresh = PaddockButtonWidth("Refresh library", PaddockButtonKind::Link);
        ImGui::SetCursorScreenPos({at.x + width - refresh, at.y + (row - 42.0F) * 0.5F});
        ImGui::BeginDisabled(locked);
        if (PaddockButton("Refresh library##library", PaddockButtonKind::Link)) {
            g_legacy_imports.refresh();
            dkr::runtime::custom_tracks::reload();
        }
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({width, row});
        PaddockGap(20.0F);
    }
    const bool characters = g_mods_page.category == 1;
    if (!characters) {
        PaddockEnter enter(1);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const PaddockType type = PaddockReading(12.0F, false, 1.5F);
        const ImVec2 tag = PaddockFormatTagSize(PaddockFormat::Legacy);
        float x = at.x;
        const float text_top = at.y + (tag.y - type.line) * 0.5F;
        PaddockFormatTag(draw, {x, at.y}, PaddockFormat::Legacy);
        x += tag.x + 8.0F;
        constexpr std::string_view legacy = "Original-ROM patches";
        PaddockDrawRun(draw, type, {x, text_top}, PaddockCol(0xC0D4DF), legacy.data(),
                       legacy.data() + legacy.size());
        x += PaddockMeasure(type, legacy) + 8.0F + 12.0F;
        PaddockFormatTag(draw, {x, at.y}, PaddockFormat::Dkr);
        x += PaddockFormatTagSize(PaddockFormat::Dkr).x + 8.0F;
        constexpr std::string_view native = "Native .dkrmap tracks";
        PaddockDrawRun(draw, type, {x, text_top}, PaddockCol(0xC0D4DF), native.data(),
                       native.data() + native.size());
        ImGui::Dummy({width, tag.y});
        PaddockGap(16.0F);
        PaddockFeedback(g_track_import_status, width);
    }
    DrawModCardBrowser(width, characters, mods, mods_locked, dkr_tracks, [] {
        g_mods_page.request_import_chooser = true;
    });
    PaddockEnter enter(7);
    PaddockGap(22.0F);
    PaddockText(PaddockReading(12.0F, false, 1.5F), PaddockRgb(0x9CB2BE),
                "Installed DKR tracks appear automatically in the in-game track menu. Enable legacy "
                "mods to include them. Track Lab is for development and testing.", width);
}

// Card caches follow the library snapshots; PLAY reads them too.
void RefreshModBrowserCards(const dkr::mods::ModLibraryView& mods) {
    for (const bool characters : {false, true}) {
        auto& browser = g_mod_browsers[characters ? 1 : 0];
        const auto snapshot = characters ? mods.characters : mods.tracks;
        if (browser.snapshot != snapshot) {
            browser.snapshot = snapshot;
            browser.all = snapshot != nullptr
                ? dkr::mods::browser::cards(*snapshot, characters)
                : std::vector<dkr::mods::browser::Card>{};
            browser.filter_key.clear();
        }
    }
}

void DrawModsHacks(float available_width, bool game_running = false) {
    namespace tracks_ns = dkr::runtime::custom_tracks;
    // Each visit starts with every section closed, unless a link asked for one.
    const int frame = ImGui::GetFrameCount();
    if (g_mods_page.context != ImGui::GetCurrentContext() ||
        g_mods_page.last_frame != frame - 1) {
        g_mods_page.context = ImGui::GetCurrentContext();
        g_mods_page.section = std::exchange(g_mods_page.entry_section, -1);
        g_mods_page.filters_open = false;
        g_mods_page.disclosures.clear();
    }
    g_mods_page.last_frame = frame;

    const float width = game_running ? std::min(available_width, 1400.0F)
                                      : std::max(available_width, 1.0F);
    const float indent = std::floor((available_width - width) * 0.5F);
    if (indent > 0.0F) ImGui::Indent(indent);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});

    const auto mods = g_legacy_imports.snapshot();
    const bool lobby = dkr::runtime::netplay::session().active();
    const bool mods_locked = game_running || lobby || g_mod_launch.snapshot().modal || OnlineModSelectionLocked();
    const bool picking = DialogJobRunning();
    const unsigned revision = g_mod_browser_revision;
    const std::vector<tracks_ns::Track> tracks = tracks_ns::tracks();
    const std::vector<DkrLibraryTrack> dkr_tracks = BuildDkrLibrary(tracks);
    const std::string armed = tracks_ns::armed_track_id();
    const auto go_to_play = [] { g_page_navigation_request = kPagePlay; };
    // Card caches follow the library snapshots even while My mods is closed.
    RefreshModBrowserCards(mods);

    // Header: the page title and the import sign.
    {
        const ImVec2 header = ImGui::GetCursorScreenPos();
        const char* import_label = "Import mods";
        const float import_width = PaddockImportButtonWidth(import_label);
        const bool stacked = width < 700.0F;
        const float text_width = stacked ? width : width - import_width - 24.0F;
        ImGui::BeginGroup();
        DrawPageHeading("MODS / HACKS");
        PaddockGap(9.0F);
        PaddockText(PaddockReading(14.0F, false, 1.55F), PaddockRgb(0xABC0CC),
                    "Your tracks, racers and race modifiers.", text_width);
        ImGui::EndGroup();
        const float title_height = ImGui::GetItemRectSize().y;
        const ImVec2 import_at = stacked
            ? ImVec2{header.x, header.y + title_height + 16.0F}
            : ImVec2{header.x + width - import_width,
                     header.y + std::round((title_height - 56.0F) * 0.5F)};
        ImGui::SetCursorScreenPos(import_at);
        ImGui::BeginDisabled(mods_locked || mods.busy || picking);
        if (PaddockImportButton(import_label, width < 420.0F ? width : 0.0F)) {
            g_mods_page.request_import_chooser = true;
        }
        ImGui::EndDisabled();
        const float header_height = stacked ? title_height + 16.0F + 56.0F
                                            : std::max(title_height, 56.0F);
        ImGui::SetCursorScreenPos(header);
        ImGui::Dummy({width, header_height});
        PaddockGap(26.0F);
    }

    // The ROM line.
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        std::size_t enabled = 0U;
        for (const auto& snapshot : {mods.tracks, mods.characters}) {
            if (snapshot == nullptr) continue;
            std::set<std::string> counted;
            for (const auto& item : snapshot->tracks) {
                if (item.enabled && !item.hidden && counted.insert(item.id).second) ++enabled;
            }
        }
        const std::string status = revision != 0U
            ? std::to_string(enabled) + " legacy mods enabled. Changes apply on the next launch."
            : "No ROM loaded. Choose your original Diddy Kong Racing ROM to play or prepare legacy mods.";
        const char* link = revision != 0U ? "Go to Play" : "Choose ROM";
        const PaddockType type = PaddockReading(13.0F, false, 1.55F);
        const PaddockType link_type = PaddockReading(14.0F, true, 1.5F);
        const float link_width = PaddockMeasure(link_type, link);
        const bool side_by_side = width - link_width - 24.0F >= 300.0F;
        const float text_width = side_by_side ? width - link_width - 24.0F : width;
        const float text_height = PaddockTextHeight(type, status, text_width);
        float height = 4.0F;
        if (side_by_side) {
            const float row = std::max(text_height, 40.0F);
            PaddockTextStyle style;
            style.colour = PaddockCol(0xC3D3DC);
            PaddockTextAt(ImGui::GetWindowDrawList(), type,
                          {at.x, at.y + 4.0F + (row - text_height) * 0.5F}, text_width, status, style);
            ImGui::SetCursorScreenPos({at.x + width - link_width, at.y + 4.0F + (row - 40.0F) * 0.5F});
            if (PaddockTextLink(link)) go_to_play();
            height += row;
        } else {
            ImGui::SetCursorScreenPos({at.x, at.y + 4.0F});
            PaddockText(type, PaddockRgb(0xC3D3DC), status, width);
            PaddockGap(12.0F);
            if (PaddockTextLink(link)) go_to_play();
            height += text_height + 12.0F + 40.0F;
        }
        height += 16.0F;
        ImGui::GetWindowDrawList()->AddRectFilled({at.x, at.y + height}, {at.x + width, at.y + height + 1.0F},
                                                  PaddockCol(0x325365));
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({width, height + 1.0F});
    }

    // The armed Track Lab track.
    if (!armed.empty()) {
        const auto found = std::find_if(tracks.begin(), tracks.end(),
            [&armed](const tracks_ns::Track& track) { return track.id == armed; });
        const std::string name = found != tracks.end() ? found->name : armed;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const PaddockType type = PaddockReading(14.0F, false, 1.5F);
        const PaddockType strong = PaddockReading(14.0F, true, 1.5F);
        constexpr std::string_view lead = "Track Lab test: ";
        const float text_top = at.y + 8.0F + (42.0F - type.line) * 0.5F;
        PaddockDrawRun(draw, type, {at.x, text_top}, PaddockCol(0xC6DCE6), lead.data(),
                       lead.data() + lead.size());
        const std::string shown = PaddockEllipsize(strong, name, width - 160.0F - PaddockMeasure(type, lead));
        PaddockDrawRun(draw, strong, {at.x + PaddockMeasure(type, lead), text_top},
                       PaddockCol(0xFFE4A5), shown.data(), shown.data() + shown.size());
        const float stop = PaddockButtonWidth("Stop testing", PaddockButtonKind::Link);
        ImGui::SetCursorScreenPos({at.x + width - stop, at.y + 8.0F});
        ImGui::BeginDisabled(mods_locked || mods.busy);
        if (PaddockButton("Stop testing##armed", PaddockButtonKind::Link)) {
            tracks_ns::arm_track_override(std::string{});
            tracks_ns::set_auto_boot(false);
        }
        ImGui::EndDisabled();
        draw->AddRectFilled({at.x, at.y + 58.0F}, {at.x + width, at.y + 59.0F}, PaddockCol(0x325365));
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({width, 59.0F});
    }

    // What needs attention.
    bool mismatch = false;
    if (revision != 0U) {
        for (const auto& browser : g_mod_browsers) {
            for (const auto& card : browser.all) {
                if (card.item.enabled && !card.item.hidden &&
                    !dkr::mods::browser::compatible(card, revision)) {
                    mismatch = true;
                }
            }
        }
    }
    if (mismatch) {
        PaddockGap(14.0F);
        PaddockAlert("Some legacy mods were prepared for a different ROM version. Use Prepare mod "
                     "on their cards.", width);
    }
    if (game_running) {
        PaddockGap(14.0F);
        PaddockAlert("Browsing is available. Return to the launcher and leave the lobby to change mods.",
                     width);
    } else if (mods_locked) {
        PaddockGap(14.0F);
        PaddockAlert("You are in an online lobby. Leave the lobby to change mods or Magic Codes.", width);
    }
    if (mods.busy) {
        PaddockGap(14.0F);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const PaddockType type = PaddockReading(14.0F, false, 1.5F);
        PaddockSpinner(ImGui::GetWindowDrawList(), {at.x + 10.0F, at.y + type.line * 0.5F}, 8.0F,
                       2.0F, PaddockCol(0xFFAB14));
        ImGui::SetCursorScreenPos({at.x + 30.0F, at.y});
        PaddockText(type, PaddockRgb(0xFFD388), mods.stage + "\xE2\x80\xA6", width - 30.0F);
    }
    if (!mods.busy && !mods.succeeded && !mods.result.empty()) {
        PaddockGap(14.0F);
        PaddockAlert(mods.result, width);
    }
    if (!g_legacy_import_status.empty()) PaddockFeedback(g_legacy_import_status, width);

    // The section switcher.
    PaddockGap(24.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        constexpr std::array<const char*, 4> labels{{
            "My mods##section", "Magic Codes##section", "Track Lab##section",
            "Help & imports##section"}};
        const float tab_height = PaddockSectionTabHeight();
        float x = at.x;
        float y = at.y;
        for (int index = 0; index < 4; ++index) {
            const float tab = PaddockSectionTabWidth(labels[index]);
            if (x > at.x && x + tab > at.x + width) {
                x = at.x;
                y += tab_height + 10.0F;
            }
            ImGui::SetCursorScreenPos({x, y});
            const bool open = g_mods_page.section == index;
            if (PaddockSectionTab(labels[index], open)) {
                g_mods_page.section = open ? -1 : index;
                g_mods_page.entered_at = PaddockClock();
            }
            x += tab + 10.0F;
        }
        const float bottom = y + tab_height + 16.0F;
        ImGui::GetWindowDrawList()->AddRectFilled({at.x, bottom}, {at.x + width, bottom + 2.0F},
                                                  PaddockCol(0x24495B));
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({width, bottom + 2.0F - at.y});
    }

    if (g_mods_page.section >= 0) {
        PaddockGap(24.0F);
        const bool tools_locked = mods_locked;
        if (g_mods_page.section == kModsSectionLibrary) {
            DrawModsLibrarySection(width, mods, mods_locked, dkr_tracks);
        } else {
            constexpr std::array<std::array<const char*, 2>, 4> titles{{
                {{"", ""}},
                {{"Magic Codes", "Choose a race modifier. Enabled codes apply on your next launch."}},
                {{"Track Lab", "Create and test your own .dkrmap tracks. These tools are for track creators."}},
                {{"Help & imports",
                  "Manage your library, review imports and prepare mods for a different ROM."}},
            }};
            const auto& [title, description] = titles[static_cast<std::size_t>(g_mods_page.section)];
            {
                PaddockEnter enter(0);
                PaddockHeading(title, 30.0F, 0xFFCD67, width);
                PaddockGap(8.0F);
            }
            {
                PaddockEnter enter(1);
                DrawToolIntro(description, width);
            }
            if (g_mods_page.section == kModsSectionMagic) {
                DrawMagicCodesSection(width, dkr::runtime::netplay::session().presentation_active());
            } else if (g_mods_page.section == kModsSectionTrackLab) {
                DrawTrackLabSection(width, tools_locked || mods.busy, tracks);
            } else {
                DrawModHelpSection(width, mods, mods_locked);
            }
        }
    }

    ImGui::PopStyleVar();
    if (indent > 0.0F) ImGui::Unindent(indent);

    DrawModImportChooser({revision != 0U, mods_locked || mods.busy || picking,
                          [] { ImportLegacyModWithDialog(); },
                          [] { ImportTrackWithDialog(); },
                          [] { ImportTrackZipWithDialog(); },
                          go_to_play});
    DrawDkrTrackDetails(dkr_tracks, revision != 0U, mods_locked || mods.busy || picking);
    if (!g_mods_page.uninstall_track_request.empty()) {
        const std::string id = std::exchange(g_mods_page.uninstall_track_request, {});
        if (!mods_locked && !mods.busy && !picking) {
            std::string error;
            g_track_import_status = tracks_ns::uninstall(id, error)
                ? "Track uninstalled. Source files, saves and HD texture packs were kept."
                : error;
        }
    }
    if (!g_mods_page.manage_pack_request.empty()) {
        g_texture_pack_manage_id = g_mods_page.manage_pack_request;
        g_texture_pack_manage_request = true;
        g_mods_page.manage_pack_request.clear();
    }
    const PaddockFlatScope paddock;
    ++g_paddock_modal_windows;
    DrawSharedTexturePackModal();
    --g_paddock_modal_windows;
}

bool BeginSettingsDialog(const char*, std::string_view, float, unsigned);
float SettingsDialogInner();
void EndSettingsDialog();
#include "runtime_play_ui.inl"
#include "runtime_online_ui.inl"

std::string FormatRecordTime(std::uint16_t frames) {
    if (frames == 0U) {
        return "No personal record yet";
    }
    const unsigned minutes = frames / (60U * 60U);
    const unsigned remainder = frames % (60U * 60U);
    const unsigned seconds = remainder / 60U;
    const unsigned hundredths = ((remainder % 60U) * 100U) / 60U;
    char result[32]{};
    std::snprintf(result, sizeof(result), "%02u:%02u:%02u",
                  minutes, seconds, hundredths);
    return result;
}

#include "runtime_settings_ui.inl"
#include "runtime_sound_ui.inl"
#include "runtime_graphics_ui.inl"
#include "runtime_saves_ui.inl"
#include "runtime_details_ui.inl"

std::string ShortcutBindingName(CaptureDevice device,
                                dkr::runtime::input::ShortcutBinding binding) {
    const auto source_name = [device](int source) {
        return device == CaptureDevice::Keyboard
            ? dkr::runtime::input::keyboard_binding_name(source)
            : dkr::runtime::input::controller_binding_name(source);
    };
    if (binding.primary == dkr::runtime::input::kUnbound) {
        return "Unbound";
    }
    std::string name = source_name(binding.primary);
    if (binding.secondary != dkr::runtime::input::kUnbound) {
        name += " + ";
        name += source_name(binding.secondary);
    }
    return name;
}

const char* ShortcutActionLabel(dkr::runtime::input::ShortcutAction action) {
    using dkr::runtime::input::ShortcutAction;
    switch (action) {
        case ShortcutAction::QuickRestart: return "QUICK RACE RESTART";
        case ShortcutAction::ToggleOverlay: return "OPEN / CLOSE DKR-R MENU";
        case ShortcutAction::ToggleTexturePack: return "TOGGLE TEXTURE PACK";
        case ShortcutAction::ToggleFullscreen: return "WINDOWED / FULLSCREEN";
        case ShortcutAction::RecenterGyro: return "RECENTER GYRO";
        default: return "SHORTCUT";
    }
}

void CommitShortcutCapture() {
    dkr::runtime::input::ShortcutBinding binding{
        g_shortcut_capture_sources[0],
        g_shortcut_capture_count > 1
            ? g_shortcut_capture_sources[1]
            : dkr::runtime::input::kUnbound};
    if (g_capture_device == CaptureDevice::Keyboard) {
        dkr::runtime::input::set_shortcut_keyboard_binding(
            g_capture_shortcut_action, binding);
    } else if (g_capture_device == CaptureDevice::Controller) {
        dkr::runtime::input::set_shortcut_controller_binding(
            g_capture_shortcut_action, binding);
    }
    SaveSettings();
    g_capture_finished = true;
}

void BeginShortcutCapture(CaptureDevice device,
                          dkr::runtime::input::ShortcutAction action) {
    g_capture_action = kShortcutCaptureAction;
    g_capture_device = device;
    g_capture_shortcut_action = action;
    g_capture_popup_pending = true;
    g_capture_finished = false;
    g_shortcut_capture_sources = {
        dkr::runtime::input::kUnbound, dkr::runtime::input::kUnbound};
    g_shortcut_capture_count = 0;
    g_shortcut_capture_deadline = {};
}

void DrawControllerMappingModal() {
    constexpr const char* kMappingPopup = "SET UP CONTROLLER";
    const auto progress =
        dkr::runtime::platform::controller_mapping_progress();
    if (g_controller_mapping_popup_pending ||
        (progress.visible && !ImGui::IsPopupOpen(kMappingPopup))) {
        ImGui::OpenPopup(kMappingPopup);
        g_controller_mapping_popup_pending = false;
    }
    if (!progress.visible) {
        // Escape can cancel capture in the event handler before this frame is
        // drawn. Close the already-open modal explicitly so its ImGui popup
        // state cannot leak into the next setup session.
        if (ImGui::IsPopupOpen(kMappingPopup) &&
            BeginPaddedModal(kMappingPopup,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return;
    }
    if (progress.complete && progress.success &&
        !g_controller_mapping_completion_saved) {
        SaveSettings();
        g_controller_mapping_completion_saved = true;
    }

    const ImVec2 display_size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(
        {std::min(620.0F, display_size.x - 32.0F),
         std::min(420.0F, display_size.y - 32.0F)},
        ImGuiCond_Appearing);
    if (!BeginPaddedModal(kMappingPopup,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    PushHeadingFont();
    ImGui::TextUnformatted(progress.success ? "CONTROLLER READY" :
                           "N64 CONTROLLER SETUP");
    PopHeadingFont();
    ImGui::TextWrapped("%s", progress.controller_name.c_str());
    if (progress.capturing) {
        const float fraction = progress.total > 0U
            ? static_cast<float>(progress.step) /
                  static_cast<float>(progress.total)
            : 0.0F;
        const std::string overlay = "Control " +
            std::to_string(std::min(progress.step + 1U, progress.total)) +
            " of " + std::to_string(progress.total);
        ImGui::ProgressBar(fraction, {-1.0F, 28.0F}, overlay.c_str());
        ImGui::Dummy({0.0F, 8.0F});
        ImGui::TextColored(kWarm, "%s", progress.prompt.c_str());
    }
    if (!progress.message.empty()) {
        ImGui::TextWrapped("%s", progress.message.c_str());
    }
    ImGui::Dummy({0.0F, 12.0F});
    if (progress.capturing) {
        if (ImGui::Button("CANCEL SETUP", {-1.0F, 46.0F})) {
            dkr::runtime::platform::cancel_controller_mapping();
            ImGui::CloseCurrentPopup();
        }
    } else {
        const char* button = progress.success ? "DONE" : "CLOSE";
        if (ImGui::Button(button, {-1.0F, 46.0F})) {
            dkr::runtime::platform::dismiss_controller_mapping();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

#include "runtime_controls_ui.inl"

bool HandleInputCaptureEvent(SDL_Event* event) {
    const auto mapping =
        dkr::runtime::platform::controller_mapping_progress();
    if (mapping.capturing) {
        if (event != nullptr && event->type == SDL_KEYDOWN &&
            event->key.repeat == 0 &&
            event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            dkr::runtime::platform::cancel_controller_mapping();
            return true;
        }
        return dkr::runtime::platform::handle_controller_mapping_event(event);
    }
    if (event == nullptr || g_capture_action == -1 || g_capture_finished) {
        return false;
    }
    if (g_capture_action == kShortcutCaptureAction) {
        if (event->type == SDL_KEYDOWN && event->key.repeat == 0 &&
            event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            g_capture_finished = true;
            return true;
        }
        int source = dkr::runtime::input::kUnbound;
        if (g_capture_device == CaptureDevice::Keyboard &&
            event->type == SDL_KEYDOWN && event->key.repeat == 0) {
            source = static_cast<int>(event->key.keysym.scancode);
        } else if (g_capture_device == CaptureDevice::Controller &&
                   event->type == SDL_CONTROLLERBUTTONDOWN &&
                   event->cbutton.which ==
                       dkr::runtime::platform::controller_instance_for_player(0U)) {
            source = dkr::runtime::input::encode_controller_button(
                event->cbutton.button);
        }
        if (source != dkr::runtime::input::kUnbound &&
            (g_shortcut_capture_count == 0 ||
             source != g_shortcut_capture_sources[0])) {
            g_shortcut_capture_sources[g_shortcut_capture_count++] = source;
            if (g_shortcut_capture_count >= 2) {
                CommitShortcutCapture();
            } else {
                g_shortcut_capture_deadline =
                    std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(800);
            }
            return true;
        }
        return false;
    }
    if (g_capture_action == kAssignControllerCaptureAction) {
        if (event->type == SDL_KEYDOWN && event->key.repeat == 0 &&
            event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            g_capture_finished = true;
            return true;
        }
        if (event->type == SDL_CONTROLLERBUTTONDOWN &&
            dkr::runtime::platform::assign_controller(
                g_selected_player, event->cbutton.which)) {
            SaveSettings();
            g_capture_finished = true;
            return true;
        }
        if (event->type == SDL_JOYBUTTONDOWN &&
            dkr::runtime::platform::begin_controller_mapping(
                event->jbutton.which, g_selected_player)) {
            g_capture_finished = true;
            g_controller_mapping_popup_pending = true;
            g_controller_mapping_completion_saved = false;
            return true;
        }
        return false;
    }
    using dkr::runtime::input::Action;
    const auto action = static_cast<Action>(g_capture_action);
    if (event->type == SDL_KEYDOWN && event->key.repeat == 0) {
        if (event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            g_capture_finished = true;
            return true;
        }
        if (g_capture_device == CaptureDevice::Keyboard) {
            dkr::runtime::input::set_keyboard_binding(
                g_selected_player, action,
                static_cast<int>(event->key.keysym.scancode));
            SaveSettings();
            g_capture_finished = true;
            return true;
        }
    }
    if (g_capture_device == CaptureDevice::Controller &&
        event->type == SDL_CONTROLLERBUTTONDOWN &&
        event->cbutton.which ==
            dkr::runtime::platform::controller_instance_for_player(
                g_selected_player)) {
        const int source = dkr::runtime::input::encode_controller_button(
            event->cbutton.button);
        if (g_capture_secondary_controller) {
            dkr::runtime::input::set_secondary_controller_binding(
                g_selected_player, action, source);
        } else {
            dkr::runtime::input::set_controller_binding(
                g_selected_player, action, source);
        }
        SaveSettings();
        g_capture_finished = true;
        return true;
    }
    if (g_capture_device == CaptureDevice::Controller &&
        event->type == SDL_CONTROLLERAXISMOTION &&
        event->caxis.which ==
            dkr::runtime::platform::controller_instance_for_player(
                g_selected_player) &&
        std::abs(event->caxis.value) >= 20000) {
        const int source = dkr::runtime::input::encode_controller_axis(
            event->caxis.axis, event->caxis.value > 0);
        if (g_capture_secondary_controller) {
            dkr::runtime::input::set_secondary_controller_binding(
                g_selected_player, action, source);
        } else {
            dkr::runtime::input::set_controller_binding(
                g_selected_player, action, source);
        }
        SaveSettings();
        g_capture_finished = true;
        return true;
    }
    return false;
}

void DrawOverlayContent(float content_width) {
    if (g_overlay_page == kPagePlay) {
        DrawPageHeading("PLAY");
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("Taj has paused the race. The island is waiting whenever you are ready.");
        ImGui::PopStyleColor();
        ImGui::Dummy({0.0F, 18.0F});
        DrawStartingLights(true);
        ImGui::Dummy({0.0F, 12.0F});
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kWarm);
        if (ImGui::Button("RETURN TO THE RACE", {content_width, 64.0F})) {
            g_overlay_visible.store(false, std::memory_order_release);
        }
        ImGui::PopStyleColor(2);
    } else if (g_overlay_page == kPageGraphics) {
        DrawGraphicsPage(content_width, true);
    } else if (g_overlay_page == kPageSound) {
        DrawSoundPage(content_width, true);
    } else if (g_overlay_page == kPageControls) {
        DrawControlsPage(content_width, true);
    } else if (g_overlay_page == kPageSaveManager) {
        DrawSaveManagerPage(content_width, true);
    } else if (g_overlay_page == kPageOnlineMp) {
        DrawOnlinePage(content_width, false, true);
    } else if (g_overlay_page == kPageModsHacks) {
        DrawModsHacks(content_width, true);
    } else if (g_overlay_page == kPageTextures) {
        DrawTexturesPage(content_width);
    } else if (g_overlay_page == kPageAbout) {
        DrawAboutPage(content_width, false);
    }
    ImGui::Dummy({0.0F, 44.0F});
}

} // namespace

void dkr::runtime::ui::configure(const std::filesystem::path& config_directory) {
    g_config_directory = config_directory;
#if defined(__ANDROID__)
    dkr::runtime::mobile::configure(config_directory);
#endif
    // Resolve only beside this executable, never from a mod or the CWD.
    std::filesystem::path mod_worker;
#if defined(__ANDROID__)
    if (const char* directory = std::getenv("DKR_ANDROID_NATIVE_LIBS")) {
        const auto candidate = std::filesystem::path(directory) / "libDKR-R-ModWorker.so";
        std::error_code error;
        if (candidate.is_absolute() && std::filesystem::is_regular_file(candidate, error))
            mod_worker = candidate;
    }
#else
    if (char* base = SDL_GetBasePath()) {
        std::filesystem::path executable_directory = std::filesystem::u8path(base);
        SDL_free(base);
        // SDL returns a trailing separator. Remove it before parent_path(),
        // otherwise an AppImage's usr/bin/ parent is still usr/bin, not usr.
        if (executable_directory.filename().empty()) executable_directory = executable_directory.parent_path();
#if defined(_WIN32)
        constexpr const char* worker_name = "DKR-R-ModWorker.exe";
#else
        constexpr const char* worker_name = "DKR-R-ModWorker";
#endif
        for (const auto& parent : {executable_directory, executable_directory.parent_path()}) {
            const auto candidate = parent / "libexec" / "dkr-r" / worker_name;
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) { mod_worker = candidate; break; }
        }
    }
#endif
    g_legacy_imports.configure(config_directory / "mods" / "legacy", mod_worker);
    shutdown_online_mods();
    g_online_mod_settings={config_directory / "online-mods",mod_worker,{}};
    g_online_mod_link=std::make_unique<dkr::runtime::netplay::DirectModSyncLink>(dkr::runtime::netplay::session());
    g_online_mod_sync=std::make_unique<dkr::mods::online::ModSync>(*g_online_mod_link);
    dkr::runtime::hud::configure(config_directory);
    const auto adventure = dkr::runtime::saves::adventure_info();
    if (adventure.exists &&
        adventure.size == dkr::runtime::saves::codec::kImageSize &&
        !adventure.valid) {
        bool changed = false;
        std::filesystem::path backup;
        std::string error;
        if (dkr::runtime::saves::repair_adventure_checksums(
                changed, backup, error) && changed) {
            g_save_manager_status =
                "DKR-R safely repaired the Adventure EEPROM checksums. The exact original is preserved at " +
                PathUtf8(backup);
        } else if (!error.empty()) {
            g_save_manager_status =
                "Automatic checksum repair was not applied: " + error;
        }
    }
    dkr::runtime::magic_codes::configure(config_directory);
    dkr::runtime::texture_packs::configure(config_directory);
    dkr::runtime::netplay::session().configure_artifact_directory(
        config_directory / "netplay" / "replays");
    RefreshCrtFilters();
    LoadSettings();
    dkr::runtime::netplay::friend_service().configure(
        config_directory, g_online_player_name);
    const std::string profile_name =
        dkr::runtime::netplay::friend_service().display_name();
    const auto copy_profile_name = [&](char* destination,
                                       std::size_t capacity) {
        const std::size_t length =
            std::min(profile_name.size(), capacity - 1U);
        std::memcpy(destination, profile_name.data(), length);
        destination[length] = '\0';
    };
    copy_profile_name(g_online_profile_name, sizeof(g_online_profile_name));
    copy_profile_name(g_online_player_name, sizeof(g_online_player_name));
    if (!g_crt_filters.empty()) {
        g_crt_filter_index = std::clamp(
            g_crt_filter_index, 0, static_cast<int>(g_crt_filters.size()) - 1);
    }
}

void dkr::runtime::ui::persist_settings() {
    SaveSettings();
}

void dkr::runtime::ui::persist_graphics_api_fallback() {
    GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    config.api_option = GraphicsApi::Auto;
    g_modern_graphics_api = GraphicsApi::Auto;
    ultramodern::renderer::set_graphics_config(config);
    SaveSettings();
    std::fprintf(stderr,
                 "[boot][settings] unavailable graphics API recovered to Automatic\n");
}

dkr::runtime::ui::StartupResult dkr::runtime::ui::run_startup_screen(
    SDL_Window* window, const std::filesystem::path& preselected_rom,
    bool software_failure_recovery) {
    StartupResult result{};
    if (window == nullptr) {
        return result;
    }
    dkr::runtime::startup_performance::mark("launcher-enter");

    window = static_cast<SDL_Window*>(dkr::runtime::platform::prepare_window_for_launcher(software_failure_recovery));
    if (!window) return result;
    SDL_SetWindowTitle(window, "DKR-R - Diddy Kong Racing Recompiled");
    const auto launcher_renderer_started_at =
        dkr::runtime::startup_performance::Clock::now();
#if defined(__linux__) && !defined(__ANDROID__)
    // The launcher and RT64 share this Vulkan-capable SDL window for the
    // complete process lifetime. An accelerated SDL renderer can replace the
    // native surface state under Gamescope, so keep the lightweight launcher
    // on the software backend and let RT64 take over the same window directly.
    SDL_Renderer* renderer = nullptr;
    const char* accelerated_launcher = SDL_getenv("DKR_LINUX_ACCELERATED_LAUNCHER");
    if (!software_failure_recovery && accelerated_launcher && std::string_view(accelerated_launcher) == "1" &&
        !(SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN)) {
        renderer = SDL_CreateRenderer(window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    }
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
#else
    SDL_Renderer* renderer = nullptr;
    struct ReturnFramebufferHint {
        std::optional<std::string> previous;
        bool active=false;
        void disable_acceleration() {
            const char* hint=SDL_GetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION);
            if(hint)previous=hint;
            active=true;
            SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION,"0",SDL_HINT_OVERRIDE);
        }
        ~ReturnFramebufferHint() {
            if(!active)return;
            if(previous)SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION,previous->c_str(),SDL_HINT_OVERRIDE);
            else SDL_ResetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION);
        }
    } return_framebuffer_hint;
    // An experimental return (graceful or failed) has retired both native GPU
    // owners. Do not recreate the D3D9 presentation path that an injected RTSS
    // hook crashed on in the Oct 2 host dumps. Initial startup, legacy returns
    // and RT64 settings are unaffected.
    if (!software_failure_recovery) {
        renderer = SDL_CreateRenderer(
            window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    }
#if defined(_WIN32)
    else {
        // Select D3D11 explicitly on the FRESH return window, never D3D9.
        // Passing -1 for software is not enough: SDL may accelerate its window
        // framebuffer internally with the same D3D9 path that crashed before.
        for(int driver=0;driver<SDL_GetNumRenderDrivers();++driver) {
            SDL_RendererInfo info{};
            if(SDL_GetRenderDriverInfo(driver,&info)==0&&info.name&&std::string_view(info.name)=="direct3d11") {
                renderer=SDL_CreateRenderer(window,driver,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
                break;
            }
        }
    }
#endif
    if (renderer == nullptr) {
        // Retain the hint through resize and renderer destruction, not just
        // initial creation: SDL can recreate its framebuffer after a resize.
        if(software_failure_recovery)return_framebuffer_hint.disable_acceleration();
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        // The software window surface is created on first drawing, not
        // necessarily in CreateRenderer. Materialize it under the GDI hint.
        if(renderer&&software_failure_recovery)SDL_GetWindowSurface(window);
    }
#endif
    dkr::runtime::startup_performance::report(
        "launcher-renderer-create", launcher_renderer_started_at);
    if (renderer == nullptr) {
        std::fprintf(stderr, "[boot][launcher] SDL renderer failed: %s\n", SDL_GetError());
        return result;
    }
    SDL_RendererInfo renderer_info{};
    if (SDL_GetRendererInfo(renderer, &renderer_info) == 0) {
        std::fprintf(stderr,
                     "[boot][launcher] SDL renderer=%s accelerated=%s "
                     "window-flags=0x%08X\n",
                     renderer_info.name != nullptr ? renderer_info.name : "unknown",
                     (renderer_info.flags & SDL_RENDERER_ACCELERATED) != 0 ? "yes" : "no",
                     static_cast<unsigned>(SDL_GetWindowFlags(window)));
    }
    const bool bounded_software_return=software_failure_recovery&&
        (renderer_info.flags&SDL_RENDERER_ACCELERATED)==0;
    if(software_failure_recovery)std::fprintf(stderr,
        "[rollback][recovery] fresh launcher return renderer=%s bounded-software=%d\n",
        renderer_info.name?renderer_info.name:"unknown",int(bounded_software_return));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    const auto launcher_assets_started_at =
        dkr::runtime::startup_performance::Clock::now();
    LoadLauncherFonts();
    ApplyStyle();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    int launcher_display_refresh_rate = 60;
    const int launcher_display = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode launcher_display_mode{};
    if (launcher_display >= 0 &&
        SDL_GetCurrentDisplayMode(launcher_display,
                                  &launcher_display_mode) == 0 &&
        launcher_display_mode.refresh_rate > 0) {
        launcher_display_refresh_rate = std::clamp(
            launcher_display_mode.refresh_rate, 30, 240);
    }
    const int launcher_interactive_refresh_rate =
        std::min(launcher_display_refresh_rate, 60);
    const int launcher_quiet_refresh_rate =
        launcher_interactive_refresh_rate;
    std::fprintf(stderr,
                 "[boot][launcher] refresh targets interactive=%dHz quiet=%dHz "
                 "unfocused=10Hz hidden=event-driven\n",
                 launcher_interactive_refresh_rate,
                 launcher_quiet_refresh_rate);

    LauncherBackgroundTexture launcher_background =
        LoadLauncherBackground(renderer, bounded_software_return);
    dkr::runtime::startup_performance::report(
        "launcher-assets-load", launcher_assets_started_at);
    const auto launcher_animation_epoch = std::chrono::steady_clock::now();
    constexpr auto kLauncherInteractionWindow =
        std::chrono::milliseconds{850};
    auto launcher_last_activity = launcher_animation_epoch;
    auto launcher_controller_activity = std::chrono::steady_clock::time_point::min();
    dkr::runtime::launcher::FrameSchedule launcher_schedule;
    dkr::runtime::launcher::Profile launcher_profile;
    dkr::runtime::launcher::PanelCache launcher_panel_cache;
    using LauncherProfile = dkr::runtime::launcher::Profile;
    LauncherDrawProfileRange background_profile{renderer, &launcher_profile, LauncherProfile::Background};
    LauncherDrawProfileRange underlay_profile{renderer, &launcher_profile, LauncherProfile::Underlay};
    auto launcher_previous_frame = launcher_animation_epoch;
    double launcher_background_scroll = 0.0;
    std::uint64_t launcher_rendered_frames = 0U;
    std::uint64_t launcher_interactive_frames = 0U;
    std::uint64_t launcher_quiet_frames = 0U;
    std::uint64_t launcher_unfocused_frames = 0U;
    std::uint64_t launcher_hidden_wakes = 0U;
    std::uint64_t launcher_total_vertices = 0U;
    std::uint64_t launcher_total_indices = 0U;
    std::uint64_t launcher_peak_vertices = 0U;
    std::uint64_t launcher_ui_build_microseconds = 0U;
    std::uint64_t launcher_present_microseconds = 0U;
    auto launcher_recovery_reported_at = launcher_animation_epoch;
    std::uint64_t launcher_recovery_reported_frames = 0U;
    std::uint64_t launcher_recovery_reported_present_us = 0U;

    std::filesystem::path selected_rom;
    g_mod_browser_revision=0;
    std::filesystem::path online_manifest_rom;
    dkr::runtime::rom::Identity online_manifest_identity{};
    std::optional<dkr::runtime::netplay::CompatibilityManifest>
        online_manifest;
    std::string rom_status = kRomChoosePrompt;
    bool rom_ready = false;
    const auto rom_catalog_started_at =
        dkr::runtime::startup_performance::Clock::now();
    std::vector<RomCatalogEntry> rom_catalog = LoadRomCatalog();
    dkr::runtime::startup_performance::report(
        "rom-catalog-load", rom_catalog_started_at);
    const std::optional<std::filesystem::path> initial_rom =
        !preselected_rom.empty()
            ? std::optional<std::filesystem::path>{preselected_rom}
            : LoadLastRom();
    if (initial_rom.has_value()) {
        std::string error;
        dkr::runtime::rom::Identity identity{};
        if (dkr::runtime::ValidateRomForLauncher(*initial_rom, identity, error)) {
            CommitRomSelection(*initial_rom, identity, selected_rom,
                               rom_catalog, rom_status);
            rom_ready = true;
        } else {
            rom_status = "T.T. could not find the previous Game Pak. Choose it again.";
        }
    }
    if (!rom_ready && !rom_catalog.empty()) {
        const RomCatalogEntry fallback = rom_catalog.front();
        rom_ready = SelectCatalogRom(fallback, selected_rom, rom_catalog,
                                     rom_status);
    }

    int page = 0;
    int last_rendered_page = -1;
    int sidebar_selection = 0;
    bool focus_content = true;
    bool running = true;
    bool launch_requested = false;
    std::filesystem::path mod_launch_rom;
    // Events may arrive between redraw deadlines. Retain actions until the
    // next ImGui frame consumes them, including controller modal requests.
    bool request_quit_popup = false;
    bool request_restart_popup = false;
    while (running) {
        g_legacy_imports.tick();
        dkr::runtime::saves::set_online_save_protection(dkr::runtime::netplay::session().active());
        if (g_play_offline_after_leave && !dkr::runtime::netplay::session().active()) {
            g_play_offline_after_leave = false; launch_requested = true;
        }
        const auto launcher_services_started = std::chrono::steady_clock::now();
        // Hash only the already-imported catalog on a worker. Applying the
        // authenticated offer still happens exclusively on this UI thread.
        using MatchingRom = std::optional<std::pair<RomCatalogEntry, dkr::runtime::rom::Identity>>;
        static std::future<MatchingRom> compatibility_scan;
        static dkr::runtime::netplay::SessionView scanned_offer;
        static std::filesystem::path scanned_selection;
        if (g_online_compatibility_sync_requested && !compatibility_scan.valid()) {
            g_online_compatibility_sync_requested = false;
            scanned_offer = dkr::runtime::netplay::session().view();
            scanned_selection = selected_rom;
            if (scanned_offer.state != dkr::runtime::netplay::ConnectionState::Failed ||
                !scanned_offer.compatibility_sync_offer || scanned_offer.invite.empty()) {
                g_online_action_status = "The authenticated host sync offer expired. Enter the current Quick Join code again.";
            } else {
                const auto expected = *scanned_offer.compatibility_sync_offer;
                compatibility_scan = std::async(std::launch::async, [catalog = rom_catalog, expected]() -> MatchingRom {
                    for (const auto& entry : catalog) {
                        dkr::runtime::rom::Identity identity{};
                        std::string error;
                        if (!dkr::runtime::ValidateRomForLauncher(entry.path, identity, error)) continue;
                        const auto revision = identity.revision == dkr::runtime::rom::Revision::UsV80
                            ? dkr::runtime::netplay::Revision::UsV80 : dkr::runtime::netplay::Revision::UsV77;
                        if (revision == expected.revision && identity.canonical_xxh3 == expected.canonical_rom_hash)
                            return std::make_pair(entry, identity);
                    }
                    return {};
                });
                g_online_action_status = "Checking imported Game Paks in the background. No settings have changed yet.";
            }
        }
        if (compatibility_scan.valid() &&
            compatibility_scan.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            MatchingRom matching_rom;
            try { matching_rom = compatibility_scan.get(); } catch (...) {}
            g_online_compatibility_sync_requested = false;
            auto& online = dkr::runtime::netplay::session();
            const auto failed_view = online.view();
            if (failed_view.state != dkr::runtime::netplay::ConnectionState::Failed ||
                !failed_view.compatibility_sync_offer || failed_view.invite != scanned_offer.invite ||
                selected_rom != scanned_selection ||
                dkr::runtime::netplay::manifest_hash(*failed_view.compatibility_sync_offer) !=
                    dkr::runtime::netplay::manifest_hash(*scanned_offer.compatibility_sync_offer)) {
                g_online_action_status = "Host sync cancelled: the lobby offer or selected Game Pak changed during validation.";
            } else {
                const auto expected = *failed_view.compatibility_sync_offer;

                if (!matching_rom.has_value()) {
                    g_online_action_status =
                        "One-click sync stopped safely: the host's exact supported Game Pak is not already imported on this device. DKR-R never transfers ROM data.";
                } else {
                    const std::filesystem::path previous_rom = selected_rom;
                    const bool previous_rom_ready = rom_ready;
                    const std::uint32_t previous_persistent =
                        dkr::runtime::magic_codes::persistent_mask();
                    const std::uint32_t previous_one_shot =
                        dkr::runtime::magic_codes::queued_one_shot_mask();
                    const auto previous_manifest = online_manifest;
                    const std::string invitation = failed_view.invite;
                    std::string sync_error;
                    if ((expected.magic_codes_hash >> 32U) != 0U) {
                        sync_error =
                            "The host supplied an invalid Magic Code compatibility value.";
                    }
                    const std::uint32_t expected_codes =
                        static_cast<std::uint32_t>(expected.magic_codes_hash);

                    bool applied = sync_error.empty();
                    if (applied) {
                        dkr::runtime::magic_codes::set_persistent_mask(
                            expected_codes &
                            dkr::runtime::magic_codes::kPersistentMagicCodeMask);
                    }
                    if (applied) {
                        applied =
                        dkr::runtime::magic_codes::set_queued_one_shot_mask(
                            expected_codes &
                            dkr::runtime::magic_codes::kOneShotMagicCodeMask,
                            sync_error);
                    }
                    if (applied) {
                        CommitRomSelection(
                            matching_rom->first.path, matching_rom->second,
                            selected_rom, rom_catalog, rom_status);
                        rom_ready = true;
                        auto candidate = BuildNetplayManifest(
                            matching_rom->second);
                        const std::string remaining =
                            dkr::runtime::netplay::session_content_sync_incompatibility(
                                expected,candidate,!expected.mod_manifest_hash.empty());
                        if (!remaining.empty()) {
                            sync_error =
                                "The host also differs in a setting that cannot be changed safely: " +
                                dkr::runtime::netplay::online_failure_display_message(
                                    remaining);
                            applied = false;
                        } else {
                            online.disconnect(
                                "Applying the authenticated host compatibility offer.");
                            online.configure_manifest(candidate);
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
                            online.enable_owned_backend(dkr::runtime::netplay::experimental::runtime_available(matching_rom->second.revision));
#endif
                            std::vector<std::uint8_t> canonical_save;
                            std::string save_error;
                            dkr::runtime::saves::canonical_adventure_bytes(
                                canonical_save, save_error);
                            online.configure_session_save(
                                std::move(canonical_save),
                                dkr::runtime::saves::install_synchronized_online_adventure);
                            online_manifest_rom = selected_rom;
                            online_manifest_identity = matching_rom->second;
                            online_manifest = candidate;
                            const bool friend_context = g_joining_friend_invite &&
                                g_joining_friend_invite->lobby_code == invitation &&
                                g_joining_friend_invite->expires_unix > static_cast<std::uint64_t>(std::time(nullptr));
                            const bool joined = friend_context
                                ? online.join_friend_invite(invitation, g_online_player_name, g_joining_friend_invite->admission, sync_error)
                                : online.join(invitation, g_online_player_name, sync_error);
                            if (!joined) {
                                applied = false;
                            }
                        }
                    }

                    if (!applied) {
                        online.disconnect(
                            "The one-click compatibility sync was rolled back safely.");
                        dkr::runtime::magic_codes::set_persistent_mask(
                            previous_persistent);
                        std::string restore_error;
                        dkr::runtime::magic_codes::set_queued_one_shot_mask(
                            previous_one_shot, restore_error);
                        selected_rom = previous_rom;
                        rom_ready = previous_rom_ready;
                        if (!selected_rom.empty()) SaveLastRom(selected_rom);
                        online_manifest_rom = selected_rom;
                        online_manifest_identity = previous_rom_ready
                            ? dkr::runtime::rom::inspect(previous_rom)
                            : dkr::runtime::rom::Identity{};
                        online_manifest = previous_manifest;
                        if (previous_manifest) {
                            online.configure_manifest(*previous_manifest);
                        }
                        SaveSettings();
                        g_online_action_status =
                            "One-click host sync made no lasting changes: " +
                            (sync_error.empty() ?
                                 "the retry could not be started." : sync_error);
                    } else {
                        SaveSettings();
                        g_online_failure_modal_active = false;
                        g_online_action_status =
                            "Host Game Pak and Magic Codes matched. Reconnecting with the authenticated invitation...";
                    }
                }
            }
        }
        if (rom_ready && selected_rom != online_manifest_rom) {
            online_manifest_identity =
                dkr::runtime::rom::inspect(selected_rom);
            online_manifest_rom = selected_rom;
            online_manifest.reset();
        }
        // Selecting a ROM is not the only thing that changes compatibility.
        // Rebuild an OFFLINE manifest after Magic Code edits; a live lobby's
        // accepted manifest must remain immutable through launch and play.
        if (online_manifest &&
            dkr::runtime::magic_codes::magic_code_manifest_needs_refresh(
                dkr::runtime::netplay::session().active(),
                online_manifest->magic_codes_hash,
                dkr::runtime::magic_codes::selected_mask())) {
            online_manifest.reset();
        }
        // ROM revision is game-core data, not frontend ownership. Keep this
        // launcher, its window, input routing and online session alive for
        // every supported revision; the matching game engine is selected only
        // after the player explicitly starts the game.
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        if (rom_ready && online_manifest_identity.supported() &&
            dkr::runtime::netplay::session().view().state == dkr::runtime::netplay::ConnectionState::Offline)
            dkr::runtime::netplay::session().enable_owned_backend(dkr::runtime::netplay::experimental::runtime_available(online_manifest_identity.revision));
#endif
        if (rom_ready && !online_manifest &&
            online_manifest_identity.supported() &&
            dkr::runtime::netplay::session().view().state ==
                dkr::runtime::netplay::ConnectionState::Offline) {
            const auto candidate = BuildNetplayManifest(
                online_manifest_identity);
            if (!online_manifest || *online_manifest != candidate) {
                dkr::runtime::netplay::session().configure_manifest(candidate);
                std::vector<std::uint8_t> canonical_save;
                std::string save_error;
                dkr::runtime::saves::canonical_adventure_bytes(
                    canonical_save, save_error);
                dkr::runtime::netplay::session().configure_session_save(
                    std::move(canonical_save),
                    dkr::runtime::saves::install_synchronized_online_adventure);
                online_manifest = candidate;
            }
        }
        PumpDirectSessionIfDue();
        PumpFriendPresence();
        g_online_mod_selected_rom=selected_rom;
        g_online_mod_identity=rom_ready?online_manifest_identity:dkr::runtime::rom::Identity{};
        PumpOnlineMods();
        if (rom_ready &&
            dkr::runtime::netplay::session().consume_launch_request()) {
            dkr::runtime::startup_performance::mark(
                "launcher-online-launch-requested");
            result.start_game = true;
            result.rom_path = selected_rom;
            running = false;
            continue;
        }
        constexpr int launcher_page_count = kMenuPageCount;
        constexpr int launcher_sidebar_count = kMenuPageCount + 2;
        if (page >= launcher_page_count) {
            page = 0;
        }
        const auto launcher_events_started = std::chrono::steady_clock::now();
        launcher_profile.add(LauncherProfile::Services, launcher_services_started,
                             launcher_events_started);
        dkr::runtime::platform::pump_input_backend_events();
        SDL_Event event{};
        const auto process_launcher_event = [&](SDL_Event& event) {
#if defined(__ANDROID__)
            if (dkr::runtime::mobile::event(event, false)) {
                launcher_last_activity = std::chrono::steady_clock::now();
                return;
            }
#endif
            const bool input_activity =
                event.type == SDL_MOUSEMOTION ||
                event.type == SDL_MOUSEBUTTONDOWN ||
                event.type == SDL_MOUSEBUTTONUP ||
                event.type == SDL_MOUSEWHEEL ||
                event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ||
                event.type == SDL_TEXTINPUT ||
                (event.type == SDL_CONTROLLERAXISMOTION &&
                 std::abs(static_cast<int>(event.caxis.value)) >= 8192) ||
                event.type == SDL_CONTROLLERBUTTONDOWN ||
                event.type == SDL_CONTROLLERBUTTONUP ||
                (event.type == SDL_WINDOWEVENT &&
                 (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                  event.window.event == SDL_WINDOWEVENT_RESTORED ||
                  event.window.event == SDL_WINDOWEVENT_SHOWN ||
                  event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED));
            if (input_activity) {
                launcher_last_activity = std::chrono::steady_clock::now();
                if (event.type == SDL_CONTROLLERAXISMOTION ||
                    event.type == SDL_CONTROLLERBUTTONDOWN ||
                    event.type == SDL_CONTROLLERBUTTONUP) {
                    launcher_controller_activity = launcher_last_activity;
                }
            }
            dkr::runtime::platform::update_fullscreen_cursor(&event);
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (HandleInputCaptureEvent(&event)) {
                return;
            }
            if (dkr::runtime::platform::handle_window_shortcut(&event, false)) {
                return;
            }
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
                event.key.keysym.scancode == SDL_SCANCODE_ESCAPE && g_rom_browser.open) {
                g_rom_browser.close_requested = true;
            }
            const bool code_keyboard_visible =
                g_online_code_keyboard_visible.load(std::memory_order_acquire);
            if (code_keyboard_visible &&
                ((event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
                  event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) ||
                 (event.type == SDL_CONTROLLERBUTTONDOWN &&
                  event.cbutton.button == SDL_CONTROLLER_BUTTON_B))) {
                g_online_code_keyboard_cancel_requested.store(
                    true, std::memory_order_release);
                return;
            }
            if (dkr::runtime::hud::editor::active() &&
                (event.type == SDL_CONTROLLERDEVICEREMOVED ||
                 (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)))
                dkr::runtime::hud::editor::interrupt_move();
            if (event.type == SDL_CONTROLLERBUTTONDOWN && !dkr::runtime::hud::editor::active()) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B && g_rom_browser.open) {
                    RomBrowserBack();
                } else if (!g_rom_browser.open && !code_keyboard_visible &&
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                    sidebar_selection = StepSidebarSelection(sidebar_selection,-1);
                    if (sidebar_selection < launcher_page_count) {
                        page = sidebar_selection;
                        focus_content = true;
                    }
                } else if (!g_rom_browser.open && !code_keyboard_visible &&
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                    sidebar_selection = StepSidebarSelection(sidebar_selection,1);
                    if (sidebar_selection < launcher_page_count) {
                        page = sidebar_selection;
                        focus_content = true;
                    }
                } else if (!g_rom_browser.open && !code_keyboard_visible &&
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_A &&
                           sidebar_selection >= launcher_page_count) {
                    if (sidebar_selection == launcher_page_count) {
                        request_restart_popup = true;
                    } else {
                        request_quit_popup = true;
                    }
                } else if (!g_rom_browser.open && page == 0 && rom_ready &&
                           event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    launch_requested = true;
                }
            }
        };
        while (SDL_PollEvent(&event) != 0) {
            process_launcher_event(event);
        }
        dkr::runtime::platform::update_fullscreen_cursor();
        launcher_profile.add(LauncherProfile::Events, launcher_events_started,
                             std::chrono::steady_clock::now());
        if (!running) break;

        const Uint32 launcher_window_flags = SDL_GetWindowFlags(window);
        const bool launcher_window_visible =
            (launcher_window_flags &
             (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0U;
        const bool launcher_window_focused = launcher_window_visible &&
            (launcher_window_flags & SDL_WINDOW_INPUT_FOCUS) != 0U;
        const auto animation_now = std::chrono::steady_clock::now();
        g_launcher_animation_seconds = std::chrono::duration<double>(
            animation_now - launcher_animation_epoch).count();
        constexpr double kBackgroundScrollPixelsPerSecond = 12.5;
        launcher_background_scroll =
            g_launcher_animation_seconds * kBackgroundScrollPixelsPerSecond;

        if (launch_requested) {
            launch_requested=false;
            if(!dkr::runtime::netplay::session().active() &&
               !g_legacy_imports.snapshot().busy && !g_mod_launch.snapshot().modal) {
                mod_launch_rom=selected_rom;
                g_mod_launch.start(g_config_directory,mod_launch_rom,false);
            }
        }
        if(const auto prepared=g_mod_launch.snapshot();prepared.modal && prepared.prepared && !prepared.busy) {
            // The modal owns the selection while preparing; do not admit a
            // local bank if an online session began in the meantime.
            if(dkr::runtime::netplay::session().active()) {
                g_mod_launch.dismiss();rom_status="Custom launch cancelled because an online lobby became active.";
            } else {
                dkr::runtime::startup_performance::mark("launcher-local-launch-requested");
                result.start_game=true;result.rom_path=mod_launch_rom;result.mods=prepared.prepared;
                g_mod_launch.dismiss();running=false;continue;
            }
        }

        const bool launcher_interaction_recent =
            animation_now - launcher_last_activity <=
                kLauncherInteractionWindow;
        const bool launcher_controller_recent =
            launcher_controller_activity != std::chrono::steady_clock::time_point::min() &&
            animation_now - launcher_controller_activity <= kLauncherInteractionWindow;
        const int launcher_frame_rate = dkr::runtime::launcher::refresh_target(
            launcher_window_visible, launcher_window_focused,
            launcher_interaction_recent, launcher_controller_recent,
            launcher_display_refresh_rate);
        const int launcher_wait_ms = launcher_schedule.wait_ms(animation_now,
                                                               launcher_frame_rate);
        if (launcher_wait_ms > 0) {
            if (!launcher_window_visible) ++launcher_hidden_wakes;
            // Input interrupts the wait, not the redraw budget. Services run
            // on every wake (at most 50 ms between waits), even while hidden.
            if (SDL_WaitEventTimeout(&event, launcher_wait_ms) != 0) {
                process_launcher_event(event);
            }
            continue;
        }
        const auto launcher_frame_started = std::chrono::steady_clock::now();
        launcher_schedule.started(launcher_frame_started, launcher_frame_rate);
        if (launcher_rendered_frames > 0) {
            launcher_profile.add(LauncherProfile::Frame, launcher_previous_frame,
                                 launcher_frame_started);
        }
        launcher_previous_frame = launcher_frame_started;
        if (!launcher_window_focused && !launcher_controller_recent) {
            ++launcher_unfocused_frames;
        } else if (launcher_interaction_recent) {
            ++launcher_interactive_frames;
        } else {
            ++launcher_quiet_frames;
        }

        const auto launcher_ui_build_started =
            std::chrono::steady_clock::now();
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        dkr::runtime::platform::update_ui_gamepad_navigation();
        ImGui::NewFrame();
        int hud_viewport_width = 0;
        int hud_viewport_height = 0;
        SDL_GetWindowSize(window, &hud_viewport_width, &hud_viewport_height);
        dkr::runtime::hud::set_viewport_extent(hud_viewport_width,
                                               hud_viewport_height);
        // The opaque cloud backdrop (or opaque missing-asset fallback) covers
        // this entire undecorated window. Drawing ImGui's background underneath
        // it wastes a full software-rendered viewport every frame.
        BeginMainWindow("DKR-R Startup", ImGuiWindowFlags_NoBackground);
        if (launcher_profile.enabled()) ImGui::GetWindowDrawList()->AddCallback(LauncherDrawProfileRange::begin, &background_profile);
        DrawLauncherBackdrop(launcher_background, renderer, launcher_background_scroll,
                             bounded_software_return);
        if (launcher_profile.enabled()) ImGui::GetWindowDrawList()->AddCallback(LauncherDrawProfileRange::end, &background_profile);
#if defined(__ANDROID__)
        dkr::runtime::mobile::menu(page, false, request_restart_popup, request_quit_popup);
        dkr::runtime::mobile::begin_content();
        const float right_inner_width = ImGui::GetContentRegionAvail().x;
        if (page != last_rendered_page) {
            ImGui::SetScrollY(0); last_rendered_page = page;
        }
#else
        const MenuPanelLayout menu = CalculateMenuPanelLayout(ImGui::GetContentRegionAvail());
        const float outer_margin = menu.margin;
        const float content_height = menu.height;
        const float sidebar_width = menu.sidebar_width;
        const float content_x = menu.content_x;
        const float right_width = menu.content_width;
        const float panel_padding = menu.padding;
        const float right_inner_width = menu.inner_width;
        ImGui::SetCursorPos({outer_margin, outer_margin});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kMenuNavSurface);
        ImGui::PushStyleColor(ImGuiCol_Border, kMenuNavBorder);
        const SidebarLayout sidebar_layout = CalculateSidebarLayout(
            content_height, sidebar_width);
        ImGui::BeginChild("launcher-nav", {sidebar_width, content_height}, true,
                          ImGuiWindowFlags_NavFlattened |
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        const float nav_padding = sidebar_layout.padding;
        const float nav_inner_width = sidebar_width - nav_padding * 2.0F;
        ImGui::SetCursorPos({nav_padding, nav_padding});
        ImGui::PushTextWrapPos(sidebar_width - nav_padding);
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            {ImGui::GetStyle().ItemSpacing.x, sidebar_layout.item_spacing_y});
        ImGui::BeginGroup();
        BrandBlock(nav_inner_width, sidebar_layout.logo_size);
        ImGui::Dummy({0.0F, sidebar_layout.brand_gap});
        if (g_page_navigation_request >= 0) {
            page = g_page_navigation_request;
            sidebar_selection = page;
            g_page_navigation_request = -1;
        }
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        ++g_race_button_sidebar;
        LauncherSidebarButton("PLAY", 0, page, sidebar_selection,
                              nav_inner_width, sidebar_layout.button_height);
        LauncherSidebarButton("DKR-R ONLINE", kPageOnlineMp, page, sidebar_selection,
                              nav_inner_width, sidebar_layout.button_height);
        LauncherSidebarButton("MODS / HACKS", kPageModsHacks, page, sidebar_selection,
                              nav_inner_width, sidebar_layout.button_height);
        LauncherSidebarButton("GRAPHICS", kPageGraphics, page, sidebar_selection,
                              nav_inner_width, sidebar_layout.button_height);
        LauncherSidebarButton("SOUND", kPageSound, page, sidebar_selection,
                              nav_inner_width, sidebar_layout.button_height);
        LauncherSidebarButton("CONTROLS", kPageControls, page, sidebar_selection,
                              nav_inner_width, sidebar_layout.button_height);
        LauncherSidebarButton("SAVE MANAGER", kPageSaveManager, page, sidebar_selection,
                              nav_inner_width, sidebar_layout.button_height);
        LauncherSidebarButton("TEXTURES", kPageTextures, page, sidebar_selection,
                              nav_inner_width, sidebar_layout.button_height);
        LauncherSidebarButton("ABOUT DKR-R", kPageAbout, page, sidebar_selection,
                              nav_inner_width, sidebar_layout.button_height);
        SidebarActionGap(nav_inner_width, sidebar_layout.action_section_gap);
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            sidebar_selection == launcher_page_count
                ? ImVec4{1.0F, 0.58F, 0.04F, 1.0F}
                : ImVec4{0.92F, 0.43F, 0.06F, 1.0F});
        if (ImGui::Button("RESTART DKR-R",
                          {nav_inner_width, sidebar_layout.button_height})) {
            sidebar_selection = launcher_page_count;
            request_restart_popup = true;
        }
        ImGui::PopStyleColor();
        ImGui::Dummy({0.0F, sidebar_layout.action_gap});
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            sidebar_selection == launcher_page_count + 1 ? kWarm : kRaceRed);
        if (ImGui::Button("EXIT DKR-R",
                          {nav_inner_width, sidebar_layout.button_height})) {
            sidebar_selection = launcher_page_count + 1;
            request_quit_popup = true;
        }
        ImGui::PopStyleColor();
        --g_race_button_sidebar;
        ImGui::PopItemFlag();
        ImGui::EndGroup();
        ImGui::PopStyleVar();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::SetCursorPos({content_x, outer_margin});
        // Every launcher tab shares the same translucent surface. Card and
        // control colours stay independent so their contrast is unchanged.
        if (launcher_profile.enabled()) ImGui::GetWindowDrawList()->AddCallback(LauncherDrawProfileRange::begin, &underlay_profile);
        DrawLinuxSoftwarePanelUnderlay(
            {right_width, content_height}, kMenuContentSurface);
        if (launcher_profile.enabled()) ImGui::GetWindowDrawList()->AddCallback(LauncherDrawProfileRange::end, &underlay_profile);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kMenuContentSurface);
        ImGui::PushStyleColor(ImGuiCol_Border, kMenuContentBorder);
        ImGui::BeginChild("launcher-content", {right_width, content_height}, true,
                          ImGuiWindowFlags_NavFlattened);
        if (page != last_rendered_page) {
            ImGui::SetScrollY(0.0F);
            last_rendered_page = page;
        }
        ImGui::SetCursorPos({panel_padding, panel_padding});
        ImGui::PushItemWidth(right_inner_width);
        ImGui::PushTextWrapPos(panel_padding + right_inner_width);
        ImGui::BeginGroup();
        if (focus_content) {
            ImGui::SetKeyboardFocusHere();
            focus_content = false;
        }
#endif
        const PaddockLauncherTypographyScope launcher_typography;
        const PlayPageContext play_context{selected_rom, rom_status, rom_ready, rom_catalog,
                                           launch_requested};
        g_launcher_rom = &play_context;
        if (page == 0) {
            DrawPlayPage(right_inner_width, play_context);
        } else if (page == 1) {
            DrawGraphicsPage(right_inner_width, false);
        } else if (page == 2) {
            DrawSoundPage(right_inner_width);
        } else if (page == 3) {
            DrawControlsPage(right_inner_width, false);
        } else if (page == 4) {
            DrawSaveManagerPage(right_inner_width, false);
        } else if (page == kPageOnlineMp) {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
            g_online_experimental_rom=selected_rom;
#endif
            DrawOnlinePage(right_inner_width, true, rom_ready);
        } else if (page == kPageModsHacks) {
            DrawModsHacks(right_inner_width);
        } else if (page == kPageTextures) {
            DrawTexturesPage(right_inner_width);
        } else {
            DrawAboutPage(right_inner_width, true);
        }
        ImGui::Dummy({0.0F, 54.0F});
        g_launcher_rom = nullptr;
#if defined(__ANDROID__)
        dkr::runtime::mobile::end_content();
#else
        ImGui::EndGroup();
        ImGui::PopTextWrapPos();
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
#endif
        DrawRomBrowser(selected_rom, rom_status, rom_ready, rom_catalog);
        if (request_restart_popup) {
            ImGui::OpenPopup("Restart DKR-R?");
            request_restart_popup = false;
        }
        if (request_quit_popup) {
            ImGui::OpenPopup("Quit DKR-R?");
            request_quit_popup = false;
        }
        if (BeginPaddedModal("Restart DKR-R?", ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Restart DKR-R and reload the launcher?");
            ImGui::TextDisabled("Your selected Game Pak and settings will be preserved.");
            if (ImGui::Button("CANCEL", {120.0F, 40.0F})) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, {0.92F, 0.43F, 0.06F, 1.0F});
            if (ImGui::Button("RESTART DKR-R", {170.0F, 40.0F})) {
                SaveSettings();
                result.lifecycle_request = LifecycleRequest::Restart;
                running = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        if (BeginPaddedModal("Quit DKR-R?", ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Exit DKR-R and return to the desktop?");
            if (ImGui::Button("CANCEL", {120.0F, 40.0F})) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, {0.45F, 0.09F, 0.10F, 1.0F});
            if (ImGui::Button("EXIT DKR-R", {140.0F, 40.0F})) {
                SaveSettings();
                result.lifecycle_request = LifecycleRequest::Exit;
                running = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        ImGui::End();
        PumpDialogJob();
        DrawTexturePackImportModal();
        DrawPaddockLegacyModImportModal();
        DrawModLaunchModal();
        DrawOnlineModModal();
        DrawTextEntryKeyboard();
        DrawOnlineNotification();
        DrawOlToast();
        DrawOnlineStartCountdown();
        DrawOnlineWaitingNotification(
            dkr::runtime::netplay::session().presentation_view());
        const bool show_online_error = UpdateOnlineErrorNotification();
        DrawOnlineFailureModal();
        if (show_online_error) {
            DrawOnlineErrorNotification();
        }

        ApplyPaddockModalDim();
#if defined(__ANDROID__)
        if (dkr::runtime::mobile::editing()) {
            dkr::runtime::mobile::draw_controls(false);
            dkr::runtime::mobile::editor();
        }
#endif
        ImGui::Render();
        const auto launcher_ui_build_finished =
            std::chrono::steady_clock::now();
        ImDrawData* launcher_draw_data = ImGui::GetDrawData();
        if (launcher_draw_data != nullptr) {
            launcher_total_vertices += static_cast<std::uint64_t>(
                std::max(launcher_draw_data->TotalVtxCount, 0));
            launcher_total_indices += static_cast<std::uint64_t>(
                std::max(launcher_draw_data->TotalIdxCount, 0));
            launcher_peak_vertices = std::max(
                launcher_peak_vertices,
                static_cast<std::uint64_t>(
                    std::max(launcher_draw_data->TotalVtxCount, 0)));
        }
        launcher_ui_build_microseconds +=
            static_cast<std::uint64_t>(std::chrono::duration_cast<
                std::chrono::microseconds>(launcher_ui_build_finished -
                                           launcher_ui_build_started).count());
        const auto launcher_present_started = launcher_ui_build_finished;
        SDL_SetRenderDrawColor(renderer, 6, 9, 14, 255);
        SDL_RenderClear(renderer);
        RenderLauncherDrawData(renderer, launcher_draw_data, launcher_profile,
                               launcher_panel_cache, bounded_software_return);
        // SDL's software backend normally executes its queued drawing inside
        // Present. Flush only in the opt-in profiler to separate those costs.
        if (launcher_profile.enabled()) SDL_RenderFlush(renderer);
        const auto launcher_draw_finished = std::chrono::steady_clock::now();
        SDL_RenderPresent(renderer);
        const auto launcher_present_finished =
            std::chrono::steady_clock::now();
        launcher_present_microseconds +=
            static_cast<std::uint64_t>(std::chrono::duration_cast<
                std::chrono::microseconds>(launcher_present_finished -
                                           launcher_present_started).count());
        ++launcher_rendered_frames;
        if (launcher_rendered_frames == 1U) {
            if(software_failure_recovery&&!(SDL_GetWindowFlags(window)&(SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED))) {
                SDL_RaiseWindow(window);SDL_PumpEvents();
            }
            dkr::runtime::startup_performance::mark(
                "launcher-first-frame-presented");
        }
        if (software_failure_recovery && launcher_present_finished - launcher_recovery_reported_at >=
            std::chrono::seconds{5}) {
            const auto frames = launcher_rendered_frames - launcher_recovery_reported_frames;
            const auto draw_us = launcher_present_microseconds - launcher_recovery_reported_present_us;
            std::fprintf(stderr,
                "[rollback][recovery] launcher-responsive frames=%llu avg-draw=%.3fms panel-cache-hits=%llu\n",
                static_cast<unsigned long long>(frames),
                frames ? double(draw_us)/double(frames)/1000.0 : 0.0,
                static_cast<unsigned long long>(launcher_panel_cache.hits()));
            launcher_recovery_reported_at = launcher_present_finished;
            launcher_recovery_reported_frames = launcher_rendered_frames;
            launcher_recovery_reported_present_us = launcher_present_microseconds;
        }
        if (ImGui::IsAnyItemActive()) {
            launcher_last_activity = launcher_present_finished;
        }
        launcher_profile.add(LauncherProfile::Build, launcher_ui_build_started,
                             launcher_ui_build_finished);
        launcher_profile.add(LauncherProfile::Draw, launcher_present_started,
                             launcher_draw_finished);
        launcher_profile.add(LauncherProfile::Present, launcher_draw_finished,
                             launcher_present_finished);
        launcher_profile.report(page,
            static_cast<int>(ImGui::GetIO().DisplaySize.x * ImGui::GetIO().DisplayFramebufferScale.x),
            static_cast<int>(ImGui::GetIO().DisplaySize.y * ImGui::GetIO().DisplayFramebufferScale.y),
            launcher_window_flags, launcher_frame_rate);
    }

    g_launcher_animation_seconds = -1.0;
    g_mod_launch.abandon();
    if(!result.start_game)shutdown_online_mods();

    std::fprintf(stderr, "[perf][launcher-background] cache-rebuilds=%llu tiles=%llu\n",
        static_cast<unsigned long long>(launcher_background.cache_rebuilds),
        static_cast<unsigned long long>(launcher_background.tiles_submitted));
    ReleaseLauncherBackground(launcher_background);
    std::fprintf(stderr, "[perf][launcher-panels] cache-builds=%llu cache-hits=%llu\n",
        static_cast<unsigned long long>(launcher_panel_cache.builds()),
        static_cast<unsigned long long>(launcher_panel_cache.hits()));
    launcher_panel_cache.clear();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    const auto launcher_elapsed_microseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - launcher_animation_epoch)
            .count();
    const double launcher_average_vertices = launcher_rendered_frames > 0U
        ? static_cast<double>(launcher_total_vertices) /
              static_cast<double>(launcher_rendered_frames)
        : 0.0;
    const double launcher_average_indices = launcher_rendered_frames > 0U
        ? static_cast<double>(launcher_total_indices) /
              static_cast<double>(launcher_rendered_frames)
        : 0.0;
    const double launcher_average_build_ms = launcher_rendered_frames > 0U
        ? static_cast<double>(launcher_ui_build_microseconds) /
              static_cast<double>(launcher_rendered_frames) / 1000.0
        : 0.0;
    const double launcher_average_present_ms = launcher_rendered_frames > 0U
        ? static_cast<double>(launcher_present_microseconds) /
              static_cast<double>(launcher_rendered_frames) / 1000.0
        : 0.0;
    std::fprintf(stderr,
                 "[perf][launcher] elapsed=%.2fs frames=%llu interactive=%llu "
                 "quiet=%llu unfocused=%llu hidden-wakes=%llu "
                 "avg-vertices=%.0f avg-indices=%.0f peak-vertices=%llu "
                 "avg-build=%.3fms avg-present=%.3fms\n",
                 static_cast<double>(launcher_elapsed_microseconds) /
                     1000000.0,
                 static_cast<unsigned long long>(launcher_rendered_frames),
                 static_cast<unsigned long long>(launcher_interactive_frames),
                 static_cast<unsigned long long>(launcher_quiet_frames),
                 static_cast<unsigned long long>(launcher_unfocused_frames),
                 static_cast<unsigned long long>(launcher_hidden_wakes),
                 launcher_average_vertices,
                 launcher_average_indices,
                 static_cast<unsigned long long>(launcher_peak_vertices),
                 launcher_average_build_ms, launcher_average_present_ms);
    std::fprintf(stderr,
                 "[boot][launcher] handoff complete window-flags=0x%08X\n",
                 static_cast<unsigned>(SDL_GetWindowFlags(window)));
    return result;
}

void dkr::runtime::ui::attach(RT64::Application& application) {
    if (application.presentQueue == nullptr || application.device == nullptr ||
        application.swapChain == nullptr) {
        return;
    }
    std::scoped_lock guard(g_inspector_guard);
    std::scoped_lock present_lock(application.presentQueue->inspectorMutex);
    application.presentQueue->inspector = std::make_unique<RT64::Inspector>(
        application.device.get(), application.swapChain.get(),
        application.chosenGraphicsAPI,
        static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window()));
    application.presentQueue->inspector->setIniPath(g_config_directory / "dkr-port-ui.ini");
    g_inspector = application.presentQueue->inspector.get();
    ImGui::GetIO().ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    LoadLauncherFonts();
    std::fprintf(stderr, "[boot][ui] in-game overlay attached (F1/Escape/Back)\n");
}

void dkr::runtime::ui::detach(RT64::Application& application) {
    std::scoped_lock guard(g_inspector_guard);
    g_inspector = nullptr;
    if (application.presentQueue != nullptr) {
        std::scoped_lock present_lock(application.presentQueue->inspectorMutex);
        dkr::runtime::crt::release();
        dkr::runtime::hud::editor::cancel();
        application.presentQueue->inspector.reset();
    }
}

void dkr::runtime::ui::draw(RT64::Application& application) {
    // Publish only an atomic value; the raster worker snapshots it into the
    // next pass's push constants. No GPU resources or samplers are rebuilt.
    // Keep this before visibility checks so closing the overlay or changing
    // presentation profile cannot leave a stale bias in the running renderer.
    RT64::setDefaultSamplerMipLODBias(
        dkr::runtime::enhancements::effective_texture_lod_bias());
    RT64::setGeneratedMipSampling(
        dkr::runtime::enhancements::modern_presentation_enabled());
    if (application.presentQueue == nullptr || application.framebufferGraphicsWorker == nullptr) {
        return;
    }
    PumpFriendPresence();
    const bool show_overlay =
        g_overlay_visible.load(std::memory_order_acquire);
    const bool show_fps = g_fps_overlay_enabled &&
        dkr::runtime::enhancements::modern_presentation_enabled();
    const bool show_crt = g_crt_enabled && g_crt_strength > 0.0F &&
        dkr::runtime::enhancements::modern_presentation_enabled() &&
        g_crt_filter_index >= 0 &&
        g_crt_filter_index < static_cast<int>(g_crt_filters.size());
    const bool show_online_error = UpdateOnlineErrorNotification();
    const bool show_online_failure_modal =
        g_online_failure_modal_requested || g_online_failure_modal_active;
    const bool show_online_notification = OnlineNotificationActive();
    const bool show_network = g_network_overlay_enabled &&
        dkr::runtime::netplay::session().presentation_active();
    const bool show_controller_input = g_controller_input_overlay_enabled &&
        dkr::runtime::netplay::session().presentation_active();
    const auto online_session_view = dkr::runtime::netplay::session().presentation_view();
    const bool show_online_countdown =
        online_session_view.launch_countdown_active &&
        online_session_view.launch_countdown_remaining_ms > 0U;
    const bool show_online_waiting = OnlineWaitingActive(online_session_view);
    const bool show_mipmap_loading = RT64::mipLoadingVisible();
    const bool keep_owned_ui =
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        dkr::runtime::netplay::experimental::runtime_view().active;
#else
        false;
#endif
    if (
#if defined(__ANDROID__)
        false && // Touch Menu remains available, even when all other overlays are off.
#endif
        !show_overlay && !show_fps && !show_crt && !show_online_error &&
        !show_online_failure_modal &&
        !show_online_notification &&
        !show_network && !show_controller_input && !show_online_countdown &&
        !show_online_waiting && !show_mipmap_loading && !keep_owned_ui) {
        if (application.presentQueue->inspector != nullptr) {
            detach(application);
        }
        return;
    }
    if (application.presentQueue->inspector == nullptr) {
        attach(application);
        if (show_overlay) {
            g_overlay_focus_requested.store(true, std::memory_order_release);
        }
    }
    if (application.presentQueue->inspector == nullptr) {
        return;
    }
    if (show_overlay) {
        dkr::runtime::platform::update_ui_gamepad_navigation();
    }
    RT64::Inspector* inspector = application.presentQueue->inspector.get();
    dkr::runtime::performance_trace::Scope ui_measure(dkr::runtime::performance_trace::Region::UiBuild);
    inspector->newFrame(application.framebufferGraphicsWorker.get());
    if (auto* hud_window = static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window())) {
        int hud_viewport_width = 0;
        int hud_viewport_height = 0;
        SDL_GetWindowSize(hud_window, &hud_viewport_width, &hud_viewport_height);
        dkr::runtime::hud::set_viewport_extent(hud_viewport_width,
                                               hud_viewport_height);
    }
    ApplyStyle();
    ImGui::GetIO().ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    if (show_crt) {
        const CrtFilterEntry& filter =
            g_crt_filters[static_cast<std::size_t>(g_crt_filter_index)];
        dkr::runtime::crt::draw(
            application, filter.path,
            g_crt_scale_mode == 1 ? dkr::runtime::crt::ScaleMode::Tile
                                  : dkr::runtime::crt::ScaleMode::Stretch,
            g_crt_strength, g_crt_status);
    }
    if (show_overlay && dkr::runtime::hud::editor::review_active()) {
        BeginMainWindow("DKR-R HUD Review", ImGuiWindowFlags_NoBackground);
        dkr::runtime::hud::editor::draw_review();
        ImGui::End();
    } else if (show_overlay) {
    BeginMainWindow("DKR-R Overlay", ImGuiWindowFlags_NoBackground);
        const PaddockLauncherTypographyScope overlay_typography;
        bool request_quit_popup = false;
        bool request_restart_popup = false;
        constexpr int overlay_sidebar_count = kMenuPageCount + 2;
        int sidebar_selection = std::clamp(
            g_overlay_sidebar_selection.load(std::memory_order_acquire), 0,
            overlay_sidebar_count - 1);
        const int sidebar_action =
            g_overlay_sidebar_action.exchange(0, std::memory_order_acq_rel);
        if (sidebar_action == 1) {
            request_restart_popup = true;
        } else if (sidebar_action == 2) {
            request_quit_popup = true;
        }
        const bool focus_selected_page =
            g_overlay_focus_requested.exchange(false, std::memory_order_acq_rel);
#if defined(__ANDROID__)
        int mobile_page = g_overlay_page.load(std::memory_order_relaxed);
        dkr::runtime::mobile::menu(mobile_page, true, request_restart_popup, request_quit_popup);
        g_overlay_page.store(mobile_page, std::memory_order_relaxed);
        dkr::runtime::mobile::begin_content();
        const float content_inner_width = ImGui::GetContentRegionAvail().x;
        DrawOverlayContent(content_inner_width);
        dkr::runtime::mobile::end_content();
#else
        const MenuPanelLayout menu = CalculateMenuPanelLayout(ImGui::GetContentRegionAvail());
        const float overlay_margin = menu.margin;
        const float sidebar_width = menu.sidebar_width;
        const float overlay_height = menu.height;
        const float content_x = menu.content_x;
        const float content_panel_width = menu.content_width;
        ImGui::SetCursorPos({overlay_margin, overlay_margin});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kMenuNavSurface);
        ImGui::PushStyleColor(ImGuiCol_Border, kMenuNavBorder);
        const SidebarLayout sidebar_layout = CalculateSidebarLayout(
            overlay_height, sidebar_width);
        ImGui::BeginChild("overlay-nav", {sidebar_width, overlay_height}, true,
                          ImGuiWindowFlags_NavFlattened |
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        const float nav_padding = sidebar_layout.padding;
        ImGui::SetCursorPos({nav_padding, nav_padding});
        const float nav_inner_width = sidebar_width - nav_padding * 2.0F;
        ImGui::PushTextWrapPos(sidebar_width - nav_padding);
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            {ImGui::GetStyle().ItemSpacing.x, sidebar_layout.item_spacing_y});
        ImGui::BeginGroup();
        BrandBlock(nav_inner_width, sidebar_layout.logo_size);
        ImGui::Dummy({0.0F, sidebar_layout.brand_gap});
        // The sidebar is a shoulder-button rail. Excluding its widgets from
        // ImGui navigation keeps the D-pad entirely inside the content panel.
        if (g_page_navigation_request >= 0) {
            sidebar_selection = g_page_navigation_request;
            g_overlay_page = sidebar_selection;
            g_overlay_sidebar_selection.store(sidebar_selection,
                                              std::memory_order_release);
            g_page_navigation_request = -1;
        }
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        ++g_race_button_sidebar;
        SidebarButton("PLAY", 0, sidebar_selection, nav_inner_width,
                      sidebar_layout.button_height);
        SidebarButton("DKR-R ONLINE", kPageOnlineMp, sidebar_selection, nav_inner_width,
                      sidebar_layout.button_height);
        SidebarButton("MODS / HACKS", kPageModsHacks, sidebar_selection, nav_inner_width,
                      sidebar_layout.button_height);
        SidebarButton("GRAPHICS", kPageGraphics, sidebar_selection, nav_inner_width,
                      sidebar_layout.button_height);
        SidebarButton("SOUND", kPageSound, sidebar_selection, nav_inner_width,
                      sidebar_layout.button_height);
        SidebarButton("CONTROLS", kPageControls, sidebar_selection, nav_inner_width,
                      sidebar_layout.button_height);
        SidebarButton("SAVE MANAGER", kPageSaveManager, sidebar_selection, nav_inner_width,
                      sidebar_layout.button_height);
        SidebarButton("TEXTURES", kPageTextures, sidebar_selection, nav_inner_width,
                      sidebar_layout.button_height);
        SidebarButton("ABOUT DKR-R", kPageAbout, sidebar_selection, nav_inner_width,
                      sidebar_layout.button_height);
        SidebarActionGap(nav_inner_width, sidebar_layout.action_section_gap);
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            sidebar_selection == kMenuPageCount
                ? ImVec4{1.0F, 0.58F, 0.04F, 1.0F}
                : ImVec4{0.92F, 0.43F, 0.06F, 1.0F});
        if (ImGui::Button("RESTART DKR-R",
                          {nav_inner_width, sidebar_layout.button_height})) {
            sidebar_selection = kMenuPageCount;
            g_overlay_sidebar_selection.store(sidebar_selection,
                                               std::memory_order_release);
            request_restart_popup = true;
        }
        ImGui::PopStyleColor();
        ImGui::Dummy({0.0F, sidebar_layout.action_gap});
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            sidebar_selection == kMenuPageCount + 1 ? kWarm : kRaceRed);
        const bool leave_island_pressed =
            ImGui::Button("EXIT DKR-R",
                          {nav_inner_width, sidebar_layout.button_height});
        if (leave_island_pressed) {
            sidebar_selection = kMenuPageCount + 1;
            g_overlay_sidebar_selection.store(sidebar_selection,
                                               std::memory_order_release);
            request_quit_popup = true;
        }
        ImGui::PopStyleColor();
        --g_race_button_sidebar;
        ImGui::PopItemFlag();
        ImGui::EndGroup();
        ImGui::PopStyleVar();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::SetCursorPos({content_x, overlay_margin});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kMenuContentSurface);
        ImGui::PushStyleColor(ImGuiCol_Border, kMenuContentBorder);
        ImGui::BeginChild("overlay-content", {content_panel_width, overlay_height}, true,
                          ImGuiWindowFlags_NavFlattened);
        const int overlay_content_page =
            g_overlay_page.load(std::memory_order_relaxed);
        if (g_overlay_last_rendered_page.exchange(
                overlay_content_page, std::memory_order_acq_rel) !=
            overlay_content_page) {
            ImGui::SetScrollY(0.0F);
        }
        const float content_padding = menu.padding;
        const float content_inner_width = menu.inner_width;
        ImGui::SetCursorPos({content_padding, content_padding});
        ImGui::PushItemWidth(content_inner_width);
        ImGui::PushTextWrapPos(content_padding + content_inner_width);
        ImGui::BeginGroup();
        if (focus_selected_page) {
            ImGui::SetKeyboardFocusHere();
        }
        DrawOverlayContent(content_inner_width);
        ImGui::EndGroup();
        ImGui::PopTextWrapPos();
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
#endif
        if (request_quit_popup) {
            // Open the modal in the same parent ID scope where it is rendered.
            // Opening it inside overlay-nav creates a different ImGui popup ID,
            // which made the Exit to Desktop button appear to do nothing.
            ImGui::OpenPopup("Quit DKR-R?");
        }
        if (request_restart_popup) {
            ImGui::OpenPopup("Restart DKR-R?");
        }
        if (BeginPaddedModal("Restart DKR-R?", ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Restart DKR-R and return to the launcher?");
            ImGui::TextDisabled("Progress since the last in-game save point may be lost.");
            if (ImGui::Button("CANCEL", {120.0F, 40.0F})) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, {0.92F, 0.43F, 0.06F, 1.0F});
            if (ImGui::Button("RESTART DKR-R", {170.0F, 40.0F})) {
                SaveSettings();
                g_lifecycle_request.store(
                    dkr::runtime::ui::LifecycleRequest::Restart,
                    std::memory_order_release);
                g_overlay_visible.store(false, std::memory_order_release);
                ultramodern::quit();
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        if (BeginPaddedModal("Quit DKR-R?", ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Exit DKR-R and return to the desktop?");
            ImGui::TextDisabled("Progress since the last in-game save point may be lost.");
            if (ImGui::Button("CANCEL", {120.0F, 40.0F})) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, {0.45F, 0.09F, 0.10F, 1.0F});
            const bool confirm_leave_pressed = ImGui::Button("EXIT DKR-R", {140.0F, 40.0F});
            if (confirm_leave_pressed) {
                SaveSettings();
                g_lifecycle_request.store(
                    dkr::runtime::ui::LifecycleRequest::Exit,
                    std::memory_order_release);
                g_overlay_visible.store(false, std::memory_order_release);
                ultramodern::quit();
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
    ImGui::End();
    }
    {
        // Match launcher modal text without enlarging FPS/network notifications
        // or the game HUD. These dialogs render outside the content child.
        const PaddockLauncherTypographyScope modal_typography(show_overlay);
        PumpDialogJob();
        DrawTexturePackImportModal();
        DrawPaddockLegacyModImportModal();
        DrawTextEntryKeyboard();
    }
    DrawFpsOverlay(application);
    DrawNetworkOverlay();
    DrawControllerInputOverlay();
    DrawOnlineStartCountdown();
    DrawOnlineWaitingNotification(online_session_view);
    DrawMipmapLoadingModal();
    DrawOnlineFailureModal();
    if (show_online_error) DrawOnlineErrorNotification();
    if (show_online_notification) DrawOnlineNotification();
    if (show_overlay) DrawOlToast();
    ApplyPaddockModalDim();
#if defined(__ANDROID__)
    dkr::runtime::mobile::draw_controls(show_overlay);
    dkr::runtime::mobile::editor();
#endif
    inspector->endFrame();
}

bool dkr::runtime::ui::handle_runtime_event(SDL_Event* event) {
    if (event == nullptr) {
        return false;
    }
#if defined(__ANDROID__)
    if (dkr::runtime::mobile::event(*event, !overlay_visible())) return true;
#endif
    if (HandleInputCaptureEvent(event)) {
        return true;
    }
    const bool code_keyboard_visible =
        g_online_code_keyboard_visible.load(std::memory_order_acquire);
    if (code_keyboard_visible &&
        ((event->type == SDL_KEYDOWN && event->key.repeat == 0 &&
          event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) ||
         (event->type == SDL_CONTROLLERBUTTONDOWN &&
          event->cbutton.button == SDL_CONTROLLER_BUTTON_B))) {
        g_online_code_keyboard_cancel_requested.store(
            true, std::memory_order_release);
        return true;
    }
    if (dkr::runtime::hud::editor::active() &&
        (event->type == SDL_CONTROLLERDEVICEREMOVED || (event->type == SDL_WINDOWEVENT &&
         event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)))
        dkr::runtime::hud::editor::interrupt_move();
    if (!dkr::runtime::hud::editor::active() && event->type == SDL_KEYDOWN && event->key.repeat == 0 &&
        event->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
        toggle_overlay();
        return true;
    }
    if (g_overlay_visible.load(std::memory_order_acquire) &&
        !dkr::runtime::hud::editor::active() &&
        !code_keyboard_visible &&
        event->type == SDL_CONTROLLERBUTTONDOWN) {
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            constexpr int sidebar_count = kMenuPageCount + 2;
            const int current = g_overlay_sidebar_selection.load(
                std::memory_order_relaxed);
            const int selected = StepSidebarSelection(current,-1);
            g_overlay_sidebar_selection.store(selected,
                                              std::memory_order_release);
            if (selected < kMenuPageCount) {
                g_overlay_page.store(selected, std::memory_order_relaxed);
                g_overlay_focus_requested.store(true,
                                                std::memory_order_release);
            }
            return true;
        }
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            constexpr int sidebar_count = kMenuPageCount + 2;
            const int current = g_overlay_sidebar_selection.load(
                std::memory_order_relaxed);
            const int selected = StepSidebarSelection(current,1);
            g_overlay_sidebar_selection.store(selected,
                                              std::memory_order_release);
            if (selected < kMenuPageCount) {
                g_overlay_page.store(selected, std::memory_order_relaxed);
                g_overlay_focus_requested.store(true,
                                                std::memory_order_release);
            }
            return true;
        }
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            const int selected = g_overlay_sidebar_selection.load(
                std::memory_order_relaxed);
            if (selected == kMenuPageCount) {
                g_overlay_sidebar_action.store(1, std::memory_order_release);
                return true;
            }
            if (selected == kMenuPageCount + 1) {
                g_overlay_sidebar_action.store(2, std::memory_order_release);
                return true;
            }
        }
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
            event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
            event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
            event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            // D-pad events continue to the ImGui content panel; sidebar items
            // carry NoNav and therefore cannot steal this focus.
        }
    }
    std::scoped_lock guard(g_inspector_guard);
    if (g_inspector == nullptr) {
        return false;
    }
    std::scoped_lock frame_lock(g_inspector->frameMutex);
    // RT64's ImGui backend owns normal pointer, keyboard and gamepad event
    // delivery. Do not turn those events into page-focus requests: ImGui must
    // be allowed to retain the item selected by the previous event so the
    // player can move from the sidebar into sliders, combos and buttons.
    return g_inspector->handleSdlEvent(event);
}

bool dkr::runtime::ui::input_capture_active() {
    return
#if defined(__ANDROID__)
           dkr::runtime::mobile::editing() ||
#endif
           dkr::runtime::hud::editor::active() || (g_capture_action != -1 && !g_capture_finished) ||
           dkr::runtime::platform::controller_mapping_progress().capturing ||
           g_online_code_keyboard_visible.load(std::memory_order_acquire);
}

void dkr::runtime::ui::toggle_overlay() {
#if defined(__ANDROID__)
    dkr::runtime::mobile::clear();
#endif
    if (dkr::runtime::hud::editor::active()) { dkr::runtime::hud::editor::request_back(); return; }
    const bool next = !g_overlay_visible.load(std::memory_order_acquire);
    g_overlay_page = 0;
    g_overlay_last_rendered_page.store(-1, std::memory_order_release);
    g_overlay_sidebar_selection.store(0, std::memory_order_release);
    g_overlay_sidebar_action.store(0, std::memory_order_release);
    g_overlay_visible.store(next, std::memory_order_release);
    g_overlay_focus_requested.store(next, std::memory_order_release);
}

bool dkr::runtime::ui::overlay_visible() {
    return g_overlay_visible.load(std::memory_order_acquire);
}

dkr::runtime::ui::LifecycleRequest dkr::runtime::ui::lifecycle_request() {
    return g_lifecycle_request.load(std::memory_order_acquire);
}

void dkr::runtime::ui::reset_lifecycle_request() {
    g_lifecycle_request.store(LifecycleRequest::None, std::memory_order_release);
}

void dkr::runtime::ui::report_mod_error(std::string error) {
    g_mod_launch.report_error(std::move(error));
}

void dkr::runtime::ui::shutdown_online_mods() {
    // Explicitly join while DirectSession is alive, outside any SDL/network
    // locks. Never let cross-translation-unit static destruction own this.
    g_online_mod_sync.reset();g_online_mod_link.reset();
    g_online_mod_host_request.reset();g_online_mod_watching=false;
}

std::shared_ptr<const dkr::mods::online::RuntimeResources> dkr::runtime::ui::online_mod_resources() {
    const auto network=netplay::session().view();const auto& digest=network.room.manifest.mod_manifest_hash;
    if(digest.empty())return {};
    if(!g_online_mod_sync)throw dkr::mods::Error("The accepted online mods have no local preparation owner.");
    const auto proof=g_online_mod_sync->snapshot();
    if((proof.phase!=dkr::mods::online::SyncPhase::HostReady && proof.phase!=dkr::mods::online::SyncPhase::Verified) ||
       !proof.profile || !proof.runtime || proof.profile->digest!=digest)
        throw dkr::mods::Error("The accepted lobby's mod resources are not locally verified. Rejoin the lobby.");
    return std::static_pointer_cast<const dkr::mods::online::RuntimeResources>(proof.runtime);
}

void dkr::runtime::ui::report_online_game_end(std::string message, bool lobby_retained) {
    g_page_navigation_request = kPageOnlineMp;
    g_online_page.section = lobby_retained ? OlSection::Lobby : OlSection::Play;
    g_online_page.was_active = lobby_retained;
    g_online_page.was_joining = false;
    g_online_page.open_match_end = true;
    g_online_page.match_end_lobby_retained = lobby_retained;
    g_online_page.match_end_notice = std::move(message);
    g_online_error_notification.clear();
    g_online_failure_modal_requested = false;
    g_online_failure_modal_active = false;
}

#if defined(DKR_EXPERIMENTAL_RACE_TEST)
bool dkr::runtime::ui::configure_owned_online_check(const rom::Identity& identity,bool host,unsigned players,std::string& error) {
    auto& online=netplay::session();
    if(!identity.supported()||players<2||players>4||!online.enable_owned_backend(netplay::experimental::runtime_available(identity.revision))) {
        error="Invalid isolated owned online check.";return false;
    }
    // This function is reachable only after the NEW isolated-profile check.
    // Compare the public Modern preset without altering a real user's setup.
    if(const char* modern=std::getenv("DKR_OWNED_CHECK_MODERN");modern&&std::string_view(modern)=="1") {
        auto config=ultramodern::renderer::get_graphics_config();
        enhancements::set_presentation_profile(enhancements::PresentationProfile::Modern);
        ApplyProfileGraphics(config,enhancements::PresentationProfile::Modern);
        if(const char* api=std::getenv("DKR_OWNED_CHECK_API");api&&std::string_view(api)=="Vulkan")
            config.api_option=GraphicsApi::Vulkan;
        // Private isolated checks only: an explicit comparable FPS target.
        if(const char* rate=std::getenv("DKR_OWNED_CHECK_FPS")) {
            const std::string_view text(rate);
            if(text=="60"||text=="120"||text=="180") {
                config.rr_option=ultramodern::renderer::RefreshRate::Manual;
                config.rr_manual_value=std::atoi(rate);
            }
        }
        ultramodern::renderer::set_graphics_config(config);
    }
    online.configure_manifest(BuildNetplayManifest(identity));
    // The diagnostic requires a new, empty profile. Seed only the in-memory
    // session save; never create or replace a single-player save for this check.
    auto bytes=saves::codec::blank_bytes();
    online.configure_session_save(std::move(bytes),saves::install_synchronized_online_adventure);
    if(!host)return true;
    g_online_experimental_rollback=true;g_online_maximum_players=int(players);
    g_online_automatic_delay=false;g_online_manual_delay=1;g_online_rollback_window=6;
    g_online_save_seed_mode=int(saves::OnlineSaveSeedMode::Fresh);
    OnlineHostRequest request;request.manifest=BuildNetplayManifest(identity);
    request.rules.maximum_players=std::uint8_t(players);request.rules.synchronization=netplay::SynchronizationMode::ExperimentalRollback;
    request.rules.automatic_input_delay=false;request.rules.manual_input_delay=1;request.rules.rollback_window=6;
    request.save_mode=saves::OnlineSaveSeedMode::Fresh;request.room=g_online_room_name;request.name=g_online_player_name;
    if(!FinishOnlineLobby(request,{},0)){error=g_online_action_status;return false;}
    return true;
}
#endif
