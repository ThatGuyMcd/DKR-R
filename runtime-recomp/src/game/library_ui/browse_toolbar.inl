void DrawLibraryBrowseToolbar(float width) {
    const auto at = ImGui::GetCursorScreenPos();
    const bool side = width >= 720.F;
    const float sort = side ? std::clamp(width * .18F, 200.F, 260.F) : width;
    const float search = side ? width - sort - 16.F : width;
    // Input, keyboard and combo share a 44-pixel control row. Labels are
    // separate so their height cannot move one control below its neighbours.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {ImGui::GetStyle().FramePadding.x, std::max(0.F, (44.F - ImGui::GetFontSize()) * .5F)});
    ImGui::BeginGroup();
    StoreField("search", "Search mods, creators or tags", search, 160, false, false, true);
    ImGui::EndGroup();
    const float search_height = ImGui::GetItemRectSize().y;
    ImGui::SetCursorScreenPos({side ? at.x + search + 16.F : at.x, side ? at.y : at.y + search_height + 8.F});
    ImGui::BeginGroup();
    g_mod_store.sort =
        StoreCombo("Sort by", g_mod_store.sort, {"Featured", "Name", "Top rated", "Recently updated"}, sort);
    ImGui::EndGroup();
    const float sort_height = ImGui::GetItemRectSize().y;
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, side ? std::max(search_height, sort_height) : search_height + 8.F + sort_height});
    PaddockGap(12.F);
}
