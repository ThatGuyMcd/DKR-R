#include "quick_restart_policy.hpp"

#include <cassert>
#include <cstdio>

using dkr::runtime::quick_restart::retail_restart_available;
using dkr::runtime::quick_restart::State;

int main() {
    State state{};
    state.modern_profile = true;
    state.world_id = 1;
    state.race_type = 0;
    assert(retail_restart_available(state));

    state.race_type = 0x40;
    assert(retail_restart_available(state));
    state.race_type = 0x41;
    assert(retail_restart_available(state));

    state.race_type = 8;
    state.tracks_mode = false;
    assert(retail_restart_available(state));
    state.tracks_mode = true;
    assert(!retail_restart_available(state));

    state = {};
    state.modern_profile = true;
    state.world_id = 0;
    state.race_type = 0;
    assert(!retail_restart_available(state));

    state.world_id = 1;
    state.paused = true;
    assert(!retail_restart_available(state));
    state.paused = false;
    state.post_race = true;
    assert(!retail_restart_available(state));
    state.post_race = false;
    state.level_loading = true;
    assert(!retail_restart_available(state));
    state.level_loading = false;
    state.trophy_race_world = 1;
    assert(!retail_restart_available(state));
    state.trophy_race_world = 0;
    state.modern_profile = false;
    assert(!retail_restart_available(state));

    std::puts("[test][quick-restart-policy] PASS");
    return 0;
}
