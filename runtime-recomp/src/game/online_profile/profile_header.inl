void DrawOlProfilePresence(float width, const OnlineFrame& frame) {
    const auto& service = dkr::runtime::netplay::friend_service();
    const auto online = dkr::online::view();
    const char* status = !online->signed_in         ? "Not signed in"
                         : !online->enabled         ? "Offline"
                         : service.appear_offline() ? "Friends see you as offline"
                         : frame.in_lobby           ? "Online - in a lobby"
                         : online->connected        ? "Online"
                                                    : "Connecting...";
    OlParagraph(status, 13.F, online->connected ? kOlGoText : kOlSoft, width);
}

void DrawOlProfileIdentity(float width, const OnlineFrame& frame) {
    const auto account = dkr::store::service().view();
    const auto at = ImGui::GetCursorScreenPos();
    constexpr float size = 56.F;
    DrawOlSelfAvatar(ImGui::GetWindowDrawList(), at, size, g_online_player_name);
    ImGui::SetCursorScreenPos({at.x + size + 14.F, at.y});
    ImGui::BeginGroup();
    const float text_width = std::max(1.F, width - size - 14.F);
    OlParagraph(g_online_player_name, 18.F, kOlInk, text_width, true);
    const auto role = dkr::store::text(account->user, "role", 32);
    if (!role.empty())
        OlParagraph(role == "admin"       ? "Administrator"
                    : role == "moderator" ? "Moderator"
                                          : "Player",
                    12.5F, kOlSoft, text_width);
    DrawOlProfilePresence(text_width, frame);
    ImGui::EndGroup();
    const float height = std::max(size, ImGui::GetItemRectSize().y);
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, height});
}

// The sidebar displays the same saved identity; editing lives in Online Profile.
void DrawOlProfileCard(float width, const OnlineFrame& frame, bool) {
    DrawOlCard(width, OlPanelLook{}, [&](float inner) {
        DrawOlProfileIdentity(inner, frame);
        PaddockGap(14.F);
        {
            const OlDisabled locked(frame.active);
            DrawOlDirectoryAccount(inner);
        }
        PaddockGap(12.F);
        const auto snapshot = dkr::runtime::netplay::friend_service().snapshot();
        const auto code = OlPermanentFriendCode(*snapshot);
        OlParagraph("Your friend code", 12.F, kOlSoft, inner);
        OlParagraph(code ? code->code : "No code yet", 15.F, kOlAmber, inner, true);
        PaddockGap(6.F);
        if (OlButton(code ? "Copy friend code" : "Create friend code", OlTone::Plain, inner)) {
            if (code) {
                CopyFriendCodeToClipboard(code->code);
                OlNotify("Friend code copied.");
            } else {
                dkr::runtime::netplay::FriendInviteView invite{};
                std::string error;
                if (dkr::runtime::netplay::friend_service().create_invite(
                        dkr::runtime::netplay::FriendInviteLifetime::Permanent, std::chrono::minutes(60), invite,
                        error)) {
                    CopyFriendCodeToClipboard(invite.code);
                    OlNotify("Friend code created and copied.");
                } else
                    OlNotify(error);
            }
        }
        if (g_online_page.section == OlSection::Lobbies && g_ol_page_width > 1000.F) {
            PaddockGap(12.F);
            DrawOlLobbyFilters(inner);
        }
    });
}

void DrawOlProfileHeader(float width, const OnlineFrame& frame) {
    const auto account = dkr::store::service().view();
    OlPanelLook look;
    look.gradient_degrees = 160.F;
    look.gradient = {{0.F, 0x0F3D5C}, {.75F, kOlPanel}};
    DrawOlCard(width, look, [&](float inner) {
        const bool side = inner >= 680.F;
        const float left_width = side ? std::floor((inner - 24.F) * .57F) : inner;
        const float right_width = side ? inner - left_width - 24.F : inner;
        const auto at = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        DrawOlProfileIdentity(left_width, frame);
        PaddockGap(16.F);
        DrawOlDisplayName(left_width, frame.active);
        ImGui::EndGroup();
        const float left = ImGui::GetItemRectSize().y;
        ImGui::SetCursorScreenPos(side ? ImVec2{at.x + left_width + 24.F, at.y} : ImVec2{at.x, at.y + left + 16.F});
        ImGui::BeginGroup();
        {
            const OlDisabled locked(frame.active || account->loading || account->writing);
            if (OlButton("Save profile", OlTone::Strong, right_width))
                SaveOlDisplayName(g_online_profile_name);
        }
        PaddockGap(10.F);
        {
            const OlDisabled locked(!account->user.is_object());
            DrawOlPictureControls(right_width, frame.active);
        }
        PaddockGap(10.F);
        {
            const OlDisabled locked(frame.active || account->loading || account->writing ||
                                    !account->user.is_object() || !account->user.contains("discordProfile"));
            if (OlButton("Use Discord profile...", OlTone::Plain, right_width))
                g_open_discord_profile = true;
        }
        ImGui::EndGroup();
        const float right = ImGui::GetItemRectSize().y;
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({inner, side ? std::max(left, right) : left + 16.F + right});
        PaddockGap(12.F);
        DrawOlDisplayNameStatus(inner);
        PaddockGap(16.F);
        const auto rule = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(rule, {rule.x + inner, rule.y}, PaddockCol(0x28566C), 1.F);
        ImGui::Dummy({inner, 1.F});
        PaddockGap(14.F);
        if (account->user.is_object() && frame.launcher) {
            const auto session_at = ImGui::GetCursorScreenPos();
            const float half = side ? std::floor((inner - 24.F) * .5F) : inner;
            ImGui::BeginGroup();
            DrawOlRememberSignIn(half, frame);
            ImGui::EndGroup();
            const float remember_height = ImGui::GetItemRectSize().y;
            ImGui::SetCursorScreenPos(side ? ImVec2{session_at.x + half + 24.F, session_at.y}
                                           : ImVec2{session_at.x, session_at.y + remember_height + 12.F});
            ImGui::BeginGroup();
            DrawOlSignOut(side ? inner - half - 24.F : inner, frame);
            ImGui::EndGroup();
            const float signout_height = ImGui::GetItemRectSize().y;
            ImGui::SetCursorScreenPos(session_at);
            ImGui::Dummy(
                {inner, side ? std::max(remember_height, signout_height) : remember_height + 12.F + signout_height});
            PaddockGap(14.F);
        }
        {
            const OlDisabled locked(frame.active);
            DrawOlDirectoryAccount(inner);
        }
    });
}

void DrawOlProfileSections(float width, const OnlineFrame& frame) {
    {
        const OlEnter enter(0);
        DrawOlProfileHeader(width, frame);
    }
    constexpr float gap = 20.F;
    PaddockGap(gap);
    const bool side = width >= 660.F;
    const float card_width = side ? std::floor((width - gap) * .5F) : width;
    const auto at = ImGui::GetCursorScreenPos();
    float bottom = at.y;
    if (side) {
        OlEqualProfileRow equal;
        for (int section = 1; section <= 2; ++section) {
            equal.capture_next = true;
            equal.section = section;
            ImGui::SetCursorScreenPos(section == 1 ? at : ImVec2{at.x + card_width + gap, at.y});
            ImGui::BeginGroup();
            {
                const OlEnter enter(section);
                if (section == 1)
                    DrawOlPrivacy(card_width);
                else
                    DrawOlFriendCodes(card_width);
            }
            ImGui::EndGroup();
            bottom = std::max(bottom, ImGui::GetItemRectMax().y);
        }
    } else {
        {
            const OlEnter enter(1);
            DrawOlPrivacy(width);
        }
        PaddockGap(gap);
        {
            const OlEnter enter(2);
            DrawOlFriendCodes(width);
        }
        bottom = ImGui::GetCursorScreenPos().y;
    }
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, bottom - at.y});
    PaddockGap(gap);
    DrawOlAccountSettings(width, frame);
}
