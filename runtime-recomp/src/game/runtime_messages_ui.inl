void DrawColoredWrapped(const ImVec4& color, std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%.*s", static_cast<int>(text.size()), text.data());
    ImGui::PopStyleColor();
}

void DrawTechnicalDetails(std::string_view diagnostic, const char* label = "Details") {
    if (diagnostic.empty())
        return;
    ImGui::PushID(diagnostic.data(), diagnostic.data() + diagnostic.size());
    if (ImGui::CollapsingHeader(label)) {
        const float height = std::clamp(ImGui::GetIO().DisplaySize.y * 0.22F, 70.0F, 180.0F);
        ImGui::BeginChild("##technical-text", {0.0F, height}, true);
        PaddockText(PaddockReading(14.0F, false, 1.5F), PaddockRgb(0xB0C9CC), diagnostic,
                    std::max(ImGui::GetContentRegionAvail().x, 1.0F));
        ImGui::EndChild();
        if (ImGui::Button("Copy details"))
            ImGui::SetClipboardText(std::string(diagnostic).c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Details may include file paths or session information.");
    }
    ImGui::PopID();
}

void DrawIssue(std::string_view diagnostic, dkr::runtime::copy::Area area) {
    const auto summary = dkr::runtime::copy::error_summary(diagnostic, area);
    DrawColoredWrapped(kWarm, summary);
    DrawTechnicalDetails(diagnostic);
}

void DrawFriendlyStatus(std::string_view text, float width,
                        dkr::runtime::copy::Area area = dkr::runtime::copy::Area::General) {
    if (text.empty())
        return;
    if (dkr::runtime::copy::looks_like_error(text))
        DrawIssue(text, area);
    else
        PaddockFeedback(text, width);
}
