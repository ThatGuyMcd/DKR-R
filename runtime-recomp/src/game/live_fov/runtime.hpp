#pragma once
#include "live_fov.hpp"
#include "guest_call.hpp"
#include "camera_clearance_metadata.h"
#include "game_payload.hpp"
#include "presentation_identity.hpp"
#include "revision_addresses.hpp"
#include "runtime_enhancements.hpp"
#include "runtime_netplay.hpp"
#include <bit>
#include <cstring>

namespace dkr::runtime::live_fov {
inline void refresh(std::uint8_t* rdram, recomp_context* context) {
    // The existing presentation-frame hook owns this RAM and stack. A slider
    // callback never invokes a guest function, and rollback/checkpoint work
    // must never observe machine-local graphics settings.
    if (!rdram || !context || !netplay::external_side_effects_allowed() || netplay::session().active())
        return;
    const auto revision = revision_addresses::selected_revision();
    if (revision != rom::Revision::UsV77 && revision != rom::Revision::UsV80)
        return;
    const auto* payload = active_payload();
    if (!payload || payload->revision != revision || !payload->cam_set_fov)
        return;
    const auto word = [&](std::uint32_t address) {
        std::uint32_t value;
        std::memcpy(&value, rdram + (address & 0x7FFFFFU), 4);
        return value;
    };
    const auto byte = [&](std::uint32_t address) { return rdram[(address & 0x7FFFFFU) ^ 3U]; };
    const auto header = word(revision_addresses::CurrentLevelHeader);
    if (header < 0x80000000U || header > 0x807FFF60U || (header & 3U))
        return;
    if (byte(revision_addresses::CutsceneCameraActive) ||
        !dkr_world_projection_eligible(std::int32_t(word(revision_addresses::GameMode)), byte(header + 0x4CU),
                                       word(revision_addresses::ViewportLayout), 1))
        return;
    const Frame frame{
        {reinterpret_cast<std::uintptr_t>(rdram), presentation::recording_scene_generation(), header, int(revision)},
        enhancements::presentation_profile(),
        enhancements::fov_offset(),
        int(byte(header + 0x9CU)),
        std::bit_cast<float>(word(revision_addresses::CurrentCameraFov)),
        true,
        false};
    static thread_local Tracker tracker;
    tracker.update(frame, [&](float target) {
        apply_guest_fov(rdram, *context, target, payload->cam_set_fov);
        return std::fabs(std::bit_cast<float>(word(revision_addresses::CurrentCameraFov)) - target) <= 0.01F;
    });
}
} // namespace dkr::runtime::live_fov
