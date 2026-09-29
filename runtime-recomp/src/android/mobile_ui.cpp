#include "mobile_ui.hpp"
#include "../game/mobile_touch_policy.hpp"
#include "../game/runtime_ui.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <SDL.h>
#if !defined(DKR_MOBILE_UI_TEST)
#include <SDL_system.h>
#include <jni.h>
#endif
#include <atomic>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <string_view>
#include <vector>

namespace dkr::runtime::mobile {
namespace {
std::mutex mutex;
Contacts contacts;
Contacts preview_contacts;
std::vector<Target> targets;
std::deque<SDL_TouchFingerEvent> pending;
std::atomic<bool> edit_active{false};
std::atomic<bool> test_active{false};
bool cancel_pending = false, floating = false;
float deadzone = .12F, sensitivity = 1, scale = 1, text_scale = 1;
int visibility = 0; // auto, always, off
bool controller_active = false;
std::filesystem::path config_path;
Layout layouts[2]{classic(), classic()}, saved[2]{classic(), classic()};
std::vector<Layout> undo;
int profile = 0, selection = 0;
std::string status;
float insets[4]{};
const char* labels[]{"Analog stick", "A / B",      "C buttons", "D-pad",       "Z trigger",
                     "L shoulder",   "R shoulder", "Start",     "Overlay menu"};
Point sizes[]{{112, 112}, {124, 112}, {144, 144}, {144, 144}, {60, 48}, {64, 48}, {64, 48}, {56, 48}, {88, 48}};
bool menu_open = false;
bool ime_requested = false;
struct Gesture {
    bool active = false, scroll = false, horizontal = false;
    SDL_FingerID id = 0;
    ImVec2 start{}, last{};
    ImGuiID window = 0;
    float velocity = 0;
    Uint32 time = 0;
} gesture;
ImGuiID fling_window = 0;
float fling_velocity = 0;

Rect safe_rect() {
    auto d = ImGui::GetIO().DisplaySize;
    return {insets[0] / scale + 12, insets[1] / scale + 8, std::max(1.0F, d.x - (insets[0] + insets[2]) / scale - 24),
            std::max(1.0F, d.y - (insets[1] + insets[3]) / scale - 16)};
}
Point group_extent(int group, Rect safe) {
    return (group == 2 || group == 3) && safe.h < 360 ? Point{96, 96} : sizes[group];
}
void read_metrics() {
#if defined(DKR_MOBILE_UI_TEST)
    scale = 2;
#else
    static Uint64 last = 0;
    const auto now = SDL_GetTicks64();
    if (now - last < 500 && last)
        return;
    last = now;
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity)
        return;
    jclass type = env->GetObjectClass(activity);
    jmethodID method = type ? env->GetMethodID(type, "getUiMetrics", "()[F") : nullptr;
    auto array = method ? static_cast<jfloatArray>(env->CallObjectMethod(activity, method)) : nullptr;
    if (!env->ExceptionCheck() && array && env->GetArrayLength(array) == 6) {
        float v[6];
        env->GetFloatArrayRegion(array, 0, 6, v);
        if (std::isfinite(v[0]) && v[0] > 0)
            scale = std::clamp(v[0], 1.0F, 5.0F);
        if (std::isfinite(v[1]) && v[1] > 0)
            text_scale = std::clamp(v[1], 1.0F, 1.5F);
        for (int i = 0; i < 4; ++i)
            insets[i] = std::isfinite(v[i + 2]) ? std::max(0.0F, v[i + 2]) : 0;
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (array)
        env->DeleteLocalRef(array);
    if (type)
        env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
#endif
}
bool save_config() {
    const auto tmp = config_path.string() + ".tmp";
    std::ofstream f(tmp, std::ios::trunc);
    f << "DKR_TOUCH 1\n" << visibility << ' ' << floating << ' ' << deadzone << ' ' << sensitivity << '\n';
    for (auto& layout : layouts)
        for (const auto& p : layout)
            f << p.x << ' ' << p.y << ' ' << p.size << ' ' << p.opacity << ' ' << p.visible << '\n';
    f.flush();
    if (!f) {
        status = "Could not save touch settings; your previous layout is retained.";
        return false;
    }
    f.close();
    // Android/POSIX rename replaces the same-filesystem destination atomically.
    std::error_code ec;
    std::filesystem::rename(tmp, config_path, ec);
    if (ec) {
        status = "Could not replace touch settings: " + ec.message();
        return false;
    }
    status = "Touch settings saved.";
    return true;
}
ImGuiWindow* scroll_window(ImVec2 point) {
    auto& context = *ImGui::GetCurrentContext();
    ImGuiWindow* modal = ImGui::GetTopMostPopupModal();
    for (int i = context.Windows.Size - 1; i >= 0; --i) {
        auto* w = context.Windows[i];
        if (!w->WasActive || w->Hidden || (w->Flags & ImGuiWindowFlags_NoInputs) ||
            !w->OuterRectClipped.Contains(point))
            continue;
        if (modal && w != modal && !ImGui::IsWindowChildOf(w, modal, true))
            continue;
        for (auto* p = w; p; p = p->ParentWindow)
            if (p->ScrollMax.y > 0 && !(p->Flags & ImGuiWindowFlags_NoScrollWithMouse))
                return p;
        return nullptr;
    }
    return nullptr;
}
void apply_scroll(ImGuiID id, float delta) {
    if (auto* w = ImGui::FindWindowByID(id))
        ImGui::SetScrollY(w, std::clamp(w->Scroll.y + delta, 0.0F, w->ScrollMax.y));
}
bool on_editor_group(ImVec2 point) {
    const auto& windows = ImGui::GetCurrentContext()->Windows;
    for (int i = windows.Size - 1; i >= 0; --i) {
        const auto* w = windows[i];
        if (!w->WasActive || w->Hidden || (w->Flags & ImGuiWindowFlags_NoInputs) ||
            !w->OuterRectClipped.Contains(point))
            continue;
        return std::string_view(w->Name).starts_with("##touch-move-");
    }
    return false;
}
struct FrameCoordinates {
    float scale = 1;
    ImVec2 framebuffer{1, 1};
    unsigned normalized_through = 0;
};
void context_hook(ImGuiContext* context, ImGuiContextHook* hook) {
    auto* frame = static_cast<FrameCoordinates*>(hook->UserData);
    if (hook->Type == ImGuiContextHookType_Shutdown) {
        delete frame;
        return;
    }
    if (hook->Type == ImGuiContextHookType_RenderPost) {
        auto* draw = ImGui::GetDrawData();
        if (!draw)
            return;
        // Both the SDL launcher and RT64 receive their original pixel-coordinate
        // contract. No renderer backend or guest HUD projection is changed.
        const float s = frame->scale;
        for (int i = 0; i < draw->CmdListsCount; ++i) {
            auto* list = draw->CmdLists[i];
            for (auto& v : list->VtxBuffer) {
                v.pos.x *= s;
                v.pos.y *= s;
            }
            for (auto& c : list->CmdBuffer) {
                c.ClipRect.x *= s;
                c.ClipRect.y *= s;
                c.ClipRect.z *= s;
                c.ClipRect.w *= s;
            }
        }
        draw->DisplayPos.x *= s;
        draw->DisplayPos.y *= s;
        draw->DisplaySize.x *= s;
        draw->DisplaySize.y *= s;
        draw->FramebufferScale = frame->framebuffer;
        return;
    }
    auto& io = context->IO;
    read_metrics();
    frame->scale = scale;
    frame->framebuffer = io.DisplayFramebufferScale;
    io.DisplaySize.x /= scale;
    io.DisplaySize.y /= scale;
    io.FontGlobalScale = text_scale;
    // Input events retained by ImGui's trickling queue must be normalized ONCE.
    for (auto& e : context->InputEventsQueue)
        if (e.EventId > frame->normalized_through) {
            if (e.Type == ImGuiInputEventType_MousePos) {
                e.MousePos.PosX /= scale;
                e.MousePos.PosY /= scale;
            }
            frame->normalized_through = e.EventId;
        }
    std::deque<SDL_TouchFingerEvent> events;
    bool cancel = false;
    {
        std::lock_guard lock(mutex);
        events.swap(pending);
        cancel = cancel_pending;
        cancel_pending = false;
    }
    if (cancel) {
        io.AddMouseButtonEvent(0, false);
        gesture = {};
        fling_velocity = 0;
    }
    if (!gesture.active && std::abs(fling_velocity) > 4) {
        apply_scroll(fling_window, -fling_velocity * std::min(io.DeltaTime, .05F));
        fling_velocity *= std::exp(-8 * io.DeltaTime);
    }
    for (const auto& e : events) {
        ImVec2 pos{e.x * io.DisplaySize.x, e.y * io.DisplaySize.y};
        if (e.type == SDL_FINGERDOWN) {
            if (gesture.active)
                continue;
            gesture = {};
            gesture.active = true;
            gesture.id = e.fingerId;
            gesture.start = gesture.last = pos;
            gesture.time = e.timestamp;
            fling_velocity = 0;
            if (auto* w = scroll_window(pos))
                gesture.window = w->ID;
            io.AddMousePosEvent(pos.x, pos.y);
            if (edit_active && on_editor_group(pos)) {
                gesture.horizontal = true;
                io.AddMouseButtonEvent(0, true);
            }
        } else if (gesture.active && gesture.id == e.fingerId) {
            float dx = pos.x - gesture.start.x, dy = pos.y - gesture.start.y;
            if (e.type == SDL_FINGERMOTION) {
                if (!gesture.scroll && !gesture.horizontal && std::hypot(dx, dy) > 8) {
                    if (gesture.window && std::abs(dy) > std::abs(dx)) {
                        gesture.scroll = true;
                        io.AddMouseButtonEvent(0, false);
                    } else {
                        gesture.horizontal = true;
                        io.AddMousePosEvent(gesture.start.x, gesture.start.y);
                        io.AddMouseButtonEvent(0, true);
                    }
                }
                if (gesture.scroll) {
                    apply_scroll(gesture.window, gesture.last.y - pos.y);
                    const auto ms = std::max(1U, e.timestamp - gesture.time);
                    gesture.velocity = std::clamp((pos.y - gesture.last.y) * 1000 / ms, -1800.0F, 1800.0F);
                } else
                    io.AddMousePosEvent(pos.x, pos.y);
                gesture.last = pos;
                gesture.time = e.timestamp;
            } else if (e.type == SDL_FINGERUP) {
                if (gesture.scroll) {
                    fling_window = gesture.window;
                    fling_velocity = gesture.velocity;
                } else {
                    io.AddMousePosEvent(pos.x, pos.y);
                    if (!gesture.horizontal)
                        io.AddMouseButtonEvent(0, true);
                    io.AddMouseButtonEvent(0, false);
                }
                gesture.active = false;
            }
        }
    }
    for (const auto& e : context->InputEventsQueue)
        frame->normalized_through = std::max(frame->normalized_through, e.EventId);
}
void remember(const Layout& layout) {
    if (undo.size() >= 32)
        undo.erase(undo.begin());
    undo.push_back(layout);
}
void remember() {
    remember(layouts[profile]);
}
void open_editor() {
    clear();
    for (int i = 0; i < 2; ++i)
        saved[i] = layouts[i];
    undo.clear();
    edit_active = true;
}
} // namespace
void configure(const std::filesystem::path& directory) {
    config_path = directory / "android-touch-layout-v1.txt";
    std::ifstream f(config_path);
    std::string magic;
    int version = 0, mode = 0;
    bool float_stick = false;
    float dz = .12F, sen = 1;
    Layout candidate[2];
    if (!(f >> magic >> version) || magic != "DKR_TOUCH" || version != 1)
        return;
    if (!(f >> mode >> float_stick >> dz >> sen) || !std::isfinite(dz) || !std::isfinite(sen))
        return;
    for (auto& l : candidate)
        for (auto& p : l)
            if (!(f >> p.x >> p.y >> p.size >> p.opacity >> p.visible))
                return;
    for (int i = 0; i < 2; ++i) {
        validate(candidate[i]);
        layouts[i] = candidate[i];
    }
    visibility = std::clamp(mode, 0, 2);
    floating = float_stick;
    deadzone = std::clamp(dz, .02F, .4F);
    sensitivity = std::clamp(sen, .5F, 2.0F);
}
void install_context() {
    auto* frame = new FrameCoordinates;
    for (auto type :
         {ImGuiContextHookType_NewFramePre, ImGuiContextHookType_RenderPost, ImGuiContextHookType_Shutdown}) {
        ImGuiContextHook hook{};
        hook.Type = type;
        hook.Callback = context_hook;
        hook.UserData = frame;
        ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
    }
    ImGui::GetIO().SetPlatformImeDataFn = [](ImGuiViewport*, ImGuiPlatformImeData* data) {
        if (data->WantVisible) {
            SDL_Rect r{int(data->InputPos.x * scale), int(data->InputPos.y * scale), 1,
                       int(data->InputLineHeight * scale)};
            SDL_SetTextInputRect(&r);
            if (!ime_requested)
                SDL_StartTextInput();
        } else if (ime_requested)
            SDL_StopTextInput();
        ime_requested = data->WantVisible;
    };
}
void clear() {
    std::lock_guard lock(mutex);
    contacts.clear();
    preview_contacts.clear();
    pending.clear();
    cancel_pending = true;
}
bool event(const SDL_Event& e, bool gameplay) {
    if (e.type == SDL_APP_WILLENTERBACKGROUND || e.type == SDL_APP_TERMINATING ||
        (e.type == SDL_WINDOWEVENT &&
         (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))) {
        clear();
        return false;
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN ||
        (e.type == SDL_CONTROLLERAXISMOTION && std::abs(int(e.caxis.value)) > 12000)) {
        std::lock_guard lock(mutex);
        controller_active = true;
        contacts.clear();
    }
    if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        std::lock_guard lock(mutex);
        controller_active = false;
        contacts.clear();
    }
    if ((e.type == SDL_MOUSEMOTION && e.motion.which == SDL_TOUCH_MOUSEID) ||
        ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) && e.button.which == SDL_TOUCH_MOUSEID))
        return true;
    if (e.type != SDL_FINGERDOWN && e.type != SDL_FINGERMOTION && e.type != SDL_FINGERUP)
        return false;
    bool open = false;
    {
        std::lock_guard lock(mutex);
        if ((gameplay && !edit_active) || test_active) {
            auto& destination = test_active ? preview_contacts : contacts;
            Point pos{e.tfinger.x, e.tfinger.y};
            if (e.type == SDL_FINGERUP)
                destination.up(e.tfinger.fingerId);
            else if (e.type == SDL_FINGERMOTION)
                destination.move(e.tfinger.fingerId, pos);
            else
                for (auto it = targets.rbegin(); it != targets.rend(); ++it)
                    if (it->rect.contains(pos)) {
                        const auto& t = *it;
                        if (t.menu) {
                            if (test_active) {
                                test_active = false;
                                preview_contacts.clear();
                            } else
                                open = true;
                            controller_active = false;
                            contacts.clear();
                        } else
                            destination.down(e.tfinger.fingerId, pos, t, floating);
                        break;
                    }
        } else {
            controller_active = false;
            // Bound the cross-thread queue. On overflow release, never strand a press.
            if (pending.size() >= 128) {
                pending.clear();
                cancel_pending = true;
            }
            pending.push_back(e.tfinger);
        }
    }
    if (open)
        ui::toggle_overlay();
    return true;
}
void merge_input(input::State& state, bool blocked) {
    std::lock_guard lock(mutex);
    if (blocked || edit_active) {
        contacts.clear();
        return;
    }
    const auto sample = contacts.sample(deadzone, sensitivity);
    state.buttons |= sample.buttons;
    if (sample.stick_owned) {
        state.stick_x = sample.x;
        state.stick_y = sample.y;
    }
}
bool editing() {
    return edit_active.load();
}
bool controller_keyboard() {
    std::lock_guard lock(mutex);
    return controller_active;
}
void constrain_modal() {
    const auto safe = safe_rect();
    ImGui::SetNextWindowSizeConstraints({std::min(200.0F, safe.w), std::min(80.0F, safe.h)}, {safe.w, safe.h});
    ImGui::SetNextWindowPos({safe.x + safe.w * .5F, safe.y + safe.h * .5F}, ImGuiCond_Always, {.5F, .5F});
}

void draw_controls(bool overlay) {
    if (overlay && !edit_active) {
        std::lock_guard lock(mutex);
        targets.clear();
        contacts.clear();
        return;
    }
    const auto safe = safe_rect();
    auto& io = ImGui::GetIO();
    const int next = io.DisplaySize.x / io.DisplaySize.y < 1.6F ? 1 : 0;
    if (next != profile) {
        profile = next;
        undo.clear();
        clear();
    }
    bool visible = true;
    Sample sample;
    {
        std::lock_guard lock(mutex);
        visible = visibility == 1 || (visibility == 0 && !controller_active);
        sample = (test_active ? preview_contacts : contacts).sample(deadzone, sensitivity);
    }
    if (edit_active)
        visible = true;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (edit_active) {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(.95F);
        ImGui::Begin("##touch-layout-canvas", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        draw = ImGui::GetWindowDrawList();
        ImGui::End();
    }
    // Reuse both published and staging capacities; previously every touch-only
    // frame allocated and destroyed a vector. Publication remains under mutex.
    thread_local std::vector<Target> frame_targets;
    frame_targets.clear();
    frame_targets.reserve(32);
    const auto add = [&](Rect r, const char* label, std::uint16_t mask, ImU32 colour, bool stick, bool menu,
                         float opacity) {
        if (!edit_active || test_active) {
            Target t{{r.x / io.DisplaySize.x, r.y / io.DisplaySize.y, r.w / io.DisplaySize.x, r.h / io.DisplaySize.y},
                     mask,
                     stick,
                     menu};
            frame_targets.push_back(t);
        }
        const int alpha = int(opacity * 255);
        colour = (colour & 0x00ffffffU) | (ImU32(alpha) << 24);
        if (stick) {
            const ImVec2 center{r.x + r.w / 2, r.y + r.h / 2};
            draw->AddNgonFilled(center, r.w / 2, colour, 8);
            draw->AddNgon(center, r.w / 2, IM_COL32(230, 236, 241, alpha), 8, 3);
            const ImVec2 knob{center.x + sample.x * r.w * .23F, center.y - sample.y * r.h * .23F};
            draw->AddCircleFilled(knob, r.w * .23F, IM_COL32(195, 207, 217, alpha), 24);
        } else {
            const bool round = mask == 0x8000 || mask == 0x4000 || (mask > 0 && mask < 16);
            const float rounding = round ? std::min(r.w, r.h) / 2 : 10;
            draw->AddRectFilled({r.x, r.y}, {r.x + r.w, r.y + r.h}, colour, rounding);
            draw->AddRect({r.x, r.y}, {r.x + r.w, r.y + r.h},
                          (sample.buttons & mask) ? IM_COL32(255, 235, 90, 255) : IM_COL32(220, 230, 237, alpha),
                          rounding, 0, 2);
            const auto text = ImGui::CalcTextSize(label);
            draw->AddText({r.x + (r.w - text.x) / 2, r.y + (r.h - text.y) / 2}, IM_COL32(255, 255, 240, 255), label);
        }
    };
    for (int g = 0; g < group_count; ++g) {
        auto& p = layouts[profile][g];
        if ((!visible || !p.visible) && g != 8 && !edit_active)
            continue;
        const bool compact_cross = (g == 2 || g == 3) && safe.h < 360;
        const Point extent = group_extent(g, safe);
        const auto r = place(safe, extent, p);
        float s = r.w / extent.x;
        if (g == 0)
            add(r, "", 0, IM_COL32(75, 93, 108, 255), true, false, p.opacity);
        if (g == 1) {
            add({r.x, r.y, 52 * s, 52 * s}, "B", 0x4000, IM_COL32(22, 161, 83, 255), false, false, p.opacity);
            add({r.x + 60 * s, r.y + 48 * s, 64 * s, 64 * s}, "A", 0x8000, IM_COL32(26, 91, 204, 255), false, false,
                p.opacity);
        }
        if (g == 2 || g == 3) {
            const char* arrow[]{"^", "<", ">", "v"};
            const std::uint16_t maskC[]{8, 2, 1, 4}, maskD[]{0x0800, 0x0200, 0x0100, 0x0400};
            const Point offsetClassic[]{{48, 0}, {0, 48}, {96, 48}, {48, 96}};
            const Point offsetCompact[]{{0, 0}, {0, 48}, {48, 0}, {48, 48}};
            const auto* offset = compact_cross ? offsetCompact : offsetClassic;
            for (int i = 0; i < 4; ++i)
                add({r.x + offset[i].x * s, r.y + offset[i].y * s, 48 * s, 48 * s}, arrow[i],
                    g == 2 ? maskC[i] : maskD[i], g == 2 ? IM_COL32(181, 132, 0, 255) : IM_COL32(94, 111, 122, 255),
                    false, false, p.opacity);
        }
        if (g >= 4) {
            const std::uint16_t masks[]{0x2000, 0x0020, 0x0010, 0x1000, 0};
            const char* names[]{"Z", "L", "R", "Start", "Menu"};
            add(r, names[g - 4], masks[g - 4], g == 7 ? IM_COL32(191, 37, 40, 255) : IM_COL32(57, 89, 111, 255), false,
                g == 8, p.opacity);
        }
        if (edit_active && !test_active) {
            ImGui::SetNextWindowPos({r.x, r.y});
            ImGui::SetNextWindowSize({r.w, r.h});
            char name[32];
            std::snprintf(name, sizeof(name), "##touch-move-%d", g);
            ImGui::Begin(name, nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav);
            ImGui::InvisibleButton("move", {r.w, r.h});
            if (ImGui::IsItemActivated()) {
                selection = g;
                remember();
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                p.x = std::clamp(p.x + io.MouseDelta.x / safe.w, 0.0F, 1.0F);
                p.y = std::clamp(p.y + io.MouseDelta.y / safe.h, 0.0F, 1.0F);
            }
            ImGui::End();
            if (selection == g)
                draw->AddRect({r.x - 3, r.y - 3}, {r.x + r.w + 3, r.y + r.h + 3}, IM_COL32(255, 214, 62, 255), 10, 0,
                              3);
        }
    }
    {
        std::lock_guard lock(mutex);
        targets.swap(frame_targets);
    }
}
void settings() {
    ImGui::SeparatorText("TOUCH SCREEN");
    bool changed = false;
    {
        std::lock_guard lock(mutex);
        changed |= ImGui::Combo("Visibility", &visibility, "Automatic\0Always on\0Off\0");
        changed |= ImGui::Checkbox("Floating analog stick", &floating);
        ImGui::SliderFloat("Touch deadzone", &deadzone, .02F, .4F, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Touch sensitivity", &sensitivity, .5F, 2.0F, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (changed)
        save_config();
    if (ImGui::Button("EDIT TOUCH LAYOUT", {0, 48}))
        open_editor();
    if (!status.empty())
        ImGui::TextWrapped("%s", status.c_str());
    ImGui::Separator();
}
void editor() {
    if (!edit_active)
        return;
    if (test_active)
        return;
    const auto safe = safe_rect();
    // Editor is a dedicated canvas; the options sheet may be collapsed to
    // expose controls beneath it. Gameplay remains blocked for its lifetime.
    ImGui::SetNextWindowPos({safe.x + std::max(0.0F, (safe.w - 340.0F) * .5F), safe.y}, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({std::min(340.0F, safe.w), std::min(330.0F, safe.h)}, ImGuiCond_Appearing);
    ImGui::Begin("Touch layout - options", nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextWrapped("Drag a group. Collapse this sheet to reach covered controls.");
    if (ImGui::BeginCombo("Group", labels[selection])) {
        for (int i = 0; i < group_count; ++i)
            if (ImGui::Selectable(labels[i], selection == i))
                selection = i;
        ImGui::EndCombo();
    }
    auto& p = layouts[profile][selection];
    const auto before_size = layouts[profile];
    ImGui::SliderFloat("Size", &p.size, 1, 1.5F, "%.2fx");
    if (ImGui::IsItemActivated())
        remember(before_size);
    const auto before_opacity = layouts[profile];
    ImGui::SliderFloat("Opacity", &p.opacity, .25F, 1, "%.2f");
    if (ImGui::IsItemActivated())
        remember(before_opacity);
    const auto before_visible = layouts[profile];
    ImGui::BeginDisabled(selection == 8);
    if (ImGui::Checkbox("Visible", &p.visible))
        remember(before_visible);
    ImGui::EndDisabled();
    bool overlap = false;
    for (int i = 0; i < group_count; ++i)
        for (int j = i + 1; j < group_count; ++j) {
            if (!layouts[profile][i].visible || !layouts[profile][j].visible)
                continue;
            auto a = place(safe, group_extent(i, safe), layouts[profile][i]),
                 b = place(safe, group_extent(j, safe), layouts[profile][j]);
            overlap |= a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
        }
    if (overlap)
        ImGui::TextWrapped("Some control groups overlap. Move them apart for reliable input.");
    const char* arrows[]{"Left", "Right", "Up", "Down"};
    for (int i = 0; i < 4; ++i) {
        if (i)
            ImGui::SameLine();
        if (ImGui::Button(arrows[i])) {
            remember();
            if (i < 2)
                p.x += i == 0 ? -.01F : .01F;
            else
                p.y += i == 2 ? -.01F : .01F;
            validate(layouts[profile]);
        }
    }
    if (ImGui::Button("Classic")) {
        remember();
        layouts[profile] = classic();
    }
    ImGui::SameLine();
    if (ImGui::Button("Left-handed")) {
        remember();
        layouts[profile] = classic(true);
    }
    if (ImGui::Button("Test controls")) {
        clear();
        test_active = true;
    }
    if (ImGui::Button("Undo") && !undo.empty()) {
        layouts[profile] = undo.back();
        undo.pop_back();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        for (int i = 0; i < 2; ++i)
            layouts[i] = saved[i];
        edit_active = false;
        clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save") && save_config()) {
        edit_active = false;
        clear();
    }
    if (!status.empty())
        ImGui::TextWrapped("%s", status.c_str());
    ImGui::End();
}
void menu(int& page, bool live, bool& restart, bool& quit) {
    const auto safe = safe_rect();
    ImGui::SetCursorPos({safe.x, safe.y});
    if (ImGui::Button(menu_open ? "CLOSE MENU" : "MENU", {140, 48}))
        menu_open = !menu_open;
    ImGui::SameLine();
    ImGui::TextUnformatted("DKR-R");
    if (live) {
        ImGui::SameLine();
        if (ImGui::Button("RESUME", {140, 48})) {
            clear();
            ui::toggle_overlay();
        }
    }
    if (menu_open) {
        ImGui::SetNextWindowPos({safe.x, safe.y + 52});
        ImGui::SetNextWindowSize({std::min(310.0F, safe.w), std::max(60.0F, safe.h - 52)});
        ImGui::Begin("Mobile navigation", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        const char* names[]{"PLAY",     "GRAPHICS",     "SOUND",        "CONTROLS",   "SAVE MANAGER",
                            "TEXTURES", "MODS / HACKS", "DKR-R ONLINE", "ABOUT DKR-R"};
        const int pages[]{0, 1, 2, 3, 4, 8, 6, 5, 7};
        for (int i = 0; i < 9; ++i)
            if (ImGui::Button(names[i], {-1, 48})) {
                page = pages[i];
                menu_open = false;
            }
        if (ImGui::Button("RESTART DKR-R", {-1, 48})) {
            restart = true;
            menu_open = false;
        }
        if (ImGui::Button("EXIT DKR-R", {-1, 48})) {
            quit = true;
            menu_open = false;
        }
        ImGui::End();
    }
}
void begin_content() {
    auto safe = safe_rect();
    ImGui::SetCursorPos({safe.x, safe.y + 60});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 16});
    ImGui::BeginChild("mobile-content", {safe.w, std::max(1.0F, safe.h - 60)}, true,
                      ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::PushItemWidth(std::max(1.0F, ImGui::GetContentRegionAvail().x));
    ImGui::PushTextWrapPos(0);
}
void end_content() {
    ImGui::PopTextWrapPos();
    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace dkr::runtime::mobile
