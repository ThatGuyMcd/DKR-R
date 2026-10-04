#pragma once
#include "recomp.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
// Authored Patch Pipeline hooks. File-output fixtures are private; the normal
// experimental backend uses only the same-process, pre-INTRO memory handoff.
bool dkr_private_replay_boot(std::uint8_t* rdram, recomp_context* context);
void dkr_private_replay_capture(std::uint8_t* rdram, recomp_context* context);
// Explicit file-output fixture preparation (never armed by a normal lobby).
// No production environment-variable bypass or live-thread restore.
bool dkr_experimental_bootstrap_arm(const std::filesystem::path& output, std::string& error);
bool dkr_experimental_bootstrap_completed();
// Same-process cold-boot ownership handoff. No map selection, scripted menu
// input, child/file/configuration rewrite, or capture before workers retire.
bool dkr_experimental_bootstrap_arm_memory(std::uint8_t players, bool host,
    std::uint64_t match_id, std::uint64_t save_hash, std::string& error);
// Frozen launch identity survives the temporary offline construction binding.
bool dkr_experimental_bootstrap_activate_save(std::string& error);
std::uint8_t dkr_experimental_bootstrap_players();
bool dkr_experimental_bootstrap_active();
std::vector<std::uint8_t> dkr_experimental_bootstrap_take_memory();
