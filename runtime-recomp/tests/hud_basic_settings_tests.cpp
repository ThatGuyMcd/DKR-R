// Exercise the production settings entry and actual ImGui selectable actions,
// not the suspended editor modal. No native game window is opened.
#include "../src/game/hud_basic_settings.cpp"
#include "imgui_internal.h"
#include <cassert>
#include <cstdio>

namespace dkr::runtime::hud {
namespace {
LayoutMode stored = LayoutMode::Custom;
bool fail_save = false;
int writes = 0;
} // namespace
LayoutMode mode() {
    return basic_mode(stored);
}
bool apply_basic_mode(LayoutMode value) {
    ++writes;
    assert(value == LayoutMode::Original || value == LayoutMode::FitToViewport);
    if (fail_save)
        return false;
    stored = value;
    return true;
}
} // namespace dkr::runtime::hud
int main() {
    namespace hud = dkr::runtime::hud;
    namespace ed = hud::editor;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Match the launcher: cards have no implicit padding, dropdown popups
    // must explicitly opt into the standard control inset.
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {0, 0};
    style.FramePadding = {16, 11};
    style.ItemSpacing = {12, 13};
    io.Fonts->AddFontDefault();
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    for (const auto size : {ImVec2{1280, 800}, ImVec2{1920, 1080}, ImVec2{640, 480}}) {
        io.DisplaySize = size;
        auto frame = [&](int action, bool modern = true) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(size);
            ImGui::Begin("HUD settings test", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGuiWindow* target = ImGui::GetCurrentWindow();
            ImGuiID id = 0;
            if (action == 1)
                id = target->GetID("##hud-basic-layout");
            else if (action == 2 || action == 3) {
                target = ImGui::FindWindowByName("##Combo_00");
                assert(target);
                id = target->GetID(action == 2 ? "4:3" : "Fit to Window");
            }
            if (id) {
                GImGui->NavWindow = target;
                GImGui->NavId = id;
                GImGui->NavActivateId = id;
                GImGui->NavActivateDownId = id;
            }
            const bool changed = ed::draw_settings(modern, size.x - 32, nullptr, nullptr, true);
            assert(style.WindowPadding.x == 0 && style.WindowPadding.y == 0);
            assert(style.FramePadding.x == 16 && style.FramePadding.y == 11);
            if (const auto* popup = ImGui::FindWindowByName("##Combo_00"); popup && popup->Active) {
                assert(popup->WindowPadding.x == 16 && popup->WindowPadding.y == 12);
                assert(popup->WorkRect.Min.x - popup->Pos.x >= 15.99F);
                assert(popup->WorkRect.Min.y - popup->Pos.y >= 11.99F);
            }
            assert(!ed::active() && !ed::review_active());
            ImGui::End();
            ImGui::Render();
            const auto* data = ImGui::GetDrawData();
            assert(data->Valid && data->TotalVtxCount > 0);
            return changed;
        };
        frame(0);
        frame(0);
        for (int i = 0; i < 12; ++i) {
            frame(1);
            frame(0);
            assert(frame(i % 2 ? 2 : 3));
            assert(hud::mode() == (i % 2 ? hud::LayoutMode::Original : hud::LayoutMode::FitToViewport));
            frame(0);
        }
        const auto previous = hud::mode();
        hud::fail_save = true;
        frame(1);
        frame(0);
        assert(!frame(3));
        assert(hud::mode() == previous && ed::save_failed);
        hud::fail_save = false;
        frame(0);
        const int before = hud::writes;
        frame(1, false);
        frame(0, false);
        assert(hud::writes == before);
        ed::request_back();
        ed::interrupt_move();
        ed::accept_preset_name("disabled");
        ed::draw_review();
        ed::cancel();
        assert(!ed::active() && !ed::review_active());
    }
    assert(hud::basic_mode(hud::LayoutMode::Custom) == hud::LayoutMode::Original);
    assert(hud::basic_mode(hud::LayoutMode::SafeArea) == hud::LayoutMode::Original);
    ImGui::DestroyContext();
    std::puts(
        "HUD basic settings: popup padding/restoration, real selector activation, repeated changes, disabled editor and save failure PASS");
}
