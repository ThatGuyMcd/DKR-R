#include "online_profile/profile_policy.hpp"
#include "online_profile/display_name_save.hpp"
#include "online_profile/linux_device_store.hpp"
#include "online_profile/profile_retry.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool condition) {
    ++checks;
    if (!condition)
        throw std::runtime_error("Online profile check " + std::to_string(checks) + " failed");
}
} // namespace
int main() try {
    using namespace dkr::profile;
    struct RequestError {
        unsigned status;
    };
    unsigned tries = 0, waits = 0;
    try {
        profile_request<RequestError>(
            [&]() -> int {
                ++tries;
                throw RequestError{503};
            },
            [&](auto) { ++waits; }, [] { return true; });
    } catch (const RequestError&) {
    }
    check(tries == 6 && waits == 5);
    for (const unsigned code : {400U, 401U, 403U, 404U, 409U, 429U}) {
        tries = waits = 0;
        try {
            profile_request<RequestError>(
                [&]() -> int {
                    ++tries;
                    throw RequestError{code};
                },
                [&](auto) { ++waits; }, [] { return true; });
        } catch (const RequestError&) {
        }
        check(tries == 1 && waits == 0);
    }
    tries = waits = 0;
    bool current = true;
    try {
        profile_request<RequestError>(
            [&]() -> int {
                ++tries;
                throw RequestError{0};
            },
            [&](auto) {
                ++waits;
                current = false;
            },
            [&] { return current; });
    } catch (const std::runtime_error&) {
    }
    check(tries == 1 && waits == 1);
    const std::string url =
        "https://cdn.discordapp.com/avatars/123456789012345678/0123456789abcdef0123456789abcdef.png?size=128";
    check(avatar_url(url));
    const auto custom = std::string(picture_origin) + std::string(64, 'a') + ".png";
    check(custom_avatar_url(custom));
    check(avatar_url(custom));
    check(!avatar_url(custom + "?token=private"));
    check(!avatar_url(std::string(picture_origin) + std::string(63, 'a') + ".png"));
    check(!avatar_url(std::string(picture_origin) + std::string(64, 'A') + ".png"));
    check(avatar_url(
        "https://cdn.discordapp.com/avatars/123456789012345678/a_0123456789abcdef0123456789abcdef.png?size=128"));
    check(avatar_url("https://cdn.discordapp.com/embed/avatars/5.png?size=128"));
    for (const auto& bad : {url + "&other=1", "http" + url.substr(5), url + "/../", url + "#fragment",
                            std::string("https://cdn.discordapp.com.evil/avatars/1/x.png?size=128"),
                            std::string("https://cdn.discordapp.com/embed/avatars/6.png?size=128"), std::string{}})
        check(!avatar_url(bad));
    check(display_name("  Racer  ") == "Racer");
    check(display_name("Racer\nName\r") == "RacerName");
    check(display_name(std::string(30, 'a')) == std::string(24, 'a'));
    check(display_name(std::string(23, 'a') + "\xC3\xA9") == std::string(23, 'a'));
    check(display_name(std::string(22, 'a') + "\xC3\xA9") == std::string(22, 'a') + "\xC3\xA9");
    check(display_name("\xED\xA0\x80").empty());
    check(display_name("\xF4\x90\x80\x80").empty());
    check(display_name("\xC0\xAF").empty());
    check(complete_display_name("ThatGuyMcd"));
    check(refresh_idle_name("OldName", "NewName", false, false, false));
    check(!refresh_idle_name("Name", "Name", false, false, false));
    check(!refresh_idle_name("OldName", "NewName", true, false, false));
    check(!refresh_idle_name("OldName", "NewName", false, true, false));
    check(!refresh_idle_name("OldName", "NewName", false, false, true));
    check(complete_display_name("mixedCASE Name"));
    check(complete_display_name(std::string(24, 'x')));
    check(complete_display_name(std::string(22, 'x') + "\xC3\xA9"));
    for (const auto& invalid :
         {std::string{}, std::string("a"), std::string(" a "), std::string("a\nb"), std::string(25, 'x'),
          std::string(23, 'x') + "\xC3\xA9", std::string("\xC3\xA9"), std::string("\xED\xA0\x80")})
        check(!complete_display_name(invalid));
    using NameResult = DisplayNameSave::Result;
    DisplayNameSave save;
    save.begin("account-a", "MixedCase", 4);
    check(save.settle("account-a", 4, "account.profile", "account-a", "MixedCase", true) == NameResult::Waiting);
    check(save.pending);
    check(save.settle("account-a", 5, "launcher.profile", "account-a", "MixedCase", true) == NameResult::Waiting);
    check(save.settle("account-a", 5, "account.profile", "account-b", "MixedCase", true) == NameResult::Waiting);
    check(save.settle("account-a", 5, "account.profile", "account-a", "OtherName", true) == NameResult::Waiting);
    check(save.settle("account-a", 5, "account.profile", "account-a", "MixedCase", true) == NameResult::Saved);
    check(!save.pending && save.name == "MixedCase");
    save.begin("account-a", "RetryMe", 6);
    check(save.settle("account-a", 6, "account.profile", "account-a", "MixedCase", false) == NameResult::Failed);
    check(!save.pending && save.name == "RetryMe");
    save.begin("account-a", "RetryMe", 6);
    check(save.settle("account-a", 7, "account.profile", "account-a", "RetryMe", false) == NameResult::Saved);
    save.begin("account-a", "OldAccount", 8);
    check(save.settle("account-b", 9, "account.profile", "account-a", "OldAccount", false) ==
          NameResult::AccountChanged);
    check(!save.pending);
    save.begin("account-a", "SignOut", 9);
    check(save.settle("", 10, "account.profile", "account-a", "SignOut", false) == NameResult::AccountChanged);
    check(save.settle("account-a", 11, "account.profile", "account-a", "SignOut", false) == NameResult::Waiting);
#ifndef _WIN32
    namespace local = dkr::profile::local_device;
    const auto root = std::filesystem::temp_directory_path() / ("dkrr-profile-check-" + std::to_string(getpid()));
    check(!std::filesystem::exists(root));
    check(local::load(root).empty());
    const std::string first(43, 'A'), second(43, 'B');
    check(local::store(root, first));
    check(local::load(root) == first);
    struct stat info {};
    check(stat((root / "device-session").c_str(), &info) == 0 && (info.st_mode & 0777) == 0700);
    check(stat((root / "device-session/device.token").c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    check(local::store(root, second));
    check(local::load(root) == second);
    check(!local::store(root, "bad-token"));
    check(local::load(root) == second);
    chmod((root / "device-session/device.token").c_str(), 0644);
    check(local::load(root).empty());
    chmod((root / "device-session/device.token").c_str(), 0600);
    local::clear(root);
    check(local::load(root).empty());
    std::ofstream(root / "target") << first;
    std::filesystem::create_symlink(root / "target", root / "device-session/device.token");
    check(local::load(root).empty());
    check(local::store(root, second));
    std::string target;
    std::ifstream(root / "target") >> target;
    check(target == first);
    chmod((root / "device-session").c_str(), 0755);
    check(!local::store(root, first));
    check(local::load(root).empty());
    chmod((root / "device-session").c_str(), 0700);
    local::clear(root);
    std::filesystem::remove_all(root);
#endif
    std::cout << checks << " online profile checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
