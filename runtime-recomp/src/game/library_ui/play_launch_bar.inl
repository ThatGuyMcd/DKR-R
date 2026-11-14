// The header and launch action stay outside the scrolling Play content on
// both UI surfaces. Heights are in the same logical pixels as other controls.
#include "panel_footer.hpp"
constexpr float kPlayStartHeight = 48.F;

// Available only with the existing token-authenticated disposable UI profile.
extern std::filesystem::path g_launcher_layout_root;
extern int g_launcher_layout_step;
bool PlayLaunchLayoutCheck() {
    const char* value = std::getenv("DKR_OWNED_CHECK_PLAY_LAUNCH");
    return !g_launcher_layout_root.empty() && value &&
           (std::string_view(value) == "1" || std::string_view(value) == "save");
}
bool SaveDraftLayoutCheck() {
    const char* value = std::getenv("DKR_OWNED_CHECK_PLAY_LAUNCH");
    return PlayLaunchLayoutCheck() && std::string_view(value) == "save";
}
struct PlayLaunchGeometry {
    ImVec2 button_min{}, button_max{}, clip_min{}, clip_max{};
    ImVec2 footer_min{}, footer_max{};
    ImVec2 panel_min{}, panel_max{};
    float available_width = 0.F;
    float body_scroll = 0.F, body_max = 0.F, parent_scroll = 0.F;
    bool focused = false, enabled = false;
    bool body_focused = false;
    bool body_scrollbar = false, main_focused = false;
    float body_right = 0.F;
};
PlayLaunchGeometry g_play_launch_geometry;
void PlayLaunchBodyCheck() {
    if (!PlayLaunchLayoutCheck())
        return;
    auto& geometry = g_play_launch_geometry;
    geometry.body_scroll = ImGui::GetScrollY();
    geometry.body_max = ImGui::GetScrollMaxY();
    geometry.body_focused = ImGui::GetCurrentContext()->NavWindow == ImGui::GetCurrentWindow();
    geometry.body_scrollbar = ImGui::GetCurrentWindow()->ScrollbarY;
    geometry.body_right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
}
void LibraryNavigationScrollWindow(ImGuiWindow*);
void LibraryNavigationPinnedAction(ImGuiID);
void PlayLaunchButtonCheck(bool enabled) {
    LibraryNavigationPinnedAction(enabled ? ImGui::GetItemID() : 0);
    if (!PlayLaunchLayoutCheck())
        return;
    auto& geometry = g_play_launch_geometry;
    const auto* window = ImGui::GetCurrentWindow();
    geometry.button_min = ImGui::GetItemRectMin();
    geometry.button_max = ImGui::GetItemRectMax();
    geometry.clip_min = window->ClipRect.Min;
    geometry.clip_max = window->ClipRect.Max;
    geometry.parent_scroll = ImGui::GetScrollY();
    geometry.panel_min = {window->Pos.x + window->WindowBorderSize, window->Pos.y + window->WindowBorderSize};
    geometry.panel_max = {window->Pos.x + window->Size.x - window->WindowBorderSize - window->ScrollbarSizes.x,
                          window->Pos.y + window->Size.y - window->WindowBorderSize - window->ScrollbarSizes.y};
    geometry.focused = ImGui::IsItemFocused();
    geometry.enabled = enabled;
    geometry.main_focused = false;
    for (auto* item = ImGui::GetCurrentContext()->NavWindow; item; item = item->ParentWindow)
        geometry.main_focused |= item == window;
}
void PlayLaunchCaptureCheck() {
    if (!PlayLaunchLayoutCheck())
        return;
    const auto& g = g_play_launch_geometry;
    const bool visible = g.button_min.x >= g.clip_min.x && g.button_min.y >= g.clip_min.y &&
                         g.button_max.x <= g.clip_max.x && g.button_max.y <= g.clip_max.y;
    std::fprintf(
        stderr,
        "[play-launch][isolated-check] step=%d visible=%d focused=%d enabled=%d "
        "height=%.1f top=%.1f bottom=%.1f body-scroll=%.1f body-max=%.1f parent-scroll=%.1f "
        "button-left=%.1f button-right=%.1f bar-left=%.1f bar-right=%.1f bar-top=%.1f bar-bottom=%.1f available=%.1f "
        "panel-left=%.1f panel-right=%.1f panel-bottom=%.1f body-focused=%d body-scrollbar=%d body-right=%.1f nav-main=%d\n",
        g_launcher_layout_step, int(visible), int(g.focused), int(g.enabled), g.button_max.y - g.button_min.y,
        g.button_min.y, g.button_max.y, g.body_scroll, g.body_max, g.parent_scroll, g.button_min.x, g.button_max.x,
        g.footer_min.x, g.footer_max.x, g.footer_min.y, g.footer_max.y, g.available_width, g.panel_min.x, g.panel_max.x,
        g.panel_max.y, int(g.body_focused), int(g.body_scrollbar), g.body_right, int(g.main_focused));
}

struct SaveDraftGeometry {
    ImRect panel, bounds;
    float top = 0.F;
    bool dirty = false;
};
SaveDraftGeometry g_save_draft_geometry;
void SaveDraftCaptureCheck() {
    const auto& g = g_save_draft_geometry;
    std::fprintf(
        stderr,
        "[save-draft][isolated-check] step=%d bar-left=%.1f bar-right=%.1f bar-top=%.1f bar-bottom=%.1f panel-left=%.1f panel-right=%.1f panel-bottom=%.1f dirty=%d\n",
        g_launcher_layout_step, g.panel.Min.x, g.panel.Max.x, g.top, g.panel.Max.y, g.bounds.Min.x, g.bounds.Max.x,
        g.bounds.Max.y, int(g.dirty));
}

float PlayLaunchButtonWidth(float width) {
    return std::max(1.F, std::min(width * .85F, 1280.F));
}

std::string PlayLaunchSummary(const PlayPageContext& play, bool live) {
    std::string game;
    for (const auto& entry : play.rom_catalog) {
        if (entry.path == play.selected_rom) {
            game = entry.label;
            break;
        }
    }
    if (game.empty())
        game = std::string(dkr::localisation::source(play.rom_ready ? "Diddy Kong Racing" : "Load a ROM to start."));
    const bool mods = live ? dkr::session_content::running_modded.load() : dkr::session_content::next_enabled.load();
    return game + "  |  " + std::string(dkr::localisation::source(SessionModsCaption(mods)));
}

float PlayLaunchBarHeight(float width, const std::string& summary) {
    return 10.F + PaddockTextHeight(PaddockReading(13.F, false, 1.4F), summary, std::max(1.F, width - 8.F)) + 8.F +
           kPlayStartHeight + 10.F;
}

void DrawPlayLaunchSummary(float width, const std::string& summary) {
    const auto at = ImGui::GetCursorScreenPos();
    auto* window = ImGui::GetCurrentWindow();
    const auto panel = PanelFooterBounds(window);
    const ImVec2 footer_min{panel.Min.x, at.y}, footer_max{panel.Max.x, panel.Max.y};
    // Paint through the panel padding, retaining its border and rounded bottom
    // corners. The normal text/item clip would leave a strip down each side.
    DrawPanelFooterBackground(window, at.y, PaddockCol(0x092534), PaddockCol(0x325365));
    if (PlayLaunchLayoutCheck()) {
        g_play_launch_geometry.footer_min = footer_min;
        g_play_launch_geometry.footer_max = footer_max;
        g_play_launch_geometry.available_width = width;
    }
    PaddockGap(10.F);
    PaddockText(PaddockReading(13.F, false, 1.4F), PaddockRgb(0xB0C9CC), summary, width - 8.F);
    PaddockGap(8.F);
    ImGui::SetCursorScreenPos(
        {std::floor((panel.Min.x + panel.Max.x - PlayLaunchButtonWidth(width)) * .5F), ImGui::GetCursorScreenPos().y});
}
