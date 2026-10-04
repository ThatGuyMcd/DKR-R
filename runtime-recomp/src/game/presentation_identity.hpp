#pragma once

#include "presentation_marker_policy.hpp"
#include "hud_group_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace dkr::runtime::presentation {

inline constexpr std::uint32_t kIgnoredIdentity = 0U;
inline constexpr std::uint32_t kAutomaticIdentity = 0xFFFFFFFFU;

constexpr std::uint32_t mix_identity(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

constexpr std::uint32_t normalise_identity(std::uint32_t value) {
    value = mix_identity(value);
    if (value == kIgnoredIdentity || value == kAutomaticIdentity) {
        value ^= 0xA511E9B3U;
    }
    return value;
}

constexpr std::uint32_t make_object_identity(std::uint32_t scene_generation,
                                              std::uint32_t object_address,
                                              std::uint32_t lifetime_generation,
                                              std::uint16_t object_id,
                                              std::uint16_t behaviour_id) {
    std::uint32_t value = scene_generation * 0x9E3779B9U;
    value ^= object_address * 0x85EBCA6BU;
    value ^= lifetime_generation * 0xC2B2AE35U;
    value ^= static_cast<std::uint32_t>(object_id) << 16U;
    value ^= behaviour_id;
    return normalise_identity(value);
}

constexpr std::uint32_t make_matrix_identity(std::uint32_t object_identity,
                                              std::uint32_t matrix_ordinal) {
    return normalise_identity(object_identity ^
        ((matrix_ordinal + 1U) * 0x27D4EB2DU));
}

// Camera-owned matrices are rebuilt in DKR's authored frame before any level
// geometry is emitted. Give each viewport and matrix role an immutable key so
// static terrain can interpolate with camera motion without inheriting an
// object, billboard, or shadow identity. Scene generation prevents history
// from crossing a load or cutscene boundary.
constexpr std::uint32_t make_camera_matrix_identity(
    std::uint32_t scene_generation, std::uint32_t camera_id,
    std::uint8_t matrix_role, std::uint32_t continuity_epoch = 0U) {
    std::uint32_t value = 0x43414D52U;
    value ^= scene_generation * 0x9E3779B9U;
    value ^= (camera_id + 1U) * 0x85EBCA6BU;
    value ^= (static_cast<std::uint32_t>(matrix_role) + 1U) * 0xC2B2AE35U;
    value ^= mix_identity(continuity_epoch + 0x165667B1U);
    return normalise_identity(value);
}

// Every combined MVP produced under one logical viewport camera must stop
// matching history when that camera performs an authored cut. Keeping this
// key separate from the matrix role lets object, attachment, wave and scoped
// presentation identities inherit exactly the same continuity boundary.
constexpr std::uint32_t make_camera_continuity_identity(
    std::uint32_t scene_generation, std::uint32_t camera_id,
    std::uint32_t continuity_epoch) {
    std::uint32_t value = 0x43435458U;
    value ^= scene_generation * 0x9E3779B9U;
    value ^= (camera_id + 1U) * 0x85EBCA6BU;
    value ^= mix_identity(continuity_epoch + 0x165667B1U);
    return normalise_identity(value);
}

constexpr std::uint32_t with_camera_continuity(
    std::uint32_t identity, std::uint32_t camera_identity) {
    if (identity == kIgnoredIdentity || camera_identity == kIgnoredIdentity) {
        return kIgnoredIdentity;
    }
    return normalise_identity(
        identity ^ mix_identity(camera_identity + 0xD3A2646CU));
}

struct CameraContinuitySample {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float fov = 60.0F;
    std::int16_t yaw = 0;
    std::int16_t pitch = 0;
    std::int16_t roll = 0;
};

constexpr std::uint32_t camera_angle_distance(std::int16_t previous,
                                               std::int16_t current) {
    const std::uint32_t forward =
        static_cast<std::uint16_t>(current) -
        static_cast<std::uint16_t>(previous);
    const std::uint32_t wrapped = forward & 0xFFFFU;
    return std::min(wrapped, 0x10000U - wrapped);
}

// Interpolation is valid only while one authored camera follows a continuous
// path. DKR reuses its four cutscene-camera slots between shots, so the slot
// number alone cannot distinguish a smooth move from a teleport. Position and
// FOV discontinuities are reliable cut indicators. Orientation alone is not:
// normal high-speed steering can rotate the camera by more than the old angle
// threshold in one authored tick. Treating that as a cut discarded every
// ordinary scenery object's interpolation history for one presentation frame.
// Authored camera cuts which do not move the camera are handled by the explicit
// scene/camera discontinuity hooks instead.
inline bool camera_sample_discontinuous(
    const CameraContinuitySample& previous,
    const CameraContinuitySample& current,
    float maximum_position_step = 640.0F,
    std::uint32_t = 0x3000U,
    float maximum_fov_step = 20.0F) {
    if (!std::isfinite(previous.x) || !std::isfinite(previous.y) ||
        !std::isfinite(previous.z) || !std::isfinite(previous.fov) ||
        !std::isfinite(current.x) || !std::isfinite(current.y) ||
        !std::isfinite(current.z) || !std::isfinite(current.fov) ||
        !std::isfinite(maximum_position_step) ||
        !std::isfinite(maximum_fov_step) ||
        maximum_position_step <= 0.0F || maximum_fov_step <= 0.0F) {
        return true;
    }

    const float dx = current.x - previous.x;
    const float dy = current.y - previous.y;
    const float dz = current.z - previous.z;
    const float distance_squared = dx * dx + dy * dy + dz * dz;
    const float maximum_distance_squared =
        maximum_position_step * maximum_position_step;
    return !std::isfinite(distance_squared) ||
        distance_squared > maximum_distance_squared ||
        std::abs(current.fov - previous.fov) > maximum_fov_step;
}

// HQ waves alternate between authored vertex/triangle buffers every frame.
// Their matrix allocation and source-buffer addresses are therefore not
// stable identities. The WaveBlockModel entry, tile transform and viewport
// are stable across both buffers and uniquely identify the procedural surface
// draw within a scene.
constexpr std::uint32_t make_wave_matrix_identity(
    std::uint32_t scene_generation, std::uint32_t viewport_id,
    std::uint32_t wave_block_address,
    std::uint32_t transform_x_bits, std::uint32_t transform_y_bits,
    std::uint32_t transform_z_bits, std::uint32_t topology_variant) {
    std::uint32_t value = 0x57415645U;
    value ^= scene_generation * 0x9E3779B9U;
    value ^= (viewport_id + 1U) * 0x85EBCA6BU;
    value ^= mix_identity(wave_block_address + 0x7FEB352DU);
    value ^= mix_identity(transform_x_bits);
    value ^= mix_identity(transform_y_bits + 0x27D4EB2DU);
    value ^= mix_identity(transform_z_bits + 0x165667B1U);
    value ^= topology_variant * 0xC2B2AE35U;
    return normalise_identity(value);
}

// waves_render selects one of twenty-five separately packed full grids for
// each visible tile when the authored wave view distance is 5 (or nine grids
// when it is 3), plus a four-vertex fallback for empty high-density subcells.
// Full-grid selections 1..25 have the same local vertex ordering and topology;
// they merely select the authored sample region used by the stable wave block.
// Treating each selection as a new topology creates a discontinuity precisely
// when the high-detail window crosses a grid boundary. Selection 0 remains a
// distinct topology because it really is the four-vertex fallback quad.
constexpr bool valid_wave_selection_pattern(std::uint8_t selection_pattern) {
    return selection_pattern <= 25U;
}

constexpr std::uint32_t make_wave_topology_variant(
    std::uint32_t subdivisions, bool double_density,
    std::uint8_t selection_pattern, std::uint32_t scale_bits) {
    const bool fallback_quad = double_density && selection_pattern == 0U;
    const bool full_grid = selection_pattern != 0U;
    std::uint32_t value = subdivisions & 0xFFU;
    value |= static_cast<std::uint32_t>(double_density) << 8U;
    value |= static_cast<std::uint32_t>(fallback_quad) << 9U;
    value |= static_cast<std::uint32_t>(full_grid) << 10U;
    value ^= mix_identity(scale_bits) & 0xFFFFC000U;
    return value;
}

// Vehicle-part billboards use one authored transform on either side of the
// camera-facing mirror boundary. Crossing that boundary adds 180 degrees to
// the billboard roll in one simulation tick. Those endpoint matrices must not
// share interpolation history: component-wise interpolation between them can
// collapse the sprite quad to zero area for an intermediate presentation
// frame. Keep interpolation continuous within each hemisphere and discrete at
// the original game's mirror transition.
constexpr std::uint32_t make_vehicle_part_matrix_identity(
    std::uint32_t object_identity, std::uint32_t transform_address,
    bool mirrored) {
    return object_identity == kIgnoredIdentity
        ? kIgnoredIdentity
        : normalise_identity(object_identity ^ 0x56504D58U ^
              ((transform_address & 0x007FFFFFU) * 0x9E3779B9U) ^
              (mirrored ? 0x4D495252U : 0U));
}

// A shadow's owner lifetime and authored scene are its interpolation identity.
// Terrain clipping is allowed to add, remove, or repartition vertices without
// changing that identity; the F3DDKR bridge canonicalises the generated
// batches before they reach RT64. Including the full scene generation prevents
// a freshly loaded track from inheriting history when its 16-bit owner token
// is reused.
constexpr std::uint32_t make_shadow_group_identity(
    std::uint16_t token, std::uint32_t scene_generation = 0U) {
    return token == 0U ? kIgnoredIdentity :
        normalise_identity(0x53484457U ^ static_cast<std::uint32_t>(token) ^
            (scene_generation * 0x9E3779B9U));
}

// Canonical shadow pages are submitted as separate RT64 transform groups. This
// lets a page whose clipped topology changed use current vertices immediately
// without disabling interpolation for the rest of the vehicle shadow.
constexpr std::uint32_t make_shadow_page_group_identity(
    std::uint32_t shadow_identity, std::size_t page,
    std::uint32_t continuity_epoch = 0U) {
    return shadow_identity == kIgnoredIdentity
        ? kIgnoredIdentity
        : normalise_identity(shadow_identity ^ 0x53485047U ^
              ((static_cast<std::uint32_t>(page) + 1U) * 0x85EBCA6BU) ^
              (continuity_epoch * 0xC2B2AE35U));
}

constexpr std::uint32_t make_vehicle_part_group_identity(std::uint16_t token) {
    return token == 0U ? kIgnoredIdentity :
        normalise_identity(0x56505254U ^ static_cast<std::uint32_t>(token));
}

inline constexpr std::uint8_t kInvalidVehiclePartSlot = 0xFFU;

// Vehicle attachments allocate one private 64-byte matrix inside their
// owning render_object call. The ordinal is stable across the two authored
// frame buffers, whereas the matrix address itself and the shared transform
// scratch address are not valid attachment identities.
constexpr std::uint8_t vehicle_part_attachment_slot(
    std::uint32_t first_matrix, std::uint32_t attachment_matrix) {
    constexpr std::uint32_t kMatrixBytes = 64U;
    constexpr std::uint32_t kEncodableSlots = 32U;
    if (attachment_matrix < first_matrix) {
        return kInvalidVehiclePartSlot;
    }
    const std::uint32_t offset = attachment_matrix - first_matrix;
    if ((offset % kMatrixBytes) != 0U) {
        return kInvalidVehiclePartSlot;
    }
    const std::uint32_t ordinal = offset / kMatrixBytes;
    return ordinal < kEncodableSlots
        ? static_cast<std::uint8_t>(ordinal)
        : kInvalidVehiclePartSlot;
}

constexpr std::uint8_t vehicle_part_frame_variant(
    std::uint32_t normalised_frame, std::uint16_t frame_count) {
    if (frame_count == 0U) {
        return 0U;
    }
    const std::uint32_t selected =
        ((normalised_frame & 0xFFU) * frame_count) >> 8U;
    return static_cast<std::uint8_t>(selected & 0x1FU);
}

constexpr std::uint8_t presentation_variant_for_address(
    std::uint32_t address) {
    return static_cast<std::uint8_t>(
        mix_identity(address & 0x007FFFFFU) & 0x1FU);
}

constexpr std::uint32_t make_billboard_group_identity(
    std::uint16_t token, std::uint8_t sprite_variant = 0U) {
    return token == 0U ? kIgnoredIdentity :
        normalise_identity(0x42494C4CU ^ static_cast<std::uint32_t>(token) ^
            (static_cast<std::uint32_t>(sprite_variant & 0x1FU) *
             0x9E3779B9U));
}

constexpr std::uint32_t make_surface_group_identity(
    std::uint16_t token, std::uint8_t variant) {
    return token == 0U ? kIgnoredIdentity :
        normalise_identity(0x53555246U ^ static_cast<std::uint32_t>(token) ^
            (static_cast<std::uint32_t>(variant & 0x1FU) * 0x9E3779B9U));
}

// The token is segmentId + 1 (zero remains the ignored sentinel) and variant
// is the authored opaque/transparent pass. Scene ownership is explicit so a
// level reload can never revive an older segment's interpolation history.
constexpr std::uint32_t make_level_segment_group_identity(
    std::uint16_t token, std::uint8_t pass,
    std::uint32_t scene_generation) {
    return token == 0U || scene_generation == 0U ? kIgnoredIdentity :
        normalise_identity(0x5345474DU ^ static_cast<std::uint32_t>(token) ^
            (static_cast<std::uint32_t>(pass & 1U) * 0x85EBCA6BU) ^
            (scene_generation * 0x9E3779B9U));
}

struct PresentationKey {
    std::uint16_t token = 0U;
    std::uint8_t variant = 0U;
};

constexpr PresentationKey level_segment_presentation_key(
    std::uint32_t segment_id, bool non_opaque) {
    // Retail DKR levels are bounded to 512 segments. Keeping the exact ordinal
    // in the token avoids hash collisions within a scene.
    return segment_id < 512U
        ? PresentationKey{static_cast<std::uint16_t>(segment_id + 1U),
                          static_cast<std::uint8_t>(non_opaque ? 1U : 0U)}
        : PresentationKey{};
}

struct ShadowVertexSample {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

// Limit only the terrain-relative deformation of a projected shadow vertex.
// The owner's measured translation remains exact, while a newly born seam
// point that has no nearby historical counterpart converges over subsequent
// authored frames instead of generating one very long interpolation vector.
inline ShadowVertexSample bounded_shadow_presentation_vertex(
    ShadowVertexSample previous, ShadowVertexSample current,
    ShadowVertexSample translation, float maximum_residual) {
    const ShadowVertexSample predicted{
        previous.x + translation.x,
        previous.y + translation.y,
        previous.z + translation.z,
    };
    if (!std::isfinite(maximum_residual) || maximum_residual <= 0.0F) {
        return predicted;
    }

    const float dx = current.x - predicted.x;
    const float dy = current.y - predicted.y;
    const float dz = current.z - predicted.z;
    const float distance_squared = dx * dx + dy * dy + dz * dz;
    const float maximum_squared = maximum_residual * maximum_residual;
    if (!std::isfinite(distance_squared) || distance_squared <= maximum_squared) {
        return current;
    }

    const float scale = maximum_residual / std::sqrt(distance_squared);
    return {
        predicted.x + dx * scale,
        predicted.y + dy * scale,
        predicted.z + dz * scale,
    };
}

inline constexpr std::size_t kCanonicalShadowBatchVertices = 24U;
// The generator retains at most 32 clipped polygons, each with at most eight
// vertices, and starts a new cache batch before a batch reaches 24 vertices.
// A new batch therefore always accepts at least two polygons, bounding a
// valid shadow to sixteen batches.
inline constexpr std::size_t kMaximumCanonicalShadowBatches = 16U;
inline constexpr std::uint8_t kInvalidShadowVertexSlot = 0xFFU;

struct ShadowTexcoordSample {
    std::int16_t s = 0;
    std::int16_t t = 0;
    bool valid = false;
};

struct ShadowTexcoordBounds {
    std::int16_t minimum_s = 0;
    std::int16_t minimum_t = 0;
    std::int16_t maximum_s = 0;
    std::int16_t maximum_t = 0;
    bool valid = false;
};

struct RigidShadowQuad {
    std::array<ShadowVertexSample, 4> vertices{};
    std::array<ShadowTexcoordSample, 4> texcoords{};
    bool valid = false;
};

// One triangle exactly as emitted by DKR's shadow generator. Texture
// coordinates belong to triangle corners in F3DDKR, rather than to the shared
// RSP vertex cache, so retaining the corner pairs is required to recover the
// same object-yaw projection used by func_8002F440.
struct ShadowTriangleSample {
    std::array<ShadowTexcoordSample, 3> texcoords{};
    std::array<ShadowVertexSample, 3> vertices{};
};

struct DecompShadowReceiverQuad {
    RigidShadowQuad quad{};
    float centre_y = 0.0F;
    std::size_t triangle_index = 0U;
    bool centre_covered = false;
    bool only_above_owner = false;
    bool valid = false;
};

// Taj's carpet shadow spans enough terrain that one receiver plane can cross
// several differently angled collision polygons. Keep the presentation mesh
// topology permanent while giving each quarter-cell its own terrain sample.
// The nine lattice points plus four cell centres fit in one canonical DKR
// shadow page (13 vertices / 16 triangles).
inline constexpr std::size_t kTajShadowSurfaceVertexCount = 13U;
inline constexpr std::size_t kTajShadowSurfaceTriangleCount = 16U;

struct TajShadowSurfacePoint {
    std::uint8_t u_quarters = 0U;
    std::uint8_t v_quarters = 0U;
};

inline constexpr std::array<TajShadowSurfacePoint,
                            kTajShadowSurfaceVertexCount>
    kTajShadowSurfacePoints{{
        {0U, 0U}, {2U, 0U}, {4U, 0U},
        {0U, 2U}, {2U, 2U}, {4U, 2U},
        {0U, 4U}, {2U, 4U}, {4U, 4U},
        {1U, 1U}, {3U, 1U}, {1U, 3U}, {3U, 3U},
    }};

inline constexpr std::array<std::array<std::uint8_t, 3>,
                            kTajShadowSurfaceTriangleCount>
    kTajShadowSurfaceTriangles{{
        {0U, 1U, 9U}, {1U, 4U, 9U},
        {4U, 3U, 9U}, {3U, 0U, 9U},
        {1U, 2U, 10U}, {2U, 5U, 10U},
        {5U, 4U, 10U}, {4U, 1U, 10U},
        {3U, 4U, 11U}, {4U, 7U, 11U},
        {7U, 6U, 11U}, {6U, 3U, 11U},
        {4U, 5U, 12U}, {5U, 8U, 12U},
        {8U, 7U, 12U}, {7U, 4U, 12U},
    }};

struct TajShadowReceiverSurface {
    std::array<ShadowVertexSample, kTajShadowSurfaceVertexCount> vertices{};
    std::array<ShadowTexcoordSample, kTajShadowSurfaceVertexCount> texcoords{};
    bool valid = false;
};

struct RigidShadowShapeMetrics {
    ShadowVertexSample centre{};
    std::array<float, 4> edge_lengths{};
    std::array<float, 2> diagonal_lengths{};
    float adjacent_edge_cosine = 0.0F;
    float area = 0.0F;
    bool valid = false;
};

inline RigidShadowShapeMetrics rigid_shadow_shape_metrics(
    const RigidShadowQuad& quad) {
    RigidShadowShapeMetrics result{};
    if (!quad.valid) return result;
    const auto subtract = [](const ShadowVertexSample& lhs,
                             const ShadowVertexSample& rhs) {
        return ShadowVertexSample{
            lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    };
    const auto dot = [](const ShadowVertexSample& lhs,
                        const ShadowVertexSample& rhs) {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    };
    const auto length = [&](const ShadowVertexSample& value) {
        return std::sqrt(std::max(0.0F, dot(value, value)));
    };
    for (const auto& vertex : quad.vertices) {
        result.centre.x += vertex.x * 0.25F;
        result.centre.y += vertex.y * 0.25F;
        result.centre.z += vertex.z * 0.25F;
    }
    std::array<ShadowVertexSample, 4> edges{};
    for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
        edges[edge] = subtract(
            quad.vertices[(edge + 1U) % quad.vertices.size()],
            quad.vertices[edge]);
        result.edge_lengths[edge] = length(edges[edge]);
    }
    result.diagonal_lengths[0] =
        length(subtract(quad.vertices[2], quad.vertices[0]));
    result.diagonal_lengths[1] =
        length(subtract(quad.vertices[3], quad.vertices[1]));
    const float adjacent_product =
        result.edge_lengths[0] * result.edge_lengths[1];
    if (adjacent_product > 1.0e-6F) {
        result.adjacent_edge_cosine =
            dot(edges[0], edges[1]) / adjacent_product;
    }
    const ShadowVertexSample cross{
        edges[0].y * edges[1].z - edges[0].z * edges[1].y,
        edges[0].z * edges[1].x - edges[0].x * edges[1].z,
        edges[0].x * edges[1].y - edges[0].y * edges[1].x,
    };
    result.area = length(cross);
    result.valid = std::isfinite(result.area) &&
        std::all_of(result.edge_lengths.begin(), result.edge_lengths.end(),
                    [](float value) { return std::isfinite(value); });
    return result;
}

// Convert the current terrain-derived affine quad into a true rigid rectangle.
// Its centre and receiver plane come from the current authored projection, but
// its two edge lengths come from the first trusted frame and its basis is kept
// orthonormal. Terrain seams can therefore translate or rotate the shadow as a
// whole, but can never shear, stretch or independently move one corner.
inline RigidShadowQuad rigid_shadow_fixed_shape_quad(
    const RigidShadowQuad& current,
    const RigidShadowQuad& reference) {
    RigidShadowQuad result{};
    const auto current_metrics = rigid_shadow_shape_metrics(current);
    const auto reference_metrics = rigid_shadow_shape_metrics(reference);
    if (!current_metrics.valid || !reference_metrics.valid) return result;

    const auto subtract = [](const ShadowVertexSample& lhs,
                             const ShadowVertexSample& rhs) {
        return ShadowVertexSample{
            lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    };
    const auto scale = [](const ShadowVertexSample& value, float factor) {
        return ShadowVertexSample{
            value.x * factor, value.y * factor, value.z * factor};
    };
    const auto add = [](const ShadowVertexSample& lhs,
                        const ShadowVertexSample& rhs) {
        return ShadowVertexSample{
            lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    };
    const auto dot = [](const ShadowVertexSample& lhs,
                        const ShadowVertexSample& rhs) {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    };
    const auto cross = [](const ShadowVertexSample& lhs,
                          const ShadowVertexSample& rhs) {
        return ShadowVertexSample{
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x,
        };
    };
    const auto normalise = [&](const ShadowVertexSample& value) {
        const float length_squared = dot(value, value);
        if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
            return ShadowVertexSample{};
        }
        return scale(value, 1.0F / std::sqrt(length_squared));
    };

    const auto current_s = subtract(current.vertices[1], current.vertices[0]);
    const auto current_t = subtract(current.vertices[3], current.vertices[0]);
    auto s_axis = normalise(current_s);
    const auto plane_normal = normalise(cross(current_s, current_t));
    if (dot(s_axis, s_axis) <= 0.0F || dot(plane_normal, plane_normal) <= 0.0F) {
        return result;
    }
    auto t_axis = normalise(cross(plane_normal, s_axis));
    if (dot(t_axis, current_t) < 0.0F) {
        t_axis = scale(t_axis, -1.0F);
    }

    const float s_length = 0.5F *
        (reference_metrics.edge_lengths[0] +
         reference_metrics.edge_lengths[2]);
    const float t_length = 0.5F *
        (reference_metrics.edge_lengths[1] +
         reference_metrics.edge_lengths[3]);
    if (!std::isfinite(s_length) || !std::isfinite(t_length) ||
        s_length <= 1.0e-4F || t_length <= 1.0e-4F) {
        return result;
    }
    const auto half_s = scale(s_axis, s_length * 0.5F);
    const auto half_t = scale(t_axis, t_length * 0.5F);
    const auto centre = current_metrics.centre;
    result.vertices[0] = subtract(subtract(centre, half_s), half_t);
    result.vertices[1] = add(subtract(centre, half_t), half_s);
    result.vertices[2] = add(add(centre, half_s), half_t);
    result.vertices[3] = add(subtract(centre, half_s), half_t);
    result.texcoords = reference.texcoords;
    result.valid = std::all_of(
        result.vertices.begin(), result.vertices.end(),
        [](const ShadowVertexSample& vertex) {
            return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                   std::isfinite(vertex.z);
        });
    return result;
}

inline RigidShadowQuad translated_rigid_shadow_quad(
    RigidShadowQuad quad, const ShadowVertexSample& translation) {
    if (!quad.valid) return {};
    for (auto& vertex : quad.vertices) {
        vertex.x += translation.x;
        vertex.y += translation.y;
        vertex.z += translation.z;
    }
    return quad;
}

// The game can omit every plane-shadow triangle for an authored frame while
// crossing occluding geometry. Keep only that genuinely missing frame visible
// by carrying the last complete receiver height and footprint with the owner's
// horizontal motion and yaw. The decomp uses this same object yaw to author the
// shadow UV axes; owner altitude is never a valid replacement for ground height.
// As soon as DKR emits ground geometry again, that authored quad is authoritative.
inline RigidShadowQuad plane_shadow_missing_frame_quad(
    RigidShadowQuad quad, const ShadowVertexSample& translation,
    std::int16_t previous_yaw, std::int16_t current_yaw) {
    if (!quad.valid) return {};

    const auto metrics = rigid_shadow_shape_metrics(quad);
    if (!metrics.valid) return {};

    std::int32_t yaw_delta =
        static_cast<std::int32_t>(static_cast<std::uint16_t>(current_yaw)) -
        static_cast<std::int32_t>(static_cast<std::uint16_t>(previous_yaw));
    if (yaw_delta > 32767) {
        yaw_delta -= 65536;
    } else if (yaw_delta < -32768) {
        yaw_delta += 65536;
    }

    constexpr float kAnglePerYawUnit =
        6.2831853071795864769F / 65536.0F;
    const float angle = static_cast<float>(yaw_delta) * kAnglePerYawUnit;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    if (!std::isfinite(cosine) || !std::isfinite(sine)) return {};

    for (auto& vertex : quad.vertices) {
        const float local_x = vertex.x - metrics.centre.x;
        const float local_z = vertex.z - metrics.centre.z;
        vertex.x = metrics.centre.x + local_x * cosine + local_z * sine +
            translation.x;
        vertex.z = metrics.centre.z - local_x * sine + local_z * cosine +
            translation.z;
    }
    quad.valid = std::all_of(
        quad.vertices.begin(), quad.vertices.end(),
        [](const ShadowVertexSample& vertex) {
            return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                   std::isfinite(vertex.z);
        });
    return quad;
}

// A missing source mesh can be a transient clipping hole, but DKR also emits
// no ordinary shadow at all over water because RENDER_WATER is excluded from
// the receiver pass. Keep a short continuity bridge without allowing the last
// land receiver to survive an extended no-ground interval.
inline constexpr std::uint64_t kMaximumTransientRigidShadowSourceGapTasks = 8U;

// Taj's carpet normally submits a receiver every authored task. Bridge one
// missing task to hide a transient clip split, but do not let an old terrain
// plane follow him through an extended interval without authored geometry.
inline constexpr std::uint64_t kMaximumTransientTajShadowSourceGapTasks = 1U;

constexpr bool rigid_shadow_source_history_is_fresh(
    std::uint64_t current_task, std::uint64_t last_source_task) {
    return last_source_task != 0U && current_task >= last_source_task &&
        current_task - last_source_task <=
            kMaximumTransientRigidShadowSourceGapTasks;
}

constexpr bool taj_shadow_source_history_is_fresh(
    std::uint64_t current_task, std::uint64_t last_source_task) {
    return last_source_task != 0U && current_task >= last_source_task &&
        current_task - last_source_task <=
            kMaximumTransientTajShadowSourceGapTasks;
}

// DKR projects shadow UVs from world X/Z before terrain height is applied.
// Recover the authored horizontal footprint from that projection, then build
// one orthogonal receiver-plane quad. This preserves DKR's intentional uniform
// height-dependent shadow scale without allowing terrain seams to shear or
// independently resize either axis.
inline RigidShadowQuad rigid_shadow_authored_scale_quad(
    const RigidShadowQuad& current) {
    RigidShadowQuad result{};
    const auto current_metrics = rigid_shadow_shape_metrics(current);
    if (!current_metrics.valid) return result;

    const auto subtract = [](const ShadowVertexSample& lhs,
                             const ShadowVertexSample& rhs) {
        return ShadowVertexSample{
            lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    };
    const auto scale = [](const ShadowVertexSample& value, float factor) {
        return ShadowVertexSample{
            value.x * factor, value.y * factor, value.z * factor};
    };
    const auto add = [](const ShadowVertexSample& lhs,
                        const ShadowVertexSample& rhs) {
        return ShadowVertexSample{
            lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    };
    const auto dot = [](const ShadowVertexSample& lhs,
                        const ShadowVertexSample& rhs) {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    };
    const auto cross = [](const ShadowVertexSample& lhs,
                          const ShadowVertexSample& rhs) {
        return ShadowVertexSample{
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x,
        };
    };
    const auto normalise = [&](const ShadowVertexSample& value) {
        const float length_squared = dot(value, value);
        if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
            return ShadowVertexSample{};
        }
        return scale(value, 1.0F / std::sqrt(length_squared));
    };
    const auto horizontal_length = [](const ShadowVertexSample& value) {
        return std::sqrt(std::max(
            0.0F, value.x * value.x + value.z * value.z));
    };

    const auto current_s = subtract(current.vertices[1], current.vertices[0]);
    const auto current_t = subtract(current.vertices[3], current.vertices[0]);
    const auto opposite_s = subtract(current.vertices[2], current.vertices[3]);
    const auto opposite_t = subtract(current.vertices[2], current.vertices[1]);
    const float authored_horizontal_length = 0.25F *
        (horizontal_length(current_s) + horizontal_length(opposite_s) +
         horizontal_length(current_t) + horizontal_length(opposite_t));
    auto s_axis = normalise(current_s);
    const auto plane_normal = normalise(cross(current_s, current_t));
    if (!std::isfinite(authored_horizontal_length) ||
        authored_horizontal_length <= 1.0e-4F ||
        dot(s_axis, s_axis) <= 0.0F ||
        dot(plane_normal, plane_normal) <= 0.0F) {
        return result;
    }
    auto t_axis = normalise(cross(plane_normal, s_axis));
    if (dot(t_axis, current_t) < 0.0F) {
        t_axis = scale(t_axis, -1.0F);
    }
    const float s_horizontal = horizontal_length(s_axis);
    const float t_horizontal = horizontal_length(t_axis);
    if (s_horizontal <= 1.0e-4F || t_horizontal <= 1.0e-4F) {
        return result;
    }

    const auto half_s = scale(
        s_axis, authored_horizontal_length * 0.5F / s_horizontal);
    const auto half_t = scale(
        t_axis, authored_horizontal_length * 0.5F / t_horizontal);
    const auto centre = current_metrics.centre;
    result.vertices[0] = subtract(subtract(centre, half_s), half_t);
    result.vertices[1] = add(subtract(centre, half_t), half_s);
    result.vertices[2] = add(add(centre, half_s), half_t);
    result.vertices[3] = add(subtract(centre, half_s), half_t);
    result.texcoords = current.texcoords;
    result.valid = std::all_of(
        result.vertices.begin(), result.vertices.end(),
        [](const ShadowVertexSample& vertex) {
            return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                   std::isfinite(vertex.z);
        });
    return result;
}

inline float rigid_shadow_horizontal_size(const RigidShadowQuad& quad) {
    if (!quad.valid) return 0.0F;
    float total = 0.0F;
    for (std::size_t edge = 0U; edge < quad.vertices.size(); ++edge) {
        const auto& first = quad.vertices[edge];
        const auto& second =
            quad.vertices[(edge + 1U) % quad.vertices.size()];
        const float dx = second.x - first.x;
        const float dz = second.z - first.z;
        total += std::sqrt(std::max(0.0F, dx * dx + dz * dz));
    }
    return total * 0.25F;
}

// Preserve a trusted receiver plane and orientation while applying only the
// authored horizontal footprint recovered from the current UV projection.
// This is used when a roof/wall mixture makes the current receiver plane
// unusable but the X/Z projection still carries DKR's intended shadow size.
inline RigidShadowQuad rigid_shadow_horizontal_resized_quad(
    const RigidShadowQuad& trusted, float target_horizontal_size) {
    RigidShadowQuad result{};
    const auto trusted_metrics = rigid_shadow_shape_metrics(trusted);
    const float trusted_horizontal_size = rigid_shadow_horizontal_size(trusted);
    if (!trusted_metrics.valid || !std::isfinite(target_horizontal_size) ||
        target_horizontal_size <= 1.0e-4F ||
        !std::isfinite(trusted_horizontal_size) ||
        trusted_horizontal_size <= 1.0e-4F) {
        return result;
    }

    result = trusted;
    const float scale = target_horizontal_size / trusted_horizontal_size;
    for (auto& vertex : result.vertices) {
        vertex.x = trusted_metrics.centre.x +
            (vertex.x - trusted_metrics.centre.x) * scale;
        vertex.y = trusted_metrics.centre.y +
            (vertex.y - trusted_metrics.centre.y) * scale;
        vertex.z = trusted_metrics.centre.z +
            (vertex.z - trusted_metrics.centre.z) * scale;
    }
    result.valid = std::all_of(
        result.vertices.begin(), result.vertices.end(),
        [](const ShadowVertexSample& vertex) {
            return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                   std::isfinite(vertex.z);
        });
    return result;
}

// Taj's carpet shadow spans enough independently sloped hub polygons that no
// single fitted terrain plane can represent the receiver without visibly
// tilting or intersecting somewhere. Keep DKR's authored X/Z footprint and
// yaw, but place the complete rigid quad on one horizontal receiver. The
// caller supplies its stable height/clearance policy. This makes the texture
// incapable of deforming across a terrain seam.
inline RigidShadowQuad rigid_shadow_horizontal_receiver_quad(
    const RigidShadowQuad& current, float receiver_y) {
    RigidShadowQuad result{};
    const auto metrics = rigid_shadow_shape_metrics(current);
    const float side_length = rigid_shadow_horizontal_size(current);
    if (!metrics.valid || !std::isfinite(receiver_y) ||
        !std::isfinite(side_length) || side_length <= 1.0e-4F) {
        return result;
    }

    const float s_x = current.vertices[1].x - current.vertices[0].x;
    const float s_z = current.vertices[1].z - current.vertices[0].z;
    const float s_length = std::sqrt(std::max(0.0F, s_x * s_x + s_z * s_z));
    if (!std::isfinite(s_length) || s_length <= 1.0e-4F) return result;

    const float unit_s_x = s_x / s_length;
    const float unit_s_z = s_z / s_length;
    float unit_t_x = -unit_s_z;
    float unit_t_z = unit_s_x;
    const float current_t_x = current.vertices[3].x - current.vertices[0].x;
    const float current_t_z = current.vertices[3].z - current.vertices[0].z;
    if (unit_t_x * current_t_x + unit_t_z * current_t_z < 0.0F) {
        unit_t_x = -unit_t_x;
        unit_t_z = -unit_t_z;
    }

    const float half_side = side_length * 0.5F;
    const float half_s_x = unit_s_x * half_side;
    const float half_s_z = unit_s_z * half_side;
    const float half_t_x = unit_t_x * half_side;
    const float half_t_z = unit_t_z * half_side;
    result.vertices = {{
        {metrics.centre.x - half_s_x - half_t_x, receiver_y,
         metrics.centre.z - half_s_z - half_t_z},
        {metrics.centre.x + half_s_x - half_t_x, receiver_y,
         metrics.centre.z + half_s_z - half_t_z},
        {metrics.centre.x + half_s_x + half_t_x, receiver_y,
         metrics.centre.z + half_s_z + half_t_z},
        {metrics.centre.x - half_s_x + half_t_x, receiver_y,
         metrics.centre.z - half_s_z + half_t_z},
    }};
    result.texcoords = current.texcoords;
    result.valid = std::all_of(
        result.vertices.begin(), result.vertices.end(),
        [](const ShadowVertexSample& vertex) {
            return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                   std::isfinite(vertex.z);
        });
    return result;
}

// RSP positions are signed integers. Rounding four float corners separately
// turns even a perfect horizontal square into a slightly skewed parallelogram.
// Quantise one X/Z edge, derive the other as its exact integer perpendicular,
// and build every corner from those two vectors. The submitted shape then has
// equal edges and right angles by construction at every authored frame.
inline RigidShadowQuad rigid_shadow_integer_square_quad(
    const RigidShadowQuad& current) {
    RigidShadowQuad result{};
    if (!current.valid) return result;

    const float desired_s_x =
        current.vertices[1].x - current.vertices[0].x;
    const float desired_s_z =
        current.vertices[1].z - current.vertices[0].z;
    const float desired_t_x =
        current.vertices[3].x - current.vertices[0].x;
    const float desired_t_z =
        current.vertices[3].z - current.vertices[0].z;
    float integer_s_x = std::round(desired_s_x);
    float integer_s_z = std::round(desired_s_z);
    if (!std::isfinite(integer_s_x) || !std::isfinite(integer_s_z) ||
        integer_s_x * integer_s_x + integer_s_z * integer_s_z <= 0.0F) {
        return result;
    }
    float integer_t_x = -integer_s_z;
    float integer_t_z = integer_s_x;
    if (integer_t_x * desired_t_x + integer_t_z * desired_t_z < 0.0F) {
        integer_t_x = -integer_t_x;
        integer_t_z = -integer_t_z;
    }

    const float origin_x = std::round(current.vertices[0].x);
    const float origin_y = std::round(current.vertices[0].y);
    const float origin_z = std::round(current.vertices[0].z);
    result.vertices = {{
        {origin_x, origin_y, origin_z},
        {origin_x + integer_s_x, origin_y, origin_z + integer_s_z},
        {origin_x + integer_s_x + integer_t_x, origin_y,
         origin_z + integer_s_z + integer_t_z},
        {origin_x + integer_t_x, origin_y, origin_z + integer_t_z},
    }};
    result.texcoords = current.texcoords;
    result.valid = std::all_of(
        result.vertices.begin(), result.vertices.end(),
        [](const ShadowVertexSample& vertex) {
            return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                   std::isfinite(vertex.z);
        });
    return result;
}

inline bool rigid_shadow_scale_is_discontinuous(
    const RigidShadowQuad& previous, const RigidShadowQuad& current) {
    const float previous_size = rigid_shadow_horizontal_size(previous);
    const float current_size = rigid_shadow_horizontal_size(current);
    if (!std::isfinite(previous_size) || !std::isfinite(current_size) ||
        previous_size <= 1.0e-4F || current_size <= 1.0e-4F) {
        return false;
    }
    const float ratio = std::max(previous_size, current_size) /
        std::min(previous_size, current_size);
    // DKR's authored height factor is linear (1 + 0.005 * height). A 25%
    // single-tick size jump cannot be ordinary flight but can result from a
    // degenerate clipped fit, so retain the previous trusted footprint.
    return ratio > 1.25F;
}

inline bool rigid_shadow_receiver_fit_is_implausible(
    const RigidShadowShapeMetrics& shape) {
    if (!shape.valid) return true;
    const auto [minimum_edge, maximum_edge] = std::minmax_element(
        shape.edge_lengths.begin(), shape.edge_lengths.end());
    if (minimum_edge == shape.edge_lengths.end() ||
        !std::isfinite(*minimum_edge) || !std::isfinite(*maximum_edge) ||
        *minimum_edge <= 1.0e-4F) {
        return true;
    }
    // A square shadow projected onto an ordinary ground plane remains close
    // to orthogonal with comparable edge lengths. Roof/wall mixtures instead
    // produce the severe shear and aspect ratios seen in the Banjo trace.
    return *maximum_edge / *minimum_edge > 1.5F ||
        std::abs(shape.adjacent_edge_cosine) > 0.25F;
}

inline bool rigid_shadow_receiver_fit_is_near_square(
    const RigidShadowShapeMetrics& shape) {
    if (!shape.valid) return false;
    const auto [minimum_edge, maximum_edge] = std::minmax_element(
        shape.edge_lengths.begin(), shape.edge_lengths.end());
    if (minimum_edge == shape.edge_lengths.end() ||
        !std::isfinite(*minimum_edge) || !std::isfinite(*maximum_edge) ||
        *minimum_edge <= 1.0e-4F) {
        return false;
    }
    return *maximum_edge / *minimum_edge <= 1.1F &&
        std::abs(shape.adjacent_edge_cosine) <= 0.1F;
}

inline bool rigid_shadow_receiver_can_reacquire(
    const RigidShadowShapeMetrics& previous,
    const RigidShadowShapeMetrics& current, float horizontal_motion) {
    if (!previous.valid || !current.valid ||
        rigid_shadow_receiver_fit_is_implausible(current) ||
        !std::isfinite(horizontal_motion)) {
        return false;
    }
    const float allowed_height_difference =
        std::max(6.0F, std::max(0.0F, horizontal_motion) * 0.75F);
    return std::abs(current.centre.y - previous.centre.y) <=
        allowed_height_difference;
}

inline bool plane_shadow_receiver_is_upward_discontinuity(
    float previous_height, float current_height,
    float horizontal_motion) {
    if (!std::isfinite(previous_height) || !std::isfinite(current_height) ||
        !std::isfinite(horizontal_motion)) {
        return false;
    }
    // Authored ground can rise continuously as the plane moves forward. A
    // roof takeover is different: the receiver rises several world units in
    // one 30 Hz step without a matching horizontal traversal. Scale the
    // allowance with motion, retaining a small minimum for quantisation.
    const float allowed_upward_motion =
        std::max(3.0F, std::max(0.0F, horizontal_motion) * 0.5F);
    return current_height - previous_height > allowed_upward_motion;
}

// These are the moving actors identifiable from behaviour alone whose
// projected ground shadows must retain one rigid presentation silhouette.
// Values are DKR's ObjectBehaviours enum: BHV_RACER,
// BHV_VEHICLE_ANIMATION (title sequence), BHV_PARK_WARDEN (Taj), and
// BHV_PARK_WARDEN_2 (the alternate Taj actor). MagicCarpet deliberately does
// not appear here: it shares BHV_ANIMATED_OBJECT with many unrelated cutscene
// actors and must be selected by its exact object ID below.
constexpr bool shadow_behaviour_uses_rigid_actor_proxy(
    std::uint16_t behaviour) {
    return behaviour == 1U || behaviour == 56U || behaviour == 62U ||
           behaviour == 80U;
}

constexpr bool shadow_behaviour_is_park_warden_actor(
    std::uint16_t behaviour) {
    return behaviour == 62U || behaviour == 80U;
}

inline constexpr std::uint16_t kMagicCarpetObjectId = 120U;
inline constexpr std::uint16_t kObjectAssetIdMask = 0x01FFU;

// The rectangular cutscene shadow is owned by ASSET_OBJECT_MAGICCARPET, not
// by either Park Warden/Taj object. Object ID 120 and behaviour 50 are stable
// in both the v1.0 and v1.1 asset tables. Requiring both fields prevents the
// other shadow-bearing BHV_ANIMATED_OBJECT actors (bosses, dancers, etc.) from
// inheriting the carpet-only terrain proxy.
constexpr bool shadow_owner_is_magic_carpet(std::uint16_t behaviour,
                                            std::uint16_t object_id) {
    return behaviour == 50U &&
        (object_id & kObjectAssetIdMask) == kMagicCarpetObjectId;
}

enum class RigidShadowOwnerKind : std::uint8_t {
    None = 0U,
    Racer = 1U,
    TitleVehicle = 2U,
    Taj = 3U,
    MagicCarpet = 4U,
};

struct RigidShadowOwnerPolicy {
    RigidShadowOwnerKind kind = RigidShadowOwnerKind::None;
    std::int8_t vehicle = -1;
    std::uint8_t texture_width = 0U;
    std::uint16_t object_id = 0U;

    constexpr bool required() const {
        return kind != RigidShadowOwnerKind::None;
    }

    constexpr bool plane() const {
        return kind == RigidShadowOwnerKind::Racer && vehicle == 2;
    }

    constexpr bool taj() const {
        return kind == RigidShadowOwnerKind::Taj;
    }

    constexpr bool magic_carpet() const {
        return kind == RigidShadowOwnerKind::MagicCarpet;
    }

    constexpr bool terrain_conforming_actor() const {
        return taj() || magic_carpet();
    }

    constexpr ShadowTexcoordBounds authored_texcoord_bounds() const {
        if (texture_width == 0U) return {};
        const auto extent = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(texture_width) * 32U);
        return {0, 0, extent, extent, true};
    }
};

// Receiver inspection is diagnostic for ordinary racer shadows, while planes,
// title vehicles, the Park Warden actors and MagicCarpet require one real
// decomp-authored terrain triangle to own the Modern rigid proxy. In
// particular, the wide carpet projection must never fit one plane through the
// complete floor/roof/wall cloud produced at geometry seams.
constexpr bool rigid_shadow_policy_inspects_decomp_receiver(
    const RigidShadowOwnerPolicy& policy) {
    return policy.kind == RigidShadowOwnerKind::Racer ||
        policy.kind == RigidShadowOwnerKind::TitleVehicle ||
        policy.terrain_conforming_actor();
}

constexpr bool rigid_shadow_policy_uses_decomp_receiver(
    const RigidShadowOwnerPolicy& policy) {
    return policy.plane() ||
        policy.kind == RigidShadowOwnerKind::TitleVehicle ||
        policy.terrain_conforming_actor();
}

constexpr RigidShadowOwnerPolicy rigid_shadow_owner_policy(
    std::uint16_t behaviour, std::int8_t vehicle = -1,
    std::uint8_t texture_width = 0U, std::uint16_t object_id = 0U) {
    const std::uint16_t asset_id = object_id & kObjectAssetIdMask;
    if (shadow_owner_is_magic_carpet(behaviour, asset_id)) {
        return {RigidShadowOwnerKind::MagicCarpet, -1, texture_width,
                asset_id};
    }
    if (behaviour == 1U) {
        return {RigidShadowOwnerKind::Racer, vehicle, texture_width,
                asset_id};
    }
    if (behaviour == 56U) {
        return {RigidShadowOwnerKind::TitleVehicle, -1, texture_width,
                asset_id};
    }
    if (shadow_behaviour_is_park_warden_actor(behaviour)) {
        return {RigidShadowOwnerKind::Taj, -1, texture_width, asset_id};
    }
    return {};
}

inline ShadowTexcoordBounds shadow_texcoord_bounds(
    std::span<const ShadowTexcoordSample> texcoords) {
    ShadowTexcoordBounds result{};
    for (const auto& texcoord : texcoords) {
        if (!texcoord.valid) continue;
        if (!result.valid) {
            result.minimum_s = result.maximum_s = texcoord.s;
            result.minimum_t = result.maximum_t = texcoord.t;
            result.valid = true;
            continue;
        }
        result.minimum_s = std::min(result.minimum_s, texcoord.s);
        result.minimum_t = std::min(result.minimum_t, texcoord.t);
        result.maximum_s = std::max(result.maximum_s, texcoord.s);
        result.maximum_t = std::max(result.maximum_t, texcoord.t);
    }
    return result;
}

// DKR clips a projected shadow against every ground polygon it overlaps. That
// mesh is correct at an authored instant but its 4/8/12/... vertex topology is
// not a stable object to interpolate. Fit the current projection to one affine
// plane in shadow-UV space and evaluate a fixed four-corner proxy instead. The
// proxy retains DKR's position, rotation, scale and local ground slope while it
// is mathematically incapable of growing seam wedges or changing silhouette.
inline RigidShadowQuad rigid_shadow_quad(
    std::span<const ShadowTexcoordSample> texcoords,
    std::span<const ShadowVertexSample> vertices,
    ShadowTexcoordBounds bounds = {}) {
    RigidShadowQuad result{};
    if (texcoords.size() != vertices.size() || vertices.size() < 3U) {
        return result;
    }
    if (!bounds.valid) {
        bounds = shadow_texcoord_bounds(texcoords);
    }
    if (!bounds.valid || bounds.minimum_s == bounds.maximum_s ||
        bounds.minimum_t == bounds.maximum_t) {
        return result;
    }

    double mean_s = 0.0;
    double mean_t = 0.0;
    double mean_x = 0.0;
    double mean_y = 0.0;
    double mean_z = 0.0;
    std::size_t valid_count = 0U;
    for (std::size_t i = 0U; i < vertices.size(); ++i) {
        if (!texcoords[i].valid || !std::isfinite(vertices[i].x) ||
            !std::isfinite(vertices[i].y) || !std::isfinite(vertices[i].z)) {
            continue;
        }
        mean_s += texcoords[i].s;
        mean_t += texcoords[i].t;
        mean_x += vertices[i].x;
        mean_y += vertices[i].y;
        mean_z += vertices[i].z;
        ++valid_count;
    }
    if (valid_count < 3U) return result;
    const double inverse_count = 1.0 / static_cast<double>(valid_count);
    mean_s *= inverse_count;
    mean_t *= inverse_count;
    mean_x *= inverse_count;
    mean_y *= inverse_count;
    mean_z *= inverse_count;

    double ss = 0.0;
    double st = 0.0;
    double tt = 0.0;
    std::array<double, 3> sp{};
    std::array<double, 3> tp{};
    for (std::size_t i = 0U; i < vertices.size(); ++i) {
        if (!texcoords[i].valid || !std::isfinite(vertices[i].x) ||
            !std::isfinite(vertices[i].y) || !std::isfinite(vertices[i].z)) {
            continue;
        }
        const double ds = static_cast<double>(texcoords[i].s) - mean_s;
        const double dt = static_cast<double>(texcoords[i].t) - mean_t;
        ss += ds * ds;
        st += ds * dt;
        tt += dt * dt;
        sp[0] += ds * (static_cast<double>(vertices[i].x) - mean_x);
        sp[1] += ds * (static_cast<double>(vertices[i].y) - mean_y);
        sp[2] += ds * (static_cast<double>(vertices[i].z) - mean_z);
        tp[0] += dt * (static_cast<double>(vertices[i].x) - mean_x);
        tp[1] += dt * (static_cast<double>(vertices[i].y) - mean_y);
        tp[2] += dt * (static_cast<double>(vertices[i].z) - mean_z);
    }
    const double determinant = ss * tt - st * st;
    const double covariance_scale = std::max(1.0, ss * tt);
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <= covariance_scale * 1.0e-9) {
        return result;
    }

    std::array<double, 3> s_axis{};
    std::array<double, 3> t_axis{};
    for (std::size_t component = 0U; component < 3U; ++component) {
        s_axis[component] =
            (sp[component] * tt - tp[component] * st) / determinant;
        t_axis[component] =
            (tp[component] * ss - sp[component] * st) / determinant;
    }
    const std::array<double, 3> mean_position{mean_x, mean_y, mean_z};
    const std::array<ShadowTexcoordSample, 4> corners{{
        {bounds.minimum_s, bounds.minimum_t, true},
        {bounds.maximum_s, bounds.minimum_t, true},
        {bounds.maximum_s, bounds.maximum_t, true},
        {bounds.minimum_s, bounds.maximum_t, true},
    }};
    for (std::size_t corner = 0U; corner < corners.size(); ++corner) {
        const double ds = static_cast<double>(corners[corner].s) - mean_s;
        const double dt = static_cast<double>(corners[corner].t) - mean_t;
        const std::array<double, 3> position{
            mean_position[0] + s_axis[0] * ds + t_axis[0] * dt,
            mean_position[1] + s_axis[1] * ds + t_axis[1] * dt,
            mean_position[2] + s_axis[2] * ds + t_axis[2] * dt,
        };
        if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
            !std::isfinite(position[2])) {
            return {};
        }
        result.vertices[corner] = {
            static_cast<float>(position[0]),
            static_cast<float>(position[1]),
            static_cast<float>(position[2]),
        };
        result.texcoords[corner] = corners[corner];
    }
    result.valid = true;
    return result;
}

// DKR generates UVs from the owner's X/Z position and yaw, then independently
// evaluates every clipped vertex on its collision plane. A tunnel can therefore
// place floor and roof triangles in the same submitted shadow mesh. Fitting Y
// through that complete cloud invents a receiver between those surfaces even
// though the original game never authored one.
//
// Recover X/Z from every exact triangle-corner UV pair (the decomp-authored
// orientation and scale), but recover Y from one actual receiver triangle at
// the texture centre. When several stacked surfaces cover that centre, retain
// the surface continuous with the previous ground receiver. On first contact,
// choose the lowest surface below the owner so an overhead roof cannot take
// ownership of the shadow.
inline DecompShadowReceiverQuad decomp_shadow_receiver_quad(
    std::span<const ShadowTexcoordSample> corner_texcoords,
    std::span<const ShadowVertexSample> corner_vertices,
    std::span<const ShadowTriangleSample> triangles,
    ShadowTexcoordBounds bounds, float owner_y = 0.0F,
    bool owner_y_valid = false, float previous_receiver_y = 0.0F,
    bool previous_receiver_valid = false) {
    DecompShadowReceiverQuad result{};
    const auto horizontal_quad = rigid_shadow_quad(
        corner_texcoords, corner_vertices, bounds);
    if (!horizontal_quad.valid || triangles.empty() || !bounds.valid) {
        return result;
    }

    const double centre_s = 0.5 *
        (static_cast<double>(bounds.minimum_s) + bounds.maximum_s);
    const double centre_t = 0.5 *
        (static_cast<double>(bounds.minimum_t) + bounds.maximum_t);
    struct Candidate {
        std::array<ShadowTexcoordSample, 3> texcoords{};
        std::array<ShadowVertexSample, 3> vertices{};
        double centre_y = 0.0;
        double uv_distance_squared = 0.0;
        std::size_t index = 0U;
        bool centre_covered = false;
        bool below_owner = false;
        bool valid = false;
    };
    std::array<Candidate,
               kMaximumCanonicalShadowBatches * 16U> candidates{};
    std::size_t candidate_count = 0U;

    const auto point_segment_distance_squared = [](
        double point_s, double point_t, double first_s, double first_t,
        double second_s, double second_t) {
        const double edge_s = second_s - first_s;
        const double edge_t = second_t - first_t;
        const double length_squared = edge_s * edge_s + edge_t * edge_t;
        double amount = 0.0;
        if (length_squared > 1.0e-12) {
            amount = std::clamp(
                ((point_s - first_s) * edge_s +
                 (point_t - first_t) * edge_t) / length_squared,
                0.0, 1.0);
        }
        const double closest_s = first_s + edge_s * amount;
        const double closest_t = first_t + edge_t * amount;
        const double distance_s = point_s - closest_s;
        const double distance_t = point_t - closest_t;
        return distance_s * distance_s + distance_t * distance_t;
    };

    for (std::size_t triangle_index = 0U;
         triangle_index < triangles.size() &&
         candidate_count < candidates.size(); ++triangle_index) {
        const auto& triangle = triangles[triangle_index];
        if (std::any_of(
                triangle.texcoords.begin(), triangle.texcoords.end(),
                [](const ShadowTexcoordSample& sample) {
                    return !sample.valid;
                }) ||
            std::any_of(
                triangle.vertices.begin(), triangle.vertices.end(),
                [](const ShadowVertexSample& vertex) {
                    return !std::isfinite(vertex.x) ||
                        !std::isfinite(vertex.y) ||
                        !std::isfinite(vertex.z);
                })) {
            continue;
        }

        const double s0 = triangle.texcoords[0].s;
        const double t0 = triangle.texcoords[0].t;
        const double s1 = triangle.texcoords[1].s;
        const double t1 = triangle.texcoords[1].t;
        const double s2 = triangle.texcoords[2].s;
        const double t2 = triangle.texcoords[2].t;
        const double determinant =
            (t1 - t2) * (s0 - s2) + (s2 - s1) * (t0 - t2);
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-6) {
            continue;
        }
        const double weight0 =
            ((t1 - t2) * (centre_s - s2) +
             (s2 - s1) * (centre_t - t2)) / determinant;
        const double weight1 =
            ((t2 - t0) * (centre_s - s2) +
             (s0 - s2) * (centre_t - t2)) / determinant;
        const double weight2 = 1.0 - weight0 - weight1;
        const double receiver_y =
            weight0 * triangle.vertices[0].y +
            weight1 * triangle.vertices[1].y +
            weight2 * triangle.vertices[2].y;
        if (!std::isfinite(receiver_y)) continue;

        const ShadowVertexSample edge0{
            triangle.vertices[1].x - triangle.vertices[0].x,
            triangle.vertices[1].y - triangle.vertices[0].y,
            triangle.vertices[1].z - triangle.vertices[0].z};
        const ShadowVertexSample edge1{
            triangle.vertices[2].x - triangle.vertices[0].x,
            triangle.vertices[2].y - triangle.vertices[0].y,
            triangle.vertices[2].z - triangle.vertices[0].z};
        const ShadowVertexSample normal{
            edge0.y * edge1.z - edge0.z * edge1.y,
            edge0.z * edge1.x - edge0.x * edge1.z,
            edge0.x * edge1.y - edge0.y * edge1.x};
        const double normal_length = std::sqrt(
            static_cast<double>(normal.x) * normal.x +
            static_cast<double>(normal.y) * normal.y +
            static_cast<double>(normal.z) * normal.z);
        // The decomp rejects exactly vertical collision planes. A small bound
        // additionally excludes near-vertical tunnel walls whose integer UVs
        // cannot provide a numerically stable full-footprint receiver.
        if (!std::isfinite(normal_length) || normal_length <= 1.0e-6 ||
            std::abs(static_cast<double>(normal.y)) / normal_length < 0.1) {
            continue;
        }

        constexpr double kBarycentricTolerance = 0.02;
        const bool centre_covered =
            weight0 >= -kBarycentricTolerance &&
            weight1 >= -kBarycentricTolerance &&
            weight2 >= -kBarycentricTolerance;
        const double uv_distance_squared = centre_covered ? 0.0 : std::min({
            point_segment_distance_squared(
                centre_s, centre_t, s0, t0, s1, t1),
            point_segment_distance_squared(
                centre_s, centre_t, s1, t1, s2, t2),
            point_segment_distance_squared(
                centre_s, centre_t, s2, t2, s0, t0),
        });
        constexpr double kDecompVertexLiftAllowance = 4.0;
        candidates[candidate_count++] = {
            triangle.texcoords,
            triangle.vertices,
            receiver_y,
            uv_distance_squared,
            triangle_index,
            centre_covered,
            !owner_y_valid ||
                receiver_y <= static_cast<double>(owner_y) +
                    kDecompVertexLiftAllowance,
            true,
        };
    }
    if (candidate_count == 0U) return result;

    const bool any_centre_covered = std::any_of(
        candidates.begin(), candidates.begin() + candidate_count,
        [](const Candidate& candidate) { return candidate.centre_covered; });
    const bool any_below_owner = std::any_of(
        candidates.begin(), candidates.begin() + candidate_count,
        [&](const Candidate& candidate) {
            return (!any_centre_covered || candidate.centre_covered) &&
                candidate.below_owner;
        });
    if (owner_y_valid && !any_below_owner) {
        // Shadows project downwards. Over water there may be no solid receiver,
        // so an overhead roof can be the only submitted surface; accepting it
        // makes the proxy jump onto the ceiling even though no ground exists.
        result.only_above_owner = true;
        return result;
    }

    const Candidate* selected = nullptr;
    for (std::size_t index = 0U; index < candidate_count; ++index) {
        const Candidate& candidate = candidates[index];
        if (any_centre_covered && !candidate.centre_covered) continue;
        if (any_below_owner && !candidate.below_owner) continue;
        if (selected == nullptr) {
            selected = &candidate;
            continue;
        }
        const double distance_epsilon = 1.0e-6;
        if (!any_centre_covered &&
            candidate.uv_distance_squared + distance_epsilon <
                selected->uv_distance_squared) {
            selected = &candidate;
            continue;
        }
        if (!any_centre_covered &&
            candidate.uv_distance_squared >
                selected->uv_distance_squared + distance_epsilon) {
            continue;
        }
        const double candidate_height_score = previous_receiver_valid
            ? std::abs(candidate.centre_y - previous_receiver_y)
            : candidate.centre_y;
        const double selected_height_score = previous_receiver_valid
            ? std::abs(selected->centre_y - previous_receiver_y)
            : selected->centre_y;
        if (candidate_height_score + 1.0e-6 < selected_height_score ||
            (std::abs(candidate_height_score - selected_height_score) <=
                 1.0e-6 &&
             candidate.index < selected->index)) {
            selected = &candidate;
        }
    }
    if (selected == nullptr) return result;

    const auto receiver_quad = rigid_shadow_quad(
        selected->texcoords, selected->vertices, bounds);
    if (!receiver_quad.valid) return result;
    result.quad = horizontal_quad;
    for (std::size_t corner = 0U; corner < result.quad.vertices.size();
         ++corner) {
        result.quad.vertices[corner].y = receiver_quad.vertices[corner].y;
    }
    result.centre_y = static_cast<float>(selected->centre_y);
    result.triangle_index = selected->index;
    result.centre_covered = selected->centre_covered;
    result.valid = true;
    return result;
}

// Build a permanent Taj-only presentation surface over the selected decomp
// receiver. X/Z and UV always come from the authored rigid footprint. Y is
// sampled independently at the fixed lattice points from the real clipped
// terrain triangles, matching the per-vertex collision-plane evaluation in
// func_8002F2AC. A selected receiver-plane sample is the fallback and the
// height reference used to reject stacked roofs.
inline TajShadowReceiverSurface taj_shadow_receiver_surface(
    const DecompShadowReceiverQuad& receiver,
    std::span<const ShadowTriangleSample> triangles,
    ShadowTexcoordBounds bounds, float owner_y = 0.0F,
    bool owner_y_valid = false) {
    TajShadowReceiverSurface result{};
    if (!receiver.valid || !receiver.quad.valid || !bounds.valid) {
        return result;
    }

    const auto bilinear_position = [&](double u, double v) {
        const double weight0 = (1.0 - u) * (1.0 - v);
        const double weight1 = u * (1.0 - v);
        const double weight2 = u * v;
        const double weight3 = (1.0 - u) * v;
        const auto& vertices = receiver.quad.vertices;
        return ShadowVertexSample{
            static_cast<float>(
                vertices[0].x * weight0 + vertices[1].x * weight1 +
                vertices[2].x * weight2 + vertices[3].x * weight3),
            static_cast<float>(
                vertices[0].y * weight0 + vertices[1].y * weight1 +
                vertices[2].y * weight2 + vertices[3].y * weight3),
            static_cast<float>(
                vertices[0].z * weight0 + vertices[1].z * weight1 +
                vertices[2].z * weight2 + vertices[3].z * weight3),
        };
    };

    for (std::size_t point_index = 0U;
         point_index < kTajShadowSurfacePoints.size(); ++point_index) {
        const auto point = kTajShadowSurfacePoints[point_index];
        const double u = static_cast<double>(point.u_quarters) * 0.25;
        const double v = static_cast<double>(point.v_quarters) * 0.25;
        const double target_s =
            static_cast<double>(bounds.minimum_s) +
            (static_cast<double>(bounds.maximum_s) - bounds.minimum_s) * u;
        const double target_t =
            static_cast<double>(bounds.minimum_t) +
            (static_cast<double>(bounds.maximum_t) - bounds.minimum_t) * v;
        const auto reference = bilinear_position(u, v);
        if (!std::isfinite(reference.x) || !std::isfinite(reference.y) ||
            !std::isfinite(reference.z)) {
            return {};
        }

        double selected_y = reference.y;
        double selected_score = std::numeric_limits<double>::max();
        bool selected = false;
        for (const auto& triangle : triangles) {
            if (std::any_of(
                    triangle.texcoords.begin(), triangle.texcoords.end(),
                    [](const ShadowTexcoordSample& sample) {
                        return !sample.valid;
                    }) ||
                std::any_of(
                    triangle.vertices.begin(), triangle.vertices.end(),
                    [](const ShadowVertexSample& vertex) {
                        return !std::isfinite(vertex.x) ||
                            !std::isfinite(vertex.y) ||
                            !std::isfinite(vertex.z);
                    })) {
                continue;
            }

            const double s0 = triangle.texcoords[0].s;
            const double t0 = triangle.texcoords[0].t;
            const double s1 = triangle.texcoords[1].s;
            const double t1 = triangle.texcoords[1].t;
            const double s2 = triangle.texcoords[2].s;
            const double t2 = triangle.texcoords[2].t;
            const double determinant =
                (t1 - t2) * (s0 - s2) + (s2 - s1) * (t0 - t2);
            if (!std::isfinite(determinant) ||
                std::abs(determinant) <= 1.0e-6) {
                continue;
            }
            const double weight0 =
                ((t1 - t2) * (target_s - s2) +
                 (s2 - s1) * (target_t - t2)) / determinant;
            const double weight1 =
                ((t2 - t0) * (target_s - s2) +
                 (s0 - s2) * (target_t - t2)) / determinant;
            const double weight2 = 1.0 - weight0 - weight1;
            constexpr double kBarycentricTolerance = 0.02;
            if (weight0 < -kBarycentricTolerance ||
                weight1 < -kBarycentricTolerance ||
                weight2 < -kBarycentricTolerance) {
                continue;
            }

            const ShadowVertexSample edge0{
                triangle.vertices[1].x - triangle.vertices[0].x,
                triangle.vertices[1].y - triangle.vertices[0].y,
                triangle.vertices[1].z - triangle.vertices[0].z};
            const ShadowVertexSample edge1{
                triangle.vertices[2].x - triangle.vertices[0].x,
                triangle.vertices[2].y - triangle.vertices[0].y,
                triangle.vertices[2].z - triangle.vertices[0].z};
            const ShadowVertexSample normal{
                edge0.y * edge1.z - edge0.z * edge1.y,
                edge0.z * edge1.x - edge0.x * edge1.z,
                edge0.x * edge1.y - edge0.y * edge1.x};
            const double normal_length = std::sqrt(
                static_cast<double>(normal.x) * normal.x +
                static_cast<double>(normal.y) * normal.y +
                static_cast<double>(normal.z) * normal.z);
            if (!std::isfinite(normal_length) || normal_length <= 1.0e-6 ||
                std::abs(static_cast<double>(normal.y)) / normal_length <
                    0.1) {
                continue;
            }

            const double sample_y =
                weight0 * triangle.vertices[0].y +
                weight1 * triangle.vertices[1].y +
                weight2 * triangle.vertices[2].y;
            constexpr double kDecompVertexLiftAllowance = 4.0;
            if (!std::isfinite(sample_y) ||
                (owner_y_valid &&
                 sample_y > static_cast<double>(owner_y) +
                     kDecompVertexLiftAllowance)) {
                continue;
            }
            const double score =
                std::abs(sample_y - static_cast<double>(reference.y));
            if (!selected || score + 1.0e-6 < selected_score ||
                (std::abs(score - selected_score) <= 1.0e-6 &&
                 sample_y < selected_y)) {
                selected = true;
                selected_y = sample_y;
                selected_score = score;
            }
        }

        result.vertices[point_index] = reference;
        result.vertices[point_index].y = static_cast<float>(selected_y);
        result.texcoords[point_index] = {
            static_cast<std::int16_t>(std::clamp(
                std::lround(target_s), -32768L, 32767L)),
            static_cast<std::int16_t>(std::clamp(
                std::lround(target_t), -32768L, 32767L)),
            true,
        };
    }
    result.valid = true;
    return result;
}

// Return the vertical separation needed to keep a submitted rigid quad at or
// above every vertex in DKR's terrain-wrapped source mesh. The submitted
// corners are supplied after RSP integer rounding so this also accounts for
// the half-unit loss that rounding can otherwise introduce. Since both the
// terrain triangles and each half of the proxy are planar, testing the clipped
// source vertices covers the extrema over the complete projected footprint.
inline float rigid_shadow_vertical_clearance(
    std::span<const ShadowTexcoordSample> texcoords,
    std::span<const ShadowVertexSample> vertices,
    const RigidShadowQuad& submitted_quad) {
    if (!submitted_quad.valid || texcoords.size() != vertices.size()) {
        return 0.0F;
    }
    const auto& uv0 = submitted_quad.texcoords[0];
    const auto& uv2 = submitted_quad.texcoords[2];
    const double extent_s = static_cast<double>(uv2.s) - uv0.s;
    const double extent_t = static_cast<double>(uv2.t) - uv0.t;
    if (extent_s == 0.0 || extent_t == 0.0) return 0.0F;

    float clearance = 0.0F;
    for (std::size_t index = 0U; index < vertices.size(); ++index) {
        if (!texcoords[index].valid || !std::isfinite(vertices[index].y)) {
            continue;
        }
        const double u = std::clamp(
            (static_cast<double>(texcoords[index].s) - uv0.s) / extent_s,
            0.0, 1.0);
        const double v = std::clamp(
            (static_cast<double>(texcoords[index].t) - uv0.t) / extent_t,
            0.0, 1.0);
        double proxy_y = 0.0;
        if (u >= v) {
            proxy_y =
                (1.0 - u) * submitted_quad.vertices[0].y +
                (u - v) * submitted_quad.vertices[1].y +
                v * submitted_quad.vertices[2].y;
        } else {
            proxy_y =
                (1.0 - v) * submitted_quad.vertices[0].y +
                u * submitted_quad.vertices[2].y +
                (v - u) * submitted_quad.vertices[3].y;
        }
        clearance = std::max(
            clearance,
            static_cast<float>(static_cast<double>(vertices[index].y) -
                               proxy_y));
    }
    return std::isfinite(clearance) ? clearance : 0.0F;
}

struct ShadowCanonicalSlotMap {
    std::array<std::uint8_t, kCanonicalShadowBatchVertices>
        source_for_slot{};
    std::array<std::uint8_t, kCanonicalShadowBatchVertices>
        slot_for_source{};
};

inline ShadowVertexSample shadow_cloud_translation(
    std::span<const ShadowTexcoordSample> previous_texcoords,
    std::span<const ShadowVertexSample> previous_vertices,
    std::span<const ShadowVertexSample> current_vertices) {
    if (previous_texcoords.size() != previous_vertices.size() ||
        current_vertices.empty()) {
        return {};
    }

    ShadowVertexSample previous_min{};
    ShadowVertexSample previous_max{};
    ShadowVertexSample current_min = current_vertices.front();
    ShadowVertexSample current_max = current_vertices.front();
    bool have_previous = false;
    for (std::size_t index = 0U; index < previous_vertices.size(); ++index) {
        if (!previous_texcoords[index].valid) continue;
        const auto& sample = previous_vertices[index];
        if (!have_previous) {
            previous_min = sample;
            previous_max = sample;
            have_previous = true;
        } else {
            previous_min.x = std::min(previous_min.x, sample.x);
            previous_min.y = std::min(previous_min.y, sample.y);
            previous_min.z = std::min(previous_min.z, sample.z);
            previous_max.x = std::max(previous_max.x, sample.x);
            previous_max.y = std::max(previous_max.y, sample.y);
            previous_max.z = std::max(previous_max.z, sample.z);
        }
    }
    if (!have_previous) return {};

    for (const auto& sample : current_vertices.subspan(1U)) {
        current_min.x = std::min(current_min.x, sample.x);
        current_min.y = std::min(current_min.y, sample.y);
        current_min.z = std::min(current_min.z, sample.z);
        current_max.x = std::max(current_max.x, sample.x);
        current_max.y = std::max(current_max.y, sample.y);
        current_max.z = std::max(current_max.z, sample.z);
    }
    return {
        (current_min.x + current_max.x - previous_min.x - previous_max.x) *
            0.5F,
        (current_min.y + current_max.y - previous_min.y - previous_max.y) *
            0.5F,
        (current_min.z + current_max.z - previous_min.z - previous_max.z) *
            0.5F,
    };
}

// Assign the current polygon vertices to the complete persistent slot page by
// their motion-compensated world positions. UVs remain a bounded semantic
// preference, but can never force a vertex to inherit a remote old position.
// This is important at terrain clipping boundaries: a UV can be recycled while
// its old point is on a different roof or track polygon. A minimum-cost
// assignment also avoids the single bad leftover pair produced by a greedy
// nearest-neighbour pass.
inline ShadowCanonicalSlotMap canonical_shadow_motion_slot_map(
    std::span<const ShadowTexcoordSample> previous_texcoords,
    std::span<const ShadowVertexSample> previous_vertices,
    std::span<const ShadowTexcoordSample> current_texcoords,
    std::span<const ShadowVertexSample> current_vertices,
    ShadowVertexSample translation) {
    ShadowCanonicalSlotMap result{};
    result.source_for_slot.fill(kInvalidShadowVertexSlot);
    result.slot_for_source.fill(kInvalidShadowVertexSlot);
    if (previous_texcoords.size() != kCanonicalShadowBatchVertices ||
        previous_vertices.size() != kCanonicalShadowBatchVertices ||
        current_texcoords.size() != current_vertices.size() ||
        current_vertices.size() > kCanonicalShadowBatchVertices ||
        current_vertices.empty()) {
        return result;
    }

    ShadowVertexSample current_min = current_vertices.front();
    ShadowVertexSample current_max = current_vertices.front();
    for (const auto& sample : current_vertices.subspan(1U)) {
        current_min.x = std::min(current_min.x, sample.x);
        current_min.y = std::min(current_min.y, sample.y);
        current_min.z = std::min(current_min.z, sample.z);
        current_max.x = std::max(current_max.x, sample.x);
        current_max.y = std::max(current_max.y, sample.y);
        current_max.z = std::max(current_max.z, sample.z);
    }
    const double extent_x = current_max.x - current_min.x;
    const double extent_y = current_max.y - current_min.y;
    const double extent_z = current_max.z - current_min.z;
    const double footprint_squared = std::max(
        1.0, extent_x * extent_x + extent_y * extent_y +
                 extent_z * extent_z);
    const double semantic_weight = footprint_squared * 4.0;
    const double safe_active_residual_squared =
        std::max(36.0, footprint_squared * 0.0625);
    const double inactive_slot_penalty =
        std::max(64.0, footprint_squared * 8.0);
    const double unsafe_active_slot_penalty =
        std::max(128.0, footprint_squared * 16.0);

    const std::size_t rows = current_vertices.size();
    const std::size_t columns = kCanonicalShadowBatchVertices;
    std::array<double, kCanonicalShadowBatchVertices + 1U> row_potential{};
    std::array<double, kCanonicalShadowBatchVertices + 1U> column_potential{};
    std::array<std::size_t, kCanonicalShadowBatchVertices + 1U> matched_row{};
    std::array<std::size_t, kCanonicalShadowBatchVertices + 1U> previous_column{};
    const auto cost = [&](std::size_t source, std::size_t slot) {
        const double dx = static_cast<double>(current_vertices[source].x) -
            (static_cast<double>(previous_vertices[slot].x) + translation.x);
        const double dy = static_cast<double>(current_vertices[source].y) -
            (static_cast<double>(previous_vertices[slot].y) + translation.y);
        const double dz = static_cast<double>(current_vertices[source].z) -
            (static_cast<double>(previous_vertices[slot].z) + translation.z);
        double value = dx * dx + dy * dy + dz * dz;
        if (previous_texcoords[slot].valid &&
            value > safe_active_residual_squared) {
            // A recycled UV on another terrain polygon is not continuity.
            // Prefer a dormant slot near the new point instead of forcing a
            // remote active slot to keep its old semantic label.
            value += unsafe_active_slot_penalty;
        }
        if (previous_texcoords[slot].valid && current_texcoords[source].valid) {
            const double ds = static_cast<double>(current_texcoords[source].s) -
                              previous_texcoords[slot].s;
            const double dt = static_cast<double>(current_texcoords[source].t) -
                              previous_texcoords[slot].t;
            const double uv_squared = ds * ds + dt * dt;
            value += semantic_weight *
                     (uv_squared / (uv_squared + 65536.0));
        } else {
            // When an active semantic match remains spatially safe it must win
            // over a dormant slot that happens to be a little closer. This is
            // what keeps triangle indices stable during ordinary shadow motion.
            value += inactive_slot_penalty;
        }
        // Make equal-cost assignments deterministic without changing a real
        // geometric decision.
        return value + static_cast<double>(slot) * 1.0e-9;
    };

    // Rectangular Hungarian assignment (rows <= columns).
    for (std::size_t row = 1U; row <= rows; ++row) {
        matched_row[0] = row;
        std::size_t column0 = 0U;
        std::array<double, kCanonicalShadowBatchVertices + 1U> minimum{};
        minimum.fill(std::numeric_limits<double>::infinity());
        std::array<bool, kCanonicalShadowBatchVertices + 1U> used{};
        do {
            used[column0] = true;
            const std::size_t row0 = matched_row[column0];
            double delta = std::numeric_limits<double>::infinity();
            std::size_t column1 = 0U;
            for (std::size_t column = 1U; column <= columns; ++column) {
                if (used[column]) continue;
                const double reduced = cost(row0 - 1U, column - 1U) -
                    row_potential[row0] - column_potential[column];
                if (reduced < minimum[column]) {
                    minimum[column] = reduced;
                    previous_column[column] = column0;
                }
                if (minimum[column] < delta ||
                    (minimum[column] == delta && column < column1)) {
                    delta = minimum[column];
                    column1 = column;
                }
            }
            for (std::size_t column = 0U; column <= columns; ++column) {
                if (used[column]) {
                    row_potential[matched_row[column]] += delta;
                    column_potential[column] -= delta;
                } else {
                    minimum[column] -= delta;
                }
            }
            column0 = column1;
        } while (matched_row[column0] != 0U);
        do {
            const std::size_t column1 = previous_column[column0];
            matched_row[column0] = matched_row[column1];
            column0 = column1;
        } while (column0 != 0U);
    }

    for (std::size_t column = 1U; column <= columns; ++column) {
        if (matched_row[column] == 0U) continue;
        const std::size_t source = matched_row[column] - 1U;
        const std::size_t slot = column - 1U;
        result.source_for_slot[slot] = static_cast<std::uint8_t>(source);
        result.slot_for_source[source] = static_cast<std::uint8_t>(slot);
    }
    return result;
}

// Dormant canonical slots are real interpolation history even though no
// triangle references them in the current authored frame. Spread them across
// the complete current page instead of clustering them at whichever old point
// happened to be nearest. A seam vertex can then be born from a nearby point
// inside the existing footprint while vertex interpolation remains enabled.
constexpr std::size_t canonical_shadow_dormant_source(
    std::size_t slot, std::size_t source_count) {
    if (source_count == 0U) return 0U;
    const std::size_t bounded_slot =
        std::min(slot, kCanonicalShadowBatchVertices - 1U);
    return std::min(
        source_count - 1U,
        bounded_slot * source_count / kCanonicalShadowBatchVertices);
}

// Every valid semantic shadow page owns a fixed 24-vertex stream. Topology and
// correspondence are diagnostic inputs to canonicalisation, never switches
// that may make an established shadow alternate between interpolated and
// authored presentation frames.
constexpr bool shadow_page_vertex_interpolation(std::uint32_t shadow_identity) {
    return shadow_identity != kIgnoredIdentity;
}

// A canonical slot may only contribute interpolation history when it was an
// active vertex in the immediately preceding page and remains close after the
// shadow owner's measured translation is removed. Newly clipped seam vertices
// still receive slots so the fixed stream stays complete, but they must render
// from their current positions instead of inheriting unrelated velocity.
inline bool canonical_shadow_slot_map_corresponds(
    std::span<const ShadowTexcoordSample> previous_texcoords,
    std::span<const ShadowVertexSample> previous_vertices,
    std::span<const ShadowVertexSample> current_vertices,
    const ShadowCanonicalSlotMap& slot_map,
    ShadowVertexSample translation,
    float maximum_residual) {
    if (previous_texcoords.size() != kCanonicalShadowBatchVertices ||
        previous_vertices.size() != kCanonicalShadowBatchVertices ||
        current_vertices.empty() ||
        current_vertices.size() > kCanonicalShadowBatchVertices ||
        !std::isfinite(maximum_residual) || maximum_residual <= 0.0F) {
        return false;
    }

    const float maximum_residual_squared = maximum_residual * maximum_residual;
    for (std::size_t source = 0U; source < current_vertices.size(); ++source) {
        const std::uint8_t slot = slot_map.slot_for_source[source];
        if (slot == kInvalidShadowVertexSlot ||
            slot >= kCanonicalShadowBatchVertices ||
            !previous_texcoords[slot].valid) {
            return false;
        }
        const float dx = current_vertices[source].x -
            (previous_vertices[slot].x + translation.x);
        const float dy = current_vertices[source].y -
            (previous_vertices[slot].y + translation.y);
        const float dz = current_vertices[source].z -
            (previous_vertices[slot].z + translation.z);
        const float residual_squared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(residual_squared) ||
            residual_squared > maximum_residual_squared) {
            return false;
        }
    }
    return true;
}

// DKR regenerates a terrain-clipped shadow mesh every authored frame. Match
// its vertices by their object-relative shadow texture coordinates so a
// recycled source index cannot acquire an unrelated velocity. Existing slots
// are matched first; newly clipped vertices consume unused slots and never
// shift surviving vertices merely because their source order changed.
inline ShadowCanonicalSlotMap canonical_shadow_slot_map(
    std::span<const ShadowTexcoordSample> previous_slots,
    std::span<const ShadowTexcoordSample> current_vertices) {
    ShadowCanonicalSlotMap result{};
    result.source_for_slot.fill(kInvalidShadowVertexSlot);
    result.slot_for_source.fill(kInvalidShadowVertexSlot);
    if (previous_slots.size() != kCanonicalShadowBatchVertices ||
        current_vertices.size() > kCanonicalShadowBatchVertices) {
        return result;
    }

    std::array<bool, kCanonicalShadowBatchVertices> slot_used{};
    std::array<bool, kCanonicalShadowBatchVertices> source_used{};
    const std::size_t current_count = current_vertices.size();

    // Repeatedly take the globally closest unused UV pair. The deterministic
    // slot/source tie-break keeps the result identical across host platforms.
    for (;;) {
        std::uint64_t best_distance =
            std::numeric_limits<std::uint64_t>::max();
        std::size_t best_slot = kCanonicalShadowBatchVertices;
        std::size_t best_source = kCanonicalShadowBatchVertices;
        for (std::size_t slot = 0U; slot < previous_slots.size(); ++slot) {
            if (slot_used[slot] || !previous_slots[slot].valid) continue;
            for (std::size_t source = 0U; source < current_count; ++source) {
                if (source_used[source] || !current_vertices[source].valid) {
                    continue;
                }
                const std::int64_t ds =
                    static_cast<std::int64_t>(current_vertices[source].s) -
                    previous_slots[slot].s;
                const std::int64_t dt =
                    static_cast<std::int64_t>(current_vertices[source].t) -
                    previous_slots[slot].t;
                const std::uint64_t distance =
                    static_cast<std::uint64_t>(ds * ds + dt * dt);
                if (distance < best_distance ||
                    (distance == best_distance &&
                     (slot < best_slot ||
                      (slot == best_slot && source < best_source)))) {
                    best_distance = distance;
                    best_slot = slot;
                    best_source = source;
                }
            }
        }
        if (best_slot == kCanonicalShadowBatchVertices) break;
        result.source_for_slot[best_slot] =
            static_cast<std::uint8_t>(best_source);
        result.slot_for_source[best_source] =
            static_cast<std::uint8_t>(best_slot);
        slot_used[best_slot] = true;
        source_used[best_source] = true;
    }

    // A first-seen vertex has no historical counterpart. Assign such vertices
    // in UV order to the lowest free slots, preserving all established slots.
    for (;;) {
        std::size_t next_source = kCanonicalShadowBatchVertices;
        for (std::size_t source = 0U; source < current_count; ++source) {
            if (source_used[source] || !current_vertices[source].valid) continue;
            if (next_source == kCanonicalShadowBatchVertices ||
                current_vertices[source].s < current_vertices[next_source].s ||
                (current_vertices[source].s == current_vertices[next_source].s &&
                 (current_vertices[source].t < current_vertices[next_source].t ||
                  (current_vertices[source].t == current_vertices[next_source].t &&
                   source < next_source)))) {
                next_source = source;
            }
        }
        if (next_source == kCanonicalShadowBatchVertices) break;

        std::size_t free_slot = 0U;
        while (free_slot < kCanonicalShadowBatchVertices &&
               slot_used[free_slot]) {
            ++free_slot;
        }
        if (free_slot == kCanonicalShadowBatchVertices) break;
        result.source_for_slot[free_slot] =
            static_cast<std::uint8_t>(next_source);
        result.slot_for_source[next_source] =
            static_cast<std::uint8_t>(free_slot);
        slot_used[free_slot] = true;
        source_used[next_source] = true;
    }
    return result;
}

using ShadowCanonicalPage =
    std::array<ShadowTexcoordSample, kCanonicalShadowBatchVertices>;

struct ShadowCanonicalPageMap {
    std::array<std::uint8_t, kMaximumCanonicalShadowBatches>
        batch_for_page{};
    std::array<std::uint8_t, kMaximumCanonicalShadowBatches>
        page_for_batch{};
};

inline constexpr std::uint8_t kInvalidShadowPage = 0xFFU;

// DKR partitions consecutive clipped polygons into temporary 24-vertex cache
// batches. A polygon entering or leaving the footprint can shift every later
// batch even though the shadow itself remains continuous. Match all current
// batches to persistent canonical pages before submitting any vertices, using
// their object-relative UV clouds rather than their temporary draw ordinal.
inline ShadowCanonicalPageMap canonical_shadow_page_map(
    std::span<const ShadowCanonicalPage> previous_pages,
    std::span<const ShadowCanonicalPage> current_batches) {
    ShadowCanonicalPageMap result{};
    result.batch_for_page.fill(kInvalidShadowPage);
    result.page_for_batch.fill(kInvalidShadowPage);
    if (previous_pages.size() != kMaximumCanonicalShadowBatches ||
        current_batches.size() > kMaximumCanonicalShadowBatches) {
        return result;
    }

    std::array<bool, kMaximumCanonicalShadowBatches> page_used{};
    std::array<bool, kMaximumCanonicalShadowBatches> batch_used{};
    const auto page_valid = [](const ShadowCanonicalPage& page) {
        return std::any_of(page.begin(), page.end(),
                           [](const ShadowTexcoordSample& sample) {
                               return sample.valid;
                           });
    };
    const auto cloud_distance = [](const ShadowCanonicalPage& previous,
                                   const ShadowCanonicalPage& current) {
        std::uint64_t total = 0U;
        std::size_t matches = 0U;
        for (const auto& sample : current) {
            if (!sample.valid) continue;
            std::uint64_t nearest =
                std::numeric_limits<std::uint64_t>::max();
            for (const auto& candidate : previous) {
                if (!candidate.valid) continue;
                const std::int64_t ds =
                    static_cast<std::int64_t>(sample.s) - candidate.s;
                const std::int64_t dt =
                    static_cast<std::int64_t>(sample.t) - candidate.t;
                nearest = std::min(
                    nearest, static_cast<std::uint64_t>(ds * ds + dt * dt));
            }
            if (nearest != std::numeric_limits<std::uint64_t>::max()) {
                total += nearest;
                ++matches;
            }
        }
        return matches == 0U
            ? std::numeric_limits<std::uint64_t>::max()
            : total / matches;
    };

    // Greedy global minimum is deterministic and avoids allowing an early
    // source batch to claim a page that is a much better match for a later
    // batch. The reverse cloud term distinguishes partial-overlap ties.
    for (;;) {
        std::uint64_t best_distance =
            std::numeric_limits<std::uint64_t>::max();
        std::size_t best_page = kMaximumCanonicalShadowBatches;
        std::size_t best_batch = kMaximumCanonicalShadowBatches;
        for (std::size_t page = 0U; page < previous_pages.size(); ++page) {
            if (page_used[page] || !page_valid(previous_pages[page])) continue;
            for (std::size_t batch = 0U; batch < current_batches.size();
                 ++batch) {
                if (batch_used[batch] || !page_valid(current_batches[batch])) {
                    continue;
                }
                const std::uint64_t forward = cloud_distance(
                    previous_pages[page], current_batches[batch]);
                const std::uint64_t reverse = cloud_distance(
                    current_batches[batch], previous_pages[page]);
                const std::uint64_t distance =
                    forward == std::numeric_limits<std::uint64_t>::max() ||
                            reverse == std::numeric_limits<std::uint64_t>::max()
                        ? std::numeric_limits<std::uint64_t>::max()
                        : forward / 2U + reverse / 2U;
                if (distance < best_distance ||
                    (distance == best_distance &&
                     (page < best_page ||
                      (page == best_page && batch < best_batch)))) {
                    best_distance = distance;
                    best_page = page;
                    best_batch = batch;
                }
            }
        }
        if (best_page == kMaximumCanonicalShadowBatches) break;
        result.batch_for_page[best_page] =
            static_cast<std::uint8_t>(best_batch);
        result.page_for_batch[best_batch] =
            static_cast<std::uint8_t>(best_page);
        page_used[best_page] = true;
        batch_used[best_batch] = true;
    }

    // First-seen batches use remaining pages in UV-centroid order so the
    // initial assignment is independent of DKR's polygon traversal order.
    for (;;) {
        std::size_t next_batch = kMaximumCanonicalShadowBatches;
        std::int64_t next_s = 0;
        std::int64_t next_t = 0;
        for (std::size_t batch = 0U; batch < current_batches.size(); ++batch) {
            if (batch_used[batch]) continue;
            std::int64_t sum_s = 0;
            std::int64_t sum_t = 0;
            std::int64_t count = 0;
            for (const auto& sample : current_batches[batch]) {
                if (!sample.valid) continue;
                sum_s += sample.s;
                sum_t += sample.t;
                ++count;
            }
            if (count == 0) continue;
            const std::int64_t centroid_s = sum_s / count;
            const std::int64_t centroid_t = sum_t / count;
            if (next_batch == kMaximumCanonicalShadowBatches ||
                centroid_s < next_s ||
                (centroid_s == next_s &&
                 (centroid_t < next_t ||
                  (centroid_t == next_t && batch < next_batch)))) {
                next_batch = batch;
                next_s = centroid_s;
                next_t = centroid_t;
            }
        }
        if (next_batch == kMaximumCanonicalShadowBatches) break;
        std::size_t page = 0U;
        while (page < kMaximumCanonicalShadowBatches && page_used[page]) {
            ++page;
        }
        if (page == kMaximumCanonicalShadowBatches) break;
        result.batch_for_page[page] =
            static_cast<std::uint8_t>(next_batch);
        result.page_for_batch[next_batch] = static_cast<std::uint8_t>(page);
        page_used[page] = true;
        batch_used[next_batch] = true;
    }
    return result;
}

inline constexpr std::uint64_t kShadowTopologyHashOffset =
    1469598103934665603ULL;

constexpr std::uint64_t shadow_topology_hash_value(std::uint64_t hash,
                                                    std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        hash ^= (value >> shift) & 0xFFU;
        hash *= 1099511628211ULL;
    }
    return hash;
}

struct ShadowBatchTopology {
    // Kept in the description so the exclusion below remains explicit and
    // regression-testable. This pointer is transient draw state, not geometry.
    std::uint32_t material_address = 0U;
    std::uint32_t triangle_count = 0U;
    std::uint32_t vertex_count = 0U;
};

// A projected shadow may select another material/texture while retaining the
// exact same vertex/index topology. Material changes must not advance the
// topology epoch or RT64 loses an otherwise valid interpolation pair for one
// presentation frame. Geometry counts and triangle indices remain part of the
// signature and the ordered vertex samples are validated separately below.
constexpr std::uint64_t shadow_batch_topology_hash(
    std::uint64_t hash, const ShadowBatchTopology& batch) {
    hash = shadow_topology_hash_value(hash, batch.triangle_count);
    return shadow_topology_hash_value(hash, batch.vertex_count);
}

// Projected shadow vertices legitimately deform as the vehicle steers and as
// terrain height changes. Only an authored topology change should advance the
// interpolation epoch; scene and object-lifetime changes are handled by the
// surrounding presentation lifetime policy.
constexpr bool shadow_topology_epoch_should_advance(
    bool previous_valid, std::uint64_t previous_signature,
    std::uint64_t current_signature) {
    return previous_valid && previous_signature != current_signature;
}

// DKR rebuilds projected shadows every authored frame. Occasionally a terrain
// boundary keeps the same triangle/index topology while assigning a different
// world-space point to one of those vertex slots. RT64 blends the original raw
// slots, so correspondence must be proven in that exact ordering: aligning or
// reordering the samples here can approve a pair that the renderer cannot blend
// safely. Remove the object's X/Z translation, then allow one small coherent
// ground-plane displacement shared by the complete shadow. This accounts for
// ordinary slope projection without permitting independently moving batches,
// reordered vertices, large terrain jumps, or shape changes.
inline bool shadow_geometry_corresponds(
    std::span<const ShadowVertexSample> previous,
    std::span<const ShadowVertexSample> current,
    std::span<const std::uint16_t> batch_vertex_counts,
    float object_delta_x, float object_delta_z,
    float maximum_residual) {
    if (previous.size() != current.size() || previous.empty() ||
        !std::isfinite(object_delta_x) || !std::isfinite(object_delta_z) ||
        !std::isfinite(maximum_residual) || maximum_residual <= 0.0F) {
        return false;
    }

    const float maximum_residual_squared =
        maximum_residual * maximum_residual;
    std::size_t batch_start = 0U;
    ShadowVertexSample coherent_residual{};
    for (const std::uint16_t count_value : batch_vertex_counts) {
        const std::size_t count = count_value;
        if (count == 0U || batch_start + count > current.size()) {
            return false;
        }

        for (std::size_t local = 0U; local < count; ++local) {
            const std::size_t index = batch_start + local;
            if (!std::isfinite(previous[index].x) ||
                !std::isfinite(previous[index].y) ||
                !std::isfinite(previous[index].z) ||
                !std::isfinite(current[index].x) ||
                !std::isfinite(current[index].y) ||
                !std::isfinite(current[index].z)) {
                return false;
            }
            coherent_residual.x += current[index].x - object_delta_x -
                                   previous[index].x;
            coherent_residual.y += current[index].y - previous[index].y;
            coherent_residual.z += current[index].z - object_delta_z -
                                   previous[index].z;
        }
        batch_start += count;
    }
    if (batch_start != current.size()) {
        return false;
    }

    const float reciprocal_count = 1.0F / static_cast<float>(current.size());
    coherent_residual.x *= reciprocal_count;
    coherent_residual.y *= reciprocal_count;
    coherent_residual.z *= reciprocal_count;
    const float coherent_distance =
        coherent_residual.x * coherent_residual.x +
        coherent_residual.y * coherent_residual.y +
        coherent_residual.z * coherent_residual.z;
    if (!std::isfinite(coherent_distance) ||
        coherent_distance > maximum_residual_squared) {
        return false;
    }

    for (std::size_t index = 0U; index < current.size(); ++index) {
        const float dx = current[index].x - object_delta_x -
                         previous[index].x - coherent_residual.x;
        const float dy = current[index].y - previous[index].y -
                         coherent_residual.y;
        const float dz = current[index].z - object_delta_z -
                         previous[index].z - coherent_residual.z;
        const float ordered_distance = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(ordered_distance) ||
            ordered_distance > maximum_residual_squared) {
            return false;
        }
    }
    return true;
}

struct MatrixInterpolation {
    std::uint32_t identity = kIgnoredIdentity;
    bool interpolate_vertices = false;
    bool interpolate_texcoords = false;
    bool interpolate_tiles = false;
    bool procedural_water = false;
    std::uint8_t water_scroll_tag = 0U;
};

MatrixInterpolation matrix_interpolation(
    std::uint32_t physical_matrix_address);
std::uint32_t matrix_identity(std::uint32_t physical_matrix_address);
PresentationKey surface_presentation_key(std::uint32_t batch_address);
std::uint16_t presentation_token_for_object(std::uint8_t* rdram,
                                            std::uint32_t object_address);
std::uint16_t presentation_token_for_registered_object(
    std::uint8_t* rdram, std::uint32_t object_address);
std::uint16_t presentation_token_for_active_capture();

struct VehiclePartPresentationKey {
    std::uint16_t attachment_token = 0U;
    std::uint8_t attachment_slot = kInvalidVehiclePartSlot;
};

VehiclePartPresentationKey active_vehicle_part_presentation_key(
    std::uint32_t attachment_matrix_address);

std::uint32_t register_active_vehicle_part_matrix(
    std::uint32_t attachment_transform_address,
    std::uint32_t attachment_matrix_address,
    bool mirrored);

struct ShadowPresentationKey {
    std::uint16_t token = 0U;
    std::uint8_t batch_count = 0U;
};

struct ShadowOwnerMotionSample {
    ShadowVertexSample position{};
    std::int16_t yaw = 0;
    bool valid = false;
};

ShadowPresentationKey shadow_presentation_key(
    std::uint8_t* rdram, std::uint32_t object_address,
    std::uint32_t shadow_address);

bool shadow_owner_uses_rigid_actor_proxy(std::uint64_t shadow_history_key);
bool shadow_owner_is_taj_carpet_actor(std::uint64_t shadow_history_key);
RigidShadowOwnerPolicy shadow_owner_rigid_policy(
    std::uint64_t shadow_history_key);
ShadowOwnerMotionSample shadow_owner_motion_sample(
    std::uint64_t shadow_history_key);

struct PresentationMarker {
    std::uint8_t mode = 0U;
    std::uint16_t token = 0U;
    std::uint8_t variant = 0U;
    PresentationMarkerKind kind = PresentationMarkerKind::Geometry;
    hud::groups::Transform hud_transform{};
};

inline constexpr std::size_t kMaximumMarkersPerCommand = 8U;

struct PresentationMarkerList {
    std::array<PresentationMarker, kMaximumMarkersPerCommand> markers{};
    std::size_t count = 0U;
};
struct LocatedPresentationMarker {std::uint32_t address;PresentationMarker marker;};
struct OwnedMatrixBinding {std::uint32_t address;MatrixInterpolation interpolation;bool weak=false;};
struct OwnedShadowBinding {std::uint16_t token;RigidShadowOwnerPolicy policy;ShadowOwnerMotionSample motion;};

// DKR flips gSPTaskNum immediately after gfxtask_run_xbus returns. A renderer
// snapshot may therefore contain the parity of the next display-list buffer,
// not the task being decoded. The frozen task's scene and data_ptr are the
// durable ownership key; comparing the mutable parity drops valid sidecars.
constexpr bool submitted_task_matches(std::uint32_t submitted_scene,
                                      std::uint32_t submitted_display_list,
                                      std::uint32_t rendered_scene,
                                      std::uint32_t rendered_display_list) {
    constexpr std::uint32_t kRdramAddressMask = 0x007FFFFFU;
    return submitted_scene == rendered_scene &&
        (submitted_display_list & kRdramAddressMask) ==
            (rendered_display_list & kRdramAddressMask);
}

// Presentation scopes are host metadata, not authored F3DDKR commands. Keep
// them in the immutable task sidecar so patches never consume or overwrite a
// slot in DKR's fixed-size display-list heaps.
bool record_presentation_marker(std::uint32_t command_address,
                                std::uint8_t mode,
                                std::uint16_t token,
                                std::uint8_t variant,
                                PresentationMarkerKind kind,
                                hud::groups::Transform hud_transform = {});
PresentationMarkerList active_presentation_markers(
    std::uint32_t command_address);

// Activates the immutable identity sidecar captured alongside one submitted
// graphics task. The renderer owns this scope for the entire F3DDKR decode so
// a delayed task can never observe identities from a newer frame that reused
// the same N64 display-list buffer.
class TaskIdentityScope {
public:
    TaskIdentityScope(std::uint8_t* rdram_snapshot,
                      std::uint32_t display_list_address);
    // Private copied-image decode only. Does not consume the stable producer's
    // submission queue or install metadata from rewindable/live CPU globals.
    explicit TaskIdentityScope(std::span<const LocatedPresentationMarker> owned_markers);
    TaskIdentityScope(std::span<const LocatedPresentationMarker> owned_markers,
        std::span<const OwnedMatrixBinding> matrices,std::span<const OwnedShadowBinding> shadows,
        std::uint32_t scene,bool interpolation_allowed);
    ~TaskIdentityScope();

    TaskIdentityScope(const TaskIdentityScope&) = delete;
    TaskIdentityScope& operator=(const TaskIdentityScope&) = delete;
};

// True only while the renderer is decoding a submitted task whose authored
// frame is safe to pair with its predecessor. Post-race spectator cameras use
// abrupt cuts and can rebuild display-list topology between consecutive
// frames, so those tasks deliberately stay at DKR's authored cadence.
bool task_interpolation_allowed();
bool task_is_owned();

// Full scene generation captured with the graphics task currently being
// decoded. Zero means no immutable presentation sidecar matched the task.
std::uint32_t task_scene_generation();

// Recording-thread continuity state for bounded visibility hysteresis. These
// values are host metadata only and are never serialized into game state.
std::uint64_t authored_frame_sequence();
std::uint32_t recording_scene_generation();
std::uint32_t current_camera_continuity_identity();

// Dormant, environment-gated interpolation diagnostics. These probes never
// alter authored state or RT64 group selection; they only expose which
// continuity boundary failed when DKR_INTERPOLATION_TRACE is enabled.
bool interpolation_trace_enabled();
void interpolation_trace_segment_region(std::uint8_t* rdram,
                                        std::uint32_t segment_id,
                                        int race_type,
                                        bool authored_region_visible,
                                        bool effective_region_visible,
                                        bool retention_active);
void interpolation_trace_segment_block(std::uint8_t* rdram,
                                       std::uint32_t segment_id,
                                       bool block_visible);
void interpolation_trace_billboard(bool has_owner_token, bool has_sprite,
                                   bool marker_recorded);

} // namespace dkr::runtime::presentation
