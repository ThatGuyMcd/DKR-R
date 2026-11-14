// Members of FriendService::Impl. This extension never exposes its private key.
Capability profile_capability{};
std::uint64_t profile_capability_expires = 0;
std::map<std::string, std::string> profile_avatars;

dkr::profile::bridge::Card profile_card_locked() {
    if (!configured)
        return {};
    if (profile_capability == Capability{} || profile_capability_expires <= unix_seconds() + 300) {
        const auto random = secure::generate_key();
        std::copy_n(random.begin(), profile_capability.size(), profile_capability.begin());
        profile_capability_expires = unix_seconds() + 86400;
        save_locked();
    }
    const ParsedFriendCode code{fingerprint(identity.public_key), profile_capability, std::nullopt,
                                profile_capability_expires, FriendInviteLifetime::Timed};
    return {secure::encode_key(identity.public_key), local_identity, encode_friend_code(code)};
}

void bind_profile() {
    const auto weak = weak_from_this();
    const auto epoch = generation.load();
    dkr::profile::bridge::bind(
        {[weak, epoch] {
             const auto p = weak.lock();
             if (!p || p->generation.load() != epoch)
                 return dkr::profile::bridge::Card{};
             std::scoped_lock lock(p->mutex);
             if (p->generation.load() != epoch)
                 return dkr::profile::bridge::Card{};
             return p->profile_card_locked();
         },
         [weak, epoch](std::string_view server) -> std::string {
             const auto p = weak.lock();
             secure::Key key{}, shared{};
             if (!p || p->generation.load() != epoch || !secure::decode_key(server, key) || key == secure::Key{})
                 return {};
             std::scoped_lock lock(p->mutex);
             if (!p->configured || p->generation.load() != epoch)
                 return {};
             crypto_x25519(shared.data(), p->identity.secret.data(), key.data());
             if (shared == secure::Key{})
                 return {};
             auto result = secure::encode_key(shared);
             crypto_wipe(shared.data(), shared.size());
             return result;
         },
         [](std::string_view public_text, std::string_view claimed_identity, std::string_view code_text) {
             secure::Key key{};
             ParsedFriendCode code{};
             return secure::decode_key(public_text, key) && key != secure::Key{} &&
                    identity_of(key) == claimed_identity && parse_friend_code(code_text, code) && !code.invite_token &&
                    code.fingerprint == fingerprint(key);
         }});
    std::scoped_lock lock(mutex);
    for (const auto& [who, avatar] : profile_avatars)
        dkr::profile::bridge::peer_avatar(who, avatar);
}
