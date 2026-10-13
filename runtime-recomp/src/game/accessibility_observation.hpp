#pragma once

#include "rom_revision.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::accessibility {

using Clock = std::chrono::steady_clock;
using SessionToken = std::uint64_t;
enum class StateKind { Unavailable, OptionsMenu, Race, TitleMenu, CharacterMenu,
                       GameSelectMenu, FileSelectMenu, TrackSelectMenu, PauseMenu, Dialogue, ResultsMenu };

struct MenuEntry {
    int index = 0;
    std::string label;
    bool enabled = true;
    bool selected = false;
};
// Authored centered stock HUD art only; sliding/offset/obscured prompts omitted.
enum class RaceStartPrompt { None, GetReady, Go };
enum class ItemCategory { None, Boost, Missile, Trap, Shield, Magnet };
struct HeldItem {
    ItemCategory category = ItemCategory::None; // Visible icon category; never its target or trajectory.
    int level = 0;        // Displayed level 1..3; 0 when no item is held.
    int quantity = 0;
};

struct RacerStatus {
    int position = 0;
    int lap = 0; // Displayed countLap + 1, capped at the authored lap total.
    int total_laps = 0;
    int bananas = 0;
    std::optional<HeldItem> held_item;
};

struct Observation {
    StateKind kind = StateKind::Unavailable;
    rom::Revision revision = rom::Revision::Unsupported;
    std::uint32_t scene = 0;
    SessionToken session = 0;
    std::uint64_t sequence = 0;
    int selection = -1;
    std::string selection_label;
    bool paused = false;
    int countdown = 0;
    RaceStartPrompt race_start_prompt = RaceStartPrompt::None;
    std::optional<RacerStatus> racer;
    std::string menu_name;
    std::string menu_detail;
    std::vector<MenuEntry> menu_entries;
};

// Reads only an 8 MiB, word-swapped submission image. This is sampled logical
// state, not an atomic guest-update boundary or a guarantee of displayed pixels.
// No memory/context pointers survive this call, and it never calls the guest.
// Coverage is source-backed stock US layouts, not yet a ROM-qualified contract.
// Supported stock US views include title/caution/options/audio, steady boot
// Controller Pak 1 tables (errors/delete/wait withheld), Save Options
// stage text, modal messages, independently visible card text/glyph fragments,
// complete balloon digits and qualified Adventure icons, character/game/file
// select, tracks, Adventure setup and trophy-round introductions, current open dialogue/subtitle pages,
// pause and postrace options/initials/stationary order/timestamps. Custom menus,
// non-ASCII encodings, multiplayer setup/HUD, moving result rows and Adventure
// preview stages with no setup GUI remain unavailable. Coverage needs real-ROM
// menu-to-race verification; synthetic fixtures do not establish acceptance.
Observation decode_submission(std::span<const std::uint8_t> memory,
                              rom::Revision revision, std::uint32_t scene);

// The caller starts/resets on every runtime session boundary, and passes the
// returned token with each submission. A decode racing reset cannot republish.
SessionToken begin_session();
// Read-only host session identity, including when demand is off or no sample
// exists. Retained one-shot information must not survive a session boundary.
SessionToken current_session();
// Disabled by default. A cheap atomic check avoids decoding submissions when
// no assistive client, speech/cue feature or explicit read needs game state.
void set_observation_demand(bool enabled);
bool observation_demanded();
void reset_session();
// Invalidates only the session owned by this token; a delayed renderer teardown
// cannot clear a newer renderer's observations.
void end_session(SessionToken token);
void observe_submission(std::span<const std::uint8_t> memory,
                        rom::Revision revision, std::uint32_t scene,
                        SessionToken token, Clock::time_point now = Clock::now());
// 1500 ms is a receipt timeout for a stalled observer, not submission freshness.
// Unsupported/transition samples replace previous values with Unavailable.
std::optional<Observation> read_observation(Clock::time_point now = Clock::now());
std::string describe_observation(const Observation& observation);

} // namespace dkr::runtime::accessibility
