#pragma once
#include "runtime_accessibility.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstring>

namespace dkr::runtime::accessibility {
// Called immediately after begin_frame, before drawing an older focused
// control. An Enter already sampled for that old control must not reach it.
inline void prepare_imgui_focus(HostTree& tree) {
    if (!tree.has_pending_focus()) return;
    auto& context = *ImGui::GetCurrentContext();
    ImGui::ClearActiveID();
    context.NavActivateId = context.NavActivateDownId = context.NavActivatePressedId = 0;
}

// Called after registration of the owning current-frame item. A newly
// validated AX focus request changes the real keyboard target once; redraws
// leave keyboard navigation free to choose its next target.
inline void apply_imgui_focus(HostTree& tree, const std::string& id, bool text_editor = false) {
    if (!tree.take_focus_request(id)) return;
    auto& context = *ImGui::GetCurrentContext();
    if (context.LastItemData.ID == 0) return;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::ClearActiveID();
    ImGui::FocusWindow(window);
    ImGui::SetFocusID(context.LastItemData.ID, window);
    context.NavDisableHighlight = false;
    context.NavDisableMouseHover = true;
    ImGui::ScrollToBringRectIntoView(window, context.LastItemData.Rect);
    if (text_editor) ImGui::ActivateItemByID(context.LastItemData.ID);
}
inline void reload_imgui_text_editor(ImGuiID id) {
    if (auto* state = ImGui::GetInputTextState(id)) state->ReloadUserBufAndMoveToEnd();
}
// Call after the input widget and before later buttons. Keep ImGui's active
// editor in sync so its private buffer cannot overwrite a native replacement.
inline bool apply_imgui_text_action(const std::optional<Action>& action,
                                    char* buffer, std::size_t capacity) {
    if (!action || action->kind != ActionKind::SetText || !buffer || capacity < 2 ||
        !valid_text_value(action->text, capacity - 1)) return false;
    const bool changed = action->text != buffer;
    std::memcpy(buffer, action->text.data(), action->text.size());
    buffer[action->text.size()] = '\0';
    reload_imgui_text_editor(ImGui::GetItemID());
    return changed;
}
} // namespace dkr::runtime::accessibility
