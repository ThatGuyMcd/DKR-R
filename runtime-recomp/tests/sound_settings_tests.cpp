#include "sound_settings/policy.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char* what, int line) {
    if (!condition) {
        std::fprintf(stderr, "sound_settings_tests.cpp:%d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(condition) check((condition), #condition, __LINE__)

std::string saved(const dkr::sound::Settings& settings) {
    std::ostringstream out;
    dkr::sound::save(out, settings);
    return out.str();
}

bool throws(const char* key, const char* value) {
    dkr::sound::Loaded loaded;
    try {
        dkr::sound::load(key, value, loaded);
    } catch (...) {
        return true;
    }
    return false;
}
} // namespace

int main() {
    using dkr::sound::Loaded;
    using dkr::sound::Settings;

    // The defaults, in the exact order the settings file has always had.
    CHECK(saved(Settings{}) == "master_volume=1\nmusic_volume=1\nsound_effects_volume=1\nvehicle_volume=1\n"
                               "nature_volume=1\nsound_muted=0\nmodern_multiplayer_race_music=0\neq_bass=0\n"
                               "eq_mid=0\neq_treble=0\n");

    Settings custom;
    custom.level = {0.5F, 0.25F, 0.75F, 0.4F, 0.6F};
    custom.muted = 5U;
    custom.custom_curve = dkr::sound::Curve{1, -2, 3};
    custom.multiplayer_race_music = true;
    custom.bass = 3.0F;
    custom.mid = -2.0F;
    custom.treble = 4.5F;
    CHECK(saved(custom) == "master_volume=0.5\nmusic_volume=0.25\nsound_effects_volume=0.75\nvehicle_volume=0.4\n"
                           "nature_volume=0.6\nsound_muted=5\neq_custom=1,-2,3\nmodern_multiplayer_race_music=1\n"
                           "eq_bass=3\neq_mid=-2\neq_treble=4.5\n");

    // Whatever is saved loads back to the same values.
    {
        Loaded loaded;
        std::istringstream lines(saved(custom));
        std::string line;
        while (std::getline(lines, line)) {
            const auto separator = line.find('=');
            CHECK(dkr::sound::load(line.substr(0, separator), line.substr(separator + 1), loaded));
        }
        for (std::size_t channel = 0; channel < dkr::sound::ChannelCount; ++channel)
            CHECK(loaded.level[channel] && *loaded.level[channel] == custom.level[channel]);
        CHECK(loaded.muted == 5U);
        CHECK(loaded.custom_curve && *loaded.custom_curve == (dkr::sound::Curve{1, -2, 3}));
        CHECK(loaded.multiplayer_race_music && *loaded.multiplayer_race_music);
        CHECK(loaded.bass && *loaded.bass == 3.0F);
        CHECK(loaded.mid && *loaded.mid == -2.0F);
        CHECK(loaded.treble && *loaded.treble == 4.5F);
    }

    // Keys outside the group are left to the other owners.
    {
        Loaded loaded;
        CHECK(!dkr::sound::load("rumble", "1", loaded));
        CHECK(!dkr::sound::load("sound_muted_extra", "1", loaded));
        CHECK(!dkr::sound::load("", "", loaded));
    }

    // A hand-made curve is clamped to the EQ's range and kept only when well formed.
    {
        Loaded loaded;
        CHECK(dkr::sound::load("eq_custom", "99,-99,0", loaded));
        CHECK(loaded.custom_curve && *loaded.custom_curve == (dkr::sound::Curve{12, -12, 0}));
        Loaded malformed;
        CHECK(dkr::sound::load("eq_custom", "1;2;3", malformed));
        CHECK(!malformed.custom_curve);
        CHECK(dkr::sound::load("eq_custom", "1,2", malformed));
        CHECK(!malformed.custom_curve);
    }

    // The mute mask is read like strtoul: garbage is zero, and it never throws.
    {
        Loaded loaded;
        CHECK(dkr::sound::load("sound_muted", "abc", loaded));
        CHECK(loaded.muted == 0U);
        CHECK(dkr::sound::load("sound_muted", "31", loaded));
        CHECK(loaded.muted == 31U);
    }

    // Numeric keys reject what the shared parser always rejected: anything
    // std::stoi refuses (including a leading dot and a value out of int range),
    // and anything std::stof refuses. The caller logs and skips the line.
    for (const char* key : {"master_volume", "music_volume", "sound_effects_volume", "vehicle_volume", "nature_volume",
                            "eq_bass", "eq_mid", "eq_treble", "modern_multiplayer_race_music"}) {
        CHECK(throws(key, "abc"));
        CHECK(throws(key, ""));
        CHECK(throws(key, ".5"));
        CHECK(throws(key, "99999999999"));
        CHECK(!throws(key, "1"));
    }
    CHECK(!throws("master_volume", "0.5abc")); // stoi and stof both stop at the first bad character.

    // A line that throws leaves the earlier value alone.
    {
        Loaded loaded;
        CHECK(dkr::sound::load("music_volume", "0.5", loaded));
        CHECK(throws("music_volume", "oops"));
        CHECK(loaded.level[1] && *loaded.level[1] == 0.5F);
        CHECK(dkr::sound::load("modern_multiplayer_race_music", "7", loaded));
        CHECK(loaded.multiplayer_race_music && *loaded.multiplayer_race_music);
    }

    if (failures == 0)
        std::puts("sound settings OK");
    return failures == 0 ? 0 : 1;
}
