// First-party pre-frame routing covers SDL2, SDL3 and the overlay backend's
// polling. No gamepad event is allowed to navigate two UI surfaces at once.
bool LibrarySidebarInputBlocked();
bool LibraryContentInputCaptured();
struct LibraryNavContext {
    ImGuiContext* context=nullptr;
    std::array<bool,6> physical{};
    dkr::library_ui::NavigationRepeat repeat;
    dkr::library_ui::SidebarInput mode=dkr::library_ui::SidebarInput::Shoulders;
    int pending=0,selection=0;
    bool stick_armed=false,scroll_armed=false;
    bool content_motion=false;
    std::array<bool,4> focus_directions{};
    ImGuiWindow* content=nullptr;
};
std::array<LibraryNavContext,2> g_library_navigation;
bool LibraryNavigationActive(const LibraryNavContext& state) {
    return !state.context->IO.AppFocusLost&&
        (&state!=&g_library_navigation[1]||g_overlay_visible.load());
}
void LibraryNavigationScroll(LibraryNavContext& state) {
    auto* context=state.context;
    if(!LibraryNavigationActive(state)||LibraryContentInputCaptured()||
       !(context->IO.ConfigFlags&ImGuiConfigFlags_NavEnableGamepad)||
       !(context->IO.BackendFlags&ImGuiBackendFlags_HasGamepad)){state.scroll_armed=false;return;}
    const float right_x=dkr::library_ui::navigation_axes[2].load(std::memory_order_relaxed);
    const float right_y=dkr::library_ui::navigation_axes[3].load(std::memory_order_relaxed);
    if(std::abs(right_x)<.25F&&std::abs(right_y)<.25F)state.scroll_armed=true;
    if(!state.scroll_armed)return;
    auto* target=context->NavWindow;
    bool content=false,popup=false;
    for(auto* ancestor=target;ancestor;ancestor=ancestor->ParentWindow) {
        content|=ancestor==state.content;popup|=(ancestor->Flags&ImGuiWindowFlags_Popup)!=0;
    }
    if(context->OpenPopupStack.Size&&!popup)return;
    if(!content&&!popup)target=state.content;
    if(!target||target->Flags&ImGuiWindowFlags_NoNavInputs)return;
    const float speed=target->CalcFontSize()*100.F*std::min(context->IO.DeltaTime,.1F);
    for(unsigned axis=0;axis<2;++axis) {
        const float value=dkr::library_ui::navigation_axes[axis+2].load(std::memory_order_relaxed);
        if(std::abs(value)<.05F)continue;
        for(auto* window=target;window;window=window->ParentWindow) {
            if((axis?window->ScrollMax.y:window->ScrollMax.x)>0.F) {
                if(axis)ImGui::SetScrollY(window,std::clamp(window->Scroll.y+value*speed,0.F,window->ScrollMax.y));
                else ImGui::SetScrollX(window,std::clamp(window->Scroll.x+value*speed,0.F,window->ScrollMax.x));
                break;
            }
            if(window==state.content||window->Flags&ImGuiWindowFlags_Popup)break;
        }
    }
}
void LibraryNavigationHook(ImGuiContext* context,ImGuiContextHook* hook) {
    auto& state=*static_cast<LibraryNavContext*>(hook->UserData);
    if(hook->Type==ImGuiContextHookType_NewFramePost){LibraryNavigationScroll(state);return;}
    const int connected=dkr::library_ui::navigation_connected.load(std::memory_order_relaxed);
    if(connected>0)context->IO.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    else if(connected==0)context->IO.BackendFlags&=~ImGuiBackendFlags_HasGamepad;
    const auto mode=dkr::library_ui::sidebar_input.load();
    if(mode!=state.mode){state.repeat={};state.mode=mode;}
    constexpr std::array<ImGuiKey,6> keys{ImGuiKey_GamepadDpadUp,ImGuiKey_GamepadDpadDown,
        ImGuiKey_GamepadDpadLeft,ImGuiKey_GamepadDpadRight,ImGuiKey_GamepadL1,ImGuiKey_GamepadR1};
    // Sample before ImGui deduplication: a filtered press makes its release
    // disappear from ImGui's event queue, but never from our physical state.
    const unsigned physical=dkr::library_ui::navigation_buttons.load(std::memory_order_relaxed);
    const unsigned edges=dkr::library_ui::navigation_press_edges.exchange(0,std::memory_order_relaxed);
    for(unsigned k=0;k<keys.size();++k)state.physical[k]=(physical&(1U<<k))!=0;
    const bool dpad=mode==dkr::library_ui::SidebarInput::Dpad;
    const bool route_sticks=!LibraryContentInputCaptured();
    const auto consumed=[&](ImGuiKey key) {
        if(route_sticks&&key>=ImGuiKey_GamepadLStickLeft&&key<=ImGuiKey_GamepadLStickDown)return true;
        if(key>=ImGuiKey_GamepadDpadLeft&&key<=ImGuiKey_GamepadDpadDown)return route_sticks||dpad;
        if(key==ImGuiKey_GamepadFaceDown&&state.selection>=11)return true;
        return !dpad&&(key==ImGuiKey_GamepadL1||key==ImGuiKey_GamepadR1);
    };
    for(int i=context->InputEventsQueue.Size-1;i>=0;--i) {
        const auto& event=context->InputEventsQueue[i];
        if(event.Type==ImGuiInputEventType_Key&&consumed(event.Key.Key))context->InputEventsQueue.erase(context->InputEventsQueue.Data+i);
    }
    for(unsigned k=4;k<keys.size();++k)if(consumed(keys[k]))context->IO.AddKeyEvent(keys[k],false);
    if(consumed(ImGuiKey_GamepadFaceDown))context->IO.AddKeyEvent(ImGuiKey_GamepadFaceDown,false);
    if(route_sticks) {
        constexpr std::array<ImGuiKey,4> sticks{ImGuiKey_GamepadLStickLeft,ImGuiKey_GamepadLStickRight,
            ImGuiKey_GamepadLStickUp,ImGuiKey_GamepadLStickDown};
        constexpr std::array<ImGuiKey,4> directions{ImGuiKey_GamepadDpadLeft,ImGuiKey_GamepadDpadRight,
            ImGuiKey_GamepadDpadUp,ImGuiKey_GamepadDpadDown};
        constexpr std::array<unsigned,4> physical_index{2,3,0,1};
        const float x=dkr::library_ui::navigation_axes[0].load(std::memory_order_relaxed);
        const float y=dkr::library_ui::navigation_axes[1].load(std::memory_order_relaxed);
        const std::array<float,4> magnitude{std::max(0.F,-x),std::max(0.F,x),std::max(0.F,-y),std::max(0.F,y)};
        if(!LibraryNavigationActive(state))state.stick_armed=false;
        else if(std::abs(x)<.25F&&std::abs(y)<.25F)state.stick_armed=true;
        for(unsigned k=0;k<sticks.size();++k) {
            context->IO.AddKeyAnalogEvent(sticks[k],false,0.F);
            state.focus_directions[k]=state.stick_armed&&magnitude[k]>(state.focus_directions[k]?.3F:.5F);
            const bool button=!dpad&&(state.physical[physical_index[k]]||(edges&(1U<<physical_index[k])))&&LibraryNavigationActive(state);
            context->IO.AddKeyAnalogEvent(directions[k],button||state.focus_directions[k],button?1.F:magnitude[k]);
        }
        if(std::any_of(state.focus_directions.begin(),state.focus_directions.end(),[](bool down){return down;})) {
            state.content_motion=true;
            bool in_content=false,in_popup=false;
            for(auto* window=context->NavWindow;window;window=window->ParentWindow) {
                in_content|=window==state.content;in_popup|=(window->Flags&ImGuiWindowFlags_Popup)!=0;
            }
            if(state.content&&!in_content&&!in_popup&&!context->OpenPopupStack.Size) {
                ImGui::FocusWindow(state.content);
                if(!context->NavId)ImGui::NavInitWindow(state.content,false);
            }
        }
    } else {
        state.stick_armed=false;state.focus_directions={};
        if(dpad)for(unsigned k=0;k<4;++k)context->IO.AddKeyEvent(keys[k],false);
    }
    const bool blocked=LibrarySidebarInputBlocked()||context->IO.AppFocusLost||
        (&state==&g_library_navigation[1]&&!g_overlay_visible.load());
    dkr::library_ui::sidebar_input_blocked.store(blocked,std::memory_order_release);
    const unsigned previous=dpad?0:4,following=dpad?1:5;
    // Events own the initial press, including a down/up tap between frames.
    // Physical polling owns release and repeat, so it cannot double-step the
    // same press or leave a filtered release stuck in ImGui.
    state.pending=state.repeat.poll(state.physical[previous]||(edges&(1U<<previous)),
        state.physical[following]||(edges&(1U<<following)),blocked,ImGui::GetTime(),
        (edges&((1U<<previous)|(1U<<following)))!=0);
}
void LibraryInstallNavigationHook(bool live) {
    auto* context=ImGui::GetCurrentContext();
    for(const auto& hook:context->Hooks)if(hook.Callback==LibraryNavigationHook)return;
    auto& state=g_library_navigation[live?1:0];state={};state.context=context;
    ImGuiContextHook hook;hook.Type=ImGuiContextHookType_NewFramePre;
    hook.Callback=LibraryNavigationHook;hook.UserData=&state;ImGui::AddContextHook(context,&hook);
    hook.Type=ImGuiContextHookType_NewFramePost;ImGui::AddContextHook(context,&hook);
}
void LibraryNavigationContentWindow() {
    for(auto& state:g_library_navigation)if(state.context==ImGui::GetCurrentContext())state.content=ImGui::GetCurrentWindow();
}
int LibraryTakeSidebarStep(bool live) {return std::exchange(g_library_navigation[live?1:0].pending,0);}
bool LibraryTakeContentMotion(bool live) {return std::exchange(g_library_navigation[live?1:0].content_motion,false);}

