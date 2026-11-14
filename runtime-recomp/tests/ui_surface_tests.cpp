#include "library_ui/surface_policy.hpp"
#include "library_ui/navigation_icons.hpp"
#include "launcher_themes/button_pattern_ui.hpp"
#include "imgui_internal.h"
#include <array>
#include <cassert>
#include <utility>
#include <iostream>
#include <set>

namespace {
std::atomic<bool> g_overlay_visible{true};
bool blocked = false;
bool captured = false;
bool LibrarySidebarInputBlocked() {
    return blocked;
}
bool LibraryContentInputCaptured() {
    return captured;
}
bool PlayLaunchLayoutCheck() {
    return false;
}
int g_launcher_layout_step = 0;
#include "library_ui/navigation_input.inl"
void PinnedContentScrollCase(bool live, dkr::library_ui::SidebarInput mode) {
    using namespace dkr::library_ui;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.F / 60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    sidebar_input = mode;
    navigation_connected = 1;
    navigation_buttons = 0;
    navigation_press_edges = 0;
    for (unsigned axis = 0; axis < 4; ++axis)
        record_navigation_axis(axis, 0);
    LibraryInstallNavigationHook(live);
    ImGuiWindow* content = nullptr;
    ImGuiWindow* body = nullptr;
    ImGuiID start = 0;
    const auto frame = [&](bool initial = false, bool pinned = true) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({480, 320});
        ImGui::Begin("Play", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginChild("Panel", {460, 300}, true, ImGuiWindowFlags_NavFlattened);
        ImGui::BeginGroup();
        LibraryNavigationContentWindow();
        content = ImGui::GetCurrentWindow();
        if (pinned) {
            ImGui::BeginChild("Content", {420, 200}, false, ImGuiWindowFlags_NavFlattened);
            body = ImGui::GetCurrentWindow();
            LibraryNavigationScrollWindow(body);
            ImGui::Dummy({0, 240}); // No focusable item in the initial visible slice.
        }
        for (int i = 0; i < 24; ++i)
            ImGui::Button(("Setting " + std::to_string(i)).c_str(), {240, 36});
        if (pinned) {
            ImGui::EndChild();
            if (initial)
                ImGui::SetKeyboardFocusHere();
            ImGui::Button("Start Game", {420, 48});
            start = ImGui::GetItemID();
            LibraryNavigationPinnedAction(start);
        }
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
    };
    frame(true);
    frame();
    frame();
    assert(GImGui->NavId == start && content->Scroll.y == 0);
    record_navigation_axis(1, -.9F);
    frame();
    frame();
    assert(GImGui->NavId != start && GImGui->NavWindow == body);
    record_navigation_axis(1, 0);
    frame();
    frame();
    frame(true);
    frame();
    frame();
    assert(GImGui->NavId == start);
    record_navigation_axis(3, .9F);
    for (int i = 0; i < 8; ++i)
        frame();
    assert(body->Scroll.y > 60 && content->Scroll.y == 0 && GImGui->NavId == start);
    record_navigation_axis(3, 0);
    frame();
    frame();
    // Opening another page must discard the inner target and scroll that page.
    frame(false, false);
    frame(false, false);
    record_navigation_axis(3, .9F);
    for (int i = 0; i < 8; ++i)
        frame(false, false);
    assert(content->Scroll.y > 60);
    record_navigation_axis(3, 0);
    navigation_connected = 0;
    ImGui::DestroyContext();
}
void NavigationInteractionCase(bool live, dkr::library_ui::SidebarInput mode) {
    using namespace dkr::library_ui;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.F / 60;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    sidebar_input = mode;
    navigation_buttons = 0;
    navigation_press_edges = 0;
    navigation_connected = 1;
    for (unsigned axis = 0; axis < 4; ++axis)
        record_navigation_axis(axis, 0);
    LibraryInstallNavigationHook(live);
    ImGuiID first = 0, second = 0;
    ImGuiWindow* content = nullptr;
    ImGuiWindow* modal = nullptr;
    bool open_modal = false, close_modal = false, focus_footer = false;
    ImGuiWindow* footer = nullptr;
    const auto frame = [&](bool initial = false) {
        ImGui::NewFrame();
        const int step = LibraryTakeSidebarStep(live);
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({480, 240});
        ImGui::Begin("Main panel", nullptr, ImGuiWindowFlags_NoSavedSettings);
        LibraryNavigationContentWindow();
        content = ImGui::GetCurrentWindow();
        if (initial)
            ImGui::SetKeyboardFocusHere();
        for (int i = 0; i < 24; ++i) {
            const auto label = "Element " + std::to_string(i);
            const auto id = ImGui::GetID(label.c_str());
            if (i == 0)
                first = id;
            if (i == 1)
                second = id;
            ImGui::Button(label.c_str(), {240, 36});
        }
        if (open_modal) {
            ImGui::OpenPopup("Modal");
            open_modal = false;
        }
        ImGui::SetNextWindowSize({420, 200});
        if (ImGui::BeginPopupModal("Modal", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            modal = ImGui::GetCurrentWindow();
            for (int i = 0; i < 24; ++i)
                ImGui::Button(("Modal " + std::to_string(i)).c_str(), {220, 36});
            if (close_modal) {
                ImGui::CloseCurrentPopup();
                close_modal = false;
            }
            ImGui::EndPopup();
        }
        ImGui::End();
        ImGui::SetNextWindowPos({500, 0});
        ImGui::SetNextWindowSize({240, 240});
        ImGui::Begin("Sidebar footer", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
        footer = ImGui::GetCurrentWindow();
        if (focus_footer) {
            ImGui::SetKeyboardFocusHere();
            focus_footer = false;
        }
        ImGui::Button("Sidebar navigation");
        ImGui::End();
        ImGui::Render();
        return step;
    };
    frame(true);
    frame();
    frame();
    assert(GImGui->NavId == first);
    // SDL2's overlay poll must not clear the active SDL3 helper's connection.
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    frame();
    assert(io.BackendFlags & ImGuiBackendFlags_HasGamepad);
    // Polling may be unchanged or deduplicated by either native backend.
    // The physical stick channel must still drive actual element navigation.
    record_navigation_axis(1, .9F);
    assert(frame() == 0);
    frame();
    assert(GImGui->NavId == second && content->Scroll.y == 0.F);
    assert(!ImGui::IsKeyDown(ImGuiKey_GamepadLStickDown));
    record_navigation_axis(1, 0);
    frame();
    frame();
    const auto focus = GImGui->NavId;
    record_navigation_axis(3, .9F);
    for (int i = 0; i < 8; ++i)
        assert(frame() == 0);
    assert(content->Scroll.y > 60.F && GImGui->NavId == focus);
    record_navigation_axis(3, 0);
    frame();
    const float stopped = content->Scroll.y;
    frame();
    assert(content->Scroll.y == stopped);
    // Opposite stick values cannot overwrite one another through ImGui's key deduplication.
    record_navigation_axis(1, .9F);
    record_navigation_axis(3, .9F);
    frame();
    frame();
    assert(ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown) && !ImGui::IsKeyDown(ImGuiKey_GamepadLStickDown));
    record_navigation_axis(1, 0);
    record_navigation_axis(3, 0);
    frame();
    open_modal = true;
    frame();
    frame();
    const float behind_modal = content->Scroll.y;
    record_navigation_axis(3, .9F);
    for (int i = 0; i < 8; ++i)
        frame();
    assert(modal && modal->Scroll.y > 60.F && content->Scroll.y == behind_modal);
    record_navigation_axis(3, 0);
    close_modal = true;
    frame();
    frame();
    if (mode == SidebarInput::Dpad) {
        record_navigation_button(1, true);
        record_navigation_press(1);
        assert(frame() == 1);
        assert(!ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown));
        record_navigation_button(1, false);
        frame();
    } else {
        record_navigation_button(1, true);
        record_navigation_press(1);
        assert(frame() == 0);
        assert(ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown));
        record_navigation_button(1, false);
        frame();
        record_navigation_button(5, true);
        record_navigation_press(5);
        assert(frame() == 1);
        record_navigation_button(5, false);
        frame();
    }
    focus_footer = true;
    frame();
    frame();
    assert(GImGui->NavWindow == footer);
    record_navigation_axis(0, -.9F);
    frame();
    frame();
    assert(GImGui->NavWindow == content && LibraryTakeContentMotion(live));
    record_navigation_axis(0, 0);
    frame();
    captured = true;
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, true, .8F);
    frame();
    assert(ImGui::IsKeyDown(ImGuiKey_GamepadLStickRight));
    captured = false;
    record_navigation_axis(0, .8F);
    frame();
    assert(!ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight));
    record_navigation_axis(0, 0);
    frame();
    record_navigation_axis(0, .8F);
    frame();
    assert(ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight));
    if (live) {
        g_overlay_visible = false;
        record_navigation_axis(3, .9F);
        frame();
        const float hidden = content->Scroll.y;
        frame();
        assert(content->Scroll.y == hidden && !ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight));
        g_overlay_visible = true;
        frame();
        assert(content->Scroll.y == hidden);
        record_navigation_axis(0, 0);
        record_navigation_axis(3, 0);
        frame();
    }
    for (unsigned axis = 0; axis < 4; ++axis)
        record_navigation_axis(axis, 0);
    navigation_connected = 0;
    frame();
    assert(!(io.BackendFlags & ImGuiBackendFlags_HasGamepad));
    navigation_connected = 1;
    frame();
    ImGui::DestroyContext();
}
} // namespace

int main() {
    // Actual draw vertices: gold/glow stays on the selected icon group and
    // fits the narrow Deck sidebar as well as desktop layouts.
    ImGui::CreateContext();
    auto& icon_io = ImGui::GetIO();
    icon_io.IniFilename = nullptr;
    icon_io.DisplaySize = {800, 600};
    unsigned char* icon_pixels;
    int icon_w, icon_h;
    icon_io.Fonts->GetTexDataAsRGBA32(&icon_pixels, &icon_w, &icon_h);
    for (float width : {160.F, 196.F, 240.F, 320.F})
        for (bool dpad : {false, true}) {
            ImGui::NewFrame();
            auto* draw = ImGui::GetForegroundDrawList();
            dkr::library_ui::draw_navigation_icons(draw, {width * .20F, 39.F}, {width * .82F, 39.F}, dpad,
                                                   icon_io.FontDefault ? icon_io.FontDefault : icon_io.Fonts->Fonts[0],
                                                   13.F);
            bool gold = false, glow = false;
            for (const auto& vertex : draw->VtxBuffer) {
                assert(vertex.pos.x >= 0 && vertex.pos.x <= width && vertex.pos.y >= 19 && vertex.pos.y <= 59);
                if ((vertex.col & 0x00ffffffU) == (dkr::library_ui::NavigationGold & 0x00ffffffU)) {
                    assert(dpad ? vertex.pos.x > width * .5F : vertex.pos.x < width * .5F);
                    gold |= (vertex.col >> 24) == 255;
                    glow |= (vertex.col >> 24) > 0 && (vertex.col >> 24) < 255;
                }
            }
            assert(gold && glow && draw->VtxBuffer.Size < 1000);
            ImGui::EndFrame();
        }
    ImGui::DestroyContext();
    using namespace dkr::library_ui;
    using namespace dkr::launcher_themes;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1280, 800};
    io.DeltaTime = .1F;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.Fonts->TexDesiredWidth = 2048;
    const int pattern = io.Fonts->AddCustomRectRegular(ButtonPatternWidth, ButtonPatternHeight);
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto* rect = io.Fonts->GetCustomRectByIndex(pattern);
    assert(rect && rect->IsPacked());
    fill_button_patterns(pixels, width, rect->X, rect->Y);
    io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(std::uintptr_t(1)));
    LibraryInstallNavigationHook(false);
    LibraryInstallNavigationHook(false);
    const auto send = [&](ImGuiKey key, bool down) {
        constexpr std::array<ImGuiKey, 6> keys{ImGuiKey_GamepadDpadUp,   ImGuiKey_GamepadDpadDown,
                                               ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight,
                                               ImGuiKey_GamepadL1,       ImGuiKey_GamepadR1};
        for (unsigned i = 0; i < keys.size(); ++i)
            if (keys[i] == key) {
                record_navigation_button(i, down);
                if (down)
                    record_navigation_press(i);
            }
        io.AddKeyEvent(key, down);
    };
    const auto frame = [&] {
        ImGui::NewFrame();
        const int step = LibraryTakeSidebarStep(false);
        ImGui::EndFrame();
        return step;
    };
    assert(frame() == 0);
    send(ImGuiKey_GamepadL1, true);
    assert(frame() == -1);
    send(ImGuiKey_GamepadL1, true);
    assert(frame() == 0);
    assert(!ImGui::IsKeyDown(ImGuiKey_GamepadL1));
    send(ImGuiKey_GamepadL1, false);
    frame();
    sidebar_input = SidebarInput::Dpad;
    // Switching mode with a direction already held must wait for release.
    send(ImGuiKey_GamepadDpadDown, true);
    assert(frame() == 0);
    send(ImGuiKey_GamepadDpadDown, false);
    frame();
    send(ImGuiKey_GamepadDpadDown, true);
    assert(frame() == 1);
    assert(!ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown));
    send(ImGuiKey_GamepadDpadLeft, true);
    frame();
    assert(!ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft));
    record_navigation_axis(0, .8F);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, true, .8F);
    frame();
    assert(ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight) && !ImGui::IsKeyDown(ImGuiKey_GamepadLStickRight));
    record_navigation_axis(0, 0);
    frame();
    blocked = true;
    assert(frame() == 0);
    blocked = false;
    assert(frame() == 0);
    send(ImGuiKey_GamepadDpadDown, false);
    frame();
    send(ImGuiKey_GamepadDpadUp, true);
    assert(frame() == -1);
    assert(!ImGui::IsKeyDown(ImGuiKey_GamepadDpadUp));
    send(ImGuiKey_GamepadDpadUp, false);
    frame();
    g_library_navigation[0].selection = SidebarToggle;
    send(ImGuiKey_GamepadFaceDown, true);
    frame();
    assert(!ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown));
    send(ImGuiKey_GamepadFaceDown, false);
    frame();
    g_library_navigation[0].selection = 0;
    send(ImGuiKey_GamepadFaceDown, true);
    frame();
    assert(ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown));
    sidebar_input = SidebarInput::Shoulders;
    frame();
    // A complete quick tap between UI frames is retained once. A polled
    // state arriving ahead of its event cannot produce a second page change.
    record_navigation_press(4);
    assert(frame() == -1);
    assert(frame() == 0);
    record_navigation_button(4, true);
    assert(frame() == 0);
    record_navigation_press(4);
    assert(frame() == -1);
    assert(frame() == 0);
    record_navigation_button(4, false);
    assert(frame() == 0);
    std::set<std::uint64_t> tiles;
    for (int theme = 0; theme <= int(Theme::Count); ++theme) {
        std::uint64_t hash = 1469598103934665603ULL;
        for (int y = 0; y < ButtonTileHeight; ++y)
            for (int x = 0; x < ButtonTileWidth; ++x)
                hash = (hash ^ button_pixel(theme, x, y)) * 1099511628211ULL;
        assert(tiles.insert(hash).second);
    }
    for (int theme = 0; theme < int(Theme::Count); ++theme)
        for (bool animate : {false, true}) {
            displayed = Theme(theme);
            preferences.animate = animate;
            ImGui::NewFrame();
            auto* draw = ImGui::GetForegroundDrawList();
            const auto last = GImGui->LastItemData.ID;
            draw_button_pattern(draw, {10, 10}, {1210, 66}, *rect, 8, .75F, 1234.5);
            assert(draw->VtxBuffer.Size > 0 && draw->VtxBuffer.Size < 200);
            for (const auto& v : draw->VtxBuffer) {
                assert(v.pos.x >= 9 && v.pos.x <= 1211 && v.pos.y >= 9 && v.pos.y <= 67);
                assert(v.uv.x >= 0 && v.uv.x <= 1 && v.uv.y >= 0 && v.uv.y <= 1);
            }
            assert(GImGui->LastItemData.ID == last);
            ImGui::EndFrame();
        }
    assert(button_variant(Theme::National, Calendar::Canada) == int(Theme::Count));
    assert(button_variant(Theme::National, Calendar::US) == int(Theme::National));
    // Tiny first/last fragments used to lose the outer corner radius as the
    // pattern scrolled. Exercise every source-pixel phase at three sizes.
    preferences.animate = true;
    displayed = Theme::Halloween;
    for (float h : {44.F, 56.F, 68.F})
        for (int phase = 0; phase < 128; ++phase) {
            ImGui::NewFrame();
            auto* draw = ImGui::GetForegroundDrawList();
            const ImVec2 a{10, 10}, b{775, 10 + h};
            constexpr float radius = 8.F;
            const double seconds = double(phase) * double(h) / 64. / 12.;
            draw_button_pattern(draw, a, b, *rect, radius, 1, seconds);
            for (const auto& v : draw->VtxBuffer) {
                const float cx = std::clamp(v.pos.x, a.x + radius, b.x - radius);
                const float cy = std::clamp(v.pos.y, a.y + radius, b.y - radius);
                const float dx = v.pos.x - cx, dy = v.pos.y - cy;
                assert(dx * dx + dy * dy <= (radius + 1.1F) * (radius + 1.1F));
                assert(v.uv.x >= float(rect->X) / io.Fonts->TexWidth &&
                       v.uv.x <= float(rect->X + rect->Width) / io.Fonts->TexWidth);
            }
            ImGui::EndFrame();
        }
    ImGui::DestroyContext();
    for (bool live : {false, true})
        for (auto mode : {SidebarInput::Shoulders, SidebarInput::Dpad}) {
            NavigationInteractionCase(live, mode);
            PinnedContentScrollCase(live, mode);
        }
    std::cout
        << "Shared sidebar routing: polling deduplication, content isolation, sticks, modal/release gates and all 20 bounded seasonal tiles passed\n";
}
