#include "hud_layout_editor.hpp"
#include "runtime_hud_layout.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <optional>
#include <string>

namespace dkr::runtime::hud::editor {
namespace {
namespace hg = groups;
struct Editor {
    hg::Layout draft, saved;
    std::deque<hg::Layout> undo, redo;
    std::optional<hg::Layout> gesture;
    std::optional<hg::Layout> slider_gesture;
    int scenario = 0, target = 0, aspect = 0, widget = 0, preset = 0;
    bool opened = false, moving = false, grid = true, safe = true, details = false, snap = true;
    bool close_prompt = false, live_review = false, reopen = false, in_game = false, bottom_bar = true;
    bool overlap_warning = true;
    int animation = 0;
    bool select_base = false;
    std::string pending_preset;
    float sample_aspect = 16.0F / 10.0F;
    std::string message;
};
Editor e;
std::string g_settings_message;
bool apply_settings(const hg::Layout& value) {
    if (apply_layout(value)) {
        g_settings_message.clear();
        return true;
    }
    preview_layout(std::nullopt);
    g_settings_message = "Could not save HUD settings. The previous layout is still active.";
    return false;
}
std::atomic<bool> g_active{false}, g_back{false}, g_interrupt_move{false};
constexpr ImU32 kGold = IM_COL32(255, 199, 35, 255), kTeal = IM_COL32(21, 200, 180, 255);
void remember(const hg::Layout& before) {
    if (before == e.draft)
        return;
    if (e.undo.size() == 32)
        e.undo.pop_front();
    e.undo.push_back(before);
    e.redo.clear();
}
void preview() {
    preview_layout(e.draft);
}
template <class F> void change(F f) {
    const auto before = e.draft;
    f();
    remember(before);
    preview();
}
template <class F> void slider_change(bool changed, F f) {
    if (ImGui::IsItemActivated())
        e.slider_gesture = e.draft;
    if (changed) {
        if (!e.slider_gesture)
            e.slider_gesture = e.draft;
        f();
        preview();
    }
    if (ImGui::IsItemDeactivated() && e.slider_gesture) {
        remember(*e.slider_gesture);
        e.slider_gesture.reset();
    }
}
hg::Scenario scenario() {
    return static_cast<hg::Scenario>(e.scenario);
}
hg::Target target() {
    return static_cast<hg::Target>(e.target);
}
Widget widget() {
    return static_cast<Widget>(e.widget);
}
hg::Aspect aspect() {
    return static_cast<hg::Aspect>(e.aspect);
}
hg::Placement& selected() {
    return e.draft.entries[hg::index(aspect(), scenario(), target(), widget())];
}
float preview_aspect() {
    constexpr float values[]{0, 4.0F / 3, 16.0F / 10, 16.0F / 9, 21.0F / 9};
    return e.aspect ? values[e.aspect] : e.sample_aspect;
}
void activate_override() {
    auto& p = selected();
    if (e.aspect && !p.enabled)
        p = e.draft.entries[hg::index(hg::Aspect::All, scenario(), target(), widget())];
    p.enabled = true;
}
bool button(const char* text, float width = 0) {
    const float available = ImGui::GetContentRegionAvail().x;
    if (width == 0)
        width = std::min(available, ImGui::CalcTextSize(text).x + ImGui::GetStyle().FramePadding.x * 2);
    if (width < 0)
        width = available;
    width = std::max(1.0F, std::min(width, available));
    const auto text_size = ImGui::CalcTextSize(text, nullptr, false, std::max(1.0F, width - 16));
    const ImVec2 size{width, std::max(44.0F, text_size.y + 12)};
    const auto start = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Button((std::string("##hud-") + text).c_str(), size);
    ImGui::GetWindowDrawList()->AddText(
        ImGui::GetFont(), ImGui::GetFontSize(),
        {start.x + std::max(8.0F, (width - text_size.x) * .5F), start.y + (size.y - text_size.y) * .5F},
        ImGui::GetColorU32(ImGuiCol_Text), text, nullptr, std::max(1.0F, width - 16));
    return pressed;
}
template <std::size_t N> bool combo(const char* id, int& value, const std::array<std::string_view, N>& names) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo(id, names[static_cast<std::size_t>(value)].data())) {
        for (std::size_t i = 0; i < N; ++i)
            if (ImGui::Selectable(names[i].data(), value == static_cast<int>(i))) {
                value = static_cast<int>(i);
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}
bool available(int w) {
    return hg::available(scenario(), target(), static_cast<Widget>(w));
}
void cycle(int delta) {
    for (int n = 0; n < static_cast<int>(hg::kWidgets); ++n) {
        e.widget = (e.widget + delta + static_cast<int>(hg::kWidgets)) % static_cast<int>(hg::kWidgets);
        if (available(e.widget))
            break;
    }
}
void undo(bool redo) {
    auto& from = redo ? e.redo : e.undo;
    auto& to = redo ? e.undo : e.redo;
    if (from.empty())
        return;
    to.push_back(e.draft);
    e.draft = from.back();
    from.pop_back();
    preview();
}
void finish_move(bool apply) {
    if (e.gesture) {
        if (apply)
            remember(*e.gesture);
        else
            e.draft = *e.gesture;
    }
    e.gesture.reset();
    e.moving = false;
    preview();
}
void start_move() {
    e.gesture = e.draft;
    activate_override();
    e.moving = true;
    e.message = "Move the whole group. A / Enter places it; B / Escape cancels this move.";
}
void nudge(float x, float y) {
    auto& p = selected();
    const auto c = hg::canvas(target(), preview_aspect());
    const auto before = hg::transform(e.draft, scenario(), target(), widget(), preview_aspect());
    const float old_x = p.x, old_y = p.y;
    p.x = std::clamp(p.x + x / c.w, -1.0F, 1.0F);
    p.y = std::clamp(p.y + y / c.h, -1.0F, 1.0F);
    const auto after = hg::transform(e.draft, scenario(), target(), widget(), preview_aspect());
    p.x = std::clamp(old_x + (after.x - before.x) / c.w, -1.0F, 1.0F);
    p.y = std::clamp(old_y + (after.y - before.y) / c.h, -1.0F, 1.0F);
    preview();
}
void move_input(float canvas_scale) {
    if (!e.moving)
        return;
    const auto id = ImGui::GetID("hud-move-owner");
    const bool fine = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_GamepadL1);
    const bool coarse = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_GamepadR1);
    const float step = fine ? .25F : coarse ? 8.0F : 1.0F;
    float dx = 0, dy = 0;
    const auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    if (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft))
        dx -= step;
    if (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight))
        dx += step;
    if (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp))
        dy -= step;
    if (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown))
        dy += step;
    const float dt = std::min(ImGui::GetIO().DeltaTime, .05F);
    const auto analog = [](ImGuiKey k) { return ImGui::GetKeyData(k)->AnalogValue; };
    dx += (analog(ImGuiKey_GamepadLStickRight) - analog(ImGuiKey_GamepadLStickLeft)) * dt * step * 60;
    dy += (analog(ImGuiKey_GamepadLStickDown) - analog(ImGuiKey_GamepadLStickUp)) * dt * step * 60;
    if (ImGui::IsMouseDragging(0)) {
        dx += ImGui::GetIO().MouseDelta.x / std::max(canvas_scale, .01F);
        dy += ImGui::GetIO().MouseDelta.y / std::max(canvas_scale, .01F);
    }
    if (dx || dy)
        nudge(dx, dy);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
        finish_move(false);
    else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) {
        if (e.snap) {
            const auto c = hg::canvas(target(), preview_aspect());
            auto& p = selected();
            p.x = std::round(p.x * c.w) / c.w;
            p.y = std::round(p.y * c.h) / c.h;
        }
        finish_move(true);
    }
    if (e.moving)
        for (const auto key :
             {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_GamepadDpadLeft,
              ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickLeft,
              ImGuiKey_GamepadLStickRight, ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown,
              ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight, ImGuiKey_Escape, ImGuiKey_Enter})
            ImGui::SetKeyOwner(key, id, ImGuiInputFlags_LockThisFrame);
}
void draw_canvas(ImVec2 space) {
    const float a = preview_aspect();
    const float h = std::min(space.y, space.x / a);
    const float w = h * a;
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("hud-canvas", {space.x, std::max(space.y, 1.0F)});
    const ImVec2 tl{origin.x + (space.x - w) * .5F, origin.y + (space.y - h) * .5F};
    const ImVec2 br{tl.x + w, tl.y + h};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(tl, br, IM_COL32(8, 30, 44, 255), 8);
    draw->AddRect(tl, br, kTeal, 8);
    const auto full = hg::canvas(hg::Target::Single, a);
    const float unit = w / full.w;
    const auto screen = [&](hg::Point p) { return ImVec2{tl.x + (p.x - full.x) * unit, tl.y + p.y * unit}; };
    draw->PushClipRect(tl, br, true);
    if (e.grid)
        for (float x = 0; x <= 320; x += 20)
            draw->AddLine(screen({x, 0}), screen({x, 240}), IM_COL32(40, 90, 102, 80));
    if (e.grid)
        for (float y = 0; y <= 240; y += 20)
            draw->AddLine(screen({full.x, y}), screen({full.x + full.w, y}), IM_COL32(40, 90, 102, 80));
    if (e.target != 0)
        draw->AddLine(screen({full.x, 120}), screen({full.x + full.w, 120}), kGold, 2);
    if (e.safe) {
        const auto c = hg::canvas(target(), a);
        draw->AddRect(screen({c.x + 4, c.y + 4}), screen({c.x + c.w - 4, c.y + c.h - 4}), IM_COL32(255, 199, 35, 100));
    }
    for (int i = 0; i < static_cast<int>(hg::kWidgets); ++i) {
        if (!available(i))
            continue;
        const auto wi = static_cast<Widget>(i);
        const auto b = hg::bounds(scenario(), target(), wi);
        auto t = hg::transform(e.draft, scenario(), target(), wi, a);
        // Schematic-only animation samples never invoke game HUD routines.
        const float phase = static_cast<float>(ImGui::GetTime());
        if (e.animation == 1)
            t.x += t.scale * 320 * std::max(0.0F, 1.0F - std::fmod(phase, 4.0F));
        if (e.animation == 2 && wi == Widget::RacePosition) {
            const float pulse = 1 + .18F * std::max(0.0F, std::sin(phase * 3));
            t.x += (1 - pulse) * t.scale * (b.x + b.w * .5F);
            t.y += (1 - pulse) * t.scale * (b.y + b.h * .5F);
            t.scale *= pulse;
        }
        const ImVec2 p = screen(t.apply({b.x, b.y})), q = screen(t.apply({b.x + b.w, b.y + b.h}));
        const bool selected_group = i == e.widget;
        draw->AddRectFilled(p, q, selected_group ? IM_COL32(13, 112, 135, 210) : IM_COL32(17, 68, 85, 155), 4);
        draw->AddRect(p, q, selected_group ? kGold : kTeal, 4, 0, selected_group ? 2.5F : 1);
        const float font = std::clamp(unit * 8 * t.scale, 9.0F, 18.0F);
        const char* sample = hg::kWidgetNames[i].data();
        switch (wi) {
        case Widget::BananaCounter:
            sample = "BANANA x 10";
            break;
        case Widget::RaceTimer:
            sample = "TIME 99:59:99";
            break;
        case Widget::GoldenBalloon:
            sample = "BALLOON x 99";
            break;
        case Widget::LapCounter:
            sample = "LAP 3 / 3";
            break;
        case Widget::RacePosition:
            sample = "1 st";
            break;
        case Widget::Weapon:
            sample = "ROCKET x 3";
            break;
        case Widget::LapTimer:
            sample = "LAP 1  1:23:45\nLAP 2  1:22:99\nLAP 3  1:21:99";
            break;
        case Widget::LapMessage:
            sample = "FINAL LAP";
            break;
        case Widget::WrongWay:
            sample = "WRONG WAY";
            break;
        case Widget::CentreMessage:
            sample = e.animation == 3 ? "FINISH!" : "GET READY / GO!";
            break;
        default:
            break;
        }
        draw->AddText(ImGui::GetFont(), font, {p.x + 3, p.y + 3}, IM_COL32(255, 246, 210, 255), sample, nullptr,
                      std::max(1.0F, q.x - p.x - 6));
        if (wi == Widget::Speedometer) {
            const auto centre = screen(t.apply({b.x + b.w * .5F, b.y + b.h * .65F}));
            const float angle = e.animation ? phase : 2.8F;
            draw->AddLine(centre,
                          {centre.x + std::cos(angle) * unit * t.scale * 20,
                           centre.y - std::abs(std::sin(angle)) * unit * t.scale * 20},
                          kGold, 2);
        }
        if (wi == Widget::Minimap) {
            const auto middle = screen(t.apply({b.x + b.w * .5F, b.y + b.h * .55F}));
            draw->AddCircle(middle, b.w * unit * t.scale * .25F, IM_COL32(175, 205, 215, 255), 24, 2);
            const float phase = static_cast<float>(ImGui::GetTime());
            for (int racer = 0; racer < 2; ++racer)
                draw->AddCircleFilled(screen(t.apply({b.x + b.w * (.5F + .25F * std::cos(phase + racer * 3)),
                                                      b.y + b.h * (.55F + .2F * std::sin(phase + racer * 3))})),
                                      unit * 2.5F * t.scale, racer ? kGold : kTeal);
        }
        if (!e.moving && ImGui::IsMouseHoveringRect(p, q) && ImGui::IsMouseClicked(0))
            e.widget = i;
    }
    draw->PopClipRect();
    move_input(unit);
}
void close_editor() {
    e.opened = false;
    g_active = false;
    e.moving = false;
    e.gesture.reset();
    preview_layout(std::nullopt);
    ImGui::CloseCurrentPopup();
}

void modal(void (*request_name)(), void (*draw_keyboard)()) {
    if (!e.opened)
        return;
    g_active = true;
    const bool was_moving = e.moving;
    if (g_interrupt_move.exchange(false) && e.moving) {
        finish_move(false);
        e.message = "Move cancelled because input focus or a controller was lost. Saved layout is unchanged.";
    }
    if (g_back.exchange(false)) {
        if (e.moving)
            finish_move(false);
        else
            e.close_prompt = true;
    }
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(
        {std::min(1180.0F, viewport->WorkSize.x - 24), std::min(760.0F, viewport->WorkSize.y - 24)}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        {viewport->WorkPos.x + viewport->WorkSize.x * .5F, viewport->WorkPos.y + viewport->WorkSize.y * .5F},
        ImGuiCond_Always, {.5F, .5F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 12});
    if (ImGui::BeginPopupModal("HUD WORKSHOP", nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped(
            "Single / two-player HUD. This is a schematic preview; native visibility and animations stay controlled by the game.");
        ImGui::BeginDisabled(e.moving);
        if (ImGui::BeginTable("hud-context", 3, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("SCENARIO");
            combo("##scenario", e.scenario, hg::kScenarioNames);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("PLAYER / AREA");
            combo("##target", e.target, hg::kTargetNames);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("SCREEN PROFILE");
            combo("##aspect", e.aspect, hg::kAspectNames);
            ImGui::EndTable();
        }
        if (!available(e.widget))
            cycle(1);
        ImGui::EndDisabled();
        const float footer = 140;
        const float body = std::max(100.0F, ImGui::GetContentRegionAvail().y - footer);
        const float panel = std::clamp(ImGui::GetContentRegionAvail().x * .30F, 240.0F, 360.0F);
        const float left = std::max(140.0F, ImGui::GetContentRegionAvail().x - panel - ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginChild("hud-preview", {left, body}, false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextUnformatted("SCHEMATIC / GROUP BOUNDS");
        const std::array<std::string_view, 4> animations{"Resting sample", "Race-start sample", "Rank-change sample",
                                                         "Finish sample"};
        combo("##animation", e.animation, animations);
        draw_canvas({ImGui::GetContentRegionAvail().x, std::max(80.0F, ImGui::GetContentRegionAvail().y - 110)});
        if (!e.moving) {
            if (button("MOVE GROUP", std::min(210.0F, left)))
                start_move();
        } else {
            if (button("PLACE GROUP"))
                finish_move(true);
            ImGui::SameLine();
            if (button("CANCEL MOVE"))
                finish_move(false);
        }
        ImGui::TextWrapped(
            e.moving
                ? "D-pad: nudge | Stick: move | LB: fine | RB: coarse | A: place | B: cancel"
                : "Select a group, then Move. LB / RB selects a group. Y opens details. Menu opens Apply / Discard.");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("hud-inspector", {0, body}, true);
        ImGui::BeginDisabled(e.moving);
        ImGui::TextUnformatted("HUD GROUP");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##widget", hg::kWidgetNames[e.widget].data())) {
            for (int i = 0; i < static_cast<int>(hg::kWidgets); ++i)
                if (available(i) && ImGui::Selectable(hg::kWidgetNames[i].data(), e.widget == i))
                    e.widget = i;
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("Icons, digits and decorative parts move and resize as one group.");
        bool fill = e.draft.custom_fill;
        if (ImGui::Checkbox("Fill-window base", &fill))
            change([&] { e.draft.custom_fill = fill; });
        if (e.aspect) {
            bool own = selected().enabled;
            if (ImGui::Checkbox("Override for this screen", &own))
                change([&] {
                    if (own)
                        activate_override();
                    else
                        selected() = {};
                });
        }
        auto p = selected();
        if (e.aspect && !p.enabled)
            p = e.draft.entries[hg::index(hg::Aspect::All, scenario(), target(), widget())];
        bool inherit = p.scale == 0;
        if (ImGui::Checkbox("Use global size", &inherit))
            change([&] {
                activate_override();
                selected().scale = inherit ? 0 : e.draft.scale;
            });
        float size = 100 * (p.scale == 0 ? e.draft.scale : p.scale);
        ImGui::BeginDisabled(inherit);
        ImGui::SetNextItemWidth(-1);
        slider_change(ImGui::SliderFloat("##group-size", &size, 50, 150, "Group: %.0f%%", ImGuiSliderFlags_AlwaysClamp),
                      [&] {
                          activate_override();
                          selected().scale = hg::safe_scale(std::round(size) / 100);
                      });
        ImGui::EndDisabled();
        float global = e.draft.scale * 100;
        const float fitted = hg::transform(e.draft, scenario(), target(), widget(), preview_aspect()).scale * 100;
        if (fitted + 0.5F < size)
            ImGui::TextWrapped("Effective size: %.0f%% (limited to keep this entire group inside its viewport).",
                               fitted);
        ImGui::SetNextItemWidth(-1);
        slider_change(ImGui::SliderFloat("##global-size-editor", &global, 50, 150, "Global: %.0f%%",
                                         ImGuiSliderFlags_AlwaysClamp),
                      [&] { e.draft.scale = hg::safe_scale(std::round(global) / 100); });
        if (button("SIZE ALL GROUPS", -1))
            ImGui::OpenPopup("Use global size everywhere?");
        if (ImGui::BeginPopupModal("Use global size everywhere?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Clear every group size override? Positions stay unchanged.");
            if (button("CONFIRM")) {
                change([&] {
                    for (auto& entry : e.draft.entries)
                        entry.scale = 0;
                });
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (button("CANCEL"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Checkbox("Grid", &e.grid);
        ImGui::SameLine();
        ImGui::Checkbox("Safe area", &e.safe);
        ImGui::Checkbox("Snap when placed", &e.snap);
        ImGui::Checkbox("Overlap warnings", &e.overlap_warning);
        if (e.overlap_warning) {
            const auto b = hg::bounds(scenario(), target(), widget());
            const auto t = hg::transform(e.draft, scenario(), target(), widget(), preview_aspect());
            const auto p = t.apply({b.x, b.y}), q = t.apply({b.x + b.w, b.y + b.h});
            bool overlap = false;
            for (int other = 0; other < int(hg::kWidgets); ++other)
                if (other != e.widget && available(other)) {
                    const auto ow = static_cast<Widget>(other);
                    const auto ob = hg::bounds(scenario(), target(), ow);
                    const auto ot = hg::transform(e.draft, scenario(), target(), ow, preview_aspect());
                    const auto op = ot.apply({ob.x, ob.y}), oq = ot.apply({ob.x + ob.w, ob.y + ob.h});
                    overlap |= p.x < oq.x && q.x > op.x && p.y < oq.y && q.y > op.y;
                }
            if (overlap)
                ImGui::TextWrapped(
                    "Bounds overlap another group. Some groups appear at different times; this is a warning only.");
        }
        if (button(e.details ? "HIDE DETAILS" : "DETAILS", -1))
            e.details = !e.details;
        if (e.details) {
            const std::array<std::string_view, 10> anchors{
                "Native anchor", "Top left",     "Top centre",  "Top right",     "Middle left",
                "Centre",        "Middle right", "Bottom left", "Bottom centre", "Bottom right"};
            int anchor = p.anchor + 1;
            if (combo("##anchor", anchor, anchors))
                change([&] {
                    activate_override();
                    selected().anchor = anchor - 1;
                });
            float x = p.x * 100, y = p.y * 100;
            ImGui::SetNextItemWidth(-1);
            slider_change(
                ImGui::SliderFloat("##offset-x", &x, -100, 100, "Across: %.1f%%", ImGuiSliderFlags_AlwaysClamp), [&] {
                    activate_override();
                    nudge((x / 100 - selected().x) * hg::canvas(target(), preview_aspect()).w, 0);
                });
            ImGui::SetNextItemWidth(-1);
            slider_change(ImGui::SliderFloat("##offset-y", &y, -100, 100, "Down: %.1f%%", ImGuiSliderFlags_AlwaysClamp),
                          [&] {
                              activate_override();
                              nudge(0, (y / 100 - selected().y) * hg::canvas(target(), preview_aspect()).h);
                          });
            ImGui::TextWrapped(
                "Offsets use this player's viewport, not absolute screen pixels. Resting groups are kept inside its safe area.");
            if (hg::split(target()) && button("COPY TO OTHER PLAYER", -1))
                change([&] {
                    hg::copy_target(e.draft, aspect(), scenario(), target(),
                                    target() == hg::Target::Top ? hg::Target::Bottom : hg::Target::Top);
                });
            if (button("COPY TO OTHER SCENARIOS", -1))
                ImGui::OpenPopup("Copy this group?");
            if (ImGui::BeginPopupModal("Copy this group?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Copy this group's placement to compatible scenarios only?");
                if (button("COPY")) {
                    change([&] {
                        const auto source = selected();
                        for (std::size_t s = 0; s < hg::kScenarios; ++s)
                            if (hg::available(static_cast<hg::Scenario>(s), target(), widget()))
                                e.draft.entries[hg::index(aspect(), static_cast<hg::Scenario>(s), target(), widget())] =
                                    source;
                    });
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (button("CANCEL"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::BeginDisabled(e.undo.empty());
        if (button("UNDO"))
            undo(false);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(e.redo.empty());
        if (button("REDO"))
            undo(true);
        ImGui::EndDisabled();
        if (button("RESET GROUP", -1))
            ImGui::OpenPopup("Reset this group?");
        if (ImGui::BeginPopupModal("Reset this group?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Restore native placement and inherited size for this group?");
            if (button("RESET")) {
                change([&] { selected() = {}; });
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (button("CANCEL"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (button("RESET SCENARIO", -1))
            ImGui::OpenPopup("Reset this scenario?");
        if (ImGui::BeginPopupModal("Reset this scenario?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Reset all groups for the selected scenario, player and screen profile?");
            if (button("RESET")) {
                change([&] { hg::reset_scenario(e.draft, aspect(), scenario(), target()); });
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (button("CANCEL"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SeparatorText("PRESETS");
        const auto names = preset_names();
        e.preset = std::clamp(e.preset, 0, std::max(0, static_cast<int>(names.size()) - 1));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##preset", names.empty() ? "No saved presets" : names[e.preset].c_str())) {
            for (std::size_t i = 0; i < names.size(); ++i)
                if (ImGui::Selectable(names[i].c_str(), e.preset == static_cast<int>(i)))
                    e.preset = static_cast<int>(i);
            ImGui::EndCombo();
        }
        if (button("SAVE AS PRESET...", -1) && request_name)
            request_name();
        ImGui::BeginDisabled(names.empty());
        if (button("LOAD PRESET", -1) && !names.empty()) {
            const auto l = load_preset(names[e.preset]);
            if (l)
                change([&] {
                    e.draft = *l;
                    e.draft.mode = LayoutMode::Custom;
                });
        }
        if (button("DELETE PRESET", -1))
            ImGui::OpenPopup("Delete this preset?");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("Delete this preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Delete the selected saved preset? Applied settings stay unchanged.");
            if (button("DELETE") && !names.empty()) {
                delete_preset(names[e.preset]);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (button("CANCEL"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
        // Keep the shared keyboard nested in this modal's popup stack.
        if (draw_keyboard)
            draw_keyboard();
        if (!e.pending_preset.empty())
            ImGui::OpenPopup("Replace saved preset?");
        if (ImGui::BeginPopupModal("Replace saved preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Replace the saved preset %s?", e.pending_preset.c_str());
            if (button("REPLACE")) {
                e.message = save_preset(e.pending_preset, e.draft) ? "Preset saved." : "Could not write preset.";
                e.pending_preset.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (button("CANCEL")) {
                e.pending_preset.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();
        if (button("APPLY", 140)) {
            if (e.moving)
                finish_move(true);
            if (apply_layout(e.draft)) {
                e.saved = e.draft;
                close_editor();
            } else
                e.message = "Could not save HUD settings. Your draft is retained.";
        }
        ImGui::SameLine();
        if (button("DISCARD / BACK", 210))
            e.close_prompt = true;
        ImGui::SameLine();
        ImGui::TextUnformatted(e.draft == e.saved ? "No unapplied changes" : "Preview only - not saved yet");
        ImGui::BeginDisabled(!e.in_game || e.moving);
        if (button("REVIEW IN GAME", 250)) {
            e.opened = false;
            e.live_review = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (!e.message.empty())
            ImGui::TextWrapped("%s", e.message.c_str());
        if (e.select_base) {
            ImGui::OpenPopup("Custom layout starting point");
            e.select_base = false;
        }
        if (ImGui::BeginPopupModal("Custom layout starting point", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Choose a starting area. Existing group edits are retained.");
            if (button("START FROM ORIGINAL")) {
                change([] { e.draft.custom_fill = false; });
                ImGui::CloseCurrentPopup();
            }
            if (button("START FROM FILL WINDOW")) {
                change([] { e.draft.custom_fill = true; });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!e.moving && !was_moving && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false))
                cycle(-1);
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false))
                cycle(1);
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false))
                e.details = !e.details;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false))
                e.close_prompt = true;
        }
        if (e.close_prompt) {
            ImGui::OpenPopup("HUD changes");
            e.close_prompt = false;
        }
        if (ImGui::BeginPopupModal("HUD changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Apply the preview, discard changes, or keep editing?");
            if (button("APPLY")) {
                if (apply_layout(e.draft)) {
                    ImGui::CloseCurrentPopup();
                    e.opened = false;
                } else
                    e.message = "Could not save HUD settings. Your draft is retained.";
            }
            ImGui::SameLine();
            if (button("DISCARD")) {
                preview_layout(std::nullopt);
                ImGui::CloseCurrentPopup();
                e.opened = false;
            }
            ImGui::SameLine();
            if (button("KEEP EDITING"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (!e.opened) {
            g_active = e.live_review;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen("HUD WORKSHOP")) {
        cancel();
    }
    ImGui::PopStyleVar();
}
} // namespace

void accept_preset_name(const std::string& name) {
    if (!e.opened)
        return;
    if (load_preset(name)) {
        e.pending_preset = name;
        return;
    }
    e.message = save_preset(name, e.draft) ? "Preset saved. Apply to keep this layout active."
                                           : "Could not save preset (maximum 8; name must be 1-48 characters).";
}
void cancel() {
    e.opened = false;
    e.live_review = false;
    e.reopen = false;
    g_active = false;
    e.moving = false;
    e.gesture.reset();
    preview_layout(std::nullopt);
}
bool active() {
    return g_active.load(std::memory_order_acquire);
}
void request_back() {
    g_back.store(true, std::memory_order_release);
}
void interrupt_move() {
    g_interrupt_move.store(true, std::memory_order_release);
}
bool review_active() {
    return e.live_review;
}
void draw_review() {
    if (!e.live_review)
        return;
    if (g_interrupt_move.exchange(false) && e.moving)
        finish_move(false);
    const bool back = g_back.exchange(false);
    const float height = 160;
    ImGui::SetCursorPos({16, e.bottom_bar ? std::max(16.0F, ImGui::GetWindowHeight() - height - 16) : 16});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {.02F, .08F, .12F, .92F});
    ImGui::BeginChild("hud-live-review", {std::min(850.0F, ImGui::GetContentRegionAvail().x - 16), height}, true);
    ImGui::TextWrapped(
        "LIVE HUD PREVIEW - current gameplay only. Online play continues; game input stays captured by the overlay.");
    ImGui::Text("Editing: %s / %s / %s", hg::kScenarioNames[e.scenario].data(), hg::kTargetNames[e.target].data(),
                hg::kWidgetNames[e.widget].data());
    const bool was_moving = e.moving;
    if (e.moving) {
        move_input(ImGui::GetMainViewport()->Size.y / 240);
        ImGui::TextUnformatted("D-pad / stick: move | A: place | B: cancel");
    } else {
        if (button("MOVE"))
            start_move();
        ImGui::SameLine();
    }
    if (button("WORKSHOP") || (back && !was_moving) ||
        (!was_moving && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) {
        if (e.moving)
            finish_move(false);
        e.live_review = false;
        e.opened = true;
        e.reopen = true;
    }
    if (back && was_moving)
        finish_move(false);
    ImGui::SameLine();
    if (button("MOVE TOOLBAR"))
        e.bottom_bar = !e.bottom_bar;
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
bool draw_settings(bool modern, float width, void (*request_name)(), void (*draw_keyboard)(), bool in_game) {
    bool changed = false;
    ImGui::SeparatorText("HUD - SINGLE / TWO PLAYER");
    ImGui::TextWrapped(
        "Original preserves the 4:3 HUD area. Fill Window moves groups without stretching. Three/four-player HUDs are locked and unaffected.");
    ImGui::BeginDisabled(!modern);
    auto value = layout();
    const char* name = value.mode == LayoutMode::Custom          ? "Custom"
                       : value.mode == LayoutMode::FitToViewport ? "Fill Window"
                                                                 : "Original (4:3 area)";
    ImGui::SetNextItemWidth(std::min(width, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("HUD layout", name)) {
        const std::array<LayoutMode, 3> modes{LayoutMode::Original, LayoutMode::FitToViewport, LayoutMode::Custom};
        const std::array<const char*, 3> names{"Original (4:3 area)", "Fill Window", "Custom"};
        for (std::size_t i = 0; i < 3; ++i)
            if (ImGui::Selectable(names[i], value.mode == modes[i])) {
                if (modes[i] == LayoutMode::Custom) {
                    e = Editor{};
                    e.saved = value;
                    e.draft = value;
                    e.draft.mode = LayoutMode::Custom;
                    e.opened = true;
                    e.reopen = true;
                    e.select_base = std::all_of(value.entries.begin(), value.entries.end(),
                                                [](const auto& p) { return p == hg::Placement{}; });
                    preview();
                } else {
                    value.mode = modes[i];
                    changed = apply_settings(value);
                }
            }
        ImGui::EndCombo();
    }
    static float scale = 100;
    if (!ImGui::IsAnyItemActive())
        scale = value.scale * 100;
    ImGui::SetNextItemWidth(std::min(width, ImGui::GetContentRegionAvail().x));
    if (ImGui::SliderFloat("HUD group size", &scale, 50, 150, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
        auto draft = value;
        draft.scale = hg::safe_scale(std::round(scale) / 100);
        preview_layout(draft);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        value.scale = hg::safe_scale(std::round(scale) / 100);
        changed = apply_settings(value);
    }
    if (button("OPEN HUD WORKSHOP", std::min(width, ImGui::GetContentRegionAvail().x))) {
        e = Editor{};
        e.saved = layout();
        e.draft = e.saved;
        e.draft.mode = LayoutMode::Custom;
        e.opened = true;
        e.select_base = std::all_of(e.saved.entries.begin(), e.saved.entries.end(),
                                    [](const auto& p) { return p == hg::Placement{}; });
        const auto size = ImGui::GetMainViewport()->Size;
        e.sample_aspect = size.y > 0 ? size.x / size.y : 16.0F / 10;
        preview();
        ImGui::OpenPopup("HUD WORKSHOP");
    }
    if (button("RESET HUD SETTINGS", std::min(width, ImGui::GetContentRegionAvail().x)))
        ImGui::OpenPopup("Reset HUD settings?");
    if (ImGui::BeginPopupModal("Reset HUD settings?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Restore Original layout and 100% group size? Saved presets are retained.");
        if (button("RESET")) {
            changed = apply_settings({});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (button("CANCEL"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    if (!g_settings_message.empty())
        ImGui::TextWrapped("%s", g_settings_message.c_str());
    if (!modern)
        ImGui::TextWrapped(
            "HUD customization is available in Modern presentation. Your custom layouts are retained in Accurate mode.");
    e.in_game = in_game;
    if (e.reopen) {
        e.reopen = false;
        ImGui::OpenPopup("HUD WORKSHOP");
    }
    modal(request_name, draw_keyboard);
    return changed;
}
} // namespace dkr::runtime::hud::editor
