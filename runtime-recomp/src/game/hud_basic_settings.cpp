#include "hud_layout_editor.hpp"
#include "hud_basic_settings.hpp"
#include "runtime_hud_layout.hpp"
#include "imgui.h"
#include <algorithm>

// Production replacement for the suspended workshop. In particular, this
// translation unit does not construct/copy a Layout or Editor on the UI stack.
namespace dkr::runtime::hud::editor {
namespace {
bool save_failed = false;
bool draw_mode_selector(float width) {
    const auto current = basic_mode(mode());
    bool changed = false;
    ImGui::SetNextItemWidth(std::max(1.0F, std::min(width, ImGui::GetContentRegionAvail().x)));
    // Match runtime_ui.cpp's ControlFontScope(true): popup content needs its
    // own inset because the launcher cards deliberately use zero padding.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0F, 12.0F));
    if (ImGui::BeginCombo("##hud-basic-layout", current == LayoutMode::Original ? "4:3" : "Fit to Window")) {
        constexpr LayoutMode values[]{LayoutMode::Original, LayoutMode::FitToViewport};
        constexpr const char* labels[]{"4:3", "Fit to Window"};
        for (int i = 0; i < 2; ++i) {
            if (ImGui::Selectable(labels[i], current == values[i])) {
                changed = apply_basic_mode(values[i]);
                save_failed = !changed;
            }
            if (current == values[i])
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    return changed;
}
} // namespace
bool draw_settings(bool modern, float width, void (*)(), void (*)(), bool) {
    ImGui::SeparatorText("HUD - SINGLE / TWO PLAYER");
    ImGui::TextWrapped(
        "Choose the 4:3 HUD area or fit the HUD to the window. HUD size stays at 100%%. Three/four-player HUDs are unchanged.");
    ImGui::TextUnformatted("HUD layout");
    ImGui::BeginDisabled(!modern);
    const bool changed = draw_mode_selector(width);
    ImGui::EndDisabled();
    if (save_failed)
        ImGui::TextWrapped("Could not save HUD settings. The previous layout is still active.");
    if (!modern)
        ImGui::TextWrapped("HUD layout options require Modern presentation.");
    return changed;
}
// Keep existing overlay integration inert; there is no editor modal, input
// capture, keyboard request, preview, or live-review surface in this build.
bool active() {
    return false;
}
bool review_active() {
    return false;
}
void draw_review() {}
void accept_preset_name(const std::string&) {}
void cancel() {}
void request_back() {}
void interrupt_move() {}
} // namespace dkr::runtime::hud::editor
