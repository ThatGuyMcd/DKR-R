// Account actions reuse the shared service and its existing typed confirmation.
void DrawOlRememberSignIn(float width, const OnlineFrame& frame) {
    const auto account = dkr::store::service().view();
    const OlDisabled locked(frame.active || account->loading || account->writing || account->signing_in);
    bool remember = account->remember;
    if (OlSwitchRow("##account-remember", "Keep me signed in",
                    "Applies when you sign in. Switching off removes the saved sign-in.", &remember, width, false))
        dkr::store::service().remember(remember);
    PaddockGap(6.F);
    OlParagraph(account->remember ? "If you just enabled this, sign out and sign in once to save this device."
                                  : "Signed in for this session.",
                12.5F, kOlSoft, width);
}

void DrawOlSignOut(float width, const OnlineFrame& frame) {
    const auto account = dkr::store::service().view();
    const OlDisabled locked(frame.active || account->loading || account->writing);
    if (OlButton("Sign out", OlTone::Plain, width))
        StoreNavigate([] { dkr::store::service().logout(); });
}

void DrawOlAccountSettings(float width, const OnlineFrame& frame) {
    if (!frame.launcher)
        return;
    const auto account = dkr::store::service().view();
    ImGui::PushID("online-account-settings");
    DrawOlCard(width, OlPanelLook{}, [&](float inner) {
        const bool side = inner >= 640.F;
        const float half = side ? std::floor((inner - 24.F) * .5F) : inner;
        const auto at = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        OlHeading("ACCOUNT SETTINGS", 22.F, kOlCream, half);
        PaddockGap(10.F);
        OlParagraph(
            "Installed mods and your Discord account are kept when deleting a library account. Confirmation is required.",
            12.5F, kOlSoft, half);
        ImGui::EndGroup();
        const float left = ImGui::GetItemRectSize().y;
        ImGui::SetCursorScreenPos(side ? ImVec2{at.x + half + 24.F, at.y} : ImVec2{at.x, at.y + left + 16.F});
        ImGui::BeginGroup();
        if (OlButton("Privacy information", OlTone::Plain, half))
            SDL_OpenURL("https://positron-portal.com/dkr-r/privacy.html");
        if (account->user.is_object()) {
            PaddockGap(10.F);
            const OlDisabled locked(frame.active || account->loading || account->writing);
            if (OlButton("Delete library account...", OlTone::Danger, half)) {
                StoreSyncIdentity(*account);
                g_mod_store.fields["account-delete"].clear();
                StoreOpenPopup("Delete your library account?");
            }
        }
        ImGui::EndGroup();
        const float right = ImGui::GetItemRectSize().y;
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({inner, side ? std::max(left, right) : left + 16.F + right});
        if (account->status.starts_with("Library account deleted")) {
            PaddockGap(10.F);
            OlParagraph(account->status, 13.F, kOlGoText, inner);
        }
    });
    ImGui::PopID();
}
