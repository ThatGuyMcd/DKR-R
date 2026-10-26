// Uses the retained launcher's native controls, cards, fonts and focus handling.
bool g_directory_friends_view = false;
const char* DirectoryPhase(const std::string& phase) {
    if (phase == "starting")
        return "Starting";
    if (phase == "testing")
        return "Testing connection";
    if (phase == "loading")
        return "Loading";
    if (phase == "racing")
        return "Racing";
    if (phase == "returning")
        return "Returning to lobby";
    return "Waiting";
}
void DrawOlDirectoryAccount(float inner) {
    const auto online = dkr::online::view();
    const auto account = dkr::store::service().view();
    OlParagraph(online->signed_in ? "Discord: " + online->account_name : "Sign in with Discord for public lobbies.",
                14.F, kOlSoft, inner);
    PaddockGap(8.F);
    if (!online->signed_in) {
        if (account->signing_in) {
            if (OlButton("Cancel sign-in"))
                dkr::store::service().cancel_login();
        } else if (OlButton("Sign in with Discord", OlTone::Go)) {
            dkr::online::connect(true);
            dkr::store::service().login();
        }
        bool remember = account->remember;
#ifdef _WIN32
        const bool secure_remember = true;
#else
        const bool secure_remember = dkr::online::keyring::available();
#endif
        if (secure_remember) {
            PaddockGap(8.F);
            if (OlSwitchRow("##directory-remember", "Keep me signed in",
                            "Protected by this computer's credential store.", &remember, inner, false))
                dkr::store::service().remember(remember);
        } else
            OlParagraph("Session-only sign-in. A desktop keyring is unavailable.", 12.5F, kOlSoft, inner);
        if (account->signing_in) {
            PaddockGap(8.F);
            OlParagraph(account->status, 12.5F, kOlSoft, inner);
        }
        if (!account->error.empty()) {
            PaddockGap(8.F);
            OlParagraph(account->error, 12.5F, kOlError, inner);
        }
    } else if (online->enabled && online->connected) {
        if (OlButton("Disconnect Online"))
            dkr::online::connect(false);
    } else if (online->enabled) {
        if (OlButton("Reconnect Online", OlTone::Go))
            dkr::online::connect(true);
    } else if (OlButton("Connect Online", OlTone::Go))
        dkr::online::connect(true);
    if (online->signed_in && account->status == "Signed in with Discord." && !account->error.empty()) {
        PaddockGap(8.F);
        OlParagraph(account->error, 12.5F, kOlError, inner);
    }
    if (!online->error.empty()) {
        PaddockGap(8.F);
        OlParagraph(online->error, 12.5F, kOlError, inner);
    }
    if (online->joining) {
        PaddockGap(8.F);
        OlParagraph("Contacting the host...", 14.F, kOlSoft, inner);
        if (OlButton("Cancel join"))
            dkr::online::cancel_join();
    }
}
const char* DirectoryVisibilityNote() {
    return dkr::online::view()->public_desired ? "Visible worldwide to signed-in players."
                                               : "Unlisted. Friends or a lobby code are required.";
}
float DirectoryVisibilityHeight(float width) {
    return OlParagraphHeight("Lobby visibility", 14.F, width, true, 1.4F) + 6.F + 44.F + 8.F +
           OlParagraphHeight(DirectoryVisibilityNote(), 15.F, width) + 10.F;
}
void DrawOlDirectoryVisibility(float width, bool disabled) {
    const auto snapshot = dkr::online::view();
    int value = snapshot->public_desired ? 1 : 0;
    {
        const OlDisabled locked(disabled);
        if (OlSelect("directory-visibility", "Lobby visibility", &value, {"Private", "Public"}, width)) {
            if (value && dkr::runtime::netplay::friend_service().appear_offline())
                OlNotify("Turn off Appear offline before publishing a public lobby.");
            else
                dkr::online::visibility(value != 0);
        }
    }
    PaddockGap(8.F);
    OlParagraph(DirectoryVisibilityNote(), 15.F, kOlSoft, width);
    PaddockGap(10.F);
}
void DrawOlPublicLobbies(float width, const OnlineFrame& frame) {
    const auto snapshot = dkr::online::view();
    DrawOlFeature(width, "Public lobbies", "Find players worldwide. The host still approves entry.", [&](float inner) {
        if (!snapshot->connected) {
            OlParagraph(snapshot->status, 15.F, kOlSoft, inner);
            return;
        }
        if (snapshot->rooms.empty()) {
            OlParagraph("No public lobbies are available. Create a lobby and choose Public to host one.", 15.F, kOlSoft,
                        inner);
            return;
        }
        for (const auto& lobby : snapshot->rooms) {
            const bool compatible = lobby.compatibility == dkr::online::compatibility();
            const bool can_join =
                frame.launcher && frame.rom_ready && !frame.active && !snapshot->joining && compatible;
            OlRoom room;
            room.id = "public-" + lobby.id;
            room.title = lobby.title;
            room.tag = DirectoryPhase(lobby.phase);
            room.summary = std::to_string(lobby.racers) + " / " + std::to_string(lobby.max_racers) + " racers";
            if (lobby.max_spectators)
                room.summary += "  |  " + std::to_string(lobby.spectators) + " / " +
                                std::to_string(lobby.max_spectators) + " spectators";
            room.note = "Hosted by " + lobby.host;
            if (!compatible)
                room.error = "A different DKR-R network build is required.";
            else if (lobby.locked)
                room.note += ". The lobby is locked.";
            else if (lobby.phase == "racing" && lobby.can_spectate)
                room.note += ". Spectator places are available.";
            else if (lobby.phase != "waiting")
                room.note += ". Joining is paused while the lobby changes state.";
            const bool watch = !lobby.can_join && lobby.can_spectate;
            room.action = watch ? "Spectate" : "Request to join";
            room.action_disabled = !can_join || (!lobby.can_join && !lobby.can_spectate);
            if (can_join && lobby.can_join && lobby.can_spectate)
                room.secondary = "Spectate";
            PaddockGap(16.F);
            const auto pressed = DrawOlRoom(room, inner);
            if (pressed == OlRoomPress::Action || pressed == OlRoomPress::Secondary)
                dkr::online::request_join(lobby.id, {}, watch || pressed == OlRoomPress::Secondary);
        }
    });
}
