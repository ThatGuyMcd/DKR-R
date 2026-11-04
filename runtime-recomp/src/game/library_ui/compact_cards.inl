// Only the grid's presentation changes. Listing data, navigation and install
// actions use the existing Library owners.
bool g_compact_card_capture=false;
std::string StoreGridTitle(const std::string& title,float width) {
    const auto type=PaddockSign(20.F,1.25F);
    const auto lines=PaddockWrap(type,title,width);
    if(lines.size()<=2)return title;
    std::string shown(lines[0].begin,lines[0].end);
    shown+='\n';
    shown+=PaddockEllipsize(type,std::string_view(lines[1].begin,title.data()+title.size()-lines[1].begin),width);
    return shown;
}

void StoreGridMetadata(const std::string& text,float width) {
    const auto type=PaddockReading(12.5F,false,1.45F);
    const auto shown=PaddockEllipsize(type,text,width);
    PaddockText(type,PaddockRgb(0xBDCFD9),shown,width);
    if(shown!=text&&ImGui::IsItemHovered())ImGui::SetTooltip("%s",text.c_str());
}

void StoreGridCard(const StoreJson& row,float width,float title_height) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0.F,0.F});
    {
        StorePanel card(width);
        const auto inner=card.inner();
        StoreCardCover(row,inner,std::clamp(inner*.53F,100.F,132.F),true);
        const auto title=StoreText(row,"title",200),shown=StoreGridTitle(title,inner);
        const auto at=ImGui::GetCursorScreenPos();
        const auto press=PaddockBeginPress("##title",{inner,title_height});
        PaddockTextStyle style;
        style.colour=PaddockCol(press.hover>.1F?0xFFCD67:0xFFF6DA);
        PaddockTextAt(ImGui::GetWindowDrawList(),PaddockSign(20.F,1.25F),at,inner,shown,style);
        PaddockEndPress(press,4.F);
        if(shown!=title&&(ImGui::IsItemHovered()||ImGui::IsItemFocused()))ImGui::SetTooltip("%s",title.c_str());
        if(press.pressed)StoreOpenMod(row,true);
        PaddockGap(6.F);
        StoreGridMetadata(dkr::localisation::format("library.by",{{"author",StoreText(row,"author",100)}}),inner);
        PaddockGap(5.F);
        StoreGridMetadata(dkr::localisation::format("library.counts",{
            {"downloads",dkr::store::community::count(row,"downloadCount")},
            {"comments",dkr::store::community::count(row,"commentCount")}}),inner);
        PaddockGap(8.F);
        const auto tags=ImGui::GetCursorScreenPos();
        StoreTags(row,inner);
        ImGui::SetCursorScreenPos({tags.x,std::max(ImGui::GetCursorScreenPos().y,tags.y+PaddockBadgeSize("x").y+8.F)});
        ImGui::Separator();PaddockGap(9.F);
        if(inner>=240.F) {
            const auto footer=ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos({footer.x,footer.y+12.F});
            StoreGridMetadata(StoreRating(row),inner-140.F);
            ImGui::SetCursorScreenPos({footer.x+inner-130.F,footer.y});
            StoreDownloadButton(row,StoreCurrentRelease(row),130.F,true);
            const float footer_height=std::max(44.F,ImGui::GetCursorScreenPos().y-footer.y);
            ImGui::SetCursorScreenPos(footer);ImGui::Dummy({inner,footer_height});
        }else {
            StoreGridMetadata(StoreRating(row),inner);PaddockGap(7.F);
            StoreDownloadButton(row,StoreCurrentRelease(row),inner,true);
        }
    }
    ImGui::PopStyleVar();
}
