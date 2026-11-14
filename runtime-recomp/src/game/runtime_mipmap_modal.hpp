#pragma once
#include "render/rt64_generated_mip_config.h"
#include "imgui.h"
#include <algorithm>

namespace dkr::runtime::ui {
inline void draw_mipmap_loading_modal() {
    constexpr const char* title = "PREPARING TEXTURES";
    const bool visible = RT64::mipLoadingVisible();
    if (visible && !ImGui::IsPopupOpen(title))
        ImGui::OpenPopup(title);
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSize(ImVec2{std::max(160.0F, std::min(560.0F, viewport->WorkSize.x - 40.0F)), 0.0F},
                             ImGuiCond_Always);
    if (ImGui::BeginPopupModal(title, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings)) {
        if (!visible)
            ImGui::CloseCurrentPopup();
        else {
            const auto pos = ImGui::GetCursorScreenPos();
            const float angle = static_cast<float>(ImGui::GetTime() * 5.0);
            auto* draw = ImGui::GetWindowDrawList();
            draw->PathArcTo(ImVec2{pos.x + 13.0F, pos.y + 14.0F}, 10.0F, angle, angle + 4.8F, 28);
            draw->PathStroke(IM_COL32(255, 190, 30, 255), 0, 3.0F);
            ImGui::Dummy(ImVec2{32.0F, 30.0F});
            ImGui::SameLine();
            ImGui::TextWrapped("Preparing textures...");
            ImGui::Spacing();
            ImGui::TextWrapped("%llu textures processed; %llu currently being prepared.",
                               static_cast<unsigned long long>(RT64::MipConfiguration::completed.load()),
                               static_cast<unsigned long long>(RT64::MipConfiguration::pending.load()));
            ImGui::TextWrapped("The game continues when preparation finishes.");
        }
        ImGui::EndPopup();
    }
}
} // namespace dkr::runtime::ui
