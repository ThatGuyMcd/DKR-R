#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

// Presentation only. Never replace the original diagnostic before error
// classification, logging, protocol handling or persisted content hashing.
namespace dkr::runtime::copy {
enum class Area { General, Online, Mods, Textures, Saves, Updates, Controller, Portable };

struct HeaderLayout {
    bool stacked;
    float action_x, action_y, height;
};
inline HeaderLayout play_header(float width, float action, float heading) {
    action = std::min(width, action);
    const bool stacked = width < action + 160.0F;
    return {stacked, stacked ? 0.0F : width - action,
            stacked ? heading + 12.0F : std::max(0.0F, (heading - 56.0F) * 0.5F),
            stacked ? heading + 12.0F + 56.0F : std::max(heading, 56.0F)};
}

inline std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

inline bool looks_like_error(std::string_view value) {
    const auto text = lowercase(value);
    for (const auto marker : {"could not", "couldn't", "cannot ", "can't ", "failed", "failure", "invalid",
                              "unsupported", "not supported", "stopped unexpectedly", "stopped safely", "cancelled",
                              "not available", "unavailable", "exceeds", "exceeded"}) {
        if (text.find(marker) != std::string::npos)
            return true;
    }
    return false;
}

inline Area runtime_error_area(std::string_view diagnostic) {
    const auto text = lowercase(diagnostic);
    if (text.find("save") != std::string::npos || text.find("eeprom") != std::string::npos)
        return Area::Saves;
    if (text.find("online") != std::string::npos || text.find("rollback") != std::string::npos)
        return Area::Online;
    if (text.find("custom") != std::string::npos || text.find("mods") != std::string::npos ||
        text.find("modded") != std::string::npos || text.find("mod ") != std::string::npos ||
        text.find("mod-") != std::string::npos)
        return Area::Mods;
    return Area::General;
}

inline std::string error_summary(std::string_view diagnostic, Area area) {
    const auto text = lowercase(diagnostic);
    const auto has = [&](std::string_view word) { return text.find(word) != std::string::npos; };
    if (has("paused"))
        return "Operation paused.";
    if (has("cancelled") || has("canceled"))
        return "Operation cancelled.";
    if (area == Area::Portable)
        return "Couldn't change portable mode. Check access to the application folder.";
    if (area == Area::Updates) {
        if (has("application was restored") || has("earlier application was restored"))
            return "The update couldn't start. Your previous version was restored.";
        if (has("rate limit") || has("limited update requests") || has("cooling down"))
            return "Too many update checks. Try again later.";
        if (has("signing identity"))
            return "This update doesn't match the installed app. Don't uninstall it to work around "
                   "this.";
        if (has("signature") || has("checksum") || has("verification") || has("verify") || has("unsafe") ||
            has("manifest") || has("unlisted") || has("changed"))
            return "The update couldn't be verified. Check Details before trying again.";
        if (has("ownership receipt") || has("qualified updater") || has("not an appimage"))
            return "Automatic installation isn't available here. Open Releases to update manually.";
        if (has("newer updater"))
            return "Open Releases to install this update manually.";
        if (has("another") && (has("instance") || has("updater")))
            return "Close other DKR-R copies before installing an update.";
        if ((has("android") && has("permission")) || has("allow dkr-r to install"))
            return "Allow DKR-R to install updates in Android settings, then try again.";
        if (has("newer android"))
            return "This update needs a newer Android version.";
        return "Couldn't complete the update. Check Details or open Releases.";
    }
    if (area == Area::Controller)
        return has("disconnect") ? "The controller disconnected. Reconnect it and try again."
                                 : "Couldn't set up this controller. Check Details and try again.";
    if (area == Area::Saves) {
        if (has("backup") && (has("create") || has("write")))
            return "Couldn't create a backup. Check access to the save folder.";
        if (has("online"))
            return "Couldn't sync the online save. Rejoin the lobby; don't replace your offline "
                   "save.";
        if (has("invalid") || has("checksum") || has("512-byte") || has("corrupt"))
            return "This save couldn't be used. Try a working save or backup.";
        return "Couldn't complete the save operation. Check Details before trying again.";
    }
    if (area == Area::Textures || area == Area::Mods) {
        if (has("source rom") || has("original game pak") || has("different original") || has("different rom"))
            return "This patch needs a different ROM version. Choose the required ROM and prepare "
                   "it again.";
        if (has("missing") && (has("worker") || has("importer") || has("helper")))
            return "A required application file is missing. Re-extract the complete DKR-R package.";
        if (has("time budget") || has("timed out"))
            return "Preparation timed out. Check Details and try again.";
        if (has("unsafe") || has("reserved") || has("colliding") || has("link") || has("encrypted"))
            return "This archive contains files DKR-R can't accept. Check Details or obtain a "
                   "supported pack.";
        if (has("limit") || has("too many") || has("exceed") || has("range is full"))
            return "This content exceeds a supported limit. Check Details; try fewer or smaller "
                   "mods.";
        if (has("save"))
            return "Couldn't use this mod selection's save. Check it in Save Manager before trying "
                   "again.";
        if (has("host connection") || has("disconnect"))
            return "Connection to the host ended. Rejoin the lobby.";
        if (has("adapter") || has("behaviour"))
            return "Some custom features aren't supported. Check the mod's Details.";
        return area == Area::Textures ? "Couldn't apply this texture pack. Check Details."
                                      : "Couldn't prepare these mods. Check Details and try again.";
    }
    if (area == Area::Online) {
        if (has("friend code") || has("invite") || has("invitation"))
            return "Couldn't use this code or invitation. Check Details in Online.";
        if (has("blocked"))
            return "This player is blocked. Check Details in Online.";
        return "Couldn't complete this online action. Check Details in Online.";
    }
    return "Couldn't complete this operation. Check Details.";
}

inline std::string progress(std::string_view diagnostic) {
    std::string value(diagnostic);
    for (const auto& [from, to] : {std::pair<std::string_view, std::string_view>{"Reconstructing locally owned assets",
                                                                                 "Preparing installed mods"},
                                   {"Reconstructing downloaded assets", "Preparing downloaded mods"},
                                   {"Host mods frozen and ready", "Lobby mods ready"},
                                   {"Checking prepared assets", "Checking mods"},
                                   {"Verifying runtime resources", "Checking mods"},
                                   {"Materializing", "Preparing"},
                                   {"Preparing legacy import", "Preparing import"},
                                   {"Import review ready", "Import ready"},
                                   {"Import stopped safely", "Import stopped"},
                                   {"Preparing texture-pack import", "Checking texture pack"}}) {
        if (const auto at = value.find(from); at != std::string::npos)
            value.replace(at, from.size(), to);
    }
    return value;
}

inline std::string mod_support(std::string_view diagnostic, bool behaviour) {
    const auto value = lowercase(diagnostic);
    if (value.find("remain unclassified") != std::string::npos || value.find("unresolved") != std::string::npos ||
        value.find("not supported") != std::string::npos)
        return "Some custom gameplay changes aren't supported. See Details.";
    return behaviour ? "Includes supported custom gameplay changes. See Details for limitations."
                     : "Uses original game behaviour. Prepare the source again to check custom features.";
}

// Explicit manual checks may reopen the result, even when this release was
// previously skipped/dismissed. Background checks retain their notification policy.
inline bool open_update_result(bool manual, bool available, bool skipped, bool seen, bool locked) {
    return manual || (available && !skipped && !seen && !locked);
}
} // namespace dkr::runtime::copy
