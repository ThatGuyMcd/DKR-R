#pragma once

#include "scheduler_event_policy.hpp"

#include <cstdint>

namespace dkr::runtime::steering_wheel {

// Render hooks receive N64 pointers directly from transient model state. A
// physical RDRAM offset of zero is addressable in a few low-level subsystems,
// but every pointer consumed by this material hook uses zero as its null
// sentinel. Keep that distinction explicit so a missing transition mesh or
// texture can never alias the first bytes of RDRAM.
inline constexpr bool is_valid_render_pointer(std::uint32_t address, std::uint32_t final_offset) {
    return address != 0U && dkr::runtime::scheduler::is_rdram_word_address(address, final_offset);
}

inline constexpr std::uint16_t kRacerBehaviour = 1U;
inline constexpr std::uint16_t kTitleVehicleAnimationBehaviour = 56U;
inline constexpr std::int8_t kCarVehicle = 0;
inline constexpr std::int8_t kNoVehicle = -1;
inline constexpr std::uint8_t kNoTextureIndex = 0xFFU;
inline constexpr std::uint32_t kAntiAliasing = 1U << 0U;
inline constexpr std::uint32_t kDepthCompare = 1U << 1U;
inline constexpr std::uint32_t kSemiTransparent = 1U << 2U;
inline constexpr std::uint32_t kDecal = 1U << 11U;
inline constexpr std::uint32_t kAuthoredBatchFlags = kAntiAliasing | kDepthCompare | kSemiTransparent | kDecal;
inline constexpr std::uint32_t kSteeringWheelTextureId = 0x81B8U;
inline constexpr std::uint8_t kSteeringWheelWidth = 16U;
inline constexpr std::uint8_t kSteeringWheelHeight = 16U;
inline constexpr std::uint8_t kRgba32Format = 0U;
inline constexpr std::uint16_t kClampBothAxes = 0x00C0U;

struct BatchIdentity {
    std::uint16_t behaviour = 0U;
    std::int8_t vehicle = -1;
    std::uint16_t batch_count = 0U;
    std::uint16_t batch_index = 0U;
    std::uint8_t texture_index = kNoTextureIndex;
    std::int32_t vertex_count = 0;
    std::int32_t triangle_count = 0;
    std::uint32_t authored_flags = 0U;
    std::uint8_t texture_width = 0U;
    std::uint8_t texture_height = 0U;
    std::uint8_t texture_format = 0xFFU;
    std::uint16_t texture_flags = 0U;
    bool texture_asset_matches = false;
};

// DKR's cockpit steering wheel is a free-standing four-vertex mesh, but its
// authored batch uses the N64 decal render mode. RT64 correctly treats decals
// as coplanar surfaces and rejects this particular mesh against background
// depth. Match every independent piece of the authored batch before changing
// that one material bit; exterior wheels and propellers use a separate object
// attachment path and can never satisfy this predicate.
inline constexpr bool is_cockpit_steering_wheel(const BatchIdentity& batch) {
    const bool supported_car_context =
        (batch.behaviour == kRacerBehaviour && batch.vehicle == kCarVehicle) ||
        (batch.behaviour == kTitleVehicleAnimationBehaviour && batch.vehicle == kNoVehicle);
    return supported_car_context && batch.batch_count != 0U && batch.batch_index + 1U == batch.batch_count &&
           batch.texture_index != kNoTextureIndex && batch.vertex_count == 4 && batch.triangle_count == 2 &&
           batch.authored_flags == kAuthoredBatchFlags && batch.texture_width == kSteeringWheelWidth &&
           batch.texture_height == kSteeringWheelHeight && (batch.texture_format & 0x0FU) == kRgba32Format &&
           (batch.texture_flags & kClampBothAxes) == kClampBothAxes && batch.texture_asset_matches;
}

inline constexpr std::uint32_t corrected_material_flags(const BatchIdentity& batch, std::uint32_t effective_flags) {
    return is_cockpit_steering_wheel(batch) ? (effective_flags & ~kDecal) : effective_flags;
}

} // namespace dkr::runtime::steering_wheel
