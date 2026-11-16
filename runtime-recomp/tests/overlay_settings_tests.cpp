#include "overlay_settings/policy.hpp"

#include <cstdio>
#include <sstream>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char* what, int line) {
    if (!condition) {
        std::fprintf(stderr, "overlay_settings_tests.cpp:%d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(condition) check((condition), #condition, __LINE__)

std::string saved(const dkr::overlay::Settings& settings) {
    std::ostringstream out;
    dkr::overlay::save(out, settings);
    return out.str();
}

// Feeds every line of a saved file back through load, as the runtime does.
dkr::overlay::Settings reloaded(const std::string& text) {
    dkr::overlay::Settings settings;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto separator = line.find('=');
        CHECK(dkr::overlay::load(line.substr(0, separator), std::stoi(line.substr(separator + 1)), settings));
    }
    return settings;
}
} // namespace

int main() {
    using dkr::overlay::Settings;

    // The defaults, in the exact order the settings file has always had.
    CHECK(saved(Settings{}) ==
          "fps_overlay_enabled=0\nfps_overlay_position=1\nfps_overlay_detail=1\nfps_overlay_single_row=0\n"
          "fps_custom_frame_time=1\nfps_custom_simulation=1\nfps_custom_graphics=0\nfps_custom_vi=0\n"
          "fps_custom_interpolation=0\nfps_custom_audio=0\nfps_custom_target=1\nfps_custom_resolution=0\n"
          "fps_font_size=24\nfps_fill_r=244\nfps_fill_g=45\nfps_fill_b=25\nfps_fill_a=255\n"
          "fps_outline_r=255\nfps_outline_g=147\nfps_outline_b=10\nfps_outline_a=255\n"
          "network_overlay_enabled=0\nnetwork_overlay_position=0\nnetwork_overlay_detail=1\n"
          "network_overlay_single_row=0\ncontroller_input_overlay_enabled=0\ncontroller_input_overlay_position=2\n");

    // Every field survives save then load.
    {
        Settings custom;
        custom.fps_enabled = true;
        custom.fps_position = 3;
        custom.fps_detail = 2;
        custom.fps_single_row = true;
        custom.fps_custom = {false, false, true, true, true, true, false, true};
        custom.fps_font_size = 40;
        custom.fps_fill = {0.2F, 0.4F, 0.6F, 0.8F};
        custom.fps_outline = {1.0F, 0.0F, 0.5F, 0.25F};
        custom.network_enabled = true;
        custom.network_position = 2;
        custom.network_detail = 2;
        custom.network_single_row = true;
        custom.controller_input_enabled = true;
        custom.controller_input_position = 0;
        const std::string text = saved(custom);
        CHECK(saved(reloaded(text)) == text);
        const Settings back = reloaded(text);
        CHECK(back.fps_custom == custom.fps_custom);
        CHECK(back.network_detail == 2 && back.controller_input_position == 0);
    }

    // Out-of-range numbers clamp exactly as the old parser did.
    {
        Settings s;
        CHECK(dkr::overlay::load("fps_overlay_position", 99, s) && s.fps_position == 3);
        CHECK(dkr::overlay::load("fps_overlay_position", -4, s) && s.fps_position == 0);
        CHECK(dkr::overlay::load("fps_overlay_detail", 9, s) && s.fps_detail == 3);
        CHECK(dkr::overlay::load("fps_font_size", 1, s) && s.fps_font_size == 16);
        CHECK(dkr::overlay::load("fps_font_size", 500, s) && s.fps_font_size == 64);
        CHECK(dkr::overlay::load("network_overlay_detail", 7, s) && s.network_detail == 2);
        CHECK(dkr::overlay::load("network_overlay_position", 7, s) && s.network_position == 3);
        CHECK(dkr::overlay::load("controller_input_overlay_position", -1, s) && s.controller_input_position == 0);
        CHECK(dkr::overlay::load("fps_fill_r", 999, s) && s.fps_fill.r == 1.0F);
        CHECK(dkr::overlay::load("fps_outline_a", -5, s) && s.fps_outline.a == 0.0F);
        CHECK(dkr::overlay::load("fps_overlay_enabled", 7, s) && s.fps_enabled);
        CHECK(dkr::overlay::load("fps_custom_audio", -1, s) && s.fps_custom[5]);
    }

    // A repeated key keeps the last value; an untouched key keeps what was there.
    {
        Settings s;
        s.network_position = 3;
        CHECK(dkr::overlay::load("fps_overlay_position", 0, s));
        CHECK(dkr::overlay::load("fps_overlay_position", 2, s));
        CHECK(s.fps_position == 2 && s.network_position == 3);
    }

    // Keys outside the group, and near misses, are left to the other owners.
    {
        Settings s;
        const Settings before = s;
        CHECK(!dkr::overlay::load("crt_enabled", 1, s));
        CHECK(!dkr::overlay::load("rumble", 1, s));
        CHECK(!dkr::overlay::load("fps_fill_x", 1, s));
        CHECK(!dkr::overlay::load("fps_fill_rr", 1, s));
        CHECK(!dkr::overlay::load("fps_fill_", 1, s));
        CHECK(!dkr::overlay::load("fps_custom_", 1, s));
        CHECK(!dkr::overlay::load("fps_custom_other", 1, s));
        CHECK(!dkr::overlay::load("fps_overlay_enabled_", 1, s));
        CHECK(!dkr::overlay::load("", 1, s));
        CHECK(saved(s) == saved(before));
    }

    return failures == 0 ? 0 : 1;
}
