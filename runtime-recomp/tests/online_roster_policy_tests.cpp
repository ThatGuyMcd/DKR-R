#include "online_roster_policy.hpp"

#include <array>
#include <cassert>
#include <cstdint>

int main() {
    using namespace dkr::runtime::netplay;

    static_assert(contiguous_online_roster({true, true, false, false}));
    static_assert(contiguous_online_roster({true, true, true, true}));
    static_assert(!contiguous_online_roster({true, false, true, false}));
    static_assert(!contiguous_online_roster({true, false, false, false}));

    std::array<std::int8_t, kDkrCharacterSlots> existing{};
    existing.fill(-1);
    existing[0] = 1;
    const CharacterSelectSeed two_player = make_character_select_seed({true, true, false, false}, existing);
    assert(two_player.active_player_count == 2U);
    assert(two_player.active_players == (std::array<std::int8_t, 4U>{1, 1, 0, 0}));
    assert(two_player.characters[0] == 1);
    assert(two_player.characters[1] == 0);
    assert(two_player.characters[2] == -1);
    assert(two_player.player_ids[0] == 0U);
    assert(two_player.player_ids[1] == 1U);

    existing[1] = 1; // Duplicate cursors are repaired deterministically.
    const CharacterSelectSeed four_player = make_character_select_seed({true, true, true, true}, existing);
    assert(four_player.active_player_count == 4U);
    assert(four_player.characters[0] == 1);
    assert(four_player.characters[1] == 0);
    assert(four_player.characters[2] == 2);
    assert(four_player.characters[3] == 3);

    assert(locked_character_select_buttons(false, 0, 0xFFFFU) == 0U);
    assert((locked_character_select_buttons(true, 0, 0xFFFFU) & kN64BButton) == 0U);
    assert((locked_character_select_buttons(true, 1, 0xFFFFU) & kN64BButton) != 0U);
}
