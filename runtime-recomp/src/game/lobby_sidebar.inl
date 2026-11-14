// Native launcher cards, typography and control IDs are retained. This file
// changes placement only; directory discovery and join behavior stay owned by
// their existing handlers.
void DrawOlLobbyFilters(float width) {
    static const std::vector<std::string> roles{"Racer", "Watch Race"};
    int role = g_online_join_watching ? 1 : 0;
    if (OlSelect("browse-join-role", "Join as", &role, roles, width))
        g_online_join_watching = role != 0;
    PaddockGap(12.F);
    static const std::vector<std::string> sources{"Public", "Friends"};
    int source = g_directory_friends_view ? 1 : 0;
    if (OlSelect("directory-source", "Browse lobbies", &source, sources, width))
        g_directory_friends_view = source != 0;
}

void DrawOlSidebarReadiness(float width, const OnlineFrame& frame) {
    DrawOlCard(width, OlPanelLook{}, [&](float inner) {
        OlHeading("Before you race", 24.F, kOlCream, inner);
        PaddockGap(16.F);
        auto row = [&](int kind, const char* title, const std::string& description, const char* action, int target) {
            PaddockBox box(inner, {10.F, 10.F});
            const auto origin = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            DrawOlCheckIcon(draw, {origin.x + 14.F, origin.y + 14.F}, kind);
            const float text_width = std::max(1.F, box.Inner() - 40.F);
            ImGui::SetCursorScreenPos({origin.x + 40.F, origin.y});
            ImGui::BeginGroup();
            PaddockText(OlRead(15.F, true, 1.4F), PaddockRgb(0xFFFFFF), title, text_width, true);
            OlParagraph(description, 14.F, kind == 1 ? kOlError : kOlSoft, text_width);
            if (action) {
                PaddockGap(4.F);
                if (OlLink(action)) {
                    if (target == kPageModsHacks)
                        g_mods_page.entry_section = kModsSectionMagic;
                    g_page_navigation_request = target;
                }
            }
            ImGui::EndGroup();
            const float height = std::max(28.F, ImGui::GetItemRectMax().y - origin.y);
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy({box.Inner(), height});
            box.End([&](ImDrawList* target_draw, ImVec2 a, ImVec2 b) {
                PaddockFill(target_draw, a, b, PaddockRound(8.F), PaddockCol(kind == 1 ? 0x2E1F22U : 0x0D2F45U));
            });
        };
        row(frame.rom_ready ? 0 : 1, "Game ROM", frame.rom_ready ? "Loaded and ready" : "Not loaded yet",
            frame.rom_ready ? nullptr : "Choose ROM", kPagePlay);
        PaddockGap(8.F);
        unsigned codes = 0;
        for (auto bits = dkr::runtime::magic_codes::selected_mask(); bits; bits &= bits - 1U)
            ++codes;
        row(2, "Magic Codes", codes ? std::to_string(codes) + " enabled." : "None enabled.",
            frame.active ? nullptr : "Change", kPageModsHacks);
        PaddockGap(8.F);
        row(2, "Adventure save", "Uses the host's online save. Offline progress stays separate.", nullptr, -1);
        PaddockGap(16.F);
        static const std::vector<std::string> profiles{"Player 1 profile", "Player 2 profile", "Player 3 profile",
                                                       "Player 4 profile"};
        if (OlSelect("local-profile", "Your controls", &g_online_input_profile, profiles, inner, frame.active)) {
            g_online_input_profile = std::clamp(g_online_input_profile, 0, 3);
            dkr::runtime::platform::set_online_input_profile(static_cast<std::size_t>(g_online_input_profile));
            SaveSettings();
        }
    });
}
