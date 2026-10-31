#pragma once
#include "resident_music.hpp"
#include <utility>

namespace dkr::spectator_admission {
// Process-local loading permission, never checkpointed or sent over the wire.
// The only production caller is the already verified observer admission arm,
// while staging mod resources and BEFORE restoring any guest RAM or journals.
inline thread_local unsigned observer_music_depth = 0;
inline bool preparing_music() noexcept { return observer_music_depth != 0; }

template<class Stage>
decltype(auto) prepare_verified_music(Stage&& stage) {
    struct Scope {
        dkr::mods::online::resident_music::PreparationScope loading;
        Scope() noexcept { ++observer_music_depth; }
        ~Scope() { --observer_music_depth; }
    } scope;
    return std::forward<Stage>(stage)();
}
}
