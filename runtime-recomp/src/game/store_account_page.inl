void StoreAccount(float width, const dkr::store::Snapshot& store) {
    width = std::min(width, 1120.F);
    StoreHeading(store.user.is_null() ? "Sign in" : "Your account", width);
    if (store.user.is_null()) {
        StorePanel panel(std::min(width, 650.F));
        StoreHeading("Sign in with Discord", panel.inner(), 22);
        StoreNote("Browse and install without an account. Sign in to review mods, send requests or use staff tools.",
                  panel.inner());
        if (store.signing_in) {
            StoreBadge("Waiting for browser sign-in", panel.inner());
            StoreNote("Finish signing in through your browser, then return here.", panel.inner());
            if (StoreButton("Cancel sign-in"))
                dkr::store::service().cancel_login();
        } else {
#ifdef _WIN32
            const bool can_remember = true;
#else
            const bool can_remember = dkr::online::keyring::available();
#endif
            if (can_remember) {
                bool remember = store.remember;
                if (StoreCheck("Keep me signed in on this PC", remember, panel.inner()))
                    dkr::store::service().remember(remember);
            } else
                StoreNote("Session-only sign-in. A desktop keyring is unavailable.", panel.inner());
            if (StoreButton("Continue with Discord", 0, true))
                dkr::store::service().login();
        }
        if (StoreButton("Privacy information"))
            SDL_OpenURL("https://positron-portal.com/dkr-r/privacy.html");
        return;
    }
    StoreHeading(StoreText(store.user, "displayName", 100), width, 22);
    const auto role = StoreText(store.user, "role", 32);
    StoreBadge(role == "admin" ? "Administrator" : role == "moderator" ? "Moderator" : "Player", width);
    g_mod_store.account_page = StoreTabLine({"Profile", "My reviews"}, g_mod_store.account_page, width, "account-tabs");
    if (g_mod_store.account_page == 1) {
        const auto reviews = store.personal.value("reviews", StoreJson::array());
        StoreField("account-review-search", "Find a review", std::min(width, 760.F), 200);
        std::size_t matching = 0;
        for (const auto& review : reviews) {
            const auto* item = StoreFind(StoreItems(store), StoreText(review, "itemId", 160));
            const auto title = item ? StoreText(*item, "title", 200) : "Unavailable listing";
            if (!StoreMatches(title + " " + StoreText(review, "text", 4000),
                              g_mod_store.fields["account-review-search"]))
                continue;
            if (++matching > std::size_t(StoreLimit("my-reviews")))
                continue;
            if (StoreRow(StoreText(review, "id", 160), item ? *item : StoreJson::object(), title,
                         std::to_string(StoreInt(review, "score")) + " / 5  |  Version " +
                             StoreText(review, "version", 80),
                         "View review", width) &&
                item) {
                StoreOpenMod(*item, true);
                g_mod_store.detail_tab = 1;
                g_mod_store.review_editing = true;
                g_mod_store.page = 0;
            }
        }
        if (!matching)
            StoreNote("No reviews match your search. Open a mod to leave one.", width);
        StoreMore("my-reviews", matching, width);
        return;
    }
    {
        StorePanel profile(std::min(width, 700.F));
        ImGui::BeginDisabled(StoreBusyAction(store));
        StoreField("profile-name", "Display name", profile.inner(), 60);
        ImGui::BeginDisabled(g_mod_store.fields["profile-name"].empty() ||
                             g_mod_store.fields["profile-name"] == StoreText(store.user, "displayName", 60));
        if (StoreButton("Save name", 0, true))
            StoreAction("account.profile", {{"displayName", g_mod_store.fields["profile-name"]}});
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        StoreActionLine(
            {{"My requests",
              [] {
                  g_mod_store.page = 1;
                  g_mod_store.request_page = 1;
              }},
             {"Privacy information", [] { SDL_OpenURL("https://positron-portal.com/dkr-r/privacy.html"); }}},
            profile.inner());
    }
    {
        StorePanel session(std::min(width, 700.F));
        StoreHeading("Session", session.inner(), 18);
#ifdef _WIN32
        StoreNote(store.remember ? "This PC remembers your sign-in." : "Signed in for this session.", session.inner());
#else
        StoreNote(store.remember ? "Your desktop keyring remembers this sign-in." : "Signed in for this session.",
                  session.inner());
#endif
        ImGui::BeginDisabled(StoreBusyAction(store));
        if (StoreButton("Sign out"))
            StoreNavigate([] { dkr::store::service().logout(); });
        ImGui::EndDisabled();
    }
    {
        StorePanel danger(std::min(width, 700.F));
        StoreHeading("Delete library account", danger.inner(), 18);
        StoreNote("Deletes your library account, not your installed mods or Discord account.", danger.inner());
        ImGui::BeginDisabled(StoreBusyAction(store));
        if (StoreButton("Delete library account")) {
            g_mod_store.fields["account-delete"].clear();
            StoreOpenPopup("Delete your library account?");
        }
        ImGui::EndDisabled();
    }
}
