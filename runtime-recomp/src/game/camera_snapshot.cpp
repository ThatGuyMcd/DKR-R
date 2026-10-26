#include "camera_snapshot.hpp"
#include "revision_addresses.hpp"
#include "camera_clearance_metadata.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace dkr::runtime::camera {
namespace {
constexpr float Pi = 3.14159265358979323846F;
struct Reader {
    std::span<const std::uint8_t> ram;
    std::uint32_t offset(std::uint32_t address, unsigned count, unsigned alignment) const {
        if (address < 0x80000000U || address >= 0x80800000U || (address & (alignment - 1)))
            throw mods::Error("Invalid camera snapshot address.");
        auto off = address - 0x80000000U;
        if (off > ram.size() || count > ram.size() - off || count > 0x800000U - off)
            throw mods::Error("Camera snapshot span exceeds retail memory.");
        return off;
    }
    std::uint32_t word(std::uint32_t address) const {
        auto off = offset(address, 4, 4);
        std::uint32_t value;
        std::memcpy(&value, ram.data() + off, 4);
        return value;
    }
    unsigned half(std::uint32_t address) const {
        auto off = offset(address, 2, 2);
        std::uint16_t value;
        std::memcpy(&value, ram.data() + (off ^ 2U), 2);
        return value;
    }
    unsigned byte(std::uint32_t address) const {
        return ram[offset(address, 1, 1) ^ 3U];
    }
    float real(std::uint32_t address) const {
        auto value = std::bit_cast<float>(word(address));
        if (!std::isfinite(value))
            throw mods::Error("Non-finite camera snapshot value.");
        return value;
    }
    float angle(std::uint32_t address) const {
        return float(std::int16_t(half(address))) * (Pi / 32768.F);
    }
};
float wrap(float value) {
    return std::remainder(value, 2 * Pi);
}
} // namespace
std::optional<DkrViewSnapshot> SnapshotReader::capture(std::span<const std::uint8_t> ram, rom::Revision revision,
                                                       unsigned view, const FrameStamp& frame) {
    if (view >= 4 || ram.size() < 0x800000U || ram.size() % 4 ||
        (revision != rom::Revision::UsV77 && revision != rom::Revision::UsV80) || !std::isfinite(frame.dt) ||
        frame.dt < 0 || frame.dt > 0.1F || !std::isfinite(frame.time) || !std::isfinite(frame.aspect) ||
        frame.aspect < 0.1F || frame.aspect > 16)
        return {};
    try {
        Reader r{ram};
        const auto& table = revision == rom::Revision::UsV77 ? revision_addresses::kUsV77 : revision_addresses::kUsV80;
        const auto count = r.word(table.NumberOfRacers), list = r.word(table.RacersByPort);
        if (!count || count > 8)
            return {};
        r.offset(list, count * 4, 4);
        std::uint32_t object = 0, racer = 0;
        // Lists can be reordered by race position and custom rosters. Match
        // the actual native player identity, not list entry zero/the host.
        for (unsigned i = 0; i < count; ++i) {
            auto candidate = r.word(list + i * 4);
            r.offset(candidate, 0x68, 4);
            auto state = r.word(candidate + 0x64);
            r.offset(state, 0x224, 4);
            if (std::int16_t(r.half(state)) == int(view)) {
                if (object)
                    throw mods::Error("Duplicate local camera racer identity.");
                object = candidate;
                racer = state;
            }
        }
        if (!object)
            return {};
        const auto header = r.word(table.CurrentLevelHeader);
        r.offset(header, 0xA0, 4);
        const auto camera = table.Cameras + view * 0x44;
        r.offset(camera, 0x44, 4);
        auto& binding = bindings_[view];
        const bool changed = binding.object != object || binding.racer != racer || binding.scene != frame.scene ||
                             binding.epoch != frame.epoch;
        if (changed) {
            if (++binding.generation == 0)
                ++binding.generation;
        }
        DkrViewSnapshot out{};
        out.api = 1;
        out.size = sizeof(out);
        out.capabilities = frame.capabilities;
        out.scene = frame.scene;
        out.epoch = frame.epoch;
        out.frame = frame.frame;
        out.view = view;
        // Opaque scene-local token, not a guest address that a mod can borrow.
        out.racer_id = 0x43410000U | view;
        out.racer_generation = binding.generation;
        out.vehicle = r.byte(racer + 0x1D6);
        out.character = r.byte(racer + 3);
        out.dt = frame.dt;
        out.time = frame.time;
        out.aspect = frame.aspect;
        for (unsigned axis = 0; axis < 3; ++axis) {
            out.position[axis] = r.real(object + 12 + axis * 4);
            out.velocity[axis] = r.real(object + 0x1C + axis * 4) * 60.F;
            out.original_position[axis] = r.real(camera + 12 + axis * 4);
        }
        out.yaw = r.angle(object);
        out.pitch = -r.angle(object + 2);
        out.roll = r.angle(object + 4);
        // Native camera looks down +Z at yaw zero, unlike the SDK's -Z.
        out.original_yaw = wrap(Pi - r.angle(camera));
        out.original_pitch = r.angle(camera + 2);
        out.original_roll = r.angle(camera + 4);
        out.speed = -(out.velocity[0] * std::sin(out.yaw) + out.velocity[2] * std::cos(out.yaw));
        out.steer = std::clamp(float(std::int8_t(r.byte(racer + 0x1E1))) / 70.F, -1.F, 1.F);
        out.boost = std::int8_t(r.byte(racer + 0x1D3)) > 0 ? 1.F : 0.F;
        out.original_fov = r.real(table.CurrentCameraFov) * Pi / 180.F;
        // Verified decomp CAMERA_NEAR / CAMERA_FAR, unchanged by the SDK.
        out.original_near = 10;
        out.original_far = 15000;
        const auto mode = r.half(camera + 0x36), layout = r.word(table.ViewportLayout);
        const auto dialogue = revision == rom::Revision::UsV77 ? 0x8011D586U : 0x8011DB06U;
        const bool scripted = r.byte(table.CutsceneCameraActive) || r.half(dialogue);
        if (scripted)
            out.flags |= DKR_VIEW_SCRIPTED;
        if (!scripted &&
            dkr_world_projection_eligible(std::int32_t(r.word(table.GameMode)), r.byte(header + 0x4C), layout, 1) &&
            (mode == 0 || mode == 1 || mode == 4) && out.vehicle <= 2)
            out.flags |= DKR_VIEW_DRIVING;
        if (std::int8_t(r.byte(racer + 0x1E2)) > 0 || std::int8_t(r.byte(racer + 0x1E5)) > 0 || out.vehicle == 2)
            out.flags |= DKR_VIEW_GROUNDED;
        if (std::int8_t(r.byte(racer + 0x1E6)))
            out.flags |= DKR_VIEW_DRIFTING;
        if (out.boost > 0)
            out.flags |= DKR_VIEW_BOOSTING;
        if (std::abs(r.real(camera + 0x30)) > 0.1F)
            out.flags |= DKR_VIEW_IMPACT;
        if (frame.online)
            out.flags |= DKR_VIEW_ONLINE;
        if (frame.inspection && !frame.online)
            out.flags |= DKR_VIEW_INSPECTION_ALLOWED;
        if (changed || frame.cut || frame.frame < binding.frame)
            out.flags |= DKR_VIEW_CUT;
        binding.object = object;
        binding.racer = racer;
        binding.scene = frame.scene;
        binding.epoch = frame.epoch;
        binding.frame = frame.frame;
        return out;
    } catch (const std::exception&) {
        return {};
    }
}
} // namespace dkr::runtime::camera
