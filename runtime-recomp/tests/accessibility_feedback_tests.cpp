#include "accessibility_feedback.hpp"

#include <cassert>
#include <chrono>

using namespace dkr::runtime::accessibility;
using namespace std::chrono_literals;

int main() {
    FeedbackScheduler scheduler;
    FeedbackPreferences preferences{true, true, 1500ms};
    const auto start = Clock::time_point{} + 10s;
    Observation race;
    race.kind = StateKind::Race;
    race.session = 1;
    race.racer = RacerStatus{3, 1, 3, 0};
    assert(scheduler.update(race, false, preferences, start) == "Position 3.");
    race.racer->position = 2;
    assert(!scheduler.update(race, false, preferences, start + 100ms));
    race.racer->position = 1;
    assert(scheduler.update(race, false, preferences, start + 1500ms) == "Position 1.");
    race.racer->bananas = 5;
    assert(!scheduler.update(race, false, preferences, start + 3s));

    // Suppression/staleness drops pending speech; no old position leaks later.
    race.racer->position = 4;
    assert(!scheduler.update(race, true, preferences, start + 4s));
    race.racer->position = 5;
    assert(scheduler.update(race, false, preferences, start + 5s) == "Position 5.");
    assert(!scheduler.update(std::nullopt, false, preferences, start + 6s));
    race.session = 2;
    assert(scheduler.update(race, false, preferences, start + 6s) == "Position 5.");

    preferences.automatic_speech = false;
    assert(!scheduler.update(race, false, preferences, start + 7s));
    preferences.automatic_speech = true;
    preferences.announce_position = false;
    assert(!scheduler.update(race, false, preferences, start + 8s));
    race.paused = true;
    assert(scheduler.update(race, false, preferences, start + 9s) == "Race paused.");

    Observation menu;
    menu.session = 2;
    menu.kind = StateKind::OptionsMenu;
    menu.menu_name = "Options"; // Match the decoded root menu's required context.
    menu.selection_label = "ENGLISH";
    assert(scheduler.update(menu, false, preferences, start + 10s));
    menu.selection_label = "SUBTITLES ON";
    assert(!scheduler.update(menu, false, preferences, start + 10050ms));
    const auto speech = scheduler.update(menu, false, preferences, start + 10200ms);
    assert(speech && speech->find("SUBTITLES ON") != std::string::npos);

    // Blank Save cards have identical visible labels but distinct selections.
    menu.menu_name = "SAVE OPTIONS"; menu.selection_label = "GAME PAK";
    menu.selection = 0;
    const auto first_card = scheduler.update(menu, false, preferences, start + 10400ms);
    assert(first_card);
    menu.selection = 1;
    assert(!scheduler.update(menu, false, preferences, start + 10450ms));
    assert(scheduler.update(menu, false, preferences, start + 10600ms) == first_card);
    assert(!scheduler.update(menu, false, preferences, start + 10620ms));
    // A brief move away and back retains only the latest selection, not a queue.
    menu.selection = 2;
    assert(!scheduler.update(menu, false, preferences, start + 10650ms));
    menu.selection = 1;
    assert(!scheduler.update(menu, false, preferences, start + 10900ms));

    Observation character;
    character.session = 2; character.kind = StateKind::CharacterMenu;
    character.menu_name = "Character select"; character.selection_label = "Diddy Kong";
    const auto portrait = scheduler.update(character, false, preferences, start + 11s);
    assert(portrait == "Character select. Diddy Kong.");
    character.selection_label = "OK?"; character.menu_detail = "Selected character: Diddy Kong";
    assert(!scheduler.update(character, false, preferences, start + 11050ms));
    assert(scheduler.update(character, false, preferences, start + 11200ms) ==
        "Character select. OK?. Selected character: Diddy Kong.");
    assert(!scheduler.update(character, false, preferences, start + 11400ms));
    character.selection_label = "Diddy Kong"; character.menu_detail.clear();
    assert(scheduler.update(character, false, preferences, start + 11600ms) == portrait);

    // Authored race prompts bypass the position interval and its opt-out.
    scheduler.reset();
    preferences.announce_position = false;
    race.paused = false;
    race.race_start_prompt = RaceStartPrompt::GetReady;
    assert(scheduler.update(race, false, preferences, start + 20s) == "Get Ready!");
    assert(!scheduler.update(race, false, preferences, start + 20050ms));
    race.race_start_prompt = RaceStartPrompt::Go;
    assert(scheduler.update(race, false, preferences, start + 20100ms) == "GO!");
    assert(!scheduler.update(race, false, preferences, start + 20200ms));
    race.race_start_prompt = RaceStartPrompt::None;
    assert(!scheduler.update(race, false, preferences, start + 20300ms));
    race.race_start_prompt = RaceStartPrompt::Go;
    assert(!scheduler.update(race, false, preferences, start + 20400ms));
    // Position speech cannot immediately replace the launch announcement.
    race.race_start_prompt = RaceStartPrompt::None;
    preferences.announce_position = true;
    assert(!scheduler.update(race, false, preferences, start + 20500ms));
    assert(scheduler.update(race, false, preferences, start + 21600ms) == "Position 5.");

    // Paused or suppressed observations cannot leak a launch announcement.
    scheduler.reset();
    race.race_start_prompt = RaceStartPrompt::Go;
    race.paused = true;
    assert(scheduler.update(race, false, preferences, start + 22s) == "Race paused.");
    race.paused = false;
    assert(!scheduler.update(race, true, preferences, start + 22100ms));
    assert(!scheduler.update(std::nullopt, false, preferences, start + 22200ms));
    race.race_start_prompt = RaceStartPrompt::None;
    assert(!scheduler.update(race, false, preferences, start + 22300ms));
    assert(scheduler.update(race, false, preferences, start + 23500ms) == "Position 5.");
    preferences.automatic_speech = false;
    race.race_start_prompt = RaceStartPrompt::GetReady;
    assert(!scheduler.update(race, false, preferences, start + 24s));
    preferences.automatic_speech = true;
    race.session += 1;
    assert(scheduler.update(race, false, preferences, start + 24100ms) == "Get Ready!");
    assert(!scheduler.update(race, false, preferences, start + 24200ms));
    race.session += 1;
    assert(scheduler.update(race, false, preferences, start + 24300ms) == "Get Ready!");

    // The latest-only mailbox can skip menus between races. A new scene is
    // a new speech context even when session, kind and visible position match.
    scheduler.reset();
    preferences = {true, true, 1500ms};
    race.scene = 17;
    race.race_start_prompt = RaceStartPrompt::None;
    race.countdown = 0;
    race.racer->position = 3;
    assert(scheduler.update(race, false, preferences, start + 30s) == "Position 3.");
    race.racer->position = 1;
    assert(!scheduler.update(race, false, preferences, start + 30100ms));
    race.scene = 18;
    race.racer->position = 3;
    assert(scheduler.update(race, false, preferences, start + 30200ms) == "Position 3.");
    assert(!scheduler.update(race, false, preferences, start + 32s));

    race.race_start_prompt = RaceStartPrompt::Go;
    race.countdown = 80; // Cached timer can disagree with qualified GO art.
    assert(scheduler.update(race, false, preferences, start + 33s) == "GO!");
    assert(!scheduler.update(race, false, preferences, start + 33050ms));
    ++race.scene;
    assert(scheduler.update(race, false, preferences, start + 33100ms) == "GO!");
    assert(!scheduler.update(race, false, preferences, start + 33200ms));
    // Losing the authored GO art must not regress to cached "Race starting".
    // Position speech still observes its normal interval after launch.
    race.race_start_prompt = RaceStartPrompt::None;
    assert(!scheduler.update(race, false, preferences, start + 33600ms));
    assert(scheduler.update(race, false, preferences, start + 34600ms) == "Position 3.");
    preferences.announce_position = false;
    assert(!scheduler.update(race, false, preferences, start + 36s));
    // A new context with no qualified startup art does not invent a prompt.
    ++race.scene;
    assert(!scheduler.update(race, false, preferences, start + 36100ms));
    preferences.automatic_speech = false;
    race.race_start_prompt = RaceStartPrompt::Go;
    assert(!scheduler.update(race, false, preferences, start + 36200ms));

    // A brief focus/visibility gap must not replay a delivered launch prompt
    // or fall back to the cached countdown in the same observed context.
    scheduler.reset();
    preferences = {true, false, 1500ms};
    race.scene = 40;
    race.countdown = 80;
    race.race_start_prompt = RaceStartPrompt::Go;
    assert(scheduler.update(race, false, preferences, start + 40s) == "GO!");
    assert(!scheduler.update(race, true, preferences, start + 40100ms));
    assert(!scheduler.update(std::nullopt, false, preferences, start + 40200ms));
    Observation unavailable;
    unavailable.session = race.session; unavailable.scene = race.scene;
    assert(!scheduler.update(unavailable, false, preferences, start + 40300ms));
    assert(!scheduler.update(race, false, preferences, start + 40400ms));
    race.race_start_prompt = RaceStartPrompt::None;
    assert(!scheduler.update(race, false, preferences, start + 40500ms));

    // Pending speech is discarded, while the delivered position and its
    // cooldown survive. Only a newly observed position may be spoken later.
    scheduler.reset();
    preferences.announce_position = true;
    race.countdown = 0;
    race.racer->position = 3;
    assert(scheduler.update(race, false, preferences, start + 50s) == "Position 3.");
    race.racer->position = 1;
    assert(!scheduler.update(race, false, preferences, start + 50100ms));
    assert(!scheduler.update(std::nullopt, false, preferences, start + 50200ms));
    race.racer->position = 3;
    assert(!scheduler.update(race, false, preferences, start + 50300ms));
    assert(!scheduler.update(race, false, preferences, start + 52s));
    race.racer->position = 2;
    assert(scheduler.update(race, false, preferences, start + 52100ms) == "Position 2.");
    assert(!scheduler.update(race, true, preferences, start + 52200ms));
    race.racer->position = 4;
    assert(!scheduler.update(race, false, preferences, start + 52300ms));
    assert(scheduler.update(race, false, preferences, start + 53600ms) == "Position 4.");

    // A real context change seen while suppressed retires old delivery state.
    scheduler.reset();
    preferences.announce_position = false;
    race.countdown = 80; race.race_start_prompt = RaceStartPrompt::Go;
    assert(scheduler.update(race, false, preferences, start + 60s) == "GO!");
    ++race.scene;
    assert(!scheduler.update(race, true, preferences, start + 60100ms));
    assert(scheduler.update(race, false, preferences, start + 60200ms) == "GO!");
    ++race.session;
    unavailable.session = race.session; unavailable.scene = race.scene;
    assert(!scheduler.update(unavailable, false, preferences, start + 60300ms));
    assert(scheduler.update(race, false, preferences, start + 60400ms) == "GO!");
    Observation intervening_menu;
    intervening_menu.session = race.session; intervening_menu.scene = race.scene;
    intervening_menu.kind = StateKind::OptionsMenu; intervening_menu.menu_name = "Options";
    assert(!scheduler.update(intervening_menu, true, preferences, start + 60500ms));
    assert(scheduler.update(race, false, preferences, start + 60600ms) == "GO!");

    // Explicit opt-out starts a new delivery baseline when enabled again.
    preferences.automatic_speech = false;
    assert(!scheduler.update(race, false, preferences, start + 60700ms));
    preferences.automatic_speech = true;
    assert(scheduler.update(race, false, preferences, start + 60800ms) == "GO!");

    // A long unknown gap does not repeat the same context's GO or resurrect
    // generic countdown speech. New authored Ready then GO still speaks.
    assert(!scheduler.update(std::nullopt, false, preferences, start + 61s));
    race.race_start_prompt = RaceStartPrompt::None;
    assert(!scheduler.update(race, false, preferences, start + 600s));
    race.race_start_prompt = RaceStartPrompt::Go;
    assert(!scheduler.update(race, false, preferences, start + 601s));
    race.race_start_prompt = RaceStartPrompt::GetReady;
    assert(scheduler.update(race, false, preferences, start + 602s) == "Get Ready!");
    assert(!scheduler.update(race, false, preferences, start + 603s));
    race.race_start_prompt = RaceStartPrompt::Go;
    assert(scheduler.update(race, false, preferences, start + 604s) == "GO!");

    // Cached countdown alone never generates startup speech, even before GO
    // was delivered. Independently visible position feedback remains available.
    scheduler.reset();
    race.race_start_prompt = RaceStartPrompt::None;
    assert(!scheduler.update(race, false, preferences, start + 700s));
    assert(!scheduler.update(race, false, preferences, start + 701s));
    preferences.announce_position = true;
    race.racer->position = 2;
    assert(scheduler.update(race, false, preferences, start + 702s) == "Position 2.");

}
