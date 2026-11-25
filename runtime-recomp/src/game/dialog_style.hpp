#pragma once

#ifndef IMGUI_VERSION
#include "imgui.h"
#endif

namespace dkr::runtime::ui {

// The Launcher and Overlay shells draw their own edges and use zero window
// padding. Give separate dialogs a surface of their own at Begin time.
class ScopedDialogStyle {
  public:
    explicit ScopedDialogStyle(ImVec2 padding = {18.0F, 14.0F}, float rounding = 16.0F) {
        constexpr ImVec4 surface{0.043F, 0.14F, 0.20F, 0.98F};
        constexpr ImVec4 title{0.04F, 0.25F, 0.35F, 1.0F};
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0F);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, surface);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, surface);
        ImGui::PushStyleColor(ImGuiCol_TitleBg, title);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, title);
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, title);
        ImGui::PushStyleColor(ImGuiCol_Border, {0.22F, 0.44F, 0.51F, 1.0F});
    }

    ~ScopedDialogStyle() {
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(5);
    }

    ScopedDialogStyle(const ScopedDialogStyle&) = delete;
    ScopedDialogStyle& operator=(const ScopedDialogStyle&) = delete;
};

inline bool begin_dialog_modal(const char* name, bool* open, ImGuiWindowFlags flags, ImVec2 padding = {18.0F, 14.0F}) {
    if (!ImGui::IsPopupOpen(name))
        return ImGui::BeginPopupModal(name, open, flags);
    const ScopedDialogStyle style(padding);
    return ImGui::BeginPopupModal(name, open, flags);
}

inline bool begin_dialog_window(const char* name, bool* open, ImGuiWindowFlags flags) {
    const ScopedDialogStyle style;
    return ImGui::Begin(name, open, flags);
}

inline bool begin_dialog_popup(const char* name, ImGuiWindowFlags flags = 0) {
    if (!ImGui::IsPopupOpen(name))
        return ImGui::BeginPopup(name, flags);
    const ScopedDialogStyle style({14.0F, 12.0F}, 12.0F);
    return ImGui::BeginPopup(name, flags);
}

inline bool begin_dialog_tooltip() {
    const ScopedDialogStyle style({12.0F, 8.0F}, 10.0F);
    return ImGui::BeginTooltip();
}

} // namespace dkr::runtime::ui
