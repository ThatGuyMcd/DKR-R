void DrawOlJoiningProgress(float width, const OnlineFrame& frame) {
    if (frame.view.host)
        return; // The host retains the existing approval cards.
    const auto rows = dkr::online_stability::control(&dkr::runtime::netplay::session())->joins();
    if (rows.empty())
        return;
    DrawOlFeature(width, "Joining players", "Session preparation is approved by the host.", [&](float inner) {
        for (const auto& row : rows) {
            ImGui::PushID(std::to_string(row.request).c_str());
            OlParagraph(row.name, 16.F, kOlCream, inner, true);
            OlParagraph(dkr::online_stability::stage_text(row.stage), 14.F, kOlSoft, inner);
            if (row.total && (row.stage == dkr::online_stability::Stage::Downloading ||
                              row.stage == dkr::online_stability::Stage::Queued)) {
                char label[96]{};
                std::snprintf(label, sizeof(label), "%.1f / %.1f MiB", double(row.received) / (1024 * 1024),
                              double(row.total) / (1024 * 1024));
                ImGui::ProgressBar(std::clamp(float(row.received) / float(row.total), 0.F, 1.F), {inner, 24.F}, label);
            }
            PaddockGap(8.F);
            ImGui::PopID();
        }
    });
    PaddockGap(16.F);
}
void DrawOlLiveDelay(float width) {
    const auto view = dkr::online_stability::control(&dkr::runtime::netplay::session())->delay();
    if (!view.requested)
        return;
    const auto text = std::string("Dynamic Input Delay: ") + std::to_string(view.frames) + " frames (" +
                      std::to_string((view.frames * 1000 + 15) / 30) + " ms added) - " + view.reason;
    OlParagraph(text, 14.F, kOlSoft, width);
}
