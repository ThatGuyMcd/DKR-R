#include "launcher_copy_policy.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace dkr::runtime::copy;
    unsigned checks = 0;
    const auto check = [&](bool value, const char* reason) {
        ++checks;
        if (!value)
            throw std::runtime_error(reason);
    };
    try {
        const std::string raw = "Could not create portable.txt (Windows error 5).";
        check(error_summary(raw, Area::Portable).find("application folder") != std::string::npos, "Portable guidance");
        check(raw == "Could not create portable.txt (Windows error 5).", "Raw diagnostic must be retained");
        check(runtime_error_area("Experimental online play stopped safely") == Area::Online,
              "Online failures must not blame mods");
        check(runtime_error_area("Save protection: invalid EEPROM") == Area::Saves,
              "Save failures must point to Save Manager");
        check(runtime_error_area("Graphics mode failed") == Area::General, "Mode is not mod");
        check(looks_like_error(raw), "Detect real failure");
        check(!looks_like_error("Offline and modded games use separate saves."), "Don't turn help into an error");
        check(!looks_like_error("Mods saved in Mods / Hacks."), "Don't turn success into an error");
        check(error_summary("The update manifest signature is not trusted.", Area::Updates).find("verified") !=
                  std::string::npos,
              "Security failure stays explicit");
        check(error_summary("This is not an AppImage installation.", Area::Updates).find("manually") !=
                  std::string::npos,
              "Manual update guidance");
        check(error_summary("The online save is invalid.", Area::Saves).find("don't replace your offline save") !=
                  std::string::npos,
              "Offline save protection");
        check(error_summary("Import cancelled.", Area::Mods) == "Operation cancelled.", "Cancellation isn't a crash");
        check(error_summary("Update work paused.", Area::Updates) == "Operation paused.", "Pause isn't cancellation");
        check(error_summary("Something unexpected happened.", Area::Mods).find("nothing") == std::string::npos,
              "Unknown failures must not claim nothing changed");
        check(mod_support("12 pre-asset byte differences remain unclassified", true).find("aren't supported") !=
                  std::string::npos,
              "Partial behaviour stays visible");
        check(mod_support("Supported adapters", false).find("original game behaviour") != std::string::npos,
              "Asset-only edition stays explicit");
        check(progress("Mod 1/44: Reconstructing locally owned assets") == "Mod 1/44: Preparing installed mods",
              "Keep real progress counts");
        check(open_update_result(true, false, false, false, false), "Manual check shows current/error result");
        check(open_update_result(true, true, true, true, false), "Manual check can reopen a skipped release");
        check(!open_update_result(false, true, true, false, false), "Automatic skip stays respected");
        check(!open_update_result(false, true, false, true, false), "Automatic dismissal stays respected");
        check(!open_update_result(false, true, false, false, true), "Don't interrupt an active lobby/import");
        check(open_update_result(false, true, false, false, false), "New available release can notify");
        for (float width : {280.F, 360.F, 540.F, 850.F, 1100.F, 1800.F, 2560.F}) {
            for (float scale : {1.F, 1.25F, 1.5F, 2.F}) {
                const float button = std::min(width, 300.F * scale);
                const auto layout = play_header(width, button, 45.F * scale);
                check(layout.action_x >= 0 && layout.action_x + button <= width, "Header action fits horizontally");
                check(layout.action_y + 56.F <= layout.height, "Header action fits vertically");
                check(!layout.stacked || layout.action_y >= 45.F * scale, "Stacked action doesn't overlap heading");
            }
        }
        std::cout << checks << " launcher copy, update-result and responsive-header checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
