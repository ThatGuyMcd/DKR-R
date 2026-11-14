#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/android/mobile_ui.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>
namespace {
float metric_scale = 2;
float metric_insets[4]{};
} // namespace
namespace dkr::runtime::mobile {
void test_metrics(float& scale, float insets[4]) {
    scale = metric_scale;
    std::copy_n(metric_insets, 4, insets);
}
} // namespace dkr::runtime::mobile
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
int gameplay_frame() {
    auto& io = ImGui::GetIO();
    io.DisplaySize = {852, 393}; // UIKit points, independently of Retina pixels.
    io.DisplayFramebufferScale = {3, 3};
    io.DeltaTime = 1.0F / 60;
    ImGui::NewFrame();
    assert(io.DisplaySize.x == 852 && io.DisplaySize.y == 393);
    dkr::runtime::mobile::draw_controls(false);
    ImGui::Render();
    auto* draw = ImGui::GetDrawData();
    assert(draw->DisplaySize.x == 852 && draw->DisplaySize.y == 393);
    assert(draw->FramebufferScale.x == 3 && draw->FramebufferScale.y == 3);
    return draw->TotalVtxCount;
}
void gameplay_finger(Uint32 type, SDL_FingerID id, float x, float y) {
    SDL_Event e{};
    e.type = type;
    e.tfinger.type = type;
    e.tfinger.fingerId = id;
    e.tfinger.x = x / 852;
    e.tfinger.y = y / 393;
    assert(dkr::runtime::mobile::event(e, true));
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

    // iPhone landscape: notch + home-indicator safe areas are UIKit points.
    metric_scale = 1;
    metric_insets[0] = metric_insets[2] = 59;
    metric_insets[3] = 21;
    clear();
    const int visible_vertices = gameplay_frame();
    // With safe rectangle (71,8,710,356), the default stick center is
    // (184.6,278.56), A center (713.6,314.12), R center (724.2,50.72).
    gameplay_finger(SDL_FINGERDOWN, 1, 184.6F, 278.56F);
    gameplay_finger(SDL_FINGERMOTION, 1, 230, 278.56F);
    gameplay_finger(SDL_FINGERDOWN, 2, 713.6F, 314.12F);
    gameplay_finger(SDL_FINGERDOWN, 3, 724.2F, 50.72F);
    dkr::runtime::input::State state{};
    merge_input(state, false);
    assert(state.buttons == 0x8010 && state.stick_x > .99F && std::abs(state.stick_y) < .001F);
    gameplay_finger(SDL_FINGERUP, 2, 713.6F, 314.12F);
    state = {};
    merge_input(state, false);
    assert(state.buttons == 0x10 && state.stick_x > .99F);

    SDL_Event controller{};
    controller.type = SDL_CONTROLLERBUTTONDOWN;
    event(controller, true);
    state = {};
    merge_input(state, false);
    assert(state.buttons == 0 && state.stick_x == 0);
    const int hidden_vertices = gameplay_frame();
    assert(hidden_vertices < visible_vertices);
    // First touch anywhere reveals automatic controls without needing Menu.
    gameplay_finger(SDL_FINGERDOWN, 4, 426, 196);
    assert(gameplay_frame() == visible_vertices);
    gameplay_finger(SDL_FINGERUP, 4, 426, 196);
    gameplay_finger(SDL_FINGERDOWN, 5, 713.6F, 314.12F);
    state = {};
    merge_input(state, false);
    assert(state.buttons == 0x8000);
    SDL_Event background{};
    background.type = SDL_APP_WILLENTERBACKGROUND;
    event(background, true);
    state = {};
    merge_input(state, false);
    assert(state.buttons == 0 && state.stick_x == 0);
    ImGui::DestroyContext();
}
