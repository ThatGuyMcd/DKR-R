#include "navigation_input.inl"

void LibrarySidebarEvent(const SDL_Event& event) {
    if(event.type!=SDL_CONTROLLERBUTTONDOWN)return;
    constexpr std::array<unsigned char,6> buttons{SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,SDL_CONTROLLER_BUTTON_DPAD_RIGHT,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
    for(unsigned i=0;i<buttons.size();++i)if(event.cbutton.button==buttons[i])dkr::library_ui::record_navigation_press(i);
}

void DrawSidebarNavigationToggle(float width,int selected) {
    using namespace dkr::library_ui;
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav,false);
    const auto at=ImGui::GetCursorScreenPos();
    const bool dpad=sidebar_input.load()==SidebarInput::Dpad;
    for(auto& state:g_library_navigation)if(state.context==ImGui::GetCurrentContext())state.selection=selected;
    const bool pressed=ImGui::InvisibleButton("Sidebar navigation##sidebar-input",{width,NavigationToggleHeight});
    const bool focused=ImGui::IsItemFocused()||selected==SidebarToggle;
    const bool hover=ImGui::IsItemHovered();
    auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 end{at.x+width,at.y+NavigationToggleHeight-6.F};
    draw->AddRectFilled(at,end,PaddockCol(hover?0x123E51:0x082536),8.F);
    draw->AddRect(at,end,PaddockCol(focused?0xFFD16B:0x35657A),8.F,0,focused?2.F:1.F);
    const auto caption=PaddockReading(12.F,true,1.2F);
    const auto title=dkr::localisation::source("Sidebar navigation");
    PaddockDrawRun(draw,caption,{at.x+10.F,at.y+7.F},PaddockCol(0xD0E1E7),title.data(),title.data()+title.size());
    const auto label=PaddockReading(13.F,true,1.1F);
    draw_navigation_icons(draw,{at.x+width*.20F,at.y+39.F},{at.x+width*.82F,at.y+39.F},dpad,label.font,label.size);
    const ImVec2 a{at.x+width*.5F-22.F,at.y+29.F},b{at.x+width*.5F+22.F,at.y+49.F};
    draw->AddRectFilled(a,b,PaddockCol(dpad?0x12B99B:0x32576A),10.F);
    draw->AddCircleFilled({dpad?b.x-10.F:a.x+10.F,at.y+39.F},7.F,PaddockCol(0xFFF3C9),16);
    if(hover)ImGui::SetTooltip("Left stick: move between page elements. Right stick: scroll.\nD-pad: Up/Down changes sidebar pages.\nLB / RB: shoulders change pages; D-pad also navigates the page.");
    if(pressed||sidebar_toggle_requested.exchange(false)) {
        sidebar_input.store(dpad?SidebarInput::Shoulders:SidebarInput::Dpad);SaveSettings();
    }
    ImGui::PopItemFlag();
}
