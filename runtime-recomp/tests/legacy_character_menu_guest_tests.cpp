// Execute the regenerated native movement function, including its guarded
// custom adapter and unchanged original path. No rewritten generated code.
#include "legacy_character_menu.hpp"
#include "recomp.h"
#include <algorithm>
#include <iostream>

using namespace dkr::mods;
extern "C" void charselect_move(std::uint8_t*, recomp_context*);
namespace {
Bytes memory(8 * MiB);
CharacterMenuFields fields{};
std::unique_ptr<CharacterMenuAdapter> adapter;
bool custom = false, duplicates = false;
unsigned sound = 0, events = 0, checks = 0;
void check(bool ok, const char* why) {
    ++checks;
    if (!ok)
        throw Error(why);
}
constexpr std::uint32_t Direction = 0x80300000, Stack = 0x807e0000;
gpr ptr(std::uint32_t p) {
    return static_cast<gpr>(static_cast<std::int32_t>(p));
}
} // namespace
extern "C" int dkr_legacy_character_menu(std::uint8_t*, recomp_context* c, unsigned event,
                                         const std::uint32_t* addresses) {
    check(event == 5, "Wrong native character selector hook.");
    ++events;
    std::copy_n(addresses, fields.size(), fields.begin());
    CharacterMenuMemory g(memory, fields);
    auto assets = std::make_shared<CharacterNamespace>();
    assets->characters = {{std::string(64, 'a'), "Yooka", 3, 906, {304, 305, 306}}};
    adapter = std::make_unique<CharacterMenuAdapter>(assets);
    g.set(CharacterMenuField::Active, 1, 0, 1);
    g.set(CharacterMenuField::Active, 1, 1, 1);
    g.set(CharacterMenuField::Players, 2);
    g.set(CharacterMenuField::NativeIndices, 0, 0, 1);
    g.set(CharacterMenuField::NativeIndices, 2, 1, 1);
    g.set(CharacterMenuField::NativeIndices, 255, 2, 1);
    g.set(CharacterMenuField::NativeIndices, 255, 3, 1);
    if (!custom)
        return 0;
    g.set(CharacterMenuField::SelectTable, 0x80300100);
    g.set(CharacterMenuField::NativeIndices, 4, 0, 1);
    g.set(CharacterMenuField::StickX, 0xffff, 0, 2);
    adapter->input(g, duplicates);
    const bool moved = adapter->native_move(g, unsigned(c->r4), std::uint32_t(c->r5), unsigned(c->r6), duplicates);
    sound = moved ? unsigned(c->r7) : g.read(std::uint32_t(c->r29) + 16);
    return 1;
}
extern "C" void get_filtered_cheats(std::uint8_t*, recomp_context* c) {
    c->r2 = duplicates ? (1U << 22) : 0;
}
extern "C" void sound_play(std::uint8_t*, recomp_context* c) {
    sound = unsigned(c->r4);
}
int main() {
    try {
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            std::fill(memory.begin(), memory.end(), 0);
            custom = (scenario & 1) != 0;
            duplicates = (scenario & 2) != 0;
            for (unsigned i = 0; i < 4; ++i)
                memory[((Direction & 0x1fffffff) + i) ^ 3] = i == 0 ? 0 : (i == 1 ? 3 : 255);
            for (unsigned i = 0; i < 4; ++i)
                memory[((Stack & 0x1fffffff) + 16 + i) ^ 3] = 0x15c >> (24 - 8 * i);
            recomp_context c{};
            c.r29 = ptr(Stack);
            c.r4 = 1;
            c.r5 = ptr(Direction);
            c.r6 = 2;
            c.r7 = 0xec;
            c.r16 = 0x1234;
            c.r17 = 0x5678;
            c.r31 = 0xabcdef;
            charselect_move(memory.data(), &c);
            CharacterMenuMemory g(memory, fields);
            check(g.get(CharacterMenuField::NativeIndices, 1, 1) == ((custom || duplicates) ? 0 : 3),
                  "Original/custom duplicate policy selected the wrong native index.");
            check(sound == 0xec, "Native successful movement lost its authored sound.");
            check(c.r29 == ptr(Stack) && c.r16 == 0x1234 && c.r17 == 0x5678 && c.r31 == 0xabcdef,
                  "Character movement corrupted caller state.");
        }
        check(events == 4, "The regenerated movement hook was not exercised.");
        std::cout << checks << " native character-menu move checks passed (v" << DKR_TEST_REVISION << ").\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
