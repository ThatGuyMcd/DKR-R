float DrawPlayHolidayGreeting(ImVec2 header,float width,
        const dkr::runtime::copy::HeaderLayout& layout,float title_width) {
    const auto message=dkr::localisation::source(dkr::launcher_themes::greeting(dkr::launcher_themes::displayed,
        dkr::launcher_themes::preferences.calendar));
    if(message.empty())return layout.height;
    const float left=title_width+24.F,right=layout.action_x-24.F;
    const bool beside=!layout.stacked&&right-left>=220.F;
    const float available=beside?right-left:width;
    const auto type=PaddockSign(std::clamp(width*.017F,18.F,26.F),1.25F);
    const float text_height=PaddockTextHeight(type,message,available,true);
    const float height=beside?std::max(layout.height,text_height):layout.height+12.F+text_height;
    const ImVec2 at{header.x+(beside?left:0.F),
        header.y+(beside?(height-text_height)*.5F:layout.height+12.F)};
    PaddockTextStyle style;
    style.colour=PaddockCol(0xFFCD67);style.shadow=PaddockCol(0x031623);style.centre=true;
    PaddockTextAt(ImGui::GetWindowDrawList(),type,at,available,message,style,true);
    return height;
}
