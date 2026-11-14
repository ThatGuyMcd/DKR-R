// All orange page actions share the same 56px sign and top edge.
void DrawSharedPageHeader(float width, const char* title, const std::string& subtitle, float action_width,
                          const std::function<void()>& actions) {
    const auto at = ImGui::GetCursorScreenPos();
    const bool side = width >= action_width + 340.F;
    ImGui::BeginGroup();
    DrawPageHeading(title);
    PaddockGap(9.F);
    PaddockText(PaddockReading(14.F, false, 1.55F), PaddockRgb(0xABC0CC), dkr::localisation::source(subtitle),
                side ? width - action_width - 24.F : width);
    ImGui::EndGroup();
    const float title_height = ImGui::GetItemRectSize().y;
    ImGui::SetCursorScreenPos({side ? at.x + width - action_width : at.x, side ? at.y : at.y + title_height + 12.F});
    ImGui::BeginGroup();
    actions();
    ImGui::EndGroup();
    const float action_height = ImGui::GetItemRectSize().y;
    const float height = side ? std::max(title_height, action_height) : title_height + 12.F + action_height;
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, height});
    PaddockGap(12.F);
}
