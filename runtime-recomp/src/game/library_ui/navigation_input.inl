// First-party pre-frame routing covers SDL2, SDL3 and the overlay backend's
// polling. No gamepad event is allowed to navigate two UI surfaces at once.
bool LibrarySidebarInputBlocked();
bool LibraryContentInputCaptured();
bool PlayLaunchLayoutCheck();
extern int g_launcher_layout_step;
struct LibraryNavContext {
    ImGuiContext* context = nullptr;
    std::array<bool, 6> physical{};
    dkr::library_ui::NavigationRepeat repeat;
    dkr::library_ui::SidebarInput mode = dkr::library_ui::SidebarInput::Shoulders;
    int pending = 0, selection = 0;
    bool stick_armed = false, scroll_armed = false;
    bool content_motion = false;
    std::array<bool, 4> focus_directions{};
    ImGuiWindow* content = nullptr;
    ImGuiWindow* scroll_content = nullptr;
    ImGuiID pinned_action = 0;
};
std::array<LibraryNavContext, 2> g_library_navigation;
bool LibraryNavigationActive(const LibraryNavContext& state) {
    return !state.context->IO.AppFocusLost && (&state != &g_library_navigation[1] || g_overlay_visible.load());
}
void LibraryNavigationEnterBody(LibraryNavContext& state) {
    auto& context = *state.context;
    // The pinned action sits outside the scrolling child. Flattened ImGui
    // navigation only scores visible child items, so an empty/clipped slice
    // can otherwise strand focus on that action. Restore the child's last
    // selection (or initialise its first control) on an explicit Up request.
    if (!LibraryNavigationActive(state) || LibraryContentInputCaptured() || context.OpenPopupStack.Size ||
        context.ActiveId || !state.pinned_action || context.NavId != state.pinned_action ||
        context.NavWindow != state.content || !state.scroll_content || !context.NavMoveSubmitted ||
        context.NavMoveDir != ImGuiDir_Up)
        return;
    ImGui::NavMoveRequestCancel();
    ImGui::FocusWindow(state.scroll_content);
    ImGui::NavInitWindow(state.scroll_content, false);
    if (context.NavInitRequest)
        context.NavInitRequestFromMove = true;
    context.NavDisableHighlight = false;
}
void LibraryNavigationScroll(LibraryNavContext& state) {
    auto* context = state.context;
    const bool play_check = PlayLaunchLayoutCheck();
    if (play_check)
        context->IO.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    if (!LibraryNavigationActive(state) || LibraryContentInputCaptured() ||
        !(context->IO.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) ||
        !(context->IO.BackendFlags & ImGuiBackendFlags_HasGamepad)) {
        state.scroll_armed = false;
        return;
    }
    const float right_x = play_check ? 0.F : dkr::library_ui::navigation_axes[2].load(std::memory_order_relaxed);
    const float right_y = play_check ? (g_launcher_layout_step == 2 ? 1.F : 0.F)
                                     : dkr::library_ui::navigation_axes[3].load(std::memory_order_relaxed);
    if (std::abs(right_x) < .25F && std::abs(right_y) < .25F)
        state.scroll_armed = true;
    if (!state.scroll_armed)
        return;
    auto* target = context->NavWindow;
    bool content = false, popup = false;
    for (auto* ancestor = target; ancestor; ancestor = ancestor->ParentWindow) {
        content |= ancestor == state.content;
        popup |= (ancestor->Flags & ImGuiWindowFlags_Popup) != 0;
    }
    if (context->OpenPopupStack.Size && !popup)
        return;
    if (!popup && (!content || target == state.content))
        target = state.scroll_content ? state.scroll_content : state.content;
    if (!target || target->Flags & ImGuiWindowFlags_NoNavInputs)
        return;
    const float speed = target->CalcFontSize() * 100.F * std::min(context->IO.DeltaTime, .1F);
    for (unsigned axis = 0; axis < 2; ++axis) {
        const float value = axis ? right_y : right_x;
        if (std::abs(value) < .05F)
            continue;
        for (auto* window = target; window; window = window->ParentWindow) {
            if ((axis ? window->ScrollMax.y : window->ScrollMax.x) > 0.F) {
                if (axis)
                    ImGui::SetScrollY(window, std::clamp(window->Scroll.y + value * speed, 0.F, window->ScrollMax.y));
                else
                    ImGui::SetScrollX(window, std::clamp(window->Scroll.x + value * speed, 0.F, window->ScrollMax.x));
                break;
            }
            if (window == state.content || window->Flags & ImGuiWindowFlags_Popup)
                break;
        }
    }
}
void LibraryNavigationHook(ImGuiContext* context, ImGuiContextHook* hook) {
    auto& state = *static_cast<LibraryNavContext*>(hook->UserData);
    if (hook->Type == ImGuiContextHookType_NewFramePost) {
        LibraryNavigationEnterBody(state);
        LibraryNavigationScroll(state);
        return;
    }
    const int connected =
        PlayLaunchLayoutCheck() ? 1 : dkr::library_ui::navigation_connected.load(std::memory_order_relaxed);
    if (connected > 0)
        context->IO.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    else if (connected == 0)
        context->IO.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    const auto mode = dkr::library_ui::sidebar_input.load();
    if (mode != state.mode) {
        state.repeat = {};
        state.mode = mode;
    }
    constexpr std::array<ImGuiKey, 6> keys{ImGuiKey_GamepadDpadUp,   ImGuiKey_GamepadDpadDown,
                                           ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight,
                                           ImGuiKey_GamepadL1,       ImGuiKey_GamepadR1};
    // Sample before ImGui deduplication: a filtered press makes its release
    // disappear from ImGui's event queue, but never from our physical state.
    const unsigned physical = dkr::library_ui::navigation_buttons.load(std::memory_order_relaxed);
    const unsigned edges = dkr::library_ui::navigation_press_edges.exchange(0, std::memory_order_relaxed);
    for (unsigned k = 0; k < keys.size(); ++k)
        state.physical[k] = (physical & (1U << k)) != 0;
    const bool dpad = mode == dkr::library_ui::SidebarInput::Dpad;
    const bool route_sticks = !LibraryContentInputCaptured();
    const auto consumed = [&](ImGuiKey key) {
        if (route_sticks && key >= ImGuiKey_GamepadLStickLeft && key <= ImGuiKey_GamepadLStickDown)
            return true;
        if (key >= ImGuiKey_GamepadDpadLeft && key <= ImGuiKey_GamepadDpadDown)
            return route_sticks || dpad;
        if (key == ImGuiKey_GamepadFaceDown && state.selection >= 11)
            return true;
        return !dpad && (key == ImGuiKey_GamepadL1 || key == ImGuiKey_GamepadR1);
    };
    for (int i = context->InputEventsQueue.Size - 1; i >= 0; --i) {
        const auto& event = context->InputEventsQueue[i];
        if (event.Type == ImGuiInputEventType_Key && consumed(event.Key.Key))
            context->InputEventsQueue.erase(context->InputEventsQueue.Data + i);
    }
    for (unsigned k = 4; k < keys.size(); ++k)
        if (consumed(keys[k]))
            context->IO.AddKeyEvent(keys[k], false);
    if (consumed(ImGuiKey_GamepadFaceDown))
        context->IO.AddKeyEvent(ImGuiKey_GamepadFaceDown, false);
    if (route_sticks) {
        constexpr std::array<ImGuiKey, 4> sticks{ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
                                                 ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown};
        constexpr std::array<ImGuiKey, 4> directions{ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight,
                                                     ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown};
        constexpr std::array<unsigned, 4> physical_index{2, 3, 0, 1};
        const bool play_check = PlayLaunchLayoutCheck();
        const float x = play_check ? (g_launcher_layout_step == 3   ? -.9F
                                      : g_launcher_layout_step == 5 ? .9F
                                                                    : 0.F)
                                   : dkr::library_ui::navigation_axes[0].load(std::memory_order_relaxed);
        const float y = play_check ? (g_launcher_layout_step == 1   ? -.9F
                                      : g_launcher_layout_step == 4 ? .9F
                                                                    : 0.F)
                                   : dkr::library_ui::navigation_axes[1].load(std::memory_order_relaxed);
        const std::array<float, 4> magnitude{std::max(0.F, -x), std::max(0.F, x), std::max(0.F, -y), std::max(0.F, y)};
        if (!LibraryNavigationActive(state))
            state.stick_armed = false;
        else if (std::abs(x) < .25F && std::abs(y) < .25F)
            state.stick_armed = true;
        for (unsigned k = 0; k < sticks.size(); ++k) {
            context->IO.AddKeyAnalogEvent(sticks[k], false, 0.F);
            state.focus_directions[k] = state.stick_armed && magnitude[k] > (state.focus_directions[k] ? .3F : .5F);
            const bool button = !dpad && (state.physical[physical_index[k]] || (edges & (1U << physical_index[k]))) &&
                                LibraryNavigationActive(state);
            context->IO.AddKeyAnalogEvent(directions[k], button || state.focus_directions[k],
                                          button ? 1.F : magnitude[k]);
        }
        if (std::any_of(state.focus_directions.begin(), state.focus_directions.end(), [](bool down) { return down; })) {
            state.content_motion = true;
            bool in_content = false, in_popup = false;
            for (auto* window = context->NavWindow; window; window = window->ParentWindow) {
                in_content |= window == state.content;
                in_popup |= (window->Flags & ImGuiWindowFlags_Popup) != 0;
            }
            if (state.content && !in_content && !in_popup && !context->OpenPopupStack.Size) {
                ImGui::FocusWindow(state.content);
                if (!context->NavId)
                    ImGui::NavInitWindow(state.content, false);
            }
        }
    } else {
        state.stick_armed = false;
        state.focus_directions = {};
        if (dpad)
            for (unsigned k = 0; k < 4; ++k)
                context->IO.AddKeyEvent(keys[k], false);
    }
    const bool blocked = LibrarySidebarInputBlocked() || context->IO.AppFocusLost ||
                         (&state == &g_library_navigation[1] && !g_overlay_visible.load());
    dkr::library_ui::sidebar_input_blocked.store(blocked, std::memory_order_release);
    const unsigned previous = dpad ? 0 : 4, following = dpad ? 1 : 5;
    // Events own the initial press, including a down/up tap between frames.
    // Physical polling owns release and repeat, so it cannot double-step the
    // same press or leave a filtered release stuck in ImGui.
    state.pending = state.repeat.poll(state.physical[previous] || (edges & (1U << previous)),
                                      state.physical[following] || (edges & (1U << following)), blocked,
                                      ImGui::GetTime(), (edges & ((1U << previous) | (1U << following))) != 0);
}
void LibraryInstallNavigationHook(bool live) {
    auto* context = ImGui::GetCurrentContext();
    for (const auto& hook : context->Hooks)
        if (hook.Callback == LibraryNavigationHook)
            return;
    auto& state = g_library_navigation[live ? 1 : 0];
    state = {};
    state.context = context;
    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_NewFramePre;
    hook.Callback = LibraryNavigationHook;
    hook.UserData = &state;
    ImGui::AddContextHook(context, &hook);
    hook.Type = ImGuiContextHookType_NewFramePost;
    ImGui::AddContextHook(context, &hook);
}
void LibraryNavigationContentWindow() {
    for (auto& state : g_library_navigation)
        if (state.context == ImGui::GetCurrentContext()) {
            state.content = ImGui::GetCurrentWindow();
            state.scroll_content = nullptr;
            state.pinned_action = 0;
        }
}
void LibraryNavigationScrollWindow(ImGuiWindow* window) {
    for (auto& state : g_library_navigation)
        if (state.context == ImGui::GetCurrentContext())
            state.scroll_content = window;
}
void LibraryNavigationPinnedAction(ImGuiID id) {
    for (auto& state : g_library_navigation)
        if (state.context == ImGui::GetCurrentContext())
            state.pinned_action = id;
}
int LibraryTakeSidebarStep(bool live) {
    return std::exchange(g_library_navigation[live ? 1 : 0].pending, 0);
}
bool LibraryTakeContentMotion(bool live) {
    return std::exchange(g_library_navigation[live ? 1 : 0].content_motion, false);
}
