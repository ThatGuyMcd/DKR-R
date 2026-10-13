#include "accessibility_observation.hpp"
#include "revision_addresses.hpp"

#include <algorithm>
#include <cassert>
#include <bit>
#include <cstdio>
#include <string_view>
#include <vector>
#include <tuple>

namespace {
using namespace dkr::runtime;
using namespace dkr::runtime::accessibility;
struct Fixture {
    rom::Revision revision;
    const revision_addresses::AddressTable& a;
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x800000);
    explicit Fixture(rom::Revision value) : revision(value), a(revision_addresses::table_for(value)) {}
    void write(std::uint32_t address, std::uint32_t value, unsigned size = 4) {
        for (unsigned i = 0; i < size; ++i)
            bytes[((address & 0x7FFFFFU) + i) ^ 3U] = static_cast<std::uint8_t>(value >> ((size - i - 1U) * 8U));
    }
    void text(std::uint32_t address, std::string_view value) {
        for (unsigned i = 0; i < value.size(); ++i) write(address + i, static_cast<unsigned char>(value[i]), 1);
        write(address + static_cast<unsigned>(value.size()), 0, 1);
    }
    std::uint32_t cursor() const { return revision == rom::Revision::UsV77 ? 0x800DF460U : 0x800DF9E0U; }
    std::uint32_t strings() const { return revision == rom::Revision::UsV77 ? 0x800DFA10U : 0x800DFF90U; }
    void options() {
        write(a.GameMode, 1); write(a.CurrentMenuId, 12); write(a.MenuDelay, 0);
        write(cursor(), 2);
        for (unsigned i = 0; i < 6; ++i) { write(strings() + i * 4, 0x80001000 + i * 128); text(0x80001000 + i * 128, "OPTION"); }
        write(strings() + 8, 0x80001000); text(0x80001000, "AUDIO OPTIONS");
    }
    void race() {
        write(a.GameMode, 0); write(a.IsInRace, 1); write(a.LevelLoadTimer, 0, 2);
        write(a.IsPaused, 0, 1); write(a.RaceStartTimer, 80);
        write(a.NumberOfActivePlayers, 1);
        write(revision == rom::Revision::UsV77 ? 0x80126D34 : 0x801272F4, 1, 1);
        write(a.NumberOfRacers, 8); write(a.CurrentLevelHeader, 0x80002000);
        write(0x8000204B, 3, 1); write(0x8000204C, 0, 1);
        write(a.RacersByPort, 0x80003000); write(0x80003000, 0x80004000);
        write(0x80004064, 0x80005000); write(0x80005000, 0, 2);
        write(0x800051AE, 2, 2); write(0x80005194, 1, 1);
        write(0x80005185, 7, 1); write(0x800051D8, 0, 1);
    }
    Observation decode() const { return decode_submission(bytes, revision, 17); }
};

void check_boot_pak(rom::Revision revision) {
    Fixture f(revision);
    const auto addr = [revision](std::uint32_t a, std::uint32_t b) { return revision == rom::Revision::UsV77 ? a : b; };
    const auto phase = addr(0x80126C20, 0x801271E0), scroll = addr(0x801263D8, 0x80126978);
    const auto labels = addr(0x80126AA0, 0x80127060), pages = addr(0x80126B70, 0x80127130);
    constexpr unsigned fonts = 0x80040000, windows = 0x80042000, strings = 0x80044000, notes = 0x80048000;
    f.write(f.a.GameMode, 1); f.write(f.a.CurrentMenuId, 26); f.write(phase, 2);
    f.write(addr(0x800DFD94, 0x800E0314), 1); f.write(addr(0x80126BB4, 0x80127174), 7);
    f.write(addr(0x80126BB0, 0x80127170), 123); f.write(0x80000300, 1);
    f.write(addr(0x801262B0, 0x80126850), 320); f.write(addr(0x801262B8, 0x80126858), 240);
    f.write(addr(0x8012A7E4, 0x8012ADA4), fonts); f.write(addr(0x8012A7E8, 0x8012ADA8), windows);
    f.write(windows + 29, 2, 1); f.write(windows + 2, 24, 2);
    for (unsigned font = 0; font < 3; ++font) {
        f.write(fonts + font * 0x400 + 0x22, 12, 2); f.write(fonts + font * 0x400 + 0x24, 6, 2);
        for (unsigned c = 33; c < 127; ++c) {
            const auto g = fonts + font * 0x400 + 0x100 + (c - 32) * 8;
            f.write(g + 1, 6, 1); f.write(g + 6, 5, 1); f.write(g + 7, 10, 1);
        }
    }
    f.write(addr(0x800DF4A0, 0x800DFA20), strings);
    const std::pair<unsigned,const char*> text[] = {{35,"CONTROLLER PAK"},{86,"CONTROLLER PAK ~"},
        {114,"FREE PAGES: ~"},{115,"NOTE"},{116,"FILENAME"},{117,"PAGES"},{51,"EXIT"}};
    for (auto [index, value] : text) { f.write(strings + index * 4, strings + 0x400 + index * 64); f.text(strings + 0x400 + index * 64, value); }
    for (unsigned i = 0; i < 16; ++i) { f.write(labels + i * 4, notes + i * 32); f.text(notes + i * 32, "-"); }
    auto observed = f.decode();
    assert(observed.kind == StateKind::OptionsMenu && observed.menu_name == "CONTROLLER PAK");
    assert(observed.menu_entries.size() == 8 && observed.selection == 0);
    assert(observed.menu_entries[0].label == "CONTROLLER PAK 1");
    assert(observed.selection_label == "NOTE 1, FILENAME -, PAGES 0");
    assert(observed.menu_detail == "FREE PAGES: 123. Press B to leave the Controller Pak manager");
    const auto original = f.bytes; (void)f.decode(); assert(f.bytes == original);
    // Header local y16 is screen y64: ink ending at bottom78 is admitted.
    const auto free_f = fonts + 0x100 + ('F' - 32) * 8;
    f.write(free_f + 7, 14, 1);
    assert(f.decode().menu_detail == "FREE PAGES: 123. Press B to leave the Controller Pak manager");
    f.write(free_f + 7, 15, 1);
    assert(f.decode().menu_detail.find("visible text fragment") != std::string::npos);
    f.bytes = original;
    f.write(f.cursor(), 3); f.text(notes + 3 * 32, "DKR.A"); f.write(pages + 3 * 4, 8);
    assert(f.decode().selection_label == "NOTE 4, FILENAME DKR.A, PAGES 8");
    f.bytes = original; f.write(f.cursor(), 0xFFFFFFFF); assert(f.decode().selection_label == "CONTROLLER PAK 1");
    // Never inspect the nine offscreen note names, page values or EXIT label.
    f.bytes = original; f.write(strings + 51 * 4, 0x80800000);
    for (unsigned i = 7; i < 16; ++i) { f.write(labels + i * 4, 0x80800000); f.write(pages + i * 4, 0xFFFFFFFF); }
    assert(f.decode().selection_label == "NOTE 1, FILENAME -, PAGES 0");
    f.bytes = original; f.write(scroll, 9); f.write(f.cursor(), 16); f.write(windows + 29, 0, 1); f.write(windows + 2, 210, 2);
    observed = f.decode(); assert(observed.menu_entries.size() == 9 && observed.selection_label == "EXIT");
    assert(observed.menu_entries[1].label == "NOTE 10, FILENAME -, PAGES 0");
    // Copied ink, not the hidden full filename, limits a long visible row.
    f.bytes = original; f.text(notes, std::string(31, 'W'));
    const auto wide_glyph = fonts + 0x500 + ('W' - 32) * 8;
    f.write(wide_glyph + 1, 9, 1); f.write(wide_glyph + 6, 8, 1);
    observed = f.decode(); assert(observed.selection_label.find("visible text fragment") != std::string::npos);
    assert(observed.selection_label.find(std::string(31, 'W')) == std::string::npos);
    for (auto [field,value,width] : std::initializer_list<std::tuple<unsigned,unsigned,unsigned>>{
        {phase,0,4},{phase,1,4},{addr(0x800DFD94,0x800E0314),0,4},
        {addr(0x80126BC8,0x80127188),1,4},{f.a.MenuStage,1,4},{f.a.MenuStage,2,4},
        {addr(0x80126C10,0x801271D0),1,4},{f.a.MenuDelay,1,4},{scroll,10,4},
        {f.cursor(),7,4},{f.cursor(),16,4},{addr(0x80126A68,0x80127028),1,4},
        {addr(0x80126BB4,0x80127174),8,4},{addr(0x80126BB0,0x80127170),124,4},
        {pages,124,4},{labels,0,4},{labels+4,notes+33,4},{strings+115*4,0,4},
        {windows+2,122,2},{windows+29,0,1},{windows+32,1,2},
        {addr(0x8012645C,0x801269FC),1,1},{addr(0x80126464,0x80126A04)+3,0xFF,1},
        {addr(0x80121140,0x801216C0),0x8000,2},{addr(0x80121140,0x801216C0)+6,0x4000,2},
        {f.a.LevelLoadTimer,1,2},{addr(0x800E31AC,0x800E373C),1,4},
        {0x80000300,0,4},{addr(0x8012A7E4,0x8012ADA4),0,4}}) {
        f.bytes = original; f.write(field,value,width); assert(f.decode().kind == StateKind::Unavailable);
    }
    f.bytes = original; for (unsigned i = 0; i < 32; ++i) f.write(notes+i,'X',1);
    assert(f.decode().kind == StateKind::Unavailable);
}

void check_trophy_round(rom::Revision revision) {
    Fixture f(revision);
    const auto addr = [revision](std::uint32_t a, std::uint32_t b) { return revision == rom::Revision::UsV77 ? a : b; };
    const auto round = addr(0x800E0FEC, 0x800E156C), worlds = addr(0x80121174, 0x801216F4);
    const auto lockout = addr(0x801263C4, 0x80126964);
    f.write(f.a.GameMode, 1); f.write(f.a.CurrentMenuId, 20); f.write(f.a.NumberOfActivePlayers, 1);
    f.write(f.a.TrophyRaceWorldId, 2); f.write(round, 2); f.write(worlds, 6); f.write(0x80000300, 1);
    f.write(addr(0x801262B0, 0x80126850), 320); f.write(addr(0x801262B8, 0x80126858), 240);
    f.write(addr(0x8011AD6C, 0x8011B2EC), 0x80030000); f.write(addr(0x8011AD70, 0x8011B2F0), 0x80031000);
    f.write(addr(0x8011ADA0, 0x8011B320), 29);
    f.write(0x80031000 + 26 * 4, 0); f.write(0x80031000 + 27 * 4, 8); f.write(0x80031000 + 28 * 4, 10);
    f.write(0x80030008, 5, 1); f.write(0x80030009, 6, 1); f.write(0x80030022, 3, 1);
    f.write(addr(0x80121170, 0x801216F0), 64); f.write(addr(0x8012116C, 0x801216EC), 0x80032000);
    f.write(0x80032000 + 3 * 4, 0x80033000); f.text(0x80033000, "SNOWFLAKE MOUNTAIN");
    f.write(0x80032000 + 5 * 4, 0x80033100); f.text(0x80033100, "ANCIENT LAKE");
    f.write(0x80032000 + 6 * 4, 0x80033200); f.text(0x80033200, "HIDDEN NEXT COURSE");
    f.write(addr(0x800DF4A0, 0x800DFA20), 0x80034000);
    f.write(0x80034000 + 70 * 4, 0x80035000); f.text(0x80035000, "TROPHY RACE");
    for (unsigned i = 0; i < 4; ++i) {
        f.write(0x80034000 + (138 + i) * 4, 0x80035100 + i * 128);
        f.text(0x80035100 + i * 128, i == 2 ? "ROUND THREE" : "OTHER ROUND");
    }
    f.write(addr(0x8012A7E4, 0x8012ADA4), 0x80036000);
    f.write(0x80036822, 12, 2); f.write(0x80036824, 8, 2);
    for (unsigned c = 33; c < 127; ++c) {
        const auto g = 0x80036900 + (c - 32) * 8;
        f.write(g + 1, 8, 1); f.write(g + 6, 8, 1); f.write(g + 7, 12, 1);
    }
    auto observed = f.decode();
    assert(observed.kind == StateKind::TrackSelectMenu && observed.menu_name == "TROPHY RACE");
    assert(observed.menu_entries.size() == 5 && observed.selection == 4);
    assert(observed.menu_detail == "SNOWFLAKE MOUNTAIN. ROUND THREE. ANCIENT LAKE");
    assert(observed.selection_label == "Press A or Start to continue" && observed.menu_entries.back().enabled);
    assert(describe_observation(observed).find("HIDDEN NEXT COURSE") == std::string::npos);
    assert(describe_observation(observed).find("OTHER ROUND") == std::string::npos);
    const auto original = f.bytes; (void)f.decode(); assert(f.bytes == original);
    const char* round_names[] = {"ROUND ONE", "ROUND TWO", "ROUND THREE", "ROUND FOUR"};
    for (unsigned current = 0; current < 4; ++current) {
        f.bytes = original;
        f.write(round, current);
        f.write(0x80030006 + current, 5, 1);
        f.text(0x80035100 + current * 128, round_names[current]);
        observed = f.decode();
        assert(observed.menu_detail == std::string("SNOWFLAKE MOUNTAIN. ") +
            round_names[current] + ". ANCIENT LAKE");
        assert(observed.selection_label == "Press A or Start to continue");
    }
    f.bytes = original;
    f.write(lockout, 1); observed = f.decode();
    assert(observed.selection == -1 && observed.selection_label.empty() && !observed.menu_entries.back().enabled);
    assert(observed.menu_detail == "SNOWFLAKE MOUNTAIN. ROUND THREE. ANCIENT LAKE");
    // The menu ignores stale gMenuStage. The renderer reads only current round.
    f.bytes = original; f.write(f.a.MenuStage, 0xFFFFFFFF); assert(f.decode().menu_name == "TROPHY RACE");
    // Do not announce an inaccessible full name when copied font ink clips it.
    f.bytes = original; f.text(0x80033100, std::string(60, 'X'));
    observed = f.decode(); assert(observed.menu_entries[3].label.find("visible text fragment") != std::string::npos);
    assert(observed.menu_entries[3].label != std::string(60, 'X'));
    // Unused future-round pointers cannot affect this current rendered page.
    f.bytes = original; f.write(0x80032000 + 6 * 4, 0x80800000); f.write(0x80034000 + 141 * 4, 0x80800000);
    assert(f.decode().menu_detail == "SNOWFLAKE MOUNTAIN. ROUND THREE. ANCIENT LAKE");
    for (auto [field, value, width] : std::initializer_list<std::tuple<std::uint32_t,unsigned,unsigned>>{
        {f.a.GameMode, 0, 4}, {f.a.CurrentMenuId, 21, 4}, {f.a.NumberOfActivePlayers, 2, 4},
        {f.a.MenuDelay, 1, 4}, {f.a.MenuDelay, 22, 4}, {f.a.LevelLoadTimer, 1, 2},
        {f.a.TrophyRaceWorldId, 0, 4}, {f.a.TrophyRaceWorldId, 6, 4}, {round, 4, 4}, {round, 0xFFFFFFFF, 4},
        {worlds, 2, 4}, {worlds, 9, 4}, {lockout, 2, 4}, {0x80000300, 0, 4},
        {addr(0x800E31AC, 0x800E373C), 1, 4}, {addr(0x8012A7F0, 0x8012ADB0), 1, 4},
        {addr(0x8011AD6C, 0x8011B2EC), 0x80800000, 4}, {addr(0x8011AD70, 0x8011B2F0), 0, 4},
        {addr(0x8011ADA0, 0x8011B320), 28, 4}, {0x80031000 + 27 * 4, 7, 4},
        {0x80031000 + 28 * 4, 8, 4}, {0x80030008, 0xFF, 1}, {0x80030022, 0xFF, 1},
        {0x80034000 + 140 * 4, 0, 4}, {addr(0x8012A7E4, 0x8012ADA4), 0, 4}, {0x80036822, 0, 2}}) {
        f.bytes = original; f.write(field, value, width); assert(f.decode().kind == StateKind::Unavailable);
    }
}

void check_hub_pause(rom::Revision revision) {
    Fixture f(revision);
    const auto addr = [revision](std::uint32_t a, std::uint32_t b) { return revision == rom::Revision::UsV77 ? a : b; };
    const auto cap = addr(0x800E0984, 0x800E0F04), selected = addr(0x80126A68, 0x80127028);
    const auto suboption = addr(0x800E0988, 0x800E0F08), options = addr(0x80126A40, 0x80127000);
    f.write(f.a.GameMode, 0); f.write(f.a.IsInRace, 0); f.write(f.a.IsPaused, 1, 1);
    // Hub/challenge gameplay has no ordinary-race qualification. Only the
    // pause renderer's labels are needed; no header/racer data is populated.
    f.write(cap, 2); f.write(selected, 0);
    f.write(options, 0x80001000); f.write(options + 4, 0x80001100);
    f.text(0x80001000, "CONTINUE"); f.text(0x80001100, "QUIT GAME");
    f.write(addr(0x800DF4A0, 0x800DFA20), 0x80002000);
    f.write(0x80002000 + 134 * 4, 0x80001200); f.text(0x80001200, "OK");
    f.write(0x80002000 + 85 * 4, 0x80001300); f.text(0x80001300, "CANCEL");
    auto observed = f.decode();
    assert(observed.kind == StateKind::PauseMenu && observed.paused && !observed.racer);
    assert(observed.menu_entries.size() == 2 && observed.selection_label == "CONTINUE");
    assert(describe_observation(observed) == "Pause options. CONTINUE.");
    f.write(selected, 1); assert(f.decode().selection_label == "QUIT GAME");
    f.write(suboption, 1); observed = f.decode();
    assert(observed.menu_name == "Confirm quit" && observed.menu_entries.size() == 2 && observed.selection_label == "OK");
    f.write(suboption, 2); assert(f.decode().selection_label == "CANCEL");
    const auto original = f.bytes; (void)f.decode(); assert(f.bytes == original);
    // Cached race timer cannot suppress independently rendered pause text.
    f.write(f.a.RaceStartTimer, 0xFFFFFFFF); assert(f.decode().selection_label == "CANCEL");
    // Returning from confirmation preserves the actual selected pause row.
    f.bytes = original; f.write(suboption, 0); assert(f.decode().selection_label == "QUIT GAME");
    const auto normal = f.bytes;
    for (auto [field, value, width] : std::initializer_list<std::tuple<std::uint32_t,unsigned,unsigned>>{
        {f.a.GameMode, 1, 4}, {f.a.GameMode, 2, 4}, {f.a.LevelLoadTimer, 1, 2}, {f.a.IsPaused, 2, 1},
        {cap, 0, 4}, {cap, 9, 4}, {selected, 2, 4}, {suboption, 3, 4}, {options + 4, 0x80800000, 4}}) {
        f.bytes = normal; f.write(field, value, width); assert(f.decode().kind == StateKind::Unavailable);
    }
    f.bytes = normal; f.write(f.a.IsPaused, 0, 1);
    assert(f.decode().kind == StateKind::Unavailable && !f.decode().racer);
}

void check_race_start_prompt(rom::Revision revision) {
    Fixture f(revision); f.race();
    // Cached countdown 80 alone does not establish visible startup artwork.
    // F8 keeps useful visible HUD details without inventing a countdown label.
    const auto no_art = f.decode();
    assert(no_art.countdown == 80 && no_art.race_start_prompt == RaceStartPrompt::None && no_art.racer);
    assert(describe_observation(no_art) ==
        "Race. Player 1 position 2, lap 2 of 3, bananas 7. No held item.");
    auto paused = no_art;
    paused.paused = true;
    assert(describe_observation(paused) ==
        "Race paused. Player 1 position 2, lap 2 of 3, bananas 7. No held item.");
    const auto addr = [revision](std::uint32_t a, std::uint32_t b) { return revision == rom::Revision::UsV77 ? a : b; };
    const auto event = addr(0x8011ADB0, 0x8011B330), hud = 0x80010000U;
    const auto ids = 0x80011000U, assets = 0x80012000U;
    f.write(f.a.TracksMode, 1); f.write(0x80000300, 1);
    f.write(addr(0x801262B0, 0x80126850), 320); f.write(addr(0x801262B8, 0x80126858), 240);
    f.write(f.a.CurrentHud, hud); f.write(f.a.PlayerHud, hud);
    f.write(addr(0x80126CF0, 0x801272B0), ids); f.write(addr(0x80126CF4, 0x801272B4), assets);
    f.write(addr(0x80126CF8, 0x801272B8), 64);
    const auto ortho = addr(0x800DD2B8, 0x800DD828);
    f.write(ortho, 0x3F800000); f.write(ortho + 20, 0x3F800000); f.write(ortho + 60, 0x43200000);
    const auto sprite_fixture = [&](bool ready) {
        const unsigned sprite_id = ready ? 22 : 7, n = ready ? 3 : 2, index = ready ? 13 : 12;
        const auto element = hud + index * 32U, sprite = ready ? 0x80013000U : 0x80014000U;
        f.write(element + 6, sprite_id, 2); f.write(element + 8, 0x3F800000);
        f.write(element + 16, std::bit_cast<unsigned>(30.0f));
        f.write(element + 26, 1, 1);
        f.write(ids + sprite_id * 2, ready ? 0x803F : 0x8040, 2); f.write(assets + sprite_id * 4, sprite);
        const auto triangles = sprite + 0x20, list = triangles + n * 32, vertices = list + n * 32 + 8, textures = vertices + n * 40;
        f.write(sprite, 1, 2); f.write(sprite + 2, n, 2); f.write(sprite + 4, 1, 2);
        f.write(sprite + 8, textures); f.write(sprite + 12, list);
        constexpr int go[2][4] = {{64,31,18,6}, {52,6,23,36}};
        constexpr int get_ready[3][4] = {{160,12,20,5}, {160,12,17,16}, {152,12,17,27}};
        auto command = list;
        const auto dma = [&](unsigned op, unsigned pointer) { f.write(command, op); f.write(command + 4, pointer & 0x1FFFFFFF); command += 8; };
        for (unsigned i = 0; i < n; ++i) {
            const auto& tile = ready ? get_ready[i] : go[i];
            const unsigned texture = 0x80015000 + index * 0x1000 + i * 0x100, texture_list = texture + 32;
            f.write(textures + i * 4, texture);
            f.write(texture, tile[0], 1); f.write(texture + 1, tile[1], 1);
            f.write(texture + 3, tile[2], 1); f.write(texture + 4, tile[3], 1);
            f.write(texture + 10, 1, 2); f.write(texture + 12, texture_list); f.write(texture + 0x12, 0x0100, 2);
            dma(0x07010008, texture_list);
            if (!i) dma(0x04000000 | ((((n * 4 - 1) << 3) | 1) << 16) | (n * 4 * 18 + 8), vertices);
            dma(0x05110020, triangles + i * 32);
            const unsigned base = i * 4, indices[] = {base + 3,base + 2,base + 1,base + 4,base + 3,base + 1};
            for (unsigned j = 0; j < 6; ++j) f.write(triangles + i * 32 + (j < 3 ? 1 + j : 16 + j - 2), indices[j], 1);
            const unsigned u = (tile[0] - 1) << 5, v = (tile[1] - 1) << 5, uv[2][6] = {{u,v,u,0,1,0}, {1,v,u,v,1,0}};
            for (unsigned t = 0; t < 2; ++t) {
                f.write(triangles + i * 32 + t * 16, 0x40, 1);
                for (unsigned j = 0; j < 6; ++j) f.write(triangles + i * 32 + t * 16 + 4 + j * 2, uv[t][j], 2);
            }
            const int left = tile[2] - (ready ? 97 : 49), right = left + tile[0] - 1;
            const int top = (ready ? 22 : 24) - tile[3] - 1, bottom = (ready ? 22 : 24) - tile[3] - tile[1];
            const int x[4] = {left,right,right,left}, y[4] = {top,top,bottom,bottom};
            for (unsigned j = 0; j < 4; ++j) {
                f.write(vertices + (i * 4 + j) * 10, x[j], 2); f.write(vertices + (i * 4 + j) * 10 + 2, y[j], 2);
                f.write(vertices + (i * 4 + j) * 10 + 6, 0xFFFFFFFF);
            }
        }
        f.write(command, 0xE7000000); f.write(command + 8, 0xB8000000);
    };
    sprite_fixture(true); sprite_fixture(false);
    f.write(event, 80);
    assert(f.decode().race_start_prompt == RaceStartPrompt::GetReady);
    assert(describe_observation(f.decode()) == "Race. Get Ready. Player 1 position 2, lap 2 of 3, bananas 7. No held item.");
    f.write(event, 2); assert(f.decode().race_start_prompt == RaceStartPrompt::GetReady);
    f.write(event, 1); // authoritative raw1 is GO despite cached timer80.
    assert(f.decode().countdown == 80 && f.decode().race_start_prompt == RaceStartPrompt::Go);
    assert(describe_observation(f.decode()) == "Race. GO. Player 1 position 2, lap 2 of 3, bananas 7. No held item.");
    f.write(event, 0); assert(f.decode().race_start_prompt == RaceStartPrompt::Go);
    f.write(hud + 0x19A, 59, 1); assert(f.decode().race_start_prompt == RaceStartPrompt::Go);
    const auto original = f.bytes;
    (void)f.decode(); assert(f.bytes == original);
    for (auto [field, value, width] : std::initializer_list<std::tuple<std::uint32_t,unsigned,unsigned>>{
        {hud + 0x19A, 60, 1}, {hud + 0x19A, 0, 1}, {hud + 0x18C, std::bit_cast<unsigned>(-8.0f), 4},
        {hud + 0x186, 22, 2}, {hud + 0x188, 0x7FC00000, 4}, {hud + 0x184, 1, 2}, {hud + 0x190, 0, 4},
        {hud + 0x198, 1, 2}, {f.a.TracksMode, 0, 4}, {f.a.NumberOfActivePlayers, 2, 4},
        {f.a.HudNumPlayers, 1, 4}, {f.a.CurrentHud, hud + 32, 4}, {f.a.PlayerHud, 0, 4},
        {addr(0x80126CD2, 0x80127292), 1, 1}, {addr(0x80126D34, 0x801272F4), 0, 1},
        {addr(0x80126D08, 0x801272C8), 1, 4}, {addr(0x80126D24, 0x801272E4), 1, 4},
        {addr(0x80126D28, 0x801272E8), 1, 4}, {addr(0x80126CD3, 0x80127293), 1, 1},
        {addr(0x80120D15, 0x80121295), 1, 1}, {addr(0x800E31AC, 0x800E373C), 1, 4},
        {addr(0x800DD384, 0x800DD8F4), 0xFFFFFFFF, 4}, {0x80000300, 0, 4}, {ortho, 0, 4},
        {ids + 14, 0x8041, 2}, {assets + 28, 0, 4}, {assets + 28, 0x80800000, 4},
        {0x80014000, 2, 2}, {0x80014002, 3, 2}, {0x80014008, 0x80014100, 4}, {0x8001400C, 0x80014100, 4},
        {0x800140A8, 0, 2}, {0x800140AE, 0, 4}, {0x80014060, 0, 4}, {0x80014024, 0, 2},
        {0x80021012, 0x0001, 2}, {0x80021000, 63, 1}, {0x80021003, 19, 1}, {0x8002100C, 0x80800000, 4}, {event, 0xFFFFFFFF, 4},
        {f.a.LevelLoadTimer, 1, 2}, {f.a.CutsceneCameraActive, 1, 1}, {f.a.PostRaceViewport, 1, 1}}) {
        f.bytes = original; f.write(field, value, width); assert(f.decode().race_start_prompt == RaceStartPrompt::None);
    }
    f.bytes = original; f.write(addr(0x8012A7E8, 0x8012ADA8), 0x80030000);
    f.write(0x80030000 + 7 * 40 + 0x1E, 0x8000, 2);
    assert(f.decode().race_start_prompt == RaceStartPrompt::None);
    f.bytes = original; f.write(f.a.IsPaused, 1, 1); assert(f.decode().race_start_prompt == RaceStartPrompt::None);
    f.bytes = original; f.write(event, 2); f.write(ids + 44, 0x803E, 2);
    assert(f.decode().race_start_prompt == RaceStartPrompt::None);
    f.bytes = original; f.write(event, 100);
    assert(f.decode().race_start_prompt == RaceStartPrompt::GetReady);
    const auto ready_fixture = f.bytes;
    for (auto [field, value, width] : std::initializer_list<std::tuple<std::uint32_t,unsigned,unsigned>>{
        {0x80013002, 2, 2}, {0x80013006, 2, 2}, {0x800130E8, 0, 2}, {0x80013138, 0, 2},
        {0x80022212, 0x0001, 2}, {0x80022200, 160, 1}, {0x80022204, 26, 1}, {0x80013080 + 4, 0x800000, 4},
        {addr(0x80126CF0, 0x801272B0), 0, 4}, {addr(0x80126CF8, 0x801272B8), 22, 4},
        {addr(0x8012A7E8, 0x8012ADA8), 0x80800000, 4}, {hud + 0x1AC, 0x80000000, 4}}) {
        f.bytes = ready_fixture; f.write(field, value, width);
        assert(f.decode().race_start_prompt == RaceStartPrompt::None);
    }
    // Track setup exits only after this menu counter exceeds40; it
    // remains stale in gameplay and is unrelated to HUD sprite visibility.
    f.bytes = original; f.write(f.a.MenuDelay, 41);
    assert(f.decode().race_start_prompt == RaceStartPrompt::Go);
    f.bytes = ready_fixture; f.write(f.a.MenuDelay, 42);
    assert(f.decode().race_start_prompt == RaceStartPrompt::GetReady);
    // Semantic values are owned after copied RAM changes.
    f.bytes = original; const auto observed = f.decode(); f.write(event, 80);
    assert(observed.race_start_prompt == RaceStartPrompt::Go);
}

void check_track_grid(rom::Revision revision) {
    Fixture f(revision);
    const auto addr = [revision](std::uint32_t a, std::uint32_t b) { return revision == rom::Revision::UsV77 ? a : b; };
    const auto records = addr(0x80126930, 0x80126EF0), images = 0x80004000U;
    const auto assets = addr(0x80126550, 0x80126AF0);
    const auto cursor_x = addr(0x801269C8, 0x80126F88), scroll_x = addr(0x801269DC, 0x80126F9C);
    const auto target_x = addr(0x801269E8, 0x80126FA8), preview_x = addr(0x801269F4, 0x80126FB4);
    f.write(f.a.GameMode, 1); f.write(f.a.CurrentMenuId, 15); f.write(0x80000300, 1);
    f.write(addr(0x80126474, 0x80126A14), 160); f.write(addr(0x80126478, 0x80126A18), 120);
    f.write(addr(0x8012647C, 0x80126A1C), 320); f.write(addr(0x80126480, 0x80126A20), 240);
    f.write(addr(0x801262B0, 0x80126850), 320); f.write(addr(0x801262B8, 0x80126858), 240);
    f.write(addr(0x8012A7E4, 0x8012ADA4), 0x80002000);
    f.write(0x80002822, 12, 2); f.write(0x80002824, 8, 2);
    for (unsigned c = 33; c < 127; ++c) {
        const auto g = 0x80002900 + (c - 32) * 8;
        f.write(g + 1, 8, 1); f.write(g + 6, 8, 1); f.write(g + 7, 12, 1);
    }
    f.write(addr(0x800DF75C, 0x800DFCDC), images);
    f.write(addr(0x800DF764, 0x800DFCE4), 255);
    for (unsigned i = 4; i <= 6; ++i) f.write(images + i * 32 + 6, i + 1, 2);
    f.text(0x80001000, "VISIBLE TRACK"); f.text(0x80001100, "?");
    f.write(addr(0x800E0978, 0x800E0EF8), 0x80001100);
    const auto cell = [&](unsigned i, int dx, int dy, std::uint32_t text, unsigned flags = 0) {
        const auto p = records + i * 16;
        f.write(p + 4, text); f.write(p + 8, dx, 2); f.write(p + 10, dy, 2);
        f.write(p + 12, 1, 1); f.write(p + 14, flags, 1); f.write(p + 15, 4, 1);
    };
    cell(4, 0, 0, 0x80001000, 0x80);
    // Opposite hidden destination: type0 must never resolve the level table.
    f.write(addr(0x801268E8, 0x80126EA8), 1, 2);
    f.write(addr(0x80121170, 0x801216F0), 64); f.write(addr(0x8012116C, 0x801216EC), 0x80006000);
    f.write(0x80006004, 0x80006100); f.text(0x80006100, "HIDDEN TARGET");
    auto observed = f.decode();
    assert(observed.kind == StateKind::TrackSelectMenu && observed.selection_label == "VISIBLE TRACK");
    assert(observed.selection == 104 && observed.menu_detail == "Grid position: row 1, column 1");
    assert(describe_observation(observed).find("HIDDEN TARGET") == std::string::npos);
    const auto original = f.bytes;
    (void)f.decode(); assert(f.bytes == original);
    // Panning shows only fully visible glyph runs, without cursor association.
    f.write(cursor_x, 1); f.write(preview_x, 1); f.write(target_x, std::bit_cast<unsigned>(320.0f));
    f.write(scroll_x, std::bit_cast<unsigned>(160.0f));
    cell(4, -160, 0, 0x80001000, 0x80);
    observed = f.decode(); assert(observed.selection_label.empty() && observed.menu_detail.empty());
    assert(observed.menu_entries.size() == 1 && observed.menu_entries[0].label == "visible text fragment \"TRACK\"");
    f.bytes = original;
    f.write(records + 4 * 16 + 4, 0x80001100);
    observed = f.decode(); assert(observed.selection_label == "?" && !observed.menu_entries[0].enabled);
    // A settled locked cursor has no matching preview/vp1: the old
    // unlocked preview remains loaded, but the drawn '?' is selected.
    f.write(cursor_x, 1); f.write(target_x, std::bit_cast<unsigned>(320.0f));
    f.write(scroll_x, std::bit_cast<unsigned>(320.0f));
    f.write(records + 4 * 16 + 14, 0, 1);
    observed = f.decode();
    assert(observed.selection_label == "?" && !observed.menu_entries[0].enabled);
    assert(observed.menu_detail == "Grid position: row 1, column 2");
    assert(describe_observation(observed).find("HIDDEN TARGET") == std::string::npos);
    f.write(scroll_x, std::bit_cast<unsigned>(160.0f));
    assert(f.decode().selection_label.empty()); // Still panning is not selected.
    f.write(scroll_x, std::bit_cast<unsigned>(320.0f));
    cell(5, 0, 0, 0x80001100); assert(f.decode().selection_label.empty());
    // Invisible/stale record pointers never participate; alpha is for thumbnails.
    f.bytes = original; f.write(records, 0x80800000); f.write(records + 4, 0x80800000);
    assert(f.decode().selection_label == "VISIBLE TRACK");
    cell(5, 0, 0, 0x80001000, 0x80);
    assert(f.decode().selection_label.empty() && f.decode().menu_detail.empty());
    // A later submitted name covers the earlier one. Ambiguity cannot select it.
    assert(f.decode().menu_entries.size() == 1 && f.decode().menu_entries[0].index == 105);
    // Later thumbnail bounds cover the earlier baseline, but not its own label.
    f.bytes = original; cell(5, 0, -88, 0x80001000);
    f.write(records + 5 * 16 + 13, 255, 1);
    const auto thumbs = addr(0x800E05D4, 0x800E0B54);
    for (unsigned i = 0; i < 3; ++i) {
        f.write(thumbs + i * 8, 0x80006200); f.write(thumbs + i * 8 + 6, -48 + i * 32, 2);
    }
    f.write(0x80006200, 64, 1); f.write(0x80006201, 32, 1);
    assert(f.decode().menu_entries.empty());
    // vp1 uses the copied viewport scale. An expanded or malformed
    // rectangle cannot reuse the nominal grid thumbnail exclusion bound.
    f.bytes = original;
    f.write(records + 4 * 16 + 13, 255, 1);
    const auto viewport = addr(0x800DD064, 0x800DD5D4);
    f.write(viewport, 80); f.write(viewport + 4, 60); f.write(viewport + 8, 240); f.write(viewport + 12, 180);
    for (unsigned i = 0; i < 3; ++i) {
        f.write(thumbs + i * 8, 0x80006200); f.write(thumbs + i * 8 + 6, -48 + i * 32, 2);
    }
    f.write(0x80006200, 64, 1); f.write(0x80006201, 32, 1);
    assert(f.decode().selection_label == "VISIBLE TRACK");
    f.write(viewport + 8, 400); observed = f.decode();
    assert(observed.menu_entries.empty() && observed.menu_detail == "Some track labels are unavailable.");
    f.write(viewport + 8, 240); f.write(viewport + 12, 300);
    assert(f.decode().menu_entries.empty());
    // Unknown border geometry withholds affected earlier text, retaining later text.
    f.bytes = original; cell(5, 0, -20, 0x80001000);
    f.write(assets + 5 * 4, 0x80006300); f.write(images + 4 * 32, 1, 2);
    // Both share this invalid asset; move later cell to a known absent model.
    f.write(records + 5 * 16 + 15, 5, 1);
    observed = f.decode(); assert(observed.menu_entries.size() == 1 && observed.menu_entries[0].index == 105);
    assert(observed.menu_detail == "Some track labels are unavailable.");
    // A verified stock model bound excludes ink; it never proves visibility.
    f.bytes = original;
    f.write(assets + 5 * 4, 0x80006300); f.write(0x80006300, 0x80006500);
    f.write(0x80006504, 0x80006600); f.write(0x80006524, 2, 2);
    f.write(0x80006600, -300, 2); f.write(0x80006602, -380, 2);
    f.write(0x8000660A, 300, 2); f.write(0x8000660C, -420, 2);
    f.write(images + 4 * 32 + 8, std::bit_cast<unsigned>(0.215f));
    f.write(images + 4 * 32 + 20, std::bit_cast<unsigned>(-500.0f));
    f.write(addr(0x800DF454, 0x800DF9D4), std::bit_cast<unsigned>(1.0f));
    f.write(addr(0x800DF750, 0x800DFCD0), 0x80006700); f.write(addr(0x800DF754, 0x800DFCD4), 8, 2);
    f.write(addr(0x80120D70, 0x801212F0), 0x80006800);
    for (unsigned i = 0; i < 16; ++i) {
        f.write(0x80006800 + i * 4, i % 5 == 0 ? 0x3F800000U : 0U);
        f.write(addr(0x800DD2B8, 0x800DD828) + i * 4, i == 0 || i == 5 ? 0x3F800000U : i == 15 ? 0x43200000U : 0U);
    }
    assert(f.decode().menu_entries.empty() && f.decode().menu_detail.empty());
    const auto model_fixture = f.bytes;
    for (auto [field, value, width] : std::initializer_list<std::tuple<std::uint32_t,unsigned,unsigned>>{
        {0x80006504, 0x80800000, 4}, {0x80006524, 0xFFFF, 2}, {0x8000670A, 0x8000, 2},
        {images + 4 * 32 + 8, 0x7FC00000, 4}, {0x80006800, 0x40000000, 4},
        {addr(0x800DD2B8, 0x800DD828), 0x40000000, 4}}) {
        f.bytes = model_fixture; f.write(field, value, width);
        observed = f.decode(); assert(observed.menu_entries.empty() && observed.menu_detail == "Some track labels are unavailable.");
    }
    f.bytes = model_fixture; f.write(addr(0x800DF764, 0x800DFCE4), 0);
    assert(f.decode().selection_label == "VISIBLE TRACK");
    // An actual arrow texture painted later can cover text at its copied offset.
    f.bytes = original; f.write(records + 4 * 16 + 14, 0x84, 1);
    f.write(addr(0x800E0678, 0x800E0BF8) + 8, 0x80006400);
    f.write(0x80006400, 0x80006200); f.write(0x80006406, 20, 2);
    f.write(0x80006200, 128, 1); f.write(0x80006201, 16, 1); f.write(0x80006404, -64, 2);
    assert(f.decode().menu_entries.empty());
    for (auto [field, value, width] : std::initializer_list<std::tuple<std::uint32_t,unsigned,unsigned>>{
        {records + 4 * 16 + 4, 0x80800000, 4}, {records + 4 * 16 + 12, 3, 1},
        {records + 4 * 16 + 14, 0x90, 1}, {records + 4 * 16 + 15, 7, 1},
        {scroll_x, 0x7FC00000, 4}, {target_x, 0x7F800000, 4}, {cursor_x, 6, 4},
        {addr(0x8012A7E4, 0x8012ADA4), 0, 4}, {0x80002822, 0, 2}, {0x80000300, 0, 4},
        {addr(0x80126480, 0x80126A20), 264, 4}}) {
        f.bytes = original; f.write(field, value, width); assert(f.decode().kind == StateKind::Unavailable);
    }
    f.bytes = original; f.write(preview_x, 1); assert(f.decode().selection_label.empty());
    f.bytes = original; f.write(records + 4 * 16 + 14, 0, 1); assert(f.decode().selection_label.empty());
}

void check_audio_options(rom::Revision revision) {
    Fixture f(revision);
    const auto addr = [revision](std::uint32_t a, std::uint32_t b) { return revision == rom::Revision::UsV77 ? a : b; };
    const auto strings = addr(0x800DFA3C, 0x800DFFBC);
    const auto outputs = addr(0x801269D0, 0x80126F90);
    const auto output = addr(0x800DFAC8, 0x800E0048);
    const auto sfx = addr(0x800DFAC0, 0x800E0040);
    const auto music = addr(0x800DFAC4, 0x800E0044);
    const auto selected = addr(0x80126C46, 0x80127206);
    f.write(f.a.GameMode, 1); f.write(f.a.CurrentMenuId, 13); f.write(f.a.MenuStage, 4);
    for (unsigned i = 0; i < 3; ++i) {
        f.write(outputs + i * 4, 0x80001000 + i * 128);
        f.text(0x80001000 + i * 128, i == 0 ? "STEREO" : i == 1 ? "MONO" : "HEADPHONES");
    }
    const std::string_view labels[] = {"STEREO", "SFX VOLUME", "MUSIC VOLUME", "RETURN", "AUDIO OPTIONS", "AUDIO OPTIONS"};
    for (unsigned i = 0; i < 6; ++i) {
        const auto pointer = i == 0 ? 0x80001000U : 0x80002000U + std::min(i, 4U) * 128;
        f.write(strings + i * 16 + 12, pointer); f.text(pointer, labels[i]);
    }
    f.write(sfx, 128); f.write(music, 256); f.write(selected, 1, 2);
    auto observed = f.decode();
    assert(observed.kind == StateKind::OptionsMenu && observed.menu_name == "AUDIO OPTIONS");
    assert(observed.menu_entries.size() == 4 && observed.selection == 1);
    assert(observed.selection_label == "SFX VOLUME: slider 50%");
    assert(observed.menu_entries[2].label == "MUSIC VOLUME: slider 100%");
    assert(describe_observation(observed) == "AUDIO OPTIONS. SFX VOLUME: slider 50%.");
    // Adjacent odd volumes have the same drawn arrow position.
    f.write(sfx, 129); assert(f.decode().selection_label == observed.selection_label);
    f.write(sfx, 130); assert(f.decode().selection_label == "SFX VOLUME: slider 51%");
    f.write(sfx, 0); assert(f.decode().selection_label == "SFX VOLUME: slider 0%");
    for (unsigned i = 0; i < 3; ++i) {
        f.write(output, i); f.write(strings + 12, 0x80001000 + i * 128); f.write(selected, 0, 2);
        assert(f.decode().selection_label == (i == 0 ? "STEREO" : i == 1 ? "MONO" : "HEADPHONES"));
    }
    f.write(selected, 3, 2); assert(f.decode().selection_label == "RETURN");
    f.write(f.a.MenuStage, 5); f.write(strings + 6 * 16 + 12, 0x80003000); f.text(0x80003000, "MUSIC TEST 07");
    observed = f.decode();
    assert(observed.menu_entries.size() == 5 && observed.selection_label == "MUSIC TEST 07");
    assert(observed.menu_entries[4].label == "RETURN");
    f.text(0x80003000, "MUSIC TEST 42"); assert(f.decode().selection_label == "MUSIC TEST 42");
    f.write(selected, 4, 2); assert(f.decode().selection_label == "RETURN");
    const auto original = f.bytes; (void)f.decode(); assert(f.bytes == original);
    for (auto [field, value, width] : std::initializer_list<std::tuple<std::uint32_t, unsigned, unsigned>>{
        {f.a.GameMode, 0, 4}, {f.a.CurrentMenuId, 14, 4}, {f.a.MenuDelay, 1, 4}, {f.a.MenuDelay, 0xFFFFFFFFU, 4},
        {f.a.MenuStage, 3, 4}, {f.a.MenuStage, 6, 4}, {selected, 5, 2}, {selected, 0xFFFF, 2},
        {output, 3, 4}, {sfx, 257, 4}, {music, 0xFFFFFFFFU, 4},
        {strings + 12, 0x80001000, 4}, {strings + 16 + 12, 0, 4},
        {strings + 2 * 16 + 12, 0x80800000, 4}, {strings + 5 * 16 + 12, 0, 4},
        {strings + 6 * 16 + 12, 0, 4}, {strings + 7 * 16 + 12, 0x80003000, 4}}) {
        f.bytes = original; f.write(field, value, width);
        const auto rejected = f.decode();
        assert(rejected.kind == StateKind::Unavailable && rejected.menu_entries.empty());
    }
    f.bytes = original; f.write(f.a.MenuStage, 4);
    assert(f.decode().kind == StateKind::Unavailable); // Stage/table mismatch.
    f.write(strings + 6 * 16 + 12, 0); f.write(selected, 3, 2);
    assert(f.decode().selection_label == "RETURN");
    f.write(selected, 4, 2); assert(f.decode().kind == StateKind::Unavailable);
}

void check_save_options(rom::Revision revision) {
    Fixture f(revision);
    const auto addr = [revision](std::uint32_t a, std::uint32_t b) { return revision == rom::Revision::UsV77 ? a : b; };
    const auto count_source = addr(0x80126A08, 0x80126FC8), count_dest = addr(0x80126A00, 0x80126FC0);
    const auto scroll_source = addr(0x80126BDC, 0x8012719C), scroll_dest = addr(0x80126BEC, 0x801271AC);
    const auto selected_source = addr(0x80126BD4, 0x80127194), selected_dest = addr(0x80126BE4, 0x801271A4);
    const unsigned source = 0x80030000, dest = source + 80 * 16, fonts = 0x80040000;
    f.write(f.a.GameMode, 1); f.write(f.a.CurrentMenuId, 14); f.write(f.a.MenuStage, 1);
    f.write(0x80000300, 1); f.write(addr(0x801262B0, 0x80126850), 320); f.write(addr(0x801262B8, 0x80126858), 240);
    f.write(addr(0x800DF4A0, 0x800DFA20), 0x80010000);
    for (auto [id, label] : std::initializer_list<std::pair<unsigned, std::string_view>>{
        {165, "SAVE OPTIONS"}, {124, "PLEASE WAIT"}, {119, "GAME PAK"}, {120, "TIMES"},
        {121, "GHOSTS"}, {122, "EMPTY SLOT"}, {123, "VIEW GHOSTS"}, {181, "GAME PAK BONUSES"},
        {81, "ERASE"}, {86, "CONTROLLER PAK 1"}, {87, "CONTROLLER PAK 2"},
        {88, "CONTROLLER PAK 3"}, {89, "CONTROLLER PAK 4"}}) {
        f.write(0x80010000 + id * 4, 0x80011000 + id * 128);
        f.text(0x80011000 + id * 128, label);
    }
    f.write(addr(0x8012A7E4, 0x8012ADA4), fonts);
    for (unsigned font = 0; font < 3; ++font) {
        f.write(fonts + font * 0x400 + 0x22, 10, 2);
        f.write(fonts + font * 0x400 + 0x24, 6, 2);
        for (unsigned ch = 33; ch <= 126; ++ch) {
            const auto glyph = fonts + font * 0x400 + 0x100 + (ch - 32) * 8;
            f.write(glyph + 1, 6, 1); f.write(glyph + 6, 6, 1); f.write(glyph + 7, 8, 1);
        }
    }
    f.write(addr(0x80126A0C, 0x80126FCC), source);
    f.write(addr(0x80126A04, 0x80126FC4), dest);
    f.write(count_source, 3); f.write(count_dest, 1);
    f.write(source, 2, 1); f.write(dest, 7, 1);
    // The partly clipped neighbor still draws some glyphs. The far card is
    // outside the renderer's visits and its invalid pointer must not be read.
    f.write(source + 16, 2, 1);
    f.write(source + 32, 6, 1); f.write(source + 40, 0xFFFFFFFFU);
    f.text(addr(0x800E0F6C, 0x800E14EC), "ABCDEFGHIJKLMNOPQRSTUVWXYZ.?    ");
    f.write(addr(0x80126530, 0x80126AD0), 0x80050000);
    f.write(0x80050050, (0U << 10) | (1U << 5) | 2U); // ABC
    f.write(0x80050000, 0x80051300); f.write(0x80051300, 12, 2);
    f.write(addr(0x800E03B0, 0x800E0930), 0x80051000); f.text(0x80051000, "GAME A");
    f.write(addr(0x800E1E10, 0x800E2390), 0x80051100); f.text(0x80051100, " (ADV.");
    f.text(0x80051200, "A");
    const unsigned images = 0x80078000, ids = 0x80079000, digits = 0x80073000, adventure1 = 0x80074000, adventure2 = 0x80075000;
    f.write(addr(0x800DF75C, 0x800DFCDC), images);
    f.write(addr(0x800DF750, 0x800DFCD0), ids); f.write(addr(0x800DF754, 0x800DFCD4), 66, 2);
    f.write(addr(0x800DF764, 0x800DFCE4), 255);
    const auto ortho = addr(0x800DD2B8, 0x800DD828);
    f.write(ortho, std::bit_cast<unsigned>(1.0f)); f.write(ortho + 20, std::bit_cast<unsigned>(1.0f));
    f.write(ortho + 60, std::bit_cast<unsigned>(160.0f));
    for (unsigned image : {2U, 11U, 12U}) {
        const unsigned asset = image == 2 ? 2 : image == 11 ? 64 : 65;
        const unsigned sprite = image == 2 ? digits : image == 11 ? adventure1 : adventure2;
        f.write(images + image * 32 + 6, asset, 2); f.write(images + image * 32 + 8, std::bit_cast<unsigned>(1.0f));
        f.write(images + image * 32 + 20, image == 2 ? std::bit_cast<unsigned>(-32.0f) : 0);
        f.write(ids + asset * 2, 0x8000 | asset, 2);
        f.write(addr(0x80126550, 0x80126AF0) + asset * 4, sprite);
        const unsigned frames = image == 2 ? 10 : 1;
        f.write(sprite, frames, 2);
        for (unsigned frame = 0; frame < frames; ++frame) {
            const unsigned list = sprite + 0x100 + frame * 0x100, vertices = list + 0x40, triangles = list + 0x80;
            f.write(sprite + 12 + frame * 4, list);
            // Exact sprite_init_frame shape: texture DMA, 4 vertices, two
            // triangles, pipe-sync, end. No generic display-list interpreter.
            f.write(list, 0x07010008); f.write(list + 4, 0x7A000);
            f.write(list + 8, 0x04190050); f.write(list + 12, vertices & 0x7FFFFF);
            f.write(list + 16, 0x05110020); f.write(list + 20, triangles & 0x7FFFFF);
            f.write(list + 24, 0xE7000000); f.write(list + 32, 0xB8000000);
            const int left = image == 2 ? 0 : -8, right = image == 2 ? 7 : 7;
            const int top = image == 2 ? -1 : 7, bottom = image == 2 ? -9 : -8;
            for (unsigned i = 0; i < 4; ++i) {
                f.write(vertices + i * 10, i == 0 || i == 3 ? left : right, 2);
                f.write(vertices + i * 10 + 2, i < 2 ? top : bottom, 2);
            }
            f.write(triangles + 1, 3, 1); f.write(triangles + 2, 2, 1); f.write(triangles + 3, 1, 1);
            f.write(triangles + 17, 4, 1); f.write(triangles + 18, 3, 1); f.write(triangles + 19, 1, 1);
        }
    }
    for (unsigned stage = 0; stage < 8; ++stage) {
        f.write(f.a.MenuStage, stage);
        const auto observed = f.decode();
        assert(observed.kind == StateKind::OptionsMenu && observed.menu_name == "SAVE OPTIONS");
        const std::string wait = stage == 1 || stage == 2 || stage == 7 ? "PLEASE WAIT" : "";
        assert(observed.menu_detail == wait);
        assert(observed.menu_entries.size() == (stage < 3 ? 0U : stage < 5 ? 2U : stage == 6 ? 4U : 3U));
        assert(observed.selection == (stage == 3 ? 0 : stage == 5 ? 80 : stage == 6 ? 160 : -1));
        if (stage >= 3) assert(observed.menu_entries[0].label == "GAME PAK: TIMES");
        if (stage >= 3) assert(observed.menu_entries[1].label.find("visible text fragment") != std::string::npos);
        if (stage >= 5) assert(observed.menu_entries[2].label == "ERASE");
        if (stage == 6) assert(observed.selection_label == "OK?");
        if (stage == 4 || stage == 7) for (const auto& entry : observed.menu_entries) assert(!entry.enabled && !entry.selected);
    }
    f.write(f.a.MenuStage, 1); assert(describe_observation(f.decode()) == "SAVE OPTIONS. PLEASE WAIT.");
    f.write(f.a.MenuStage, 0); assert(describe_observation(f.decode()) == "SAVE OPTIONS.");
    f.write(f.a.MenuStage, 3);
    const auto baseline = f.bytes;
    for (auto [type, label] : std::initializer_list<std::pair<unsigned, std::string_view>>{
        {1, "GAME PAK: ABC, 12 balloons, Adventure One"}, {2, "GAME PAK: TIMES"}, {3, "CONTROLLER PAK 1: ABC (ADV.A), 0 balloons, Adventure One"},
        {4, "CONTROLLER PAK 1: A"}, {5, "CONTROLLER PAK 1: GHOSTS"},
        {6, "CONTROLLER PAK 1: A"}, {7, "ERASE"}, {8, "CONTROLLER PAK 1: EMPTY SLOT"},
        {9, "VIEW GHOSTS"}, {10, "GAME PAK: GAME PAK BONUSES"}}) {
        f.bytes = baseline; f.write(source, type, 1); f.write(source + 4, 34, 2); f.write(source + 8, 0x80051200);
        const auto before = f.bytes;
        assert(f.decode().selection_label == label);
        assert(f.bytes == before); // No validation, deletion or copy operation.
    }
    f.bytes = baseline; f.write(source, 1, 1); f.write(0x8005004B, 1, 1);
    assert(f.decode().selection_label == "GAME PAK: GAME A");
    f.write(0x8005004B, 0, 1); f.write(0x80050010, 4); f.write(source + 7, 255, 1); f.write(source + 12, 0xFFFFFFFFU);
    assert(f.decode().selection_label == "GAME PAK: ABC, 12 balloons, Adventure Two"); // Hidden file metadata remains unused.
    f.write(0x8005004B, 0, 1); f.write(0x80050050, 31U * 1057U);
    assert(f.decode().selection_label == "GAME PAK, 12 balloons, Adventure Two"); // Empty printed filename.
    f.bytes = baseline;
    f.write(scroll_source, std::bit_cast<unsigned>(0.5f));
    assert(f.decode().selection_label == "GAME PAK: TIMES" && f.decode().menu_entries.size() == 2); // Fully visible fields on clipped cards.
    f.bytes = baseline; f.write(count_source, 1);
    // Glyph ink, not just advance/card extent, governs label visibility.
    const auto times_glyph = fonts + 0x400 + 0x100 + ('T' - 32) * 8;
    f.write(times_glyph + 2, 255, 1);
    assert(f.decode().selection_label == "GAME PAK: visible text fragment \"IMES\"");
    f.write(times_glyph, 255, 1);
    assert(f.decode().selection_label == "GAME PAK: visible text fragment \"IMES\"");
    f.bytes = baseline; f.write(count_source, 1); f.write(source, 4, 1); f.write(source + 8, 0x80051200); f.text(0x80051200, "A A A");
    const auto letter_a = fonts + 0x400 + 0x100 + ('A' - 32) * 8;
    f.write(letter_a + 2, revision == rom::Revision::UsV77 ? 146 : 145, 1);
    assert(f.decode().selection_label == "CONTROLLER PAK 1: A A A");
    // get_text_width kerns spaces, but render_text_string advances their full
    // width. At the right edge that one-pixel origin difference clips ink.
    f.write(addr(0x8012A7F0, 0x8012ADB0), 1);
    assert(f.decode().selection_label == "CONTROLLER PAK 1: visible text fragment \"A A\"");
    f.bytes = baseline; f.write(source, 1, 1);
    const auto save_symbols = f.bytes;
    f.write(count_source, 2); f.write(scroll_source, std::bit_cast<unsigned>(0.675f)); // x=-30: only the second digit is fully visible.
    assert(f.decode().selection_label == "GAME PAK: ABC, visible balloon count fragment \"...2\"");
    assert(f.decode().selection_label.find("12 balloons") == std::string::npos);
    f.bytes = save_symbols; f.write(0x80050010, 4);
    assert(f.decode().selection_label == "GAME PAK: ABC, 12 balloons, Adventure Two");
    const auto read_only_symbols = f.bytes; (void)f.decode(); assert(f.bytes == read_only_symbols);
    f.bytes = save_symbols; f.write(addr(0x800DF764, 0x800DFCE4), 0);
    assert(f.decode().selection_label == "GAME PAK: ABC" && f.decode().menu_detail.empty());
    f.bytes = save_symbols; f.write(addr(0x80126550, 0x80126AF0) + 2 * 4, 0);
    assert(f.decode().selection_label == "GAME PAK: ABC, Adventure One"); // No digit resource is drawn.
    f.bytes = save_symbols; f.write(addr(0x80126550, 0x80126AF0) + 64 * 4, 0);
    assert(f.decode().selection_label == "GAME PAK: ABC, 12 balloons"); // No icon resource is drawn.
    // Unsupported/malformed sprite fields do not erase independently visible
    // printed labels or authorize reading hidden file metadata.
    for (auto [field, value, width] : std::initializer_list<std::tuple<unsigned, unsigned, unsigned>>{
        {digits + 12 + 4, 0, 4}, {digits + 0x200 + 8, 0x04190051, 4},
        {digits + 0x200 + 20, 0x800000, 4}, {digits + 0x280 + 1, 0, 1},
        {digits + 0x240 + 10, 9, 2}, // Inconsistent rectangle vertices.
        {images + 2 * 32 + 8, 0x7FC00000, 4}, {ids + 2 * 2, 0xC002, 2},
        {digits, 9, 2}, {digits + 6, 2, 2}, {0x80050000, 0xFFFFFFFFU, 4},
        {0x80051300, 100, 2}, {ortho, 0, 4}, {addr(0x80120CE4, 0x80121264), 4, 4}}) {
        f.bytes = save_symbols; f.write(field, value, width);
        const auto observed = f.decode();
        assert(observed.kind == StateKind::OptionsMenu && observed.selection_label.find("GAME PAK: ABC") == 0);
        assert(observed.selection_label.find("12 balloons") == std::string::npos);
        assert(observed.menu_detail == "Some balloon counts or Adventure icons are unavailable");
    }
    f.bytes = save_symbols; f.write(images + 11 * 32 + 0x18, 1, 2);
    assert(f.decode().selection_label == "GAME PAK: ABC, 12 balloons");
    assert(!f.decode().menu_detail.empty());
    // Same-card name ink is drawn after the digits. Wide copied metrics can
    // obscure both digits even when their frame vertices fit the viewport.
    f.bytes = save_symbols; f.write(source, 3, 1); f.write(source + 2, 12, 1);
    f.write(source + 4, 34, 2); f.write(source + 8, 0x80051200); f.write(fonts + 0x420, 12, 2);
    assert(f.decode().selection_label == "CONTROLLER PAK 1: ABC (ADV.A), Adventure One");
    // A later neighboring card background can cover an outlying first digit.
    f.bytes = save_symbols;
    for (unsigned i = 0; i < 4; ++i) f.write(digits + 0x240 + i * 10, i == 0 || i == 3 ? 200 : 207, 2);
    assert(f.decode().selection_label == "GAME PAK: ABC, visible balloon count fragment \"...2\", Adventure One");
    // The renderer paints forward cards first, then the previous card. A
    // left neighbor can therefore cover a digit belonging to the center card.
    f.bytes = save_symbols; f.write(count_source, 2); f.write(source, 2, 1); f.write(source + 16, 1, 1);
    f.write(selected_source, 1); f.write(scroll_source, std::bit_cast<unsigned>(1.0f));
    for (unsigned i = 0; i < 4; ++i) f.write(digits + 0x240 + i * 10, i == 0 || i == 3 ? -70 : -63, 2);
    assert(f.decode().selection_label == "GAME PAK: ABC, visible balloon count fragment \"...2\", Adventure One");
    // The later same-card device texture also participates in visibility.
    f.bytes = save_symbols;
    const auto device_icon = addr(0x800DFC10, 0x800E0190);
    f.write(device_icon, 0x8007B000); f.write(device_icon + 4, -34, 2); f.write(device_icon + 6, 44, 2);
    f.write(0x8007B000, 32, 1); f.write(0x8007B001, 8, 1);
    assert(f.decode().selection_label == "GAME PAK: ABC, Adventure One");
    // Long text can extend underneath the next card's later-painted background.
    // Its hidden suffix must not be reconstructed as the selected full name.
    f.bytes = baseline; f.write(source, 6, 1); f.write(source + 8, 0x80051200);
    f.text(0x80051200, std::string(50, 'A'));
    const auto clipped_name = f.decode().selection_label;
    assert(clipped_name.find("visible text fragment") != std::string::npos);
    assert(clipped_name.find(std::string(39, 'A')) != std::string::npos);
    assert(clipped_name.find(std::string(50, 'A')) == std::string::npos);
    // Last-painted confirmation text can obscure a background count without
    // hiding a card's other text. Artificial tall big-font glyphs reproduce it.
    f.bytes = save_symbols; f.write(f.a.MenuStage, 6);
    f.write(fonts + 2 * 0x400 + 0x22, 40, 2);
    for (unsigned char ch : std::string("OK?")) {
        const auto glyph = fonts + 2 * 0x400 + 0x100 + (ch - 32) * 8;
        f.write(glyph + 2, 0, 1); f.write(glyph + 6, 100, 1); f.write(glyph + 7, 30, 1);
    }
    // Shift actual digit frame1 under the overlay's first glyph at x151.
    for (unsigned i = 0; i < 4; ++i) f.write(digits + 0x240 + i * 10, i == 0 || i == 3 ? 44 : 51, 2);
    const auto covered = f.decode();
    assert(covered.selection_label == "OK?");
    assert(covered.menu_entries[0].label.find("12 balloons") == std::string::npos);
    f.bytes = baseline;
    for (auto [field, value, width] : std::initializer_list<std::tuple<unsigned, unsigned, unsigned>>{
        {f.a.MenuStage, 16, 4}, {f.a.MenuDelay, 1, 4}, {f.a.LevelLoadTimer, 1, 2},
        {0x80000300, 0, 4}, {addr(0x801262C8, 0x80126868), 2, 4},
        {addr(0x801262B0, 0x80126850), 640, 4}, {addr(0x801262B8, 0x80126858), 264, 4},
        {count_source, 81, 4}, {count_source, 0, 4}, {selected_source, 3, 4},
        {scroll_source, 0x7FC00000, 4}, {scroll_source, 0x7F800000, 4}, {scroll_source, 0xBF800000, 4},
        {source, 11, 1}, {source, 0, 1}, {addr(0x80126A0C, 0x80126FCC), 0, 4},
        {addr(0x8012A7E4, 0x8012ADA4), 0, 4}, {fonts + 0x422, 0, 2}}) {
        f.bytes = baseline; f.write(field, value, width);
        assert(f.decode().kind == StateKind::Unavailable && f.decode().menu_entries.empty());
    }
    f.bytes = baseline; f.write(f.a.MenuStage, 5); f.write(selected_dest, 1);
    assert(f.decode().kind == StateKind::Unavailable);
    f.write(selected_dest, 0); f.write(scroll_dest, std::bit_cast<unsigned>(1.0f));
    assert(f.decode().kind == StateKind::Unavailable);

    // A live error dialog takes precedence over both visible rows. Option
    // identity comes from the actual queued nodes, not highlight colors or
    // an unrelated stale source-card selection.
    f.bytes = baseline; f.write(f.a.MenuStage, 13);
    const unsigned windows = 0x80060000, pool = 0x80061000, window = windows + 7 * 40;
    f.write(addr(0x8012A7E8, 0x8012ADA8), windows); f.write(addr(0x8012A7EC, 0x8012ADAC), pool);
    f.write(window + 4, 40, 2); f.write(window + 6, 20, 2); f.write(window + 8, 280, 2);
    f.write(window + 10, 220, 2); f.write(window + 12, 241, 2); f.write(window + 14, 201, 2);
    f.write(window + 30, 0x8000, 2); f.write(window + 36, pool);
    for (unsigned i = 0; i < 4; ++i) {
        f.write(pool + i * 32 + 1, 2, 1); f.write(pool + i * 32 + 4, 0x80062000 + i * 128);
        f.text(0x80062000 + i * 128, i == 0 ? "PAK ERROR" : i == 1 ? "CONTROLLER ~" : i == 2 ? "RETRY" : "CANCEL");
        f.write(pool + i * 32 + 8, 10, 2); f.write(pool + i * 32 + 10, 20 + i * 24, 2);
        f.write(pool + i * 32 + 24, 255, 1); f.write(pool + i * 32 + 28, i == 3 ? 0 : pool + (i + 1) * 32);
    }
    const auto messages = addr(0x80126A80, 0x80127040);
    f.write(addr(0x80126A74, 0x80127034), 2); f.write(addr(0x80126A78, 0x80127038), 1);
    f.write(messages, pool + 64); f.write(messages + 4, pool + 96);
    auto modal = f.decode();
    assert(modal.kind == StateKind::Dialogue && modal.menu_entries.size() == 2 && modal.selection_label == "CANCEL");
    assert(modal.menu_detail == "PAK ERROR CONTROLLER 2");
    f.write(pool + 32 + 16, 0xCFCFCF5A); // Body color cannot create a third choice.
    assert(f.decode().menu_entries.size() == 2 && f.decode().selection_label == "CANCEL");
    const auto modal_memory = f.bytes; (void)f.decode(); assert(f.bytes == modal_memory);
    f.write(source, 255, 1); assert(f.decode().selection_label == "CANCEL"); // Covered card isn't inspected.
    f.write(messages + 4, pool + 64); assert(f.decode().kind == StateKind::Unavailable);
    f.bytes = modal_memory; f.write(messages + 4, pool + 128); assert(f.decode().kind == StateKind::Unavailable);
    f.bytes = modal_memory; f.write(window + 30, 0, 2); assert(f.decode().kind == StateKind::Unavailable);
    f.bytes = modal_memory; f.write(addr(0x80126A74, 0x80127034), 5); assert(f.decode().kind == StateKind::Unavailable);
    f.bytes = modal_memory; f.write(addr(0x80126A78, 0x80127038), 2); assert(f.decode().kind == StateKind::Unavailable);
    f.bytes = modal_memory; f.write(pool + 3 * 32 + 10, 220, 2); assert(f.decode().kind == StateKind::Unavailable);
}

void check_character_confirmation(rom::Revision revision) {
    Fixture f(revision);
    const auto addr = [revision](unsigned a, unsigned b) { return revision == rom::Revision::UsV77 ? a : b; };
    const auto roster = addr(0x800DFDD0, 0x800E0350);
    f.write(f.a.GameMode, 1); f.write(f.a.CurrentMenuId, 3); f.write(f.a.NumberOfActivePlayers, 1);
    f.write(f.a.PlayersCharacterArray, 0xFF01FFFF); f.write(f.a.ActivePlayersArray + 1, 1, 1);
    f.write(addr(0x801263CC, 0x8012696C), roster);
    const unsigned voices[] = {0, 9, 1, 5, 3, 2, 7, 4};
    for (unsigned i = 0; i < 8; ++i) f.write(roster + i * 14 + 12, voices[i], 2);
    const auto status = f.a.CharacterSelectStatus + 1;
    const auto before = f.decode();
    const auto portrait_speech = describe_observation(before);
    assert(before.selection == 1 && before.selection_label == "Diddy Kong" && before.menu_entries.size() == 8);
    assert(portrait_speech == "Character select. Diddy Kong.");
    // First A updates readiness after that frame's text has been drawn. Do
    // not announce a new prompt solely from the transient CONFIRMED status.
    f.write(status, 1, 1); f.write(f.a.NumberOfReadyPlayers, 1);
    auto observed = f.decode();
    assert(observed.kind == StateKind::CharacterMenu && observed.selection_label == "Diddy Kong");
    assert(describe_observation(observed) == portrait_speech);
    for (const auto& entry : observed.menu_entries) assert(!entry.enabled);
    // The following READY sample corresponds to the stable rendered OK?.
    f.write(status, 2, 1);
    observed = f.decode();
    assert(observed.kind == StateKind::CharacterMenu && observed.menu_entries.size() == 9);
    assert(observed.selection == 8 && observed.selection_label == "OK?");
    assert(observed.menu_detail == "Selected character: Diddy Kong");
    assert(observed.menu_entries.back().selected && observed.menu_entries.back().enabled);
    for (unsigned i = 0; i < 8; ++i) assert(!observed.menu_entries[i].selected && !observed.menu_entries[i].enabled);
    const auto ready_speech = describe_observation(observed);
    assert(ready_speech == "Character select. OK?. Selected character: Diddy Kong.");
    assert(ready_speech != portrait_speech);
    const auto ready_memory = f.bytes; (void)f.decode(); assert(f.bytes == ready_memory);
    // B deselects the character; the portrait announcement and navigation
    // return, without a stale OK? or selected-character context.
    f.write(status, 0, 1); f.write(f.a.NumberOfReadyPlayers, 0);
    observed = f.decode();
    assert(observed.menu_entries.size() == 8 && observed.selection == 1 && observed.menu_detail.empty());
    assert(describe_observation(observed) == portrait_speech);
    // Confirmation begins a fade. The observer's existing non-fade gate
    // withholds the departing menu instead of inventing the next screen.
    f.bytes = ready_memory; f.write(f.a.MenuDelay, 1);
    assert(f.decode().kind == StateKind::Unavailable);
    for (auto [field, value, width] : std::initializer_list<std::tuple<unsigned, unsigned, unsigned>>{
        {status, 0, 1}, {status, 3, 1}, {status, 255, 1}, {f.a.NumberOfReadyPlayers, 0, 4},
        {f.a.NumberOfReadyPlayers, 2, 4}, {f.a.NumberOfReadyPlayers, 0xFFFFFFFFU, 4},
        {f.a.NumberOfActivePlayers, 0, 4}, {f.a.NumberOfActivePlayers, 2, 4},
        {f.a.ActivePlayersArray + 1, 0, 1}, {f.a.ActivePlayersArray + 1, 2, 1},
        {f.a.ActivePlayersArray, 1, 1}, {f.a.CharacterSelectStatus, 1, 1},
        {f.a.PlayersCharacterArray, 0, 1}, {f.a.PlayersCharacterArray + 1, 255, 1}}) {
        f.bytes = ready_memory; f.write(field, value, width);
        const auto rejected = f.decode();
        assert(rejected.kind == StateKind::Unavailable && rejected.menu_entries.empty());
    }
    // The sole active player need not occupy controller port one. Inactive
    // slots/statuses must remain consistent for every supported port.
    for (unsigned port = 0; port < 4; ++port) {
        f.bytes = ready_memory;
        for (unsigned i = 0; i < 4; ++i) {
            f.write(f.a.ActivePlayersArray + i, i == port ? 1 : 0, 1);
            f.write(f.a.PlayersCharacterArray + i, i == port ? 5 : 255, 1);
            f.write(f.a.CharacterSelectStatus + i, i == port ? 2 : 0, 1);
        }
        assert(f.decode().selection_label == "OK?" && f.decode().menu_detail == "Selected character: Tiptup");
    }
}

void check_menus_and_items(rom::Revision revision) {
    Fixture f(revision);
    const auto addr = [revision](std::uint32_t a, std::uint32_t b) { return revision == rom::Revision::UsV77 ? a : b; };
    const auto menu_text = addr(0x800DF4A0, 0x800DFA20);
    f.write(menu_text, 0x80010000);
    for (auto [id, label] : std::initializer_list<std::pair<unsigned, std::string_view>>{
             {8, "ENTER INITIALS"}, {77, "GAME TO COPY"}, {78, "GAME TO COPY TO"}, {79, "GAME TO ERASE"},
             {80, "COPY"}, {81, "ERASE"}, {85, "CANCEL"}, {134, "OK"}, {146, "ADVENTURE"}, {147, "ADVENTURE TWO"}}) {
        f.write(0x80010000 + id * 4, 0x80011000 + id * 128);
        f.text(0x80011000 + id * 128, label);
    }
    f.write(f.a.GameMode, 1); f.write(f.a.CurrentMenuId, 0); f.write(f.a.TitleRevealTimer, 0);
    assert(f.decode().kind == StateKind::Unavailable);
    const auto title_selection = addr(0x800DF45C, 0x800DF9DC);
    const auto controller_status = addr(0x800DD384, 0x800DD8F4);
    f.write(f.a.TitleRevealTimer, 32); f.write(f.cursor(), 0); f.write(title_selection, 1);
    f.write(addr(0x800DF7A4, 0x800DFD24), 0x80001000); f.text(0x80001000, "START");
    f.write(addr(0x800DF7A4, 0x800DFD24) + 4, 0x80001100); f.text(0x80001100, "OPTIONS");
    assert(f.decode().kind == StateKind::TitleMenu && f.decode().menu_entries.size() == 2);
    assert(f.decode().selection_label == "OPTIONS");
    const auto title_memory = f.bytes; (void)f.decode(); assert(f.bytes == title_memory);
    f.write(title_selection, 0); f.write(f.cursor(), 1);
    assert(f.decode().selection_label == "START");
    for (unsigned invalid : {2U, 0xFFFFFFFFU}) {
        f.write(title_selection, invalid);
        const auto rejected = f.decode();
        assert(rejected.kind == StateKind::Unavailable && rejected.menu_entries.empty());
    }
    f.write(title_selection, 1); f.write(controller_status, 0xFFFFFFFFU);
    const auto missing_controller = f.decode();
    assert(missing_controller.kind == StateKind::Unavailable && missing_controller.menu_entries.empty());
    f.write(controller_status, 0); assert(f.decode().selection_label == "OPTIONS");
    f.write(f.a.TitleRevealTimer, 31); assert(f.decode().kind == StateKind::Unavailable);
    f.write(f.a.CurrentMenuId, 19); f.write(f.a.MenuStage, 1);
    f.write(addr(0x80126460, 0x80126A00), 0x80012000);
    f.write(0x80012000 + 3 * 32 + 0x14, 0x80001300); f.text(0x80001300, "ADVENTURE");
    f.write(0x80012000 + 5 * 32 + 0x14, 0x80001400); f.text(0x80001400, "TRACKS");
    assert(f.decode().kind == StateKind::GameSelectMenu && f.decode().selection_label == "TRACKS");
    f.write(f.a.MenuStage, 2); assert(f.decode().kind == StateKind::Unavailable);
    f.write(0x80012000 + 7 * 32 + 0x14, 0x80001500); f.text(0x80001500, "TRACKS");
    f.text(0x80001400, "ADVENTURE TWO");
    assert(f.decode().menu_entries.size() == 3 && f.decode().selection_label == "ADVENTURE TWO");
    f.write(f.a.MenuDelay, 1); assert(f.decode().kind == StateKind::Unavailable); f.write(f.a.MenuDelay, 0);
    const auto original = f.bytes; (void)f.decode(); assert(f.bytes == original);
    f.write(f.a.CurrentMenuId, 3); f.write(f.a.NumberOfActivePlayers, 1);
    f.write(f.a.PlayersCharacterArray, 0xFF01FFFF);
    f.write(f.a.ActivePlayersArray + 1, 1, 1);
    const auto roster = addr(0x800DFDD0, 0x800E0350);
    f.write(addr(0x801263CC, 0x8012696C), roster);
    const unsigned voices[] = {0, 9, 1, 5, 3, 2, 7, 4};
    for (unsigned i = 0; i < 8; ++i) f.write(roster + i * 14 + 12, voices[i], 2);
    assert(f.decode().kind == StateKind::CharacterMenu && f.decode().selection_label == "Diddy Kong");
    f.write(addr(0x801263CC, 0x8012696C), 0x80020000); assert(f.decode().kind == StateKind::Unavailable);
    f.write(addr(0x801263CC, 0x8012696C), roster); f.write(f.a.PlayersCharacterArray + 1, 8, 1);
    assert(f.decode().kind == StateKind::Unavailable);
    f.write(f.a.CurrentMenuId, 6); f.write(f.a.MenuStage, 0);
    f.write(addr(0x800DF4CC, 0x800DFA4C), 1);
    for (unsigned i = 0; i < 3; ++i) {
        f.text(addr(0x800E03B0, 0x800E0930) + i * 4, "DKR");
        f.text(addr(0x801264A0, 0x80126A40) + i * 12 + 4, "ABC");
    }
    f.write(addr(0x801264A0, 0x80126A40) + 13, 1, 1);
    f.write(addr(0x801264A0, 0x80126A40) + 14, 12, 2);
    assert(f.decode().kind == StateKind::FileSelectMenu && f.decode().menu_entries.size() == 5);
    assert(f.decode().selection_label == "File 2: ABC, 12 balloons");
    f.write(f.a.MenuStage, 1); assert(f.decode().selection_label == "COPY");
    f.write(addr(0x80126484, 0x80126A24), 1); f.write(addr(0x8012648C, 0x80126A2C), 1);
    assert(f.decode().menu_name == "GAME TO COPY" && f.decode().menu_entries[1].enabled);
    f.write(addr(0x80126494, 0x80126A34), 1); f.write(addr(0x80126490, 0x80126A30), 0);
    assert(f.decode().menu_name == "GAME TO COPY TO" && !f.decode().menu_entries[1].enabled);
    f.write(addr(0x80126494, 0x80126A34), 2); assert(f.decode().selection_label == "OK?");
    f.write(addr(0x80126484, 0x80126A24), 0); f.write(addr(0x80126488, 0x80126A28), 1);
    f.write(addr(0x80126494, 0x80126A34), 0); assert(f.decode().menu_name == "GAME TO ERASE");
    f.write(addr(0x80126494, 0x80126A34), 1); assert(f.decode().selection_label == "OK?");
    f.write(addr(0x80126488, 0x80126A28), 0); f.write(addr(0x80126CC0, 0x80127280), 1);
    f.write(addr(0x80126C6C, 0x8012722C), 0x80013000); f.write(0x80013000, 29);
    f.write(addr(0x80126C74, 0x80127238), 0x80013100); f.text(0x80013100, "AB");
    f.write(addr(0x800E0FA0, 0x800E1520), 2); f.write(addr(0x80126C78, 0x8012723C), 3);
    assert(f.decode().selection_label == "Delete" && f.decode().menu_detail == "Initials: AB");
    f.write(0x80013000, 31); assert(f.decode().kind == StateKind::Unavailable);
    f.write(addr(0x80126CC0, 0x80127280), 0);
    f.write(f.a.CurrentMenuId, 15); f.write(f.a.MenuStage, 0);
    f.write(addr(0x801269C8, 0x80126F88), 0); f.write(addr(0x801269CC, 0x80126F8C), 0);
    f.write(addr(0x801268E8, 0x80126EA8), 1, 2);
    f.write(addr(0x80121170, 0x801216F0), 64); f.write(addr(0x8012116C, 0x801216EC), 0x80014000);
    f.write(0x80014004, 0x80015000); f.text(0x80015000, "ANCIENT LAKE"); f.text(0x8001500D, "GERMAN");
    f.write(addr(0x801267D0, 0x80126D70), 1);
    f.write(addr(0x8012117C, 0x801216FC), 0x80016000); f.write(0x80016008, 0x70, 1);
    f.write(addr(0x80123510, 0x80123A90), 0x80017000); f.write(0x80017004, 0x80018000); f.write(0x80018004, 2);
    f.write(f.a.PlayerSelectVehicle, 2, 1);
    assert(f.decode().menu_name == "Vehicle" && f.decode().selection_label == "Plane");
    f.write(0x80018004, 0); assert(!f.decode().menu_entries[0].enabled && f.decode().menu_entries[2].enabled);
    f.write(f.a.MenuStage, 1); f.write(addr(0x800E0414, 0x800E0994), 1);
    assert(f.decode().menu_name == "Time trial" && f.decode().selection_label == "On");
    f.write(f.a.MenuStage, 2); assert(f.decode().selection_label == "OK?");
    f.race();
    f.write(f.a.IsPaused, 1, 1); f.write(addr(0x800E0984, 0x800E0F04), 2);
    f.write(addr(0x80126A68, 0x80127028), 1);
    f.write(addr(0x80126A40, 0x80127000), 0x80019000); f.text(0x80019000, "CONTINUE");
    f.write(addr(0x80126A40, 0x80127000) + 4, 0x80019100); f.text(0x80019100, "QUIT GAME");
    assert(f.decode().kind == StateKind::PauseMenu && f.decode().selection_label == "QUIT GAME" && !f.decode().racer);
    f.write(addr(0x800E0988, 0x800E0F08), 2); assert(f.decode().selection_label == "CANCEL");
    f.write(addr(0x800E0988, 0x800E0F08), 3); assert(f.decode().kind == StateKind::Unavailable);
    f.race(); assert(f.decode().racer->held_item->category == ItemCategory::None);
    for (unsigned type = 0; type < 5; ++type) {
        f.write(0x80005172, type, 1); f.write(0x80005173, 2, 1); f.write(0x80005174, 2, 1);
        const auto item = f.decode().racer->held_item;
        assert(item && item->category == static_cast<ItemCategory>(type + 1) && item->quantity == 2 && item->level == 3);
    }
    assert(describe_observation(f.decode()).find("Magnet, level 3, quantity 2") != std::string::npos);
    f.write(0x80005174, 3, 1); assert(!f.decode().racer->held_item);
    f.write(0x80005174, 2, 1); f.write(0x80005173, 0xFF, 1); assert(!f.decode().racer->held_item);
    f.write(0x80005173, 2, 1);
    f.write(0x80005172, 5, 1); assert(!f.decode().racer->held_item);
    f.write(0x80005173, 0, 1); assert(f.decode().racer->held_item->category == ItemCategory::None);
    // The setup toggle is adjacent to gIsTimeTrial, but is_in_time_trial reads
    // only the latter byte. An enabled setup alone does not hide the race HUD.
    Fixture trial(revision); trial.race();
    const auto trial_active = addr(0x8011AEF5, 0x8011B475);
    trial.write(trial_active - 1, 1, 1); assert(trial.decode().racer);
    trial.write(trial_active + 1, 0xFFFF, 2); assert(trial.decode().racer);
    trial.write(trial_active, 1, 1);
    const auto hidden_trial = trial.decode();
    assert(hidden_trial.kind == StateKind::Race && !hidden_trial.racer);
    trial.write(0x800051AE, 7, 2); trial.write(0x80005194, 2, 1);
    trial.write(0x80005185, 9, 1); trial.write(0x80005172, 4, 1);
    trial.write(0x80005173, 2, 1); trial.write(0x80005174, 2, 1);
    assert(describe_observation(trial.decode()) == describe_observation(hidden_trial));
    trial.write(trial_active, 0, 1); assert(trial.decode().racer->position == 7);
    for (auto [field, value, width] : std::initializer_list<std::tuple<std::uint32_t, unsigned, unsigned>>{
        {addr(0x80126CD2, 0x80127292), 1, 1}, {addr(0x80126D34, 0x801272F4), 0, 1},
        {f.a.NumberOfActivePlayers, 2, 4}, {f.a.CutsceneCameraActive, 1, 1},
        {addr(0x800DF4C0, 0x800DFA40), 1, 4}, {addr(0x8012718A, 0x8012774A), 1, 1}, {0x800020BC, 2, 1}}) {
        Fixture hidden(revision); hidden.race(); hidden.write(field, value, width);
        assert(!hidden.decode().racer);
        hidden.write(0x800051AE, 7, 2); hidden.write(0x80005172, 4, 1);
        assert(!hidden.decode().racer); // Hidden state never enters the observation.
    }
}

void check_adventure_journey(rom::Revision revision) {
    Fixture f(revision);
    const auto addr = [revision](std::uint32_t a, std::uint32_t b) { return revision == rom::Revision::UsV77 ? a : b; };
    const auto strings = [&f](std::uint32_t table, unsigned count, std::uint32_t storage, std::string_view label) {
        for (unsigned i = 0; i < count; ++i) { f.write(table + i * 4, storage + i * 128); f.text(storage + i * 128, label); }
    };
    f.write(f.a.GameMode, 1); f.write(f.a.CurrentMenuId, 28);
    const auto caution = addr(0x800DFFD8, 0x800E0558);
    for (unsigned i = 0; i < 13; ++i) {
        f.write(caution + i * 32 + 20, 0x80020000 + i * 128);
        f.text(0x80020000 + i * 128, i < 2 ? "CAUTION" : "Controller Pak caution text.");
    }
    f.write(addr(0x801263C4, 0x80126964), 60);
    auto observed = f.decode();
    assert(observed.kind == StateKind::Dialogue && observed.menu_name == "CAUTION");
    assert(observed.menu_detail.find("Controller Pak caution text.") != std::string::npos);
    assert(observed.menu_detail.find("Press A, B, or Start") != std::string::npos && !observed.menu_entries[0].enabled);
    f.write(addr(0x801263C4, 0x80126964), 0); assert(f.decode().menu_entries[0].enabled);
    f.write(caution + 20 + 2 * 32, 0xB0000000); assert(f.decode().kind == StateKind::Unavailable);

    // A live font window contains only the current page's queued elements.
    const std::uint32_t windows = 0x80022000, pool = 0x80023000;
    f.write(addr(0x8012A7E8, 0x8012ADA8), windows); f.write(addr(0x8012A7EC, 0x8012ADAC), pool);
    const auto window = [&](unsigned id, bool open) {
        f.write(windows + id * 40 + 4, 20, 2); f.write(windows + id * 40 + 6, 10, 2);
        f.write(windows + id * 40 + 8, 250, 2); f.write(windows + id * 40 + 10, 200, 2);
        f.write(windows + id * 40 + 12, 230, 2); f.write(windows + id * 40 + 14, 190, 2);
        f.write(windows + id * 40 + 30, open ? 0x8000 : 0, 2);
    };
    const auto node = [&](unsigned i, std::string_view text, unsigned y, unsigned colour, unsigned opacity = 255) {
        f.write(pool + i * 32 + 1, 1, 1); f.write(pool + i * 32 + 4, 0x80024000 + i * 128);
        f.text(0x80024000 + i * 128, text); f.write(pool + i * 32 + 8, 5, 2); f.write(pool + i * 32 + 10, y, 2);
        f.write(pool + i * 32 + 16, colour, 4); f.write(pool + i * 32 + 24, opacity, 1);
        f.write(pool + i * 32 + 25, 0, 1); f.write(pool + i * 32 + 28, 0);
    };
    f.write(f.a.GameMode, 0); f.write(f.a.IsInRace, 0);
    window(1, true); f.write(windows + 40 + 36, pool);
    node(0, "VEHICLE SELECT", 6, 0xCFCFCF5A); // inherited selected colour on heading is not selection.
    node(1, "CAR", 30, 0x00007F5A); node(2, "HOVERCRAFT", 50, 0xCFCFCF5A);
    node(3, "PLANE", 70, 0x00007F5A, 120);
    node(4, "UNREVEALED NEXT PAGE", 90, 0xCFCFCF5A);
    for (unsigned i = 0; i < 3; ++i) f.write(pool + i * 32 + 28, pool + (i + 1) * 32);
    observed = f.decode();
    assert(observed.kind == StateKind::Dialogue && observed.selection_label == "HOVERCRAFT");
    assert(observed.menu_entries.size() == 3 && !observed.menu_entries[2].enabled);
    assert(observed.menu_detail == "VEHICLE SELECT");
    f.text(0x80024000 + 4 * 128, "CHANGED UNREVEALED PAGE");
    assert(describe_observation(f.decode()) == describe_observation(observed));
    // Even linked text outside the active clipped view is not announced.
    f.write(pool + 3 * 32 + 28, pool + 4 * 32); f.write(pool + 4 * 32 + 10, 400, 2);
    assert(describe_observation(f.decode()) == describe_observation(observed));
    f.write(pool + 4 * 32 + 4, 0xB0000000); assert(describe_observation(f.decode()) == describe_observation(observed));
    f.write(pool + 3 * 32 + 28, 0);
    const auto original = f.bytes; (void)f.decode(); assert(f.bytes == original);
    f.write(pool + 3 * 32 + 28, pool); assert(f.decode().kind == StateKind::Unavailable);
    f.write(pool + 3 * 32 + 28, 0); window(1, false); assert(f.decode().kind == StateKind::Unavailable);
    window(1, true); f.write(windows + 40 + 36, pool + 1); assert(f.decode().kind == StateKind::Unavailable);
    f.write(windows + 40 + 36, pool); f.write(f.a.LevelLoadTimer, 1, 2); assert(f.decode().kind == StateKind::Unavailable);
    f.write(f.a.LevelLoadTimer, 0, 2);
    node(0, "COLLECT ~ BANANAS", 10, 0xFFFFFFFF); f.write(pool + 1, 10, 1);
    f.write(addr(0x8012A789, 0x8012AD49), 1, 1); f.write(addr(0x8012A787, 0x8012AD47), 1, 1);
    observed = f.decode(); assert(observed.selection_label == "COLLECT 10 BANANAS");
    assert(observed.menu_detail.find("Press A to continue") != std::string::npos);
    window(1, false); window(6, true); f.write(windows + 6 * 40 + 36, pool);
    assert(f.decode().menu_name == "Message" && f.decode().selection_label == "COLLECT 10 BANANAS");
    f.write(pool + 24, 0, 1); assert(f.decode().kind == StateKind::Unavailable);
    window(6, false);

    f.write(f.a.GameMode, 1); f.write(f.a.CurrentMenuId, 5); f.write(f.a.NumberOfActivePlayers, 1);
    f.write(addr(0x80123510, 0x80123A90), 0x80026000); f.write(0x8002604C, 0x80027000); f.write(0x80027002, 1, 1);
    f.write(0x80026004, 0x80028000); f.write(0x80028004, 6);
    f.write(addr(0x80121170, 0x801216F0), 64); f.write(addr(0x8012116C, 0x801216EC), 0x80029000);
    f.write(0x80029004, 0x8002A000); f.text(0x8002A000, "ANCIENT LAKE");
    f.write(addr(0x8012117C, 0x801216FC), 0x8002B000); f.write(0x8002B008, 0x70, 1);
    f.write(f.a.PlayerSelectVehicle, 1, 1);
    observed = f.decode(); assert(observed.kind == StateKind::TrackSelectMenu && observed.selection_label == "Hovercraft");
    assert(observed.menu_detail == "ANCIENT LAKE" && observed.menu_entries[0].enabled);
    f.write(0x80028004, 2); assert(!f.decode().menu_entries[0].enabled);
    f.write(f.a.MenuStage, 1); assert(f.decode().selection_label == "OK?");
    f.write(f.a.MenuStage, 0xFFFFFFFF); assert(f.decode().kind == StateKind::Unavailable);
    f.write(f.a.MenuStage, 0); f.write(0x8002B007, 64, 1); assert(f.decode().kind == StateKind::Unavailable);
    // Challenge instructions supersede setup when their current page is open.
    window(1, true); f.write(pool + 24, 255, 1); assert(f.decode().selection_label == "COLLECT 10 BANANAS");
    window(1, false); f.write(0x8002B007, 0, 1);

    f.race(); f.write(f.a.PostRaceViewport, 1, 1); f.write(f.a.MenuStage, 6);
    window(7, true); f.write(windows + 7 * 40 + 36, pool);
    node(0, "TRY AGAIN", 12, 0xFFFFFFFF); node(1, "RETURN TO LOBBY", 28, 0xFFFFFFFF); node(2, "QUIT", 44, 0xFFFFFFFF);
    f.write(pool + 28, pool + 32); f.write(pool + 32 + 28, pool + 64);
    f.write(addr(0x80126C14, 0x801271D4), 3); f.write(addr(0x80126A68, 0x80127028), 1);
    strings(addr(0x80126BF0, 0x801271B0), 3, 0x8002C000, "TRY AGAIN");
    f.text(0x8002C080, "RETURN TO LOBBY"); f.text(0x8002C100, "QUIT");
    observed = f.decode(); assert(observed.kind == StateKind::ResultsMenu && observed.selection_label == "RETURN TO LOBBY");
    window(7, false); assert(f.decode().kind == StateKind::Unavailable); window(7, true);
    f.write(addr(0x80126C1C, 0x801271DC), 0x8002D000);
    assert(f.decode().kind == StateKind::Dialogue && f.decode().menu_name == "Race results message");
    f.write(addr(0x80126C1C, 0x801271DC), 0);
    f.write(f.a.MenuStage, 2); f.write(addr(0x800DF794, 0x800DFD14), 1);
    const auto elements = addr(0x800E0BEC, 0x800E116C);
    f.write(addr(0x800DF798, 0x800DFD18), elements); f.write(addr(0x80126170, 0x80126710), 60);
    // The first element may be a portrait; use a visible text/timestamp pair.
    f.write(elements + 20, 0x8002E000); f.text(0x8002E000, "LAP TIMES"); f.write(elements + 16, 255, 1); f.write(elements + 19, 0, 1);
    f.write(elements + 32 + 20, 0x8002F000); f.write(0x8002F000, 3690, 2);
    f.write(elements + 32 + 16, 255, 1); f.write(elements + 32 + 19, 1, 1);
    assert(f.decode().selection_label == "LAP TIMES, 1:01.50");
    f.write(addr(0x800DF794, 0x800DFD14), 0); assert(f.decode().kind == StateKind::Unavailable);
    f.write(0x8002F000, 12345, 2); assert(f.decode().kind == StateKind::Unavailable);
    f.write(addr(0x800DF794, 0x800DFD14), 1); f.write(addr(0x80126170, 0x80126710), 50);
    f.write(0x8002F000, 3050, 2); assert(f.decode().selection_label == "LAP TIMES, 1:01.00");
    // The stationary race-order strip reads only its actually drawn portraits.
    f.write(f.a.MenuStage, 3);
    const auto order = addr(0x800E0CEC, 0x800E126C);
    f.write(addr(0x800DF798, 0x800DFD18), order);
    for (unsigned i = 0; i < 10; ++i) f.write(addr(0x800E0AF0, 0x800E1070) + i * 4, 0x80030000 + i * 16);
    for (unsigned i = 0; i < 8; ++i) {
        f.write(order + i * 32 + 20, 0x80030000 + i * 16);
        f.write(order + i * 32 + 16, 255, 1); f.write(order + i * 32 + 19, 3, 1);
    }
    observed = f.decode(); assert(observed.selection_label.find("8: Krunch") != std::string::npos);
    assert(observed.selection_label.find("1: T.T.") != std::string::npos);
    f.write(order + 20, 0xB0000000); assert(f.decode().kind == StateKind::Unavailable);
    // Postrace initials use the same live alphabet target and entered length.
    f.write(f.a.MenuStage, 4);
    f.write(addr(0x800DF4A0, 0x800DFA20), 0x80031000);
    for (auto [id, text] : std::initializer_list<std::pair<unsigned, std::string_view>>{{8, "ENTER INITIALS"}, {85, "CANCEL"}, {134, "OK"}}) {
        f.write(0x80031000 + id * 4, 0x80032000 + id * 128); f.text(0x80032000 + id * 128, text);
    }
    f.write(addr(0x80126C6C, 0x8012722C), 0x80033000); f.write(0x80033000, 30);
    f.write(addr(0x80126C74, 0x80127238), 0x80033100); f.text(0x80033100, "AB ");
    f.write(addr(0x800E0FA0, 0x800E1520), 2); f.write(addr(0x80126C78, 0x8012723C), 3);
    assert(f.decode().kind == StateKind::ResultsMenu && f.decode().selection_label == "OK");
    assert(f.decode().menu_detail == "Initials: AB");
    // Rankings navigation publishes the four displayed placement counts with
    // the same 999 cap as results_render, rather than unseen stored values.
    f.write(f.a.PostRaceViewport, 0, 1); f.write(f.a.GameMode, 1); f.write(f.a.CurrentMenuId, 17); f.write(f.a.MenuStage, 1);
    strings(addr(0x800E0BCC, 0x800E114C), 4, 0x80034000, "1ST");
    f.text(0x80034080, "2ND"); f.text(0x80034100, "3RD"); f.text(0x80034180, "4TH");
    f.write(0x8002605C, 1000, 2); f.write(0x8002605E, 3, 2);
    observed = f.decode(); assert(observed.kind == StateKind::ResultsMenu && observed.selection_label == "RETURN TO LOBBY");
    assert(observed.menu_detail.find("1ST: 999, 2ND: 3") != std::string::npos);
    f.write(addr(0x800E0988, 0x800E0F08), 2); assert(f.decode().selection_label == "CANCEL");
    f.write(f.a.MenuStage, 0); assert(f.decode().selection_label == "Rankings appearing");
}

void check_revision(rom::Revision revision) {
    Fixture f(revision);
    f.options();
    auto result = f.decode();
    assert(result.kind == StateKind::OptionsMenu && result.selection == 2);
    assert(result.selection_label == "AUDIO OPTIONS" && result.scene == 17 && result.revision == revision);
    // Explicit sample provenance determines its table, not mutable frontend aliases.
    revision_addresses::select(revision == rom::Revision::UsV77 ? rom::Revision::UsV80 : rom::Revision::UsV77);
    assert(f.decode().selection_label == "AUDIO OPTIONS");
    assert(describe_observation(result) == "Options. Observed selection: AUDIO OPTIONS.");
    const auto original = f.bytes;
    (void)f.decode();
    assert(f.bytes == original);
    f.write(f.a.MenuDelay, 1); assert(f.decode().kind == StateKind::Unavailable);
    f.write(f.a.MenuDelay, 0); f.write(f.cursor(), 6); assert(f.decode().kind == StateKind::Unavailable);
    f.write(f.cursor(), 0xFFFFFFFF); assert(f.decode().kind == StateKind::Unavailable);
    f.write(f.cursor(), 2);
    for (auto pointer : {0U, 0x00001000U, 0x80801000U, 0xB0001000U, 0xFFFFFFFFU}) {
        f.write(f.strings() + 8, pointer); assert(f.decode().kind == StateKind::Unavailable);
    }
    f.write(f.strings() + 8, 0xA0001000); assert(f.decode().kind == StateKind::OptionsMenu);
    f.write(f.strings() + 8, 0x807FFFFF); f.write(0x807FFFFF, 'X', 1);
    assert(f.decode().kind == StateKind::Unavailable);
    f.write(f.strings() + 8, 0x80001000); f.text(0x80001000, std::string(96, 'A'));
    assert(f.decode().kind == StateKind::Unavailable);
    f.text(0x80001000, "AUDIO\nOPTIONS"); assert(f.decode().kind == StateKind::Unavailable);
    f.text(0x80001000, ""); assert(f.decode().kind == StateKind::Unavailable);
    f.options(); f.write(f.a.CurrentMenuId, 13); assert(f.decode().kind == StateKind::Unavailable);
    f.race(); result = f.decode();
    assert(result.kind == StateKind::Race && result.countdown == 80 && !result.paused && result.racer);
    assert(result.racer->position == 2 && result.racer->lap == 2 && result.racer->total_laps == 3 && result.racer->bananas == 7);
    const auto race_original = f.bytes;
    (void)f.decode();
    assert(f.bytes == race_original);
    f.write(0x80005194, 3, 1); assert(f.decode().racer->lap == 3);
    f.write(f.a.IsPaused, 1, 1); assert(!f.decode().racer);
    f.write(f.a.IsPaused, 2, 1); assert(f.decode().kind == StateKind::Unavailable);
    f.race(); f.write(f.a.RaceStartTimer, 101); assert(f.decode().kind == StateKind::Unavailable);
    f.race(); f.write(f.a.LevelLoadTimer, 1, 2); assert(f.decode().kind == StateKind::Unavailable);
    // Level-load timer is s16. Its adjacent halfword is unrelated state.
    f.race(); f.write(f.a.LevelLoadTimer + 2, 0xFFFF, 2); assert(f.decode().kind == StateKind::Race);
    f.race(); f.write(f.a.IsInRace, 0); assert(f.decode().kind == StateKind::Unavailable);
    for (auto count : {0U, 11U, 0xFFFFFFFFU}) {
        f.race(); f.write(f.a.NumberOfRacers, count);
        assert(f.decode().kind == StateKind::Race && !f.decode().racer);
    }
    for (auto field : {f.a.CurrentLevelHeader, f.a.RacersByPort, 0x80003000U, 0x80004064U}) {
        for (auto pointer : {0U, 0xB0001000U, 0x80001001U, 0x807FFFFCU}) {
            f.race(); f.write(field, pointer); assert(!f.decode().racer);
        }
    }
    f.race(); f.write(0x80005000, 1, 2); assert(!f.decode().racer);
    f.race(); f.write(0x80005000, 0xFFFF, 2); assert(!f.decode().racer);
    f.race(); f.write(0x800051D8, 1, 1); assert(!f.decode().racer);
    f.race(); f.write(0x8000204C, 1, 1); assert(!f.decode().racer);
    f.race(); f.write(0x8000204B, 0, 1); assert(!f.decode().racer);
    f.race(); f.write(0x800051AE, 9, 2); assert(!f.decode().racer);
    f.race(); f.write(0x800051AE, 0, 2); assert(!f.decode().racer);
    f.race(); f.write(0x80005194, 0xFF, 1); assert(!f.decode().racer);
    f.race(); f.write(0x80005185, 11, 1); assert(!f.decode().racer);
    assert(decode_submission(std::span(f.bytes).first(32), revision, 0).kind == StateKind::Unavailable);
    assert(decode_submission(f.bytes, rom::Revision::Unsupported, 0).kind == StateKind::Unavailable);
}


}

int main() {
    check_boot_pak(rom::Revision::UsV77);
    check_boot_pak(rom::Revision::UsV80);
    check_character_confirmation(rom::Revision::UsV77);
    check_character_confirmation(rom::Revision::UsV80);
    check_save_options(rom::Revision::UsV77);
    check_save_options(rom::Revision::UsV80);
    check_trophy_round(rom::Revision::UsV77);
    check_trophy_round(rom::Revision::UsV80);
    check_hub_pause(rom::Revision::UsV77);
    check_hub_pause(rom::Revision::UsV80);
    check_race_start_prompt(rom::Revision::UsV77);
    check_race_start_prompt(rom::Revision::UsV80);
    check_track_grid(rom::Revision::UsV77);
    check_track_grid(rom::Revision::UsV80);
    check_audio_options(rom::Revision::UsV77);
    check_audio_options(rom::Revision::UsV80);
    check_adventure_journey(rom::Revision::UsV77);
    check_adventure_journey(rom::Revision::UsV80);
    check_menus_and_items(rom::Revision::UsV77);
    check_menus_and_items(rom::Revision::UsV80);
    check_revision(rom::Revision::UsV77);
    check_revision(rom::Revision::UsV80);
    Fixture f(rom::Revision::UsV77); f.options();
    const auto now = Clock::time_point{} + std::chrono::seconds(10);
    const auto old_session = begin_session();
    assert(!observation_demanded());
    observe_submission(f.bytes, f.revision, 1, old_session, now);
    assert(!read_observation(now));
    set_observation_demand(true);
    observe_submission(f.bytes, f.revision, 1, old_session, now);
    assert(read_observation(now)->session == old_session && read_observation(now)->sequence == 1);
    assert(!read_observation(now - std::chrono::milliseconds(1)));
    assert(!read_observation(now + std::chrono::milliseconds(1501)));
    set_observation_demand(false);
    assert(!read_observation(now));
    set_observation_demand(true);
    assert(!read_observation(now)); // Re-enabling cannot expose the old sample.
    const auto session = begin_session(); assert(session > old_session && !read_observation(now));
    observe_submission(f.bytes, f.revision, 2, old_session, now); assert(!read_observation(now));
    observe_submission(f.bytes, f.revision, 3, session, now);
    assert(read_observation(now)->scene == 3 && read_observation(now)->sequence == 1);
    end_session(old_session);
    assert(read_observation(now)->session == session);
    // Publication owns decoded strings and fields after the source is mutated.
    f.text(0x80001000, "Changed source");
    assert(read_observation(now)->selection_label == "AUDIO OPTIONS");
    observe_submission(f.bytes, rom::Revision::Unsupported, 4, session, now);
    assert(read_observation(now)->kind == StateKind::Unavailable && read_observation(now)->sequence == 2);
    f.write(f.a.MenuDelay, 1);
    observe_submission(f.bytes, f.revision, 5, session, now);
    assert(read_observation(now)->kind == StateKind::Unavailable);
    reset_session(); assert(!read_observation(now));
    observe_submission(f.bytes, f.revision, 6, session, now); assert(!read_observation(now));
    const auto ending_session = begin_session(); f.options();
    observe_submission(f.bytes, f.revision, 7, ending_session, now);
    end_session(ending_session); assert(!read_observation(now));
    observe_submission(f.bytes, f.revision, 8, ending_session, now); assert(!read_observation(now));
    std::puts("[test][accessibility-observation] PASS");
}
