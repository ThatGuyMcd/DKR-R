#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/android/mobile_ui.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <SDL.h>
#include <cassert>
#include <cmath>
namespace dkr::runtime::ui {
void toggle_overlay() {}
} // namespace dkr::runtime::ui
namespace {
void frame() {
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1200, 800};
    io.DeltaTime = 1.0F / 60;
    ImGui::NewFrame();
    assert(io.DisplaySize.x == 600 && io.DisplaySize.y == 400);
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({600, 400});
    ImGui::Begin("Test scrolling", nullptr, ImGuiWindowFlags_NoDecoration);
    for (int i = 0; i < 80; ++i)
        ImGui::Text("Scrollable row %d", i);
    ImGui::End();
    ImGui::Render();
    auto* draw = ImGui::GetDrawData();
    assert(draw->DisplaySize.x == 1200 && draw->DisplaySize.y == 800);
    assert(draw->FramebufferScale.x == 1);
}
void finger(Uint32 type, float x, float y, Uint32 timestamp) {
    SDL_Event e{};
    e.type = type;
    e.tfinger.type = type;
    e.tfinger.fingerId = 21;
    e.tfinger.x = x;
    e.tfinger.y = y;
    e.tfinger.timestamp = timestamp;
    assert(dkr::runtime::mobile::event(e, false));
}
} // namespace
int main() {
    using namespace dkr::runtime::mobile;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    install_context();
    frame();
    frame();
    finger(SDL_FINGERDOWN, .5F, .7F, 100);
    frame();
    finger(SDL_FINGERMOTION, .5F, .4F, 120);
    frame();
    frame();
    auto* window = ImGui::FindWindowByName("Test scrolling");
    assert(window && window->Scroll.y > 50);
    finger(SDL_FINGERUP, .5F, .4F, 140);
    frame();
    clear();
    frame();
    frame();
    assert(!io.MouseDown[0]);
    SDL_Event synthetic{};
    synthetic.type = SDL_MOUSEBUTTONDOWN;
    synthetic.button.which = SDL_TOUCH_MOUSEID;
    assert(event(synthetic, false));
    // Retained mouse events are normalized exactly once, not on every frame.
    io.AddMousePosEvent(400, 200);
    frame();
    assert(std::abs(io.MousePos.x - 200) < .01F);
    frame();
    assert(std::abs(io.MousePos.x - 200) < .01F);
    ImGui::DestroyContext();
}
