void DrawCloudSaves(float width, bool locked) {
    auto& service = dkr::store::service();
    const auto view = service.view();
    const auto account = dkr::store::text(view->user, "id", 160);
    if (account.empty()) {
        SavesNote("Sign in with Discord to sync saves between your devices.", width);
        if (SavesRaceButton("SIGN IN WITH DISCORD", 0, SavesTone::Primary, locked || view->signing_in))
            service.login();
        return;
    }
    const bool enabled = view->cloud_saves_account == account;
    SavesNote(
        "Existing cloud saves take priority. Progress made from a synced save is uploaded when its cloud copy is current.",
        width);
    PaddockGap(12);
    if (SavesRaceButton(enabled ? "PAUSE CLOUD SAVES" : "ENABLE CLOUD SAVES", 0,
                        enabled ? SavesTone::Plain : SavesTone::Primary, locked)) {
        ToggleCloudSaves(account, !enabled);
    }
    if (!enabled)
        return;
    PaddockGap(12);
    if (SavesRaceButton("SYNC NOW", 0, SavesTone::Primary, locked))
        RunCloudSaveSync(account);
    if (!view->cloud_saves_status.empty())
        SavesNote(view->cloud_saves_status, width);
}