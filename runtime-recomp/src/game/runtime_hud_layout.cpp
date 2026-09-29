#include "runtime_hud_layout.hpp"
#include "atomic_snapshot.hpp"

#include "librecomp/helpers.hpp"
#include "presentation_identity.hpp"
#include "recomp.h"
#include "revision_addresses.hpp"
#include "runtime_enhancements.hpp"
#include "runtime_platform.hpp"
#include "virtual_pak_policy.hpp"
#include "hud_layout_config.hpp"
#include "hud_basic_settings.hpp"
#include "hud_reference_layout.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace {

using dkr::runtime::hud::Anchor;
using dkr::runtime::hud::LayoutMode;
namespace hg = dkr::runtime::hud::groups;

struct PublishedState {
    LayoutMode mode = LayoutMode::Original;
    float scale = 1.0F;
    int viewport_width = 640;
    int viewport_height = 480;
    hg::Layout layout;
};

struct SavedElement {
    bool active = false;
    gpr address = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t scale = 0;
};

std::mutex g_mutex;
std::filesystem::path g_config_directory;
LayoutMode g_mode = LayoutMode::Original;
float g_scale = 1.0F;
hg::Layout g_layout;
std::optional<hg::Layout> g_preview;
nlohmann::json g_presets = nlohmann::json::object();
int g_viewport_width = 640;
int g_viewport_height = 480;
dkr::runtime::AtomicSnapshot<const PublishedState> g_published;
thread_local std::shared_ptr<const PublishedState> g_frame_state;
thread_local bool g_group_frame = false;
thread_local hg::Scenario g_scenario = hg::Scenario::Race;
thread_local int g_frame_layout = 0;
thread_local bool g_widget_active = false;
thread_local std::uint32_t g_widget_holder = 0;
thread_local bool g_map_widget_active = false;
thread_local std::uint32_t g_map_holder = 0;
thread_local std::optional<dkr::runtime::hud::Widget> g_timer_widget;
thread_local bool g_text_widget_active=false;
thread_local std::uint32_t g_text_holder=0;
thread_local std::uint32_t g_text_box=0;
thread_local bool g_text_bias=false;
thread_local bool g_rect_bias_active=false;
thread_local std::uint32_t g_rect_holder=0;
thread_local SavedElement g_saved_element;
thread_local SavedElement g_saved_minimap;
thread_local bool g_player_pass_active = false;
thread_local bool g_general_pass_active = false;
thread_local bool g_dialogue_pass_active = false;
thread_local std::uint32_t g_dialogue_pass_holder = 0U;
thread_local bool g_counter_pass_active = false;
thread_local std::uint32_t g_counter_pass_holder = 0U;
thread_local bool g_counter_colour_changed = false;

constexpr std::uint8_t kHudPassMarkerBeginMode = 1U;
constexpr std::uint8_t kHudPassMarkerEndMode = 0U;

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

float ReadFloat(std::uint8_t* rdram, gpr address, int offset) {
    return std::bit_cast<float>(static_cast<std::uint32_t>(MEM_W(offset, address)));
}

void WriteFloat(std::uint8_t* rdram, gpr address, int offset, float value) {
    MEM_W(offset, address) = std::bit_cast<std::uint32_t>(value);
}

bool IsRdramPointer(std::uint32_t value, std::uint32_t size) {
    if (value < 0x80000000U || value > 0x807FFFFFU) return false;
    const std::uint32_t offset = value & 0x00FFFFFFU;
    return offset <= 0x00800000U && size <= 0x00800000U - offset;
}

std::filesystem::path SettingsPath() {
    return g_config_directory / "hud-placement.ini";
}

std::filesystem::path BackupPath() {
    return g_config_directory / "hud-placement.ini.bak";
}

std::filesystem::path LegacyPath() {
    return g_config_directory / "hud-layouts-v1.ini";
}

void PublishLocked() {
    auto state = std::make_shared<PublishedState>();
    state->mode = g_mode;
    state->scale = g_scale;
    state->viewport_width = g_viewport_width;
    state->viewport_height = g_viewport_height;
    g_layout.mode = g_mode;
    g_layout.scale = g_scale;
    // Publish only the two supported presets, at native size. The heap-owned
    // default layout has no custom offsets, even if an old file contains them.
    // Do not copy a 70 KB layout through optional::value_or on the UI stack.
    state->layout.mode = dkr::runtime::hud::basic_mode(g_mode);
    state->mode = state->layout.mode;
    state->scale = state->layout.scale = 1.0F;
    g_published.store(std::move(state), std::memory_order_release);
}

bool LoadFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return false;

    int mode = static_cast<int>(LayoutMode::Original);
    float scale = 1.0F;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1U);
        try {
            if (key == "mode") mode = std::stoi(value);
            else if (key == "scale") scale = std::stof(value);
        } catch (...) {
            // Keep the last valid value when a user-edited line is malformed.
        }
    }
    g_mode = static_cast<LayoutMode>(std::clamp(
        mode, static_cast<int>(LayoutMode::Original),
        static_cast<int>(LayoutMode::FitToViewport)));
    g_scale = hg::safe_scale(scale);
    return true;
}

bool SaveLocked() {
    if (g_config_directory.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(g_config_directory, error);
    const auto target = g_config_directory / "hud-layouts-v2.json";
    const auto temporary = target.string() + ".tmp";
    const auto backup = g_config_directory / "hud-layouts-v2.json.bak";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return false;
        g_layout.mode=g_mode; g_layout.scale=g_scale;
        auto document = hg::encode(g_layout);
        document["presets"] = g_presets;
        output << document.dump(2) << '\n';
        output.flush();
        if (!output) return false;
    }
    std::filesystem::remove(backup, error);
    error.clear();
    if (std::filesystem::exists(target, error)) {
        error.clear();
        std::filesystem::rename(target, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    error.clear();
    std::filesystem::rename(temporary, target, error);
    std::error_code backup_error;
    if (error && std::filesystem::exists(backup, backup_error)) {
        std::error_code restore_error;
        std::filesystem::rename(backup, target, restore_error);
    }
    return !error;
}

void LoadLocked() {
    g_mode = LayoutMode::Original;
    g_scale = 1.0F;
    g_layout = {};
    g_preview.reset();
    g_presets = nlohmann::json::object();
    for (const auto* name : {"hud-layouts-v2.json", "hud-layouts-v2.json.bak"}) {
        const auto path = g_config_directory / name;
        std::error_code error;
        if (std::filesystem::file_size(path, error) > 2*1024*1024 || error) continue;
        try {
            std::ifstream stream(path);
            const auto document = nlohmann::json::parse(stream);
            const auto loaded = hg::decode(document);
            if (!loaded) continue;
            g_layout = *loaded;
            g_mode = g_layout.mode;
            g_scale = g_layout.scale;
            if (document.contains("presets") && document["presets"].is_object() && document["presets"].size() <= 8) {
                for (auto it = document["presets"].begin(); it != document["presets"].end(); ++it)
                    if (!it.key().empty() && it.key().size() <= 48 && hg::decode(it.value())) g_presets[it.key()] = it.value();
            }
            return;
        } catch (...) { /* A corrupt current file must not prevent backup recovery. */ }
    }
    if (LoadFile(SettingsPath())) return;
    if (LoadFile(BackupPath())) {
        SaveLocked();
        return;
    }
    // Preserve the useful mode/scale portion of Workshop-era settings once,
    // while deliberately ignoring custom profiles that are no longer exposed.
    if (LoadFile(LegacyPath())) SaveLocked();
}

std::size_t ResolveElementIndex(std::uint8_t* rdram, std::uint32_t element,
                                int& owner) {
    owner = -1;
    constexpr std::uint32_t kElementSize = 0x20U;
    constexpr std::uint32_t kHudDataSize =
        static_cast<std::uint32_t>(dkr::runtime::hud::kElements.size()) *
        kElementSize;
    for (int player = 0; player < 4; ++player) {
        const std::uint32_t base = static_cast<std::uint32_t>(MEM_W(
            player * 4,
            RdramAddress(dkr::runtime::revision_addresses::PlayerHud)));
        if (IsRdramPointer(base, kHudDataSize) && element >= base &&
            element < base + kHudDataSize &&
            ((element - base) % kElementSize) == 0U) {
            owner = player;
            return (element - base) / kElementSize;
        }
    }
    const std::uint32_t base = static_cast<std::uint32_t>(MEM_W(
        0, RdramAddress(dkr::runtime::revision_addresses::CurrentHud)));
    if (IsRdramPointer(base, kHudDataSize) && element >= base &&
        element < base + kHudDataSize &&
        ((element - base) % kElementSize) == 0U) {
        return (element - base) / kElementSize;
    }
    return dkr::runtime::hud::kElements.size();
}


bool RecordHudMarker(std::uint8_t* rdram, std::uint32_t holder,
                     std::uint8_t mode, std::uint16_t token,
                     std::uint8_t variant) {
    if (rdram == nullptr || !IsRdramPointer(holder, sizeof(std::uint32_t))) {
        return false;
    }
    const std::uint32_t command = static_cast<std::uint32_t>(MEM_W(
        0, static_cast<gpr>(static_cast<std::int32_t>(holder))));
    if (!IsRdramPointer(command, 8U)) return false;
    return dkr::runtime::presentation::record_presentation_marker(
        command, mode, token, variant,
        dkr::runtime::presentation::PresentationMarkerKind::HudPass);
}

bool RecordHudPassMarker(std::uint8_t* rdram, std::uint32_t holder,
                         bool begin, std::uint16_t viewport_cover_token) {
    return RecordHudMarker(
        rdram, holder,
        begin ? kHudPassMarkerBeginMode : kHudPassMarkerEndMode,
        begin ? viewport_cover_token : 0U, 0U);
}

bool BeginHudPassForHolder(std::uint8_t* rdram, std::uint32_t holder) {
    if (rdram == nullptr ||
        !dkr::runtime::enhancements::modern_presentation_enabled()) {
        return false;
    }
    const auto& state = g_frame_state;
    if (!g_group_frame || !state) return false;
    const float host_aspect = state->viewport_height > 0
        ? static_cast<float>(state->viewport_width) /
              static_cast<float>(state->viewport_height)
        : 4.0F / 3.0F;
    return RecordHudMarker(rdram, holder, kHudPassMarkerBeginMode,
        dkr::runtime::hud::encode_hud_viewport_cover(host_aspect),
        dkr::runtime::hud::reference::kPassVariant);
}

bool BeginHudPass(std::uint8_t* rdram, recomp_context* context,
                  int holder_stack_offset) {
    if (rdram == nullptr || context == nullptr) return false;
    return BeginHudPassForHolder(
        rdram,
        static_cast<std::uint32_t>(MEM_W(holder_stack_offset, context->r29)));
}

void EndHudPassForHolder(std::uint8_t* rdram, std::uint32_t holder,
                         bool& active) {
    if (!active || rdram == nullptr) return;
    RecordHudPassMarker(rdram, holder, false, 0U);
    active = false;
}

void EndHudPass(std::uint8_t* rdram, recomp_context* context,
                int holder_stack_offset, bool& active) {
    if (!active || rdram == nullptr || context == nullptr) return;
    EndHudPassForHolder(
        rdram,
        static_cast<std::uint32_t>(MEM_W(holder_stack_offset, context->r29)),
        active);
}

} // namespace

extern "C" void dkr_hud_asset_load_failed(std::uint8_t*, recomp_context* context) {
    // One message per asset per guest thread: no retry loop or per-frame I/O.
    // Registers are observed at the hash-pinned native failed-load branch.
    static thread_local std::array<bool, 256> reported{};
    const auto sprite = static_cast<std::uint32_t>(context->r8);
    if (sprite >= reported.size() || reported[sprite]) return;
    reported[sprite] = true;
    std::fprintf(stderr, "[hud][asset-load-failed] sprite=%u element=0x%08X; native draw skipped, asset will retry on next draw\n",
        sprite, static_cast<std::uint32_t>(context->r16));
}

namespace {
int CurrentHudOwner(std::uint8_t* rdram) {
    const auto current = static_cast<std::uint32_t>(MEM_W(0, RdramAddress(dkr::runtime::revision_addresses::CurrentHud)));
    int owner = -1;
    ResolveElementIndex(rdram, current, owner);
    return owner;
}
hg::Target GroupTarget(int owner, bool shared = false) {
    if (g_frame_layout == 0) return hg::Target::Single;
    if (shared) return hg::Target::Shared;
    return owner == 0 ? hg::Target::Top : hg::Target::Bottom;
}
bool BeginWidget(std::uint8_t* rdram, std::uint32_t holder,
                 dkr::runtime::hud::Widget widget, int owner,
                 std::size_t element = dkr::runtime::hud::kElements.size(), bool slides = true,
                 float authored_x = 0.0F) {
    if (!g_group_frame || !g_frame_state || owner < 0 || owner > 1 ||
        !IsRdramPointer(holder, 4)) return false;
    const auto& state = *g_frame_state;
    const float aspect = dkr::runtime::enhancements::fit_to_window_enabled() ?
        static_cast<float>(state.viewport_width) / state.viewport_height : 4.0F/3.0F;
    const bool v80=dkr::runtime::revision_addresses::CurrentHud==dkr::runtime::revision_addresses::kUsV80.CurrentHud;
    const auto transform = dkr::runtime::hud::reference::transform(state.mode, aspect,
        dkr::runtime::hud::reference::anchor(element, widget, g_scenario,
            g_frame_layout == 1, g_general_pass_active, authored_x),
        static_cast<float>(MEM_W(0,RdramAddress(v80?0x801272E4U:0x80126D24U))), slides);
    if (transform.identity()) return false;
    const auto command = static_cast<std::uint32_t>(MEM_W(0, RdramAddress(holder)));
    if (!IsRdramPointer(command, 8)) return false;
    return dkr::runtime::presentation::record_presentation_marker(command, 1, 0, 0,
        dkr::runtime::presentation::PresentationMarkerKind::HudWidget, transform);
}
void EndWidget(std::uint8_t* rdram, std::uint32_t holder, bool& active) {
    if (active && rdram && IsRdramPointer(holder,4)) {
        const auto command = static_cast<std::uint32_t>(MEM_W(0,RdramAddress(holder)));
        dkr::runtime::presentation::record_presentation_marker(command,0,0,0,
            dkr::runtime::presentation::PresentationMarkerKind::HudWidget);
    }
    active = false;
}
bool RectBiasMarker(std::uint8_t* rdram,std::uint32_t holder,bool begin,unsigned axes=3) {
    if(!rdram||!IsRdramPointer(holder,4))return false;
    const auto command=static_cast<std::uint32_t>(MEM_W(0,RdramAddress(holder)));
    if(!IsRdramPointer(command,8))return false;
    return dkr::runtime::presentation::record_presentation_marker(command,begin?1:0,0,static_cast<std::uint8_t>(axes),
        dkr::runtime::presentation::PresentationMarkerKind::HudRect);
}
} // namespace

// Bias only qualified retail HUD rectangle arguments into the unsigned RDP
// command range. The bridge removes this bias before the group transform.
// This retains Rare's texture loading, sampling and alpha paths verbatim.
extern "C" void dkr_hud_rect_begin(std::uint8_t* rdram,recomp_context* ctx,int scaled) {
    g_rect_bias_active=false;
    if(!g_widget_active||!ctx)return;
    const float x=scaled?std::bit_cast<float>(static_cast<std::uint32_t>(ctx->r6)):static_cast<float>(static_cast<std::int32_t>(ctx->r6));
    const float y=scaled?std::bit_cast<float>(static_cast<std::uint32_t>(ctx->r7)):static_cast<float>(static_cast<std::int32_t>(ctx->r7));
    if(!std::isfinite(x)||!std::isfinite(y)||x< -200||x>700||y< -100||y>600)return;
    g_rect_holder=static_cast<std::uint32_t>(ctx->r4);
    if(!RectBiasMarker(rdram,g_rect_holder,true))return;
    g_rect_bias_active=true;
    ctx->r6=scaled?S32(std::bit_cast<std::uint32_t>(x+256)):ADD32(ctx->r6,256);
    ctx->r7=scaled?S32(std::bit_cast<std::uint32_t>(y+128)):ADD32(ctx->r7,128);
}
extern "C" void dkr_hud_rect_extent(std::uint8_t*,recomp_context* ctx) {
    if(g_rect_bias_active)ctx->r2=0x04000400; // local scaled-rect CPU clip extent only
}
extern "C" void dkr_hud_rect_end(std::uint8_t* rdram,recomp_context*) {
    if(g_rect_bias_active)RectBiasMarker(rdram,g_rect_holder,false);
    g_rect_bias_active=false;
}
extern "C" void dkr_hud_text_begin(std::uint8_t* rdram,recomp_context* ctx) {
    g_text_widget_active=false;
    g_text_bias=false;
    if(!ctx||!g_group_frame)return;
    const auto ra=static_cast<std::uint32_t>(ctx->r31);
    const bool v80=dkr::runtime::revision_addresses::CurrentHud==dkr::runtime::revision_addresses::kUsV80.CurrentHud;
    const std::array<std::uint32_t,6> stopwatch=v80?std::array<std::uint32_t,6>{0x800A3A98,0x800A3AD4,0x800A3B10,0x800A3B68,0x800A3BA0,0x800A3BD8}:
        std::array<std::uint32_t,6>{0x800A3550,0x800A358C,0x800A35C8,0x800A3620,0x800A3658,0x800A3690};
    const std::array<std::uint32_t,3> finish=v80?std::array<std::uint32_t,3>{0x800A65D4,0x800A66B0,0x800A678C}:
        std::array<std::uint32_t,3>{0x800A608C,0x800A6168,0x800A6244};
    std::optional<dkr::runtime::hud::Widget> w;
    if(std::find(stopwatch.begin(),stopwatch.end(),ra)!=stopwatch.end())w=dkr::runtime::hud::Widget::Stopwatch;
    if(std::find(finish.begin(),finish.end(),ra)!=finish.end())w=dkr::runtime::hud::Widget::RaceFinish;
    if(!w)return;
    g_text_holder=static_cast<std::uint32_t>(ctx->r4);
    g_text_widget_active=BeginWidget(rdram,g_text_holder,*w,CurrentHudOwner(rdram));
    // Text uses a persistent cursor; bias only its X coordinate (Y controls
    // native line clipping), then restore the final cursor at the return.
    g_text_box=static_cast<std::uint32_t>(MEM_W(0,RdramAddress(v80?0x8012ADA8U:0x8012A7E8U)));
    const int x=static_cast<std::int32_t>(ctx->r5);
    if(g_text_widget_active&&x>=-200&&x<=700&&IsRdramPointer(g_text_box,4)&&RectBiasMarker(rdram,g_text_holder,true,1)) {
        ctx->r5=ADD32(ctx->r5,256);g_text_bias=true;
    }
}
extern "C" void dkr_hud_text_end(std::uint8_t* rdram,recomp_context*) {
    if(g_text_bias){MEM_H(0,RdramAddress(g_text_box))=MEM_H(0,RdramAddress(g_text_box))-256;RectBiasMarker(rdram,g_text_holder,false,1);g_text_bias=false;}
    EndWidget(rdram,g_text_holder,g_text_widget_active);
}

void dkr::runtime::hud::begin_authored_frame(std::uint8_t* rdram) {
    g_frame_state = g_published.load(std::memory_order_acquire);
    g_group_frame = false;
    g_timer_widget.reset();
    if (!rdram || !g_frame_state) return;
    const auto read = [&](std::uint32_t a){return static_cast<int>(MEM_W(0,RdramAddress(a)));};
    g_frame_layout = read(revision_addresses::HudNumPlayers);
    const int players = static_cast<unsigned char>(MEM_B(0,RdramAddress(revision_addresses::NumberOfGameplayPlayers)));
    const int session = read(revision_addresses::NumberOfActivePlayers);
    g_group_frame = hg::eligible(enhancements::modern_presentation_enabled(),
        std::max(players,session),g_frame_layout,read(revision_addresses::ViewportLayout));
    const auto level = static_cast<std::uint32_t>(read(revision_addresses::CurrentLevelHeader));
    if (!IsRdramPointer(level,0x50)) { g_group_frame=false; return; }
    const int type = static_cast<unsigned char>(MEM_B(0x4C,RdramAddress(level)));
    // Addresses independently verified against both decomp symbol maps.
    const bool v80 = revision_addresses::CurrentHud == revision_addresses::kUsV80.CurrentHud;
    const bool trial = MEM_B(0,RdramAddress(v80 ? 0x8011B474U : 0x8011AEF4U)) != 0;
    // Match retail hud_init_element / Golden Balloon's mode predicates.
    // Taj challenges use the race layout, not the boss-only banana relocation.
    g_scenario = trial ? hg::Scenario::TimeTrial :
        type == 1 ? hg::Scenario::Adventure : hg::scenario_for(type,false);
}

void dkr::runtime::hud::configure(
    const std::filesystem::path& config_directory) {
    std::scoped_lock lock(g_mutex);
    g_config_directory = config_directory;
    LoadLocked();
    PublishLocked();
}

LayoutMode dkr::runtime::hud::mode() {
    std::scoped_lock lock(g_mutex);
    return basic_mode(g_mode);
}

bool dkr::runtime::hud::apply_basic_mode(LayoutMode value) {
    if (value != LayoutMode::Original && value != LayoutMode::FitToViewport) return false;
    std::scoped_lock lock(g_mutex);
    const auto previous_mode = g_mode;
    const auto previous_scale = g_scale;
    g_mode = value;
    g_scale = 1.0F;
    bool saved = false;
    try { saved = SaveLocked(); } catch (const std::exception&) { saved = false; }
    if (!saved) {
        g_layout.mode = g_mode = previous_mode;
        g_layout.scale = g_scale = previous_scale;
        return false;
    }
    g_preview.reset();
    PublishLocked();
    return true;
}

bool dkr::runtime::hud::self_test_basic_settings(const std::filesystem::path& directory) {
    // Explicit diagnostic route: reject non-empty targets; never touch a user's
    // configured profile. Exercise the same persistence/publication as the UI.
    std::error_code error;
    if (directory.empty() || (std::filesystem::exists(directory,error) &&
        !std::filesystem::is_empty(directory,error)) || error) return false;
    std::filesystem::create_directories(directory,error);
    if(error) return false;
    configure(directory);
    for(int i=0;i<32;++i) {
        const auto value=i%2?LayoutMode::FitToViewport:LayoutMode::Original;
        if(!apply_basic_mode(value) || mode()!=value || global_scale()!=1) return false;
    }
    configure(directory);
    if(mode()!=LayoutMode::FitToViewport || apply_basic_mode(LayoutMode::Custom))return false;

    // Simulate a previously saved custom layout; it must be preserved on disk
    // and in storage, but cannot enter the rendered snapshot in this build.
    auto custom=std::make_unique<hg::Layout>();
    custom->mode=LayoutMode::Custom;custom->scale=1.5F;
    custom->entries[0].x=.25F;custom->entries[0].scale=.75F;
    auto document=hg::encode(*custom);document["presets"]={{"Keep me",hg::encode(*custom)}};
    {
        std::ofstream output(directory/"hud-layouts-v2.json");output<<document.dump(2);output.flush();
        if(!output)return false;
    }
    configure(directory);
    auto state=g_published.load();
    if(mode()!=LayoutMode::Original || global_scale()!=1 || state->layout.mode!=LayoutMode::Original ||
        state->layout.scale!=1 || state->layout.entries[0]!=hg::Placement{} || g_layout.entries[0]!=custom->entries[0])return false;
    if(!apply_basic_mode(LayoutMode::FitToViewport))return false;
    nlohmann::json saved;
    {std::ifstream input(directory/"hud-layouts-v2.json");input>>saved;}
    if(saved["placements"]!=document["placements"] || saved["presets"]!=document["presets"] ||
        saved["mode"]!="fill" || saved["scale"]!=1)return false;

    // A blocked temporary path forces the real save-failure branch.
    std::filesystem::create_directory(directory/"hud-layouts-v2.json.tmp",error);
    if(error || apply_basic_mode(LayoutMode::Original) || mode()!=LayoutMode::FitToViewport)return false;
    state=g_published.load();
    if(state->mode!=LayoutMode::FitToViewport || state->scale!=1)return false;
    nlohmann::json unchanged;
    {std::ifstream input(directory/"hud-layouts-v2.json");input>>unchanged;}
    return unchanged==saved;
}

void dkr::runtime::hud::set_mode(LayoutMode value) {
    std::scoped_lock lock(g_mutex);
    g_mode = static_cast<LayoutMode>(std::clamp(
        static_cast<int>(value), static_cast<int>(LayoutMode::Original),
        static_cast<int>(LayoutMode::Custom)));
    PublishLocked();
    SaveLocked();
}

float dkr::runtime::hud::global_scale() {
    return 1.0F;
}

void dkr::runtime::hud::set_global_scale(float value) {
    std::scoped_lock lock(g_mutex);
    g_scale = hg::safe_scale(value);
    PublishLocked();
    SaveLocked();
}

void dkr::runtime::hud::set_viewport_extent(int width, int height) {
    if (width <= 0 || height <= 0) return;
    std::scoped_lock lock(g_mutex);
    if (width == g_viewport_width && height == g_viewport_height) return;
    g_viewport_width = width;
    g_viewport_height = height;
    PublishLocked();
}

hg::Layout dkr::runtime::hud::layout() {
    std::scoped_lock lock(g_mutex);
    return g_layout;
}

bool dkr::runtime::hud::apply_layout(const hg::Layout& value) {
    if (!hg::valid(value)) return false;
    std::scoped_lock lock(g_mutex);
    const auto previous=g_layout;
    g_layout = value;
    g_mode = value.mode;
    g_scale = value.scale;
    if(!SaveLocked()) {g_layout=previous;g_mode=previous.mode;g_scale=previous.scale;return false;}
    g_preview.reset();
    PublishLocked();
    return true;
}

void dkr::runtime::hud::preview_layout(const std::optional<hg::Layout>& value) {
    if (value && !hg::valid(*value)) return;
    std::scoped_lock lock(g_mutex);
    g_preview = value;
    PublishLocked(); // no disk writes while moving or previewing a slider
}

std::vector<std::string> dkr::runtime::hud::preset_names() {
    std::scoped_lock lock(g_mutex);
    std::vector<std::string> names;
    for (auto it=g_presets.begin(); it!=g_presets.end(); ++it) names.push_back(it.key());
    return names;
}
bool dkr::runtime::hud::save_preset(const std::string& name, const hg::Layout& value) {
    if (name.empty() || name.size()>48 || !hg::valid(value) ||
        std::any_of(name.begin(), name.end(), [](unsigned char c){return c<32;})) return false;
    std::scoped_lock lock(g_mutex);
    if (!g_presets.contains(name) && g_presets.size()>=8) return false;
    const auto previous=g_presets;
    g_presets[name] = hg::encode(value);
    if(!SaveLocked()){g_presets=previous;return false;}
    return true;
}
std::optional<hg::Layout> dkr::runtime::hud::load_preset(const std::string& name) {
    std::scoped_lock lock(g_mutex);
    const auto it=g_presets.find(name);
    return it == g_presets.end() ? std::nullopt : hg::decode(*it);
}
bool dkr::runtime::hud::delete_preset(const std::string& name) {
    std::scoped_lock lock(g_mutex);
    const auto previous=g_presets;
    if (g_presets.erase(name)==0) return false;
    if(!SaveLocked()){g_presets=previous;return false;}return true;
}

void dkr::runtime::hud::begin_element(std::uint8_t* rdram,
                                      recomp_context* context) {
    g_saved_element = {};
    if (rdram == nullptr || context == nullptr ||
        !dkr::runtime::enhancements::modern_presentation_enabled()) return;
    const auto state = g_published.load(std::memory_order_acquire);
    if (!state) return;
    const int layout = static_cast<int>(MEM_W(
        0, RdramAddress(dkr::runtime::revision_addresses::ViewportLayout)));
    const bool expanded_split = (layout == 2 || layout == 3) &&
        dkr::runtime::enhancements::fit_to_window_enabled() &&
        static_cast<float>(state->viewport_width) >
            state->viewport_height * kOriginalHudAspect;
    const std::uint32_t raw = static_cast<std::uint32_t>(context->r7);
    if (!IsRdramPointer(raw, 0x20U)) return;
    const gpr address = static_cast<gpr>(static_cast<std::int32_t>(raw));
    int owner = -1;
    const std::size_t index = ResolveElementIndex(rdram, raw, owner);
    if (g_group_frame) {
        const auto widget = index < kElements.size() ? std::optional<Widget>(kElements[index].widget) : g_timer_widget;
        if (owner < 0 && g_timer_widget) owner = CurrentHudOwner(rdram);
        if (!widget) return; // Unknown/local draws never inherit a guessed timer anchor.
        g_widget_holder = static_cast<std::uint32_t>(context->r4);
        // Retail suppresses the slide for asset 40 (PRO AM), not for every
        // speedometer member. Keep this asset test separate from semantic ID.
        g_widget_active = BeginWidget(rdram,g_widget_holder,*widget,owner,index,
            MEM_H(6,address) != 40,
            *widget == Widget::CourseArrows ? ReadFloat(rdram,address,0x0C) : 0.0F);
        if (g_widget_active) {
            const auto colour = RdramAddress(revision_addresses::HudColour);
            // Keep unit textures inside their widget scope instead of the later
            // anonymous batch. Do not alter the scaled-animation alpha sentinel.
            g_counter_colour_changed = counter_requires_immediate_texture_draw(
                ReadFloat(rdram,address,0x08),static_cast<std::uint32_t>(MEM_W(0,colour)));
            if (g_counter_colour_changed) MEM_W(0,colour) = 0xFFFFFFFFU;
        }
        return;
    }
    // Accepted 3/4-player path is independent of every new setting, including
    // global size. Single-camera transitions from a quadrant session fall back
    // unchanged rather than becoming eligible for the new single-player path.
    if (!expanded_split) return;
    const auto counter_anchor = quadrant_counter_anchor(
        expanded_split, layout, owner, index);
    const bool item = quadrant_item_element(index);
    if (counter_anchor != 0U) {
        g_counter_pass_holder = static_cast<std::uint32_t>(context->r4);
        g_counter_pass_active = RecordHudMarker(
            rdram, g_counter_pass_holder, kHudPassMarkerBeginMode,
            encode_hud_viewport_cover(static_cast<float>(state->viewport_width) /
                                      state->viewport_height),
            item ? 0U : counter_anchor);
        if (g_counter_pass_active && !item) {
            // Only the unit-scale texture path is deferred. Scaled animation
            // frames already draw immediately: preserve their alpha sentinel
            // so retail selects the transparency-enabled scaled render mode.
            const auto colour = RdramAddress(revision_addresses::HudColour);
            g_counter_colour_changed = counter_requires_immediate_texture_draw(
                ReadFloat(rdram, address, 0x08),
                static_cast<std::uint32_t>(MEM_W(0, colour)));
            if (g_counter_colour_changed) MEM_W(0, colour) = 0xFFFFFFFFU;
        }
    }
    if (!(g_counter_pass_active && item)) return;
    const float authored_x = ReadFloat(rdram, address, 0x0C);
    // The renderer already owns the counter's horizontal anchor; never also
    // translate it into negative retail coordinates (DKR clips those away).
    const Anchor anchor = g_counter_pass_active ?
        (item ? Anchor::BottomCentre : Anchor::TopCentre) :
        dkr::runtime::hud::effective_anchor(index, authored_x);
    g_saved_element = {
        true, address,
        static_cast<std::uint32_t>(MEM_W(0x0C, address)),
        static_cast<std::uint32_t>(MEM_W(0x10, address)),
        static_cast<std::uint32_t>(MEM_W(0x08, address))};
    if (g_counter_pass_active && item) {
        // Ortho sprites do not have retail's unsigned rectangle clipping.
        // Translate both weapon members in the same full-canvas coordinates.
        const float gutter = fullscreen_gutter_authored(
            static_cast<float>(state->viewport_width) / state->viewport_height);
        WriteFloat(rdram, address, 0x0C,
            ReadFloat(rdram, address, 0x0C) +
            (counter_anchor == kHudAnchorRight ? gutter : -gutter));
    }
}

void dkr::runtime::hud::begin_minimap(std::uint8_t* rdram,
                                      recomp_context* context) {
    if (rdram && context && g_group_frame) {
        g_map_holder = revision_addresses::HudDisplayList;
        g_map_widget_active = BeginWidget(rdram,g_map_holder,Widget::Minimap,0);
    }
}

void dkr::runtime::hud::end_element(std::uint8_t* rdram,
                                    recomp_context* context) {
    EndWidget(rdram,g_widget_holder,g_widget_active);
    EndHudPassForHolder(rdram, g_counter_pass_holder, g_counter_pass_active);
    g_counter_pass_holder = 0U;
    if (rdram != nullptr && g_counter_colour_changed) {
        MEM_W(0, RdramAddress(revision_addresses::HudColour)) = 0xFFFFFFFEU;
    }
    g_counter_colour_changed = false;
    if (!g_saved_element.active || rdram == nullptr) return;
    MEM_W(0x0C, g_saved_element.address) = g_saved_element.x;
    MEM_W(0x10, g_saved_element.address) = g_saved_element.y;
    MEM_W(0x08, g_saved_element.address) = g_saved_element.scale;
    g_saved_element = {};
}

void dkr::runtime::hud::end_minimap(std::uint8_t* rdram,
                                    recomp_context*) {
    EndWidget(rdram,g_map_holder,g_map_widget_active);
    if (!g_saved_minimap.active || rdram == nullptr) return;
    MEM_W(0x0C, g_saved_minimap.address) = g_saved_minimap.x;
    MEM_W(0x10, g_saved_minimap.address) = g_saved_minimap.y;
    MEM_W(0x08, g_saved_minimap.address) = g_saved_minimap.scale;
    g_saved_minimap = {};
}

void dkr::runtime::hud::begin_player_pass(std::uint8_t* rdram,
                                           recomp_context* context) {
    g_player_pass_active = BeginHudPass(rdram, context, 0x30);
}

void dkr::runtime::hud::end_player_pass(std::uint8_t* rdram,
                                         recomp_context* context) {
    EndHudPass(rdram, context, 0x30, g_player_pass_active);
}

void dkr::runtime::hud::begin_general_pass(std::uint8_t* rdram,
                                            recomp_context* context) {
    g_general_pass_active = BeginHudPass(rdram, context, 0x160);
}

void dkr::runtime::hud::end_general_pass(std::uint8_t* rdram,
                                          recomp_context* context) {
    EndHudPass(rdram, context, 0x160, g_general_pass_active);
}

void dkr::runtime::hud::begin_dialogue_pass(std::uint8_t* rdram,
                                             recomp_context* context) {
    g_dialogue_pass_active = false;
    g_dialogue_pass_holder = 0U;
    if (context == nullptr) return;

    // render_dialogue_boxes receives Gfx ** as its first argument. Keep the
    // original holder because the function is free to reuse argument
    // registers before its common return hook runs.
    g_dialogue_pass_holder = static_cast<std::uint32_t>(context->r4);
    // Dialogue, pause and file-select are explicitly outside the HUD editor.
}

void dkr::runtime::hud::end_dialogue_pass(std::uint8_t* rdram,
                                           recomp_context*) {
    EndHudPassForHolder(
        rdram, g_dialogue_pass_holder, g_dialogue_pass_active);
    g_dialogue_pass_holder = 0U;
}

extern "C" void dkr_hud_element_begin(std::uint8_t* rdram,
                                        recomp_context* context) {
    dkr::runtime::hud::begin_element(rdram, context);
}

extern "C" void dkr_hud_element_end(std::uint8_t* rdram,
                                      recomp_context* context) {
    dkr::runtime::hud::end_element(rdram, context);
}

extern "C" void dkr_hud_minimap_begin(std::uint8_t* rdram,
                                        recomp_context* context) {
    dkr::runtime::hud::begin_minimap(rdram, context);
}

extern "C" void dkr_hud_minimap_end(std::uint8_t* rdram,
                                      recomp_context* context) {
    dkr::runtime::hud::end_minimap(rdram, context);
}

extern "C" void dkr_hud_player_pass_begin(std::uint8_t* rdram,
                                            recomp_context* context) {
    dkr::runtime::hud::begin_player_pass(rdram, context);
}

extern "C" void dkr_hud_player_pass_end(std::uint8_t* rdram,
                                          recomp_context* context) {
    dkr::runtime::hud::end_player_pass(rdram, context);
}

extern "C" void dkr_hud_general_pass_begin(std::uint8_t* rdram,
                                             recomp_context* context) {
    dkr::runtime::hud::begin_general_pass(rdram, context);
}

extern "C" void dkr_hud_general_pass_end(std::uint8_t* rdram,
                                           recomp_context* context) {
    dkr::runtime::hud::end_general_pass(rdram, context);
}

extern "C" void dkr_hud_dialogue_pass_begin(std::uint8_t* rdram,
                                              recomp_context* context) {
    dkr::runtime::hud::begin_dialogue_pass(rdram, context);
}

extern "C" void dkr_hud_dialogue_pass_end(std::uint8_t* rdram,
                                            recomp_context* context) {
    dkr::runtime::hud::end_dialogue_pass(rdram, context);
}

void dkr::runtime::hud::begin_timer(std::uint8_t*, recomp_context*) {}
void dkr::runtime::hud::end_timer(std::uint8_t*, recomp_context*) { g_timer_widget.reset(); }
extern "C" void dkr_hud_timer_begin(std::uint8_t* r,recomp_context* c) { dkr::runtime::hud::begin_timer(r,c); }
extern "C" void dkr_hud_timer_end(std::uint8_t* r,recomp_context* c) { dkr::runtime::hud::end_timer(r,c); }
extern "C" void dkr_hud_timer_select(std::uint8_t*,recomp_context*,int widget) {
    g_timer_widget = widget == 0 ? dkr::runtime::hud::Widget::RaceTimer :
        widget == 1 ? dkr::runtime::hud::Widget::LapTimer : dkr::runtime::hud::Widget::RaceFinish;
}

extern "C" void dkr_refresh_combined_accessories(std::uint8_t* rdram,
                                                    recomp_context*) {
    if (rdram == nullptr) return;
    std::array<bool, 4> assigned{};
    std::array<bool, 4> connected{};
    std::array<bool, 4> rumble_capable{};
    for (std::size_t port = 0; port < 4; ++port) {
        const auto status =
            dkr::runtime::platform::player_controller_status(port);
        assigned[port] = status.assigned;
        connected[port] = status.connected;
        rumble_capable[port] = status.rumble;
    }
    const std::uint8_t retail = MEM_B(
        0, RdramAddress(dkr::runtime::revision_addresses::RumblePresent));
    MEM_B(0, RdramAddress(dkr::runtime::revision_addresses::RumblePresent)) =
        dkr::runtime::pak::policy::combined_rumble_mask(
            dkr::runtime::platform::rumble_enabled(), retail,
            assigned, connected, rumble_capable);
}
