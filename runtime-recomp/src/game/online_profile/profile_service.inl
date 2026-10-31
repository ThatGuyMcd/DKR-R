// Members of the existing background account service. No file decoding or HTTP
// request runs in an ImGui callback.
std::string profile_preview_png, profile_preview_key;
unsigned profile_preview_account = 0;

void profile_action(const std::string& name, const Json& input, unsigned epoch, const std::string& credential,
                    std::stop_token stop) {
    const auto current = [&] {
        std::lock_guard lock(mutex);
        return account == epoch && token == credential && !stop.stop_requested();
    };
    const auto finish = [&](Json result, const char* message) {
        update([&](Snapshot& s) {
            if (account != epoch || token != credential || stop.stop_requested())
                return;
            s.last_action = name;
            s.last_result = std::move(result);
            ++s.mutation;
            s.error.clear();
            s.status = message;
        });
    };
    if (!current())
        return;
    if (name == "launcher.profile.preview") {
        const auto picture = dkr::profile::read_picture(std::filesystem::u8path(text(input, "file", 32768)));
        const auto key = "profile-preview:" +
                         dkr::updates::hash(
                             std::span(reinterpret_cast<const std::uint8_t*>(picture.png.data()), picture.png.size())) +
                         ":" + std::to_string(epoch);
        auto image = std::make_shared<Image>();
        image->width = image->height = dkr::profile::picture_size;
        image->rgba = picture.rgba;
        {
            std::lock_guard lock(mutex);
            if (account != epoch || token != credential || stop.stop_requested())
                return;
            if (!profile_preview_key.empty())
                images.erase(profile_preview_key);
            if (images.size() >= 32)
                images.erase(images.begin());
            images[key] = image;
            profile_preview_png = picture.png;
            profile_preview_key = key;
            profile_preview_account = epoch;
        }
        finish({{"previewKey", key}}, "Picture prepared. Save Picture to use it.");
        return;
    }
    const auto card = dkr::profile::bridge::card();
    if (card.public_key.empty() || card.friend_code.empty())
        throw Error("Your online profile is not ready yet. Try again in a moment.");
    const auto request = [&](const std::string& path, const Json* body = nullptr) {
        if (!current())
            throw Error("The signed-in account changed. Reopen your profile.");
        try {
            return dkr::profile::profile_request<Error>(
                [&] {
                    return document({std::string(Api) + path, body ? "POST" : "GET",
                                     body ? body->dump() : std::string{}, credential},
                                    stop);
                },
                [&](std::chrono::milliseconds delay) {
                    std::mutex pause_mutex;
                    std::condition_variable_any pause;
                    std::unique_lock lock(pause_mutex);
                    pause.wait_for(lock, stop, delay, [] { return false; });
                },
                current);
        } catch (const Error& error) {
            if (error.status == 401)
                expire(credential);
            if (error.status == 404)
                throw Error(
                    "Profile pictures are unavailable on this service. Please try again after the service is updated.",
                    error.status);
            throw;
        }
    };
    const auto previous = request("/v1/launcher/profile?key=" + card.public_key);
    Json body = {{"publicKey", card.public_key},
                 {"identity", card.identity},
                 {"friendCode", card.friend_code},
                 {"source", text(input, "source", 16)},
                 {"sharing", input.value("sharing", true)}};
    if (text(previous, "publicKey", 64) != card.public_key) {
        const Json query = {{"publicKey", card.public_key}};
        const auto challenge = request("/v1/launcher/profile-challenge", &query);
        const auto proof = dkr::profile::bridge::proof(text(challenge, "publicKey", 64));
        if (proof.size() != 64)
            throw Error("The launcher profile could not be confirmed. Try again.");
        body["challenge"] = text(challenge, "id", 43);
        body["proof"] = proof;
    }
    if (body["source"] == "custom") {
        std::lock_guard lock(mutex);
        if (account != epoch || token != credential || profile_preview_account != epoch ||
            profile_preview_key.empty() || text(input, "previewKey", 160) != profile_preview_key ||
            profile_preview_png.empty())
            throw Error("Choose your profile picture again before saving it.");
        body["picture"] = base64(
            std::span(reinterpret_cast<const std::uint8_t*>(profile_preview_png.data()), profile_preview_png.size()));
    }
    const auto result = request("/v1/launcher/profile", &body);
    const auto avatar = text(result, "avatarUrl", 180);
    const auto local = published.load();
    if (!dkr::profile::shared_avatar(avatar) || text(result, "publicKey", 64) != card.public_key ||
        text(result, "accountId", 80) != text(local->user, "id", 80) || !result.contains("sharing") ||
        !result["sharing"].is_boolean())
        throw Error("The profile service returned invalid profile details.");
    {
        std::lock_guard lock(mutex);
        if (account != epoch || token != credential || stop.stop_requested())
            return;
        state.user["launcherProfile"] = result;
        if (body["source"] == "custom" && !profile_preview_key.empty()) {
            if (const auto image = images.find(profile_preview_key); image != images.end())
                images[avatar] = image->second;
        }
    }
    dkr::online::refresh_profile();
    finish(result, "Online profile saved.");
}
