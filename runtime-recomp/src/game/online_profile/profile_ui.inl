#include "display_name_ui.inl"
#include "account_settings.inl"

struct OlProfileTexture {
    SDL_Texture* texture = nullptr;
    std::shared_ptr<const dkr::store::Image> pixels;
    int last_frame = -1;
};
std::map<std::string, OlProfileTexture> g_profile_textures;
SDL_Renderer* g_profile_texture_renderer = nullptr;
bool g_open_discord_profile = false;
std::string g_profile_uid, g_profile_source = "keep", g_profile_preview_key, g_profile_wire, g_profile_attempted;
bool g_profile_dirty = false, g_profile_silent = false;
bool g_open_picture_editor = false, g_close_picture_editor = false, g_picture_share_before = true;
std::uint64_t g_profile_mutation = 0;
std::string g_profile_picture_after_name;
auto g_profile_renew_at = std::chrono::steady_clock::time_point{};

void ReleaseDiscordProfileTexture() {
    for (auto& [key, entry] : g_profile_textures)
        if (entry.texture)
            SDL_DestroyTexture(entry.texture);
    g_profile_textures.clear();
    g_profile_texture_renderer = nullptr;
}

void DrawOlProfileAvatar(ImDrawList* draw, ImVec2 at, float size, std::string_view name, const std::string& url,
                         bool dimmed) {
    DrawOlAvatar(draw, at, size, name, dimmed);
    const bool preview = url.starts_with("profile-preview:") && url.size() <= 160;
    if ((!dkr::profile::avatar_url(url) && !preview) || !g_mod_store_renderer ||
        !ImGui::IsRectVisible(at, {at.x + size, at.y + size}))
        return;
    auto& store = dkr::store::service();
    const auto pixels = store.image(url);
    if (!pixels) {
        if (!preview)
            store.request_image(url);
        return;
    }
    if (!pixels->width || !pixels->height || pixels->width > 512 || pixels->height > 512 ||
        pixels->rgba.size() != std::size_t(pixels->width) * pixels->height * 4)
        return;
    if (g_profile_texture_renderer != g_mod_store_renderer) {
        ReleaseDiscordProfileTexture();
        g_profile_texture_renderer = g_mod_store_renderer;
    }
    const int frame = ImGui::GetFrameCount();
    auto found = g_profile_textures.find(url);
    if (found == g_profile_textures.end()) {
        if (g_profile_textures.size() >= 48) {
            const auto victim = std::find_if(g_profile_textures.begin(), g_profile_textures.end(),
                                             [&](const auto& row) { return row.second.last_frame != frame; });
            if (victim == g_profile_textures.end())
                return;
            SDL_DestroyTexture(victim->second.texture);
            g_profile_textures.erase(victim);
        }
        found = g_profile_textures.emplace(url, OlProfileTexture{}).first;
    }
    auto& entry = found->second;
    if (entry.pixels != pixels && entry.last_frame != frame) {
        auto texture = SDL_CreateTexture(g_mod_store_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                         int(pixels->width), int(pixels->height));
        if (!texture)
            return;
        if (SDL_UpdateTexture(texture, nullptr, pixels->rgba.data(), int(pixels->width * 4))) {
            SDL_DestroyTexture(texture);
            return;
        }
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        if (entry.texture)
            SDL_DestroyTexture(entry.texture);
        entry.texture = texture;
        entry.pixels = pixels;
    }
    if (entry.texture) {
        entry.last_frame = frame;
        draw->AddImageRounded(reinterpret_cast<ImTextureID>(entry.texture), at, {at.x + size, at.y + size}, {0, 0},
                              {1, 1}, PaddockCol(0xFFFFFF, dimmed ? 170U : 255U), size * .5F);
        draw->AddCircle({at.x + size * .5F, at.y + size * .5F}, size * .5F - 1.F, PaddockCol(0xFFFFFF, 46U), 32, 2.F);
    }
}

void DrawOlSelfAvatar(ImDrawList* draw, ImVec2 at, float size, std::string_view name) {
    DrawOlProfileAvatar(draw, at, size, name, g_discord_avatar_url);
}

void SaveOlProfilePicture(bool silent) {
    auto& service = dkr::store::service();
    const auto account = service.view();
    const auto uid = dkr::store::text(account->user, "id", 80);
    if (uid.empty()) {
        if (g_profile_dirty)
            OlNotify("Sign in with Discord to save a profile picture.");
        return;
    }
    if (account->writing || account->loading) {
        if (!silent)
            OlNotify("Wait for the current profile request to finish.");
        return;
    }
    g_profile_silent = silent;
    service.action(
        "launcher.profile",
        {{"source", g_profile_source}, {"previewKey", g_profile_preview_key}, {"sharing", g_profile_share_picture}});
}

void SyncOlProfilePicture() {
    const auto account = dkr::store::service().view();
    const auto uid = dkr::store::text(account->user, "id", 80);
    if (uid != g_profile_uid) {
        g_profile_uid = uid;
        g_profile_wire.clear();
        g_profile_source = "keep";
        g_profile_preview_key.clear();
        g_profile_dirty = false;
        g_profile_attempted.clear();
        g_profile_mutation = account->mutation;
        dkr::profile::bridge::own_avatar({});
        g_profile_picture_after_name.clear();
    }
    if (uid.empty())
        return;
    if (!g_profile_picture_after_name.empty() && !g_display_name_save.pending && !account->writing &&
        !account->loading) {
        const bool apply = g_profile_picture_after_name == uid && g_display_name_error.empty();
        g_profile_picture_after_name.clear();
        if (apply)
            SaveOlProfilePicture(false);
    }
    if (account->mutation != g_profile_mutation) {
        g_profile_mutation = account->mutation;
        if (account->last_action == "launcher.profile.preview") {
            g_profile_preview_key = dkr::store::text(account->last_result, "previewKey", 160);
            if (!g_profile_preview_key.empty()) {
                g_profile_source = "custom";
                g_profile_dirty = true;
            }
        } else if (account->last_action == "launcher.profile" &&
                   dkr::store::text(account->last_result, "accountId", 80) == uid) {
            g_profile_source = "keep";
            g_profile_preview_key.clear();
            g_profile_dirty = false;
            g_profile_wire.clear();
            if (!g_profile_silent) {
                OlNotify("Online profile saved.");
                g_close_picture_editor = true;
            }
        }
    }
    const auto metadata =
        account->user.is_object() ? account->user.value("launcherProfile", dkr::store::Json{}) : dkr::store::Json{};
    const auto serialized = metadata.dump();
    if (!g_profile_dirty && serialized != g_profile_wire && !dkr::store::text(metadata, "publicKey", 64).empty()) {
        const auto avatar = dkr::store::text(metadata, "avatarUrl", 180);
        if (dkr::profile::shared_avatar(avatar) && metadata.contains("sharing") && metadata["sharing"].is_boolean()) {
            g_discord_avatar_url = avatar;
            g_profile_share_picture = metadata.value("sharing", true);
            g_profile_wire = serialized;
            dkr::profile::bridge::own_avatar(g_profile_share_picture ? avatar : std::string{});
            SaveSettings();
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (metadata.is_object() && !g_profile_dirty && !account->writing && !account->loading &&
        !dkr::runtime::netplay::session().presentation_active() &&
        (g_profile_attempted != uid || now >= g_profile_renew_at)) {
        g_profile_attempted = uid;
        g_profile_renew_at = now + std::chrono::hours(12);
        if (dkr::store::text(metadata, "publicKey", 64).empty() && g_discord_choice_account == uid &&
            dkr::profile::avatar_url(g_discord_avatar_url) && !dkr::profile::custom_avatar_url(g_discord_avatar_url))
            g_profile_source = "discord";
        SaveOlProfilePicture(true);
    }
}

void ChooseOlProfilePicture() {
    if (NFD_Init() != NFD_OKAY) {
        OlNotify("The picture picker could not open.");
        return;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"Profile picture", "png,jpg,jpeg"}};
    const auto selected = NFD_OpenDialogU8(&result, filters, 1, nullptr);
    std::filesystem::path path;
    if (selected == NFD_OKAY) {
        path = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
    } else if (selected == NFD_ERROR)
        OlNotify("The picture picker could not read the selected file.");
    NFD_Quit();
    if (path.empty())
        return;
    const auto bytes = path.u8string();
    dkr::store::service().action("launcher.profile.preview", {{"file", std::string(bytes.begin(), bytes.end())}});
}

void DrawOlPictureEditorControls(float width, bool active) {
    const auto account = dkr::store::service().view();
    const auto uid = dkr::store::text(account->user, "id", 80);
    const auto discord =
        account->user.is_object() ? account->user.value("discordProfile", dkr::store::Json{}) : dkr::store::Json{};
    const auto discord_avatar = dkr::store::text(discord, "avatarUrl", 180);
    PaddockGap(14.F);
    OlParagraph("Profile picture", 14.F, kOlSoft, width, true);
    PaddockGap(8.F);
    const std::string preview = g_profile_source == "custom"     ? g_profile_preview_key
                                : g_profile_source == "discord"  ? discord_avatar
                                : g_profile_source == "initials" ? std::string{}
                                                                 : g_discord_avatar_url;
    const auto at = ImGui::GetCursorScreenPos();
    DrawOlProfileAvatar(ImGui::GetWindowDrawList(), at, 64.F, g_online_player_name, preview);
    ImGui::Dummy({width, 72.F});
    {
        const OlDisabled disabled(active || uid.empty() || account->writing || account->loading);
        if (OlButton("Choose picture...", OlTone::Strong, width))
            ChooseOlProfilePicture();
        PaddockGap(8.F);
        {
            const OlDisabled unavailable(!dkr::profile::avatar_url(discord_avatar));
            if (OlButton("Use Discord picture", OlTone::Plain, width)) {
                g_profile_source = "discord";
                g_profile_dirty = true;
            }
        }
        PaddockGap(8.F);
        if (OlButton("Use initials", OlTone::Plain, width)) {
            g_profile_source = "initials";
            g_profile_preview_key.clear();
            g_profile_dirty = true;
        }
        PaddockGap(10.F);
        if (OlSwitchRow("##profile-share-picture", "Share my profile picture",
                        "Show it to friends and players in my lobby.", &g_profile_share_picture, width, false))
            g_profile_dirty = true;
    }
    PaddockGap(8.F);
    OlParagraph("PNG or JPG, up to 8 MB. Pictures are cropped to a square.", 12.F, kOlSoft, width);
    if (uid.empty())
        OlParagraph("Sign in with Discord to save your picture.", 12.F, kOlSoft, width);
    else if (!account->user.contains("launcherProfile"))
        OlParagraph("Profile pictures are unavailable on this service. Please try again after the service is updated.",
                    12.F, kOlAmber, width);
    if (!account->error.empty()) {
        PaddockGap(8.F);
        OlParagraph(account->error, 12.F, kOlError, width);
    }
}

void DrawOlPictureControls(float width, bool active) {
    const OlDisabled disabled(active);
    if (OlButton("Change profile picture...", OlTone::Plain, width)) {
        g_picture_share_before = g_profile_share_picture;
        g_close_picture_editor = false;
        g_open_picture_editor = true;
    }
}

void DrawOlPictureEditor() {
    constexpr const char* title = "Profile picture###online-picture-editor";
    if (std::exchange(g_open_picture_editor, false))
        ImGui::OpenPopup(title);
    if (BeginOlDialog(title)) {
        if (g_close_picture_editor) {
            g_close_picture_editor = false;
            ImGui::CloseCurrentPopup();
        } else {
            const auto account = dkr::store::service().view();
            const float width = std::max(1.F, ImGui::GetContentRegionAvail().x);
            DrawOlPictureEditorControls(width, false);
            PaddockGap(12.F);
            const OlDisabled disabled(account->writing || account->loading);
            const int action = DrawOlDialogActions({{"SAVE PICTURE"}, {"CANCEL"}});
            if (action == 0)
                SaveOlProfilePicture(false);
            else if (action == 1) {
                g_profile_source = "keep";
                g_profile_preview_key.clear();
                g_profile_dirty = false;
                g_profile_share_picture = g_picture_share_before;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void DrawDiscordProfilePrompt() {
    SyncOlDisplayName();
    SyncOlProfilePicture();
    if (dkr::runtime::netplay::session().presentation_active())
        return;
    DrawOlPictureEditor();
    if (ImGui::IsPopupOpen("Profile picture###online-picture-editor"))
        return;
    constexpr const char* title = "Use your Discord profile?";
    const auto account = dkr::store::service().view();
    const auto uid = dkr::store::text(account->user, "id", 80);
    const auto profile =
        account->user.is_object() ? account->user.value("discordProfile", dkr::store::Json{}) : dkr::store::Json{};
    const auto name = dkr::profile::display_name(dkr::store::text(profile, "displayName", 200));
    const auto avatar = dkr::store::text(profile, "avatarUrl", 180);
    static std::string prompted_account;
    static bool use_name = true, use_image = true;
    const bool ready = !uid.empty() && !name.empty() && dkr::store::text(account->user, "status", 16) == "active";
    if (ready && !ImGui::IsPopupOpen(title) && (g_open_discord_profile || uid != g_discord_choice_account)) {
        prompted_account = uid;
        use_name = true;
        use_image = dkr::profile::avatar_url(avatar);
        g_open_discord_profile = false;
        ImGui::OpenPopup(title);
    }
    if (BeginOlDialog(title)) {
        if (!ready || prompted_account != uid)
            ImGui::CloseCurrentPopup();
        else {
            ImGui::TextWrapped("Choose what to use for your DKR-R online profile. You can keep your current details.");
            ImGui::Dummy({0, 12});
            ImGui::Checkbox("Use Discord display name", &use_name);
            ImGui::TextWrapped("%s", name.c_str());
            ImGui::Dummy({0, 10});
            {
                const OlDisabled disabled(!dkr::profile::avatar_url(avatar));
                ImGui::Checkbox("Use Discord profile image", &use_image);
            }
            ImGui::Dummy({0, 12});
            const OlDisabled pending(account->writing || account->loading);
            const int action = DrawOlDialogActions({{"USE SELECTED"}, {"KEEP MY PROFILE"}});
            if (action >= 0) {
                if (action == 0 && use_name && !SaveOlDisplayName(name))
                    OlNotify(g_display_name_error);
                else {
                    if (action == 0 && use_image && dkr::profile::avatar_url(avatar)) {
                        g_profile_source = "discord";
                        g_profile_dirty = true;
                        if (g_display_name_save.pending)
                            g_profile_picture_after_name = uid;
                        else
                            SaveOlProfilePicture(false);
                    }
                    g_discord_choice_account = uid;
                    SaveSettings();
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }
}
