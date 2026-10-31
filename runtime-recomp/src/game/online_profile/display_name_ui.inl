#include "display_name_save.hpp"

dkr::profile::DisplayNameSave g_display_name_save;
std::string g_display_name_account, g_display_name_remote, g_display_name_error;
bool g_display_name_initialized = false;

bool ApplyOlDisplayName(const std::string& name) {
    if (!dkr::profile::complete_display_name(name))
        return false;
    std::string error;
    if (!dkr::runtime::netplay::friend_service().set_display_name(name, error)) {
        g_display_name_error = error;
        return false;
    }
    std::snprintf(g_online_player_name, sizeof(g_online_player_name), "%s", name.c_str());
    SaveSettings();
    return true;
}

void SyncOlDisplayName() {
    const auto account = dkr::store::service().view();
    const auto uid = dkr::store::text(account->user, "id", 80);
    const auto remote = dkr::store::text(account->user, "displayName", 200);
    const bool active = dkr::runtime::netplay::session().presentation_active();
    const bool changed_account = !g_display_name_initialized || uid != g_display_name_account;
    if (changed_account) {
        g_display_name_initialized = true;
        g_display_name_account = uid;
        g_display_name_remote.clear();
        g_display_name_error.clear();
        g_display_name_save.pending = false;
        std::snprintf(g_online_profile_name, sizeof(g_online_profile_name), "%s", g_online_player_name);
    }
    const auto result = g_display_name_save.settle(
        uid, account->mutation, account->last_action, dkr::store::text(account->last_result, "id", 80),
        dkr::store::text(account->last_result, "displayName", 200), account->loading || account->writing);
    if (result == dkr::profile::DisplayNameSave::Result::Saved) {
        g_display_name_error.clear();
        if (!active && ApplyOlDisplayName(g_display_name_save.name)) {
            std::snprintf(g_online_profile_name, sizeof(g_online_profile_name), "%s", g_display_name_save.name.c_str());
            OlNotify("Profile saved.");
        }
    } else if (result == dkr::profile::DisplayNameSave::Result::Failed) {
        g_display_name_error = account->error.empty()
                                   ? "Your name could not be saved. Your typed name is kept; try Save profile again."
                                   : account->error;
    }
    // Defer account refreshes during a lobby; never rename an admitted racer.
    if (!uid.empty() && remote != g_display_name_remote && !active && !g_display_name_save.pending) {
        if (dkr::profile::complete_display_name(remote)) {
            const bool clean_draft = changed_account ||
                                     std::string_view(g_online_profile_name) == g_display_name_remote ||
                                     std::string_view(g_online_profile_name) == g_online_player_name;
            if (std::string_view(g_online_player_name) == remote || ApplyOlDisplayName(remote)) {
                if (clean_draft)
                    std::snprintf(g_online_profile_name, sizeof(g_online_profile_name), "%s", remote.c_str());
                g_display_name_remote = remote;
            }
        } else {
            g_display_name_remote = remote;
            g_display_name_error =
                "Choose a name of at least two characters that fits online play. Your existing account name is kept until you save.";
        }
    }
}

bool SaveOlDisplayName(std::string_view input) {
    const auto account = dkr::store::service().view();
    if (dkr::runtime::netplay::session().presentation_active()) {
        g_display_name_error = "Leave the lobby before changing your display name.";
        return false;
    }
    if (account->loading || account->writing || g_display_name_save.pending) {
        g_display_name_error = "Wait for the current profile request to finish.";
        return false;
    }
    const auto name = dkr::profile::display_name(input);
    const auto first = input.find_first_not_of(' '), last = input.find_last_not_of(' ');
    const auto trimmed = first == input.npos ? std::string_view{} : input.substr(first, last - first + 1);
    if (name != trimmed || !dkr::profile::complete_display_name(name)) {
        g_display_name_error =
            "Use at least two characters. If the name is too long for online play, shorten it before saving.";
        return false;
    }
    g_display_name_error.clear();
    const auto uid = dkr::store::text(account->user, "id", 80);
    std::snprintf(g_online_profile_name, sizeof(g_online_profile_name), "%s", name.c_str());
    if (uid.empty()) {
        if (!ApplyOlDisplayName(name))
            return false;
        OlNotify("Profile saved on this device. Sign in with Discord to use it for your account.");
    } else {
        g_display_name_save.begin(uid, name, account->mutation);
        dkr::store::service().action("account.profile", {{"displayName", name}});
    }
    return true;
}

void DrawOlDisplayName(float width, bool disabled) {
    const auto account = dkr::store::service().view();
    const OlDisabled locked(disabled || account->loading || account->writing || g_display_name_save.pending);
    OlInputOptions options;
    options.disabled = disabled || account->loading || account->writing || g_display_name_save.pending;
    const auto result = OlInput("##shared-display-name", "Display name", g_online_profile_name,
                                sizeof(g_online_profile_name), width, options);
    if (result == OlInputResult::Gamepad)
        RequestTextEntryKeyboard(TextEntryTarget::OnlineProfileName);
    PaddockGap(8.F);
    OlParagraph("Used in lobbies, friends and the Mod Store.", 12.5F, kOlSoft, width);
}

void DrawOlDisplayNameStatus(float width) {
    if (g_display_name_save.pending)
        OlParagraph("Saving profile...", 13.F, kOlSoft, width);
    else if (!g_display_name_error.empty())
        OlParagraph(g_display_name_error, 13.F, kOlError, width);
    else {
        const auto status = dkr::runtime::netplay::friend_service().status();
        if (status.starts_with("Cannot save Online Profile"))
            OlParagraph(status, 13.F, kOlError, width);
    }
}

void DrawOlStoreProfileName(float width, bool busy) {
    const bool active = dkr::runtime::netplay::session().presentation_active();
    DrawOlDisplayName(width, active || busy);
    PaddockGap(12.F);
    {
        const OlDisabled locked(active || busy);
        if (OlButton("Save profile", OlTone::Strong, width))
            SaveOlDisplayName(g_online_profile_name);
    }
    PaddockGap(8.F);
    DrawOlDisplayNameStatus(width);
}
