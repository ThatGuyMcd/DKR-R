#pragma once
#include "mods/magic_code_game_world.hpp"
#include "rom_revision.hpp"

namespace dkr::runtime::code_mods {
// Called only while every guest producer is retired.
void begin(std::shared_ptr<const mods::code::FrozenSet>, rom::Revision, std::uint64_t seed, bool online = false);
void validate_online(const mods::code::FrozenSet&, rom::Revision);
void reset();
void bind_thread();
void unbind_thread();
void boot(std::uint8_t* ram, std::size_t bytes);
std::string failure();
// Called inside the quiesced callback, BEFORE recomp::start frees its RAM.
mods::Bytes retire_bootstrap_checkpoint(std::span<std::uint8_t> live_ram);
std::shared_ptr<const mods::code::FrozenSet> frozen();
} // namespace dkr::runtime::code_mods
