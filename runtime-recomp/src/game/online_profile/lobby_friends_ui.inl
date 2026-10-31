std::optional<dkr::online::SocialMember> OlLobbyProfile(std::string_view sender) {
    const auto directory = dkr::online::view();
    if (!directory->signed_in || !directory->connected)
        return std::nullopt;
    for (const auto& member : directory->members)
        if (member.sender == sender)
            return member;
    return std::nullopt;
}

bool OlLobbyFriendAction(std::string_view sender, bool you) {
    return !you && OlLobbyProfile(sender).has_value();
}

void DrawOlLobbyFriendAction(const std::string& sender, float width) {
    const auto profile = OlLobbyProfile(sender);
    if (!profile)
        return;
    auto& service = dkr::runtime::netplay::friend_service();
    const auto friends = service.snapshot();
    const auto existing = std::find_if(friends->friends.begin(), friends->friends.end(),
                                       [&](const auto& friend_) { return friend_.identity == profile->identity; });
    if (existing != friends->friends.end()) {
        const OlDisabled disabled(true);
        OlButton(existing->blocked ? "Blocked" : "Friends", OlTone::Plain, width);
        return;
    }
    for (const auto& request : friends->requests)
        if (request.identity == profile->identity) {
            if (request.incoming) {
                if (OlButton("Accept request", OlTone::Go, width)) {
                    std::string error;
                    if (!service.accept_request(request.request_id, error))
                        OlNotify(error);
                }
            } else {
                const OlDisabled disabled(true);
                OlButton("Request sent", OlTone::Plain, width);
            }
            return;
        }
    if (OlButton("Add Friend", OlTone::Strong, width)) {
        const auto current = OlLobbyProfile(sender);
        if (!current || current->public_key != profile->public_key ||
            !dkr::profile::bridge::verify(current->public_key, current->identity, current->friend_code)) {
            OlNotify("The lobby changed. Select the player again.");
            return;
        }
        std::string error;
        if (!service.submit_friend_code(current->friend_code, error))
            OlNotify(error);
        else
            OlNotify("Friend request queued. The other player will need to accept.");
    }
}

std::string OlLobbyAvatar(const std::string& sender, bool you) {
    if (you)
        return g_discord_avatar_url;
    const auto profile = OlLobbyProfile(sender);
    return profile ? profile->avatar : std::string{};
}
