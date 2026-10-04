#pragma once
#include <cstdint>
#include <span>
bool dkr_probe_owned_adapter_check(std::span<const std::uint8_t> fixture, std::span<const std::uint8_t> rom, unsigned frames=12);
bool dkr_probe_owned_title_check(std::span<const std::uint8_t> fixture, std::span<const std::uint8_t> rom);
// Exercises correction on the closed, private object-update component only.
// This is NOT the game's complete-tick runtime adapter or an admission gate.
bool dkr_probe_driver_check(std::span<const std::uint8_t> fixture, unsigned players, unsigned frames = 12, bool water_phases = false, bool mode_phases = false, bool audio_phases = false, bool input_phases = false, bool authored_cpu = false);
bool dkr_probe_timeline_check(std::span<const std::uint8_t> fixture, unsigned players, unsigned frames = 12, bool water_phases = false);
// Real loopback UDP and encrypted owner input against the closed guest
// component. No renderer/audio workers, real scene transactions or live saves.
bool dkr_probe_network_check(std::span<const std::uint8_t> fixture, unsigned players, unsigned frames = 12, bool water_phases = false, bool impaired_paced = false, bool mode_phases = false, bool audio_phases = false, bool input_phases = false, bool authored_cpu = false);
// Private unit requests drive the ACTUAL two retail unload branches. Proves
// reversible intent/correction and exact resume PC, NOT actual scene loading.
bool dkr_probe_scene_cut_check(std::span<const std::uint8_t> fixture, bool authored_cpu = false);
// Exact retail allocator/object/HUD/particle/text teardown after confirmation
// and immutable consumer drain; no real next-scene construction yet.
bool dkr_probe_scene_unload_check(std::span<const std::uint8_t> fixture,bool construct=false);
// Real confirmed mode/main continuation, new owned epoch, then exhaustive
// actual guest replay and late input correction. Still no live GPU or lobby.
bool dkr_probe_scene_resume_check(std::span<const std::uint8_t> fixture);
bool dkr_probe_menu_flow_check(std::span<const std::uint8_t> fixture, bool adventure=false);
// Real scene continuations driven by the production experimental Session,
// encrypted impaired loopback and owner Pump. No GPU/VI/lobby admission.
bool dkr_probe_scene_session_check(std::span<const std::uint8_t> fixture,unsigned players,unsigned final_frames=12);
