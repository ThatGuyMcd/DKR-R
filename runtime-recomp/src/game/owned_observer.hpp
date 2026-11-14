#pragma once
#include "observer_playback.hpp"

namespace dkr::runtime::spectators {
struct OwnedObserverHooks {
    void* context = nullptr;
    Work (*retire)(void*, Cursor, std::string&) = nullptr;
    bool (*admit)(void*, const Baseline&, std::string&) = nullptr;
    std::uint32_t (*magic_actions)(void*) = nullptr;
    void (*read_only)(void*, bool) = nullptr;
};
// Simulation-owner thread-local registration. No raw pointer is sent over the
// network or put in guest checkpoints. Foreign-thread calls find no binding.
void register_owned_observer(const void* world, OwnedObserverHooks hooks);
void unregister_owned_observer(const void* world);
Work retire_owned_observer(const void* world, Cursor next, std::string& error);
bool admit_owned_observer(const void* world, const Baseline& baseline, std::string& error);
std::uint32_t owned_magic_actions(const void* world);
void owned_observer_read_only(const void* world, bool enabled);
} // namespace dkr::runtime::spectators
