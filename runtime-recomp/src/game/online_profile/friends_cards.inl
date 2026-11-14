void DrawOlFriendCards(const std::vector<OlFriendRow>& rows, float width) {
    constexpr float gap = 12.F, minimum = 260.F;
    const int columns = width >= minimum * 3 + gap * 2 ? 3 : width >= minimum * 2 + gap ? 2 : 1;
    const float cell = std::floor((width - gap * (columns - 1)) / columns);
    const auto origin = ImGui::GetCursorScreenPos();
    float bottom = origin.y;
    for (std::size_t first = 0; first < rows.size(); first += columns) {
        const float top = first ? bottom + gap : origin.y;
        OlEqualProfileRow equal;
        for (std::size_t n = first; n < std::min(first + columns, rows.size()); ++n) {
            const auto& row = rows[n];
            ImGui::PushID(row.key.c_str());
            ImGui::SetCursorScreenPos({origin.x + float(n - first) * (cell + gap), top});
            ImGui::BeginGroup();
            equal.capture_next = true;
            equal.section = 0;
            DrawOlCard(cell, OlPanelLook{}, [&](float inner) {
                const auto at = ImGui::GetCursorScreenPos();
                auto* draw = ImGui::GetWindowDrawList();
                DrawOlProfileAvatar(draw, at, 38.F, row.name, row.avatar_url, row.dimmed);
                draw->AddCircleFilled({at.x + 32.F, at.y + 32.F}, 5.F, PaddockCol(row.dot), 12);
                const auto strong = OlRead(15.F, true, 1.5F), caption = OlRead(13.F);
                const float text_width = std::max(1.F, inner - 48.F);
                const float name_height = PaddockTextHeight(strong, row.label, text_width);
                PaddockTextStyle style;
                style.colour = PaddockCol(kOlCream);
                PaddockTextAt(draw, strong, {at.x + 48.F, at.y}, text_width, row.label, style);
                style.colour = PaddockCol(row.status_colour);
                PaddockTextAt(draw, caption, {at.x + 48.F, at.y + name_height + 3.F}, text_width, row.status, style);
                ImGui::Dummy(
                    {inner, std::max(42.F, name_height + 3.F + PaddockTextHeight(caption, row.status, text_width))});
                PaddockGap(12.F);
                float total = 0;
                for (const auto& action : row.actions)
                    total += OlButtonWidth(action.label.c_str(), action.tone) + 8.F;
                const bool stack = total > inner + 8.F;
                const auto actions = ImGui::GetCursorScreenPos();
                float y = actions.y;
                const float action_width = stack ? inner
                                                 : (inner - 8.F * float(row.actions.size() - 1)) /
                                                       float(std::max<std::size_t>(1, row.actions.size()));
                for (std::size_t a = 0; a < row.actions.size(); ++a) {
                    const auto& action = row.actions[a];
                    ImGui::PushID(int(a));
                    ImGui::SetCursorScreenPos(
                        {actions.x + (stack ? 0.F : float(a) * (action_width + 8.F)), stack ? y : actions.y});
                    {
                        const OlDisabled disabled(action.disabled);
                        if (OlButton(action.label.c_str(), action.tone, action_width, action.tooltip))
                            action.run();
                    }
                    if (stack)
                        y += 48.F;
                    ImGui::PopID();
                }
                ImGui::SetCursorScreenPos(actions);
                ImGui::Dummy({inner, stack ? std::max(0.F, y - actions.y - 8.F) : 40.F});
            });
            ImGui::EndGroup();
            bottom = std::max(bottom, ImGui::GetItemRectMax().y);
            ImGui::PopID();
        }
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, bottom - origin.y});
}
