// Shared by My mods and Controls. Keep the same type, colours, focus feedback
// and wrapping; Controls simply omits the library item-count badges.
int LauncherCategoryTabs(const char* id, std::span<const char* const> labels,
                         std::span<const std::string> counts, int current, float available) {
    const PaddockType type=PaddockSign(19.F,1.4F);
    const PaddockType badge=PaddockReading(12.F,true,1.5F);
    const bool show_counts=counts.size()==labels.size();
    const float height=std::ceil(type.line+16.F),limit=std::max(1.F,available-8.F);
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    std::vector<ImVec2> positions;
    std::vector<float> widths;
    float x=4.F,y=4.F,total=0;
    for(std::size_t i=0;i<labels.size();++i) {
        const float count=show_counts?9.F+std::max(23.F,PaddockMeasure(badge,counts[i])+12.F):0;
        const float width=std::min(limit,std::ceil(28.F+PaddockMeasure(type,dkr::localisation::source(labels[i]))+count));
        if(x>4.F&&x+width+4.F>available){x=4.F;y+=height+4.F;}
        positions.push_back({origin.x+x,origin.y+y});widths.push_back(width);
        x+=width+4.F;total=std::max(total,x);
    }
    if(labels.empty())return current;
    auto* draw=ImGui::GetWindowDrawList();
    PaddockPanelStyle frame;
    frame.radii=PaddockRound(12.F);frame.fill=PaddockRgb(0x031C2C);
    frame.ring=PaddockRgb(0x316782);frame.ring_width=1.F;
    PaddockPanel(draw,origin,{origin.x+total,origin.y+y+height+4.F},frame);
    int selected=current;
    ImGui::PushID(id);
    for(std::size_t i=0;i<labels.size();++i) {
        ImGui::SetCursorScreenPos(positions[i]);
        ImGui::PushID(int(i));
        const auto press=PaddockBeginPress(labels[i],{widths[i],height});
        if(press.pressed)selected=int(i);
        const float on=PaddockEase(PaddockTween(PaddockKey("category",press.id),current==int(i),.15F,.15F));
        const auto fill=PaddockMix(PaddockMix(PaddockRgb(0x244353,0U),PaddockRgb(0x244353),press.hover),PaddockRgb(0x087C91),on);
        if(on>0)PaddockFill(draw,{press.min.x,press.min.y+2.F},{press.max.x,press.max.y+2.F},PaddockRound(8.F),PaddockCol(0x03131E,unsigned(255.F*on)));
        PaddockFill(draw,press.min,press.max,PaddockRound(8.F),PaddockApply(fill));
        if(on>0){draw->PushClipRect(press.min,{press.max.x,press.min.y+2.F},true);PaddockFill(draw,press.min,press.max,PaddockRound(8.F),PaddockCol(0xFFFFFF,unsigned(36.F*on)));draw->PopClipRect();}
        if(press.hover>0)PaddockStroke(draw,press.min,press.max,PaddockRound(8.F),PaddockCol(0x7A9BA7,unsigned(255.F*press.hover)),1.F);
        const auto ink=PaddockApply(PaddockMix(PaddockRgb(0xBED9E5),PaddockRgb(0xFFF4CE),on));
        const float count=show_counts?std::max(23.F,PaddockMeasure(badge,counts[i])+12.F):0;
        const auto label=PaddockEllipsize(type,dkr::localisation::source(labels[i]),std::max(1.F,widths[i]-28.F-(show_counts?count+9.F:0)));
        const float top=press.min.y+8.F;
        draw->PushClipRect(press.min,press.max,true);
        PaddockDrawRun(draw,type,{press.min.x+14.F,top+2.F},PaddockCol(0x031623),label.data(),label.data()+label.size());
        PaddockDrawRun(draw,type,{press.min.x+14.F,top},ink,label.data(),label.data()+label.size());
        if(show_counts){const float bx=press.max.x-14.F-count,by=press.min.y+(height-badge.line)*.5F;PaddockFill(draw,{bx,by},{bx+count,by+badge.line},PaddockRound(5.F),PaddockCol(0xFFFFFF,13U));PaddockDrawRun(draw,badge,{bx+(count-PaddockMeasure(badge,counts[i]))*.5F,by},ink,counts[i].data(),counts[i].data()+counts[i].size());}
        draw->PopClipRect();
        PaddockEndPress(press,8.F);
        ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(origin);ImGui::Dummy({total,y+height+4.F});
    return selected;
}
