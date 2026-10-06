#include "presentation_identity.hpp"
#include "retained_metadata_table.hpp"
#include "netplay/experimental_performance.hpp"
#include "custom_music.hpp"
#include "revision_addresses.hpp"
#include "water_scroll_policy.hpp"

#include "recomp.h"
#include "finish_presentation_policy.hpp"

#include "runtime_enhancements.hpp"
#include "runtime_hud_layout.hpp"
#include "runtime_netplay.hpp"
#include "vehicle_context_policy.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stdexcept>

namespace {

constexpr std::uint32_t kRdramMask = 0x007FFFFFU;
constexpr std::uint32_t kObjectBehaviourOffset = 0x48U;
constexpr std::uint32_t kObjectIdOffset = 0x4AU;
const std::uint32_t& kObjectCurrentMatrixAddress =
    dkr::runtime::revision_addresses::ObjectCurrentMatrix;
const std::uint32_t& kSpTaskNumberAddress =
    dkr::runtime::revision_addresses::SpTaskNumber;
const std::uint32_t& kCamerasAddress =
    dkr::runtime::revision_addresses::Cameras;
constexpr std::uint32_t kCameraSize = 0x44U;
const std::uint32_t& kActiveCameraIdAddress =
    dkr::runtime::revision_addresses::ActiveCameraId;
const std::uint32_t& kCurrentCameraFovAddress =
    dkr::runtime::revision_addresses::CurrentCameraFov;
const std::uint32_t& kCutsceneCameraActiveAddress =
    dkr::runtime::revision_addresses::CutsceneCameraActive;
constexpr std::uint32_t kCameraModeOffset = 0x36U;
const std::uint32_t& kWaveControllerAddress =
    dkr::runtime::revision_addresses::WaveController;
constexpr std::uint32_t kWaveSubdivisionsOffset = 0x00U;
constexpr std::uint32_t kWaveDoubleDensityOffset = 0x28U;
const std::uint32_t& kShadowHeapFlipAddress =
    dkr::runtime::revision_addresses::ShadowHeapFlip;
const std::uint32_t& kShadowHeapTrianglesAddress =
    dkr::runtime::revision_addresses::ShadowHeapTriangles;
const std::uint32_t& kShadowHeapVerticesAddress =
    dkr::runtime::revision_addresses::ShadowHeapVertices;
const std::uint32_t& kShadowHeapDataAddress =
    dkr::runtime::revision_addresses::ShadowHeapData;
constexpr std::uint32_t kObjectHeaderOffset = 0x40U;
constexpr std::uint32_t kObjectHeaderShadowGroupOffset = 0x32U;
constexpr std::uint32_t kShadowTextureOffset = 0x04U;
constexpr std::uint32_t kTextureWidthOffset = 0x00U;
constexpr std::uint32_t kShadowMeshStartOffset = 0x08U;
constexpr std::uint32_t kShadowMeshEndOffset = 0x0AU;
constexpr std::uint32_t kShadowHeapPropertySize = 0x08U;
constexpr std::uint32_t kTriangleSize = 0x10U;
constexpr std::uint32_t kVertexSize = 0x0AU;
constexpr std::int32_t kMaximumShadowBatches = 400;
constexpr std::int32_t kMaximumShadowTriangles = 800;
constexpr std::int32_t kMaximumShadowVertices = 2000;
constexpr std::size_t kMaximumObjectNesting = 16U;
constexpr std::size_t kMaximumPendingFrames = 16U;

struct ShadowGeometrySnapshot {
    std::vector<dkr::runtime::presentation::ShadowVertexSample> vertices;
    std::vector<std::uint16_t> batch_vertex_counts;
    float object_x = 0.0F;
    float object_y = 0.0F;
    float object_z = 0.0F;
    float footprint = 0.0F;
    std::int16_t object_yaw = 0;
};

struct Lifetime {
    std::uint32_t generation = 0;
    std::uint16_t presentation_token = 0;
    std::uint64_t shadow_topology_signature = 0;
    std::uint8_t shadow_topology_epoch = 0;
    std::array<std::uint16_t, 32> vehicle_part_tokens{};
    ShadowGeometrySnapshot shadow_geometry{};
    bool shadow_topology_valid = false;
    bool alive = false;
};

struct ObjectCapture {
    std::uint32_t object = 0;
    std::uint32_t identity = 0;
    std::uint32_t camera_identity = 0;
    std::uint32_t first_matrix = 0;
    std::uint32_t buffer = 0;
    std::uint16_t presentation_token = 0;
    dkr::runtime::presentation::WorldProjection world_projection{};
};

struct ObjectOwner {
    std::uint32_t scene = 0;
    std::uint32_t address = 0;
    std::uint32_t lifetime = 0;

    bool operator==(const ObjectOwner&) const = default;
};

struct MatrixBinding {
    std::uint32_t matrix_identity = 0;
    std::uint32_t object_identity = 0;
    bool interpolate_vertices = false;
    bool interpolate_texcoords = false;
    bool interpolate_tiles = false;
    bool procedural_water = false;
    std::uint8_t water_scroll_tag = 0U;
    dkr::runtime::presentation::WorldProjection world_projection{};
};

struct SubmittedFrame {
    std::uint32_t display_list_address = 0;
    std::uint32_t task_number = 0;
    std::uint32_t scene_generation = 0;
    std::uint64_t submission_sequence = 0;
    std::unordered_map<std::uint32_t, MatrixBinding> matrices;
    std::unordered_map<std::uint32_t,
        std::vector<dkr::runtime::presentation::PresentationMarker>> markers;
    std::unordered_map<std::uint64_t,
        dkr::runtime::presentation::ShadowOwnerMotionSample>
        shadow_owner_motion;
    bool interpolation_allowed = false;
};

struct CameraContinuityState {
    dkr::runtime::presentation::CameraContinuitySample sample{};
    std::uint32_t scene = 0U;
    std::uint32_t task = 0U;
    std::uint32_t epoch = 1U;
    bool valid = false;
};

std::mutex g_identity_mutex;
std::unordered_map<std::uint32_t, Lifetime> g_lifetimes;
std::unordered_map<std::uint32_t, ObjectOwner> g_identity_owners;
std::unordered_set<std::uint32_t> g_collided_identities;
std::unordered_set<std::uint64_t> g_rigid_actor_shadow_keys;
std::unordered_set<std::uint64_t> g_taj_carpet_shadow_keys;
std::unordered_map<std::uint64_t,
                   dkr::runtime::presentation::RigidShadowOwnerPolicy>
    g_rigid_shadow_owner_policies;
std::array<std::unordered_map<std::uint32_t, MatrixBinding>, 2> g_matrix_maps;
std::array<std::unordered_map<std::uint32_t,
    std::vector<dkr::runtime::presentation::PresentationMarker>>, 2>
    g_marker_maps;
std::array<dkr::runtime::presentation::MarkerRecordingIntegrity, 2>
    g_marker_integrity{};
dkr::runtime::presentation::PresentationDiagnosticBudget g_marker_rejections{};
std::array<std::unordered_map<std::uint64_t,
    dkr::runtime::presentation::ShadowOwnerMotionSample>, 2>
    g_shadow_owner_motion_maps;
std::deque<SubmittedFrame> g_submitted_frames;
bool g_submission_overflowed = false;
std::atomic<std::uint32_t> g_scene_generation{1U};
std::atomic<std::uint64_t> g_authored_frame_sequence{0U};
std::atomic<std::uint64_t> g_next_submission_sequence{1U};
std::atomic<std::uint32_t> g_next_lifetime{1U};
std::uint32_t g_next_presentation_token = 1U;
std::atomic<std::uint64_t> g_identity_collisions{0U};
std::atomic<std::uint64_t> g_matrix_ranges{0U};
std::array<CameraContinuityState, 8> g_camera_continuity{};
std::array<dkr::runtime::presentation::FinishCameraShot, 8> g_finish_camera_shots{};
thread_local std::array<ObjectCapture, kMaximumObjectNesting> g_capture_stack{};
thread_local std::size_t g_capture_depth = 0U;
thread_local std::size_t g_capture_overflow_depth = 0U;
thread_local std::uint32_t g_recording_buffer = 0U;
thread_local std::uint32_t g_current_camera_identity = 0U;
thread_local bool g_recording_interpolation_allowed = false;
thread_local bool g_active_task_interpolation_allowed = false;
thread_local bool g_active_owned_task = false;
thread_local dkr::runtime::presentation::RetainedMetadataTable<std::uint64_t,
    dkr::runtime::presentation::RigidShadowOwnerPolicy> g_active_owned_shadow_policies;
thread_local dkr::runtime::presentation::RetainedMetadataTable<std::uint32_t,
    MatrixBinding> g_active_owned_matrices;
thread_local dkr::runtime::presentation::RetainedMetadataTable<std::uint32_t,
    dkr::runtime::presentation::PresentationMarkerList> g_active_owned_markers;
thread_local dkr::runtime::presentation::RetainedMetadataTable<std::uint64_t,
    dkr::runtime::presentation::ShadowOwnerMotionSample> g_active_owned_shadow_motion;
thread_local std::uint32_t g_active_task_scene_generation = 0U;
thread_local std::unordered_map<std::uint32_t, MatrixBinding>
    g_active_matrix_map;
thread_local std::unordered_map<std::uint32_t,
    std::vector<dkr::runtime::presentation::PresentationMarker>>
    g_active_marker_map;
thread_local std::unordered_map<std::uint64_t,
    dkr::runtime::presentation::ShadowOwnerMotionSample>
    g_active_shadow_owner_motion;
thread_local bool g_wave_capture_active = false;
thread_local std::uint32_t g_wave_capture_viewport = 0U;
thread_local std::uint32_t g_wave_block_address = 0U;
thread_local bool g_wave_block_valid = false;
thread_local std::uint8_t g_wave_selection_pattern = 0U;
thread_local bool g_wave_selection_valid = false;

struct InterpolationTraceCounters {
    std::atomic<std::uint64_t> camera_roots{0U};
    std::atomic<std::uint64_t> camera_root_failures{0U};
    std::atomic<std::uint64_t> matrix_lookups{0U};
    std::atomic<std::uint64_t> matrix_misses{0U};
    std::atomic<std::uint64_t> object_ranges{0U};
    std::atomic<std::uint64_t> object_matrices{0U};
    std::atomic<std::uint64_t> billboard_markers{0U};
    std::atomic<std::uint64_t> billboard_owner_misses{0U};
    std::atomic<std::uint64_t> billboard_sprite_misses{0U};
    std::atomic<std::uint64_t> billboard_marker_failures{0U};
    std::atomic<std::uint64_t> sidecar_matches{0U};
    std::atomic<std::uint64_t> sidecar_mismatches{0U};
    std::atomic<std::uint64_t> sidecar_overflows{0U};
    std::atomic<std::uint64_t> segment_region_checks{0U};
    std::atomic<std::uint64_t> segment_region_relaxed{0U};
    std::atomic<std::uint64_t> segment_block_checks{0U};
    std::atomic<std::uint64_t> segment_block_rejects{0U};
    std::atomic<std::uint64_t> segment_visibility_drops{0U};
};

InterpolationTraceCounters g_interpolation_trace{};
std::atomic<std::uint32_t> g_interpolation_trace_events{0U};
std::mutex g_interpolation_trace_mutex;
struct SegmentTraceState {
    int race_type = -1;
    bool authored_region_visible = false;
    bool effective_region_visible = false;
    bool retention_active = false;
    bool block_visible = false;
    bool has_block_sample = false;
};
std::unordered_map<std::uint64_t, SegmentTraceState>
    g_interpolation_segment_visibility;

bool InterpolationTraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("DKR_INTERPOLATION_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

template <typename Counter>
std::uint64_t TraceTake(Counter& counter) {
    return counter.exchange(0U, std::memory_order_relaxed);
}

void PrintInterpolationTraceSummary(std::uint64_t frame) {
    if (!InterpolationTraceEnabled() || frame <= 1U ||
        ((frame - 1U) % 120U) != 0U) {
        return;
    }
    std::fprintf(stderr,
        "[trace][interpolation][summary] frames=%llu..%llu "
        "roots=%llu root-fail=%llu matrix-lookups=%llu matrix-miss=%llu "
        "object-ranges=%llu object-matrices=%llu billboard=%llu "
        "billboard-owner-miss=%llu billboard-sprite-miss=%llu "
        "billboard-marker-fail=%llu sidecar-match=%llu "
        "sidecar-mismatch=%llu sidecar-overflow=%llu "
        "segment-region=%llu segment-relaxed=%llu segment-block=%llu "
        "segment-reject=%llu segment-drops=%llu\n",
        static_cast<unsigned long long>(frame - 120U),
        static_cast<unsigned long long>(frame - 1U),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.camera_roots)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.camera_root_failures)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.matrix_lookups)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.matrix_misses)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.object_ranges)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.object_matrices)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.billboard_markers)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.billboard_owner_misses)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.billboard_sprite_misses)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.billboard_marker_failures)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.sidecar_matches)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.sidecar_mismatches)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.sidecar_overflows)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.segment_region_checks)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.segment_region_relaxed)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.segment_block_checks)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.segment_block_rejects)),
        static_cast<unsigned long long>(TraceTake(g_interpolation_trace.segment_visibility_drops)));
    g_interpolation_trace_events.store(0U, std::memory_order_relaxed);
}

void TraceCameraRootFailure(const char* reason, std::uint32_t reference,
                            std::uint32_t address) {
    if (!InterpolationTraceEnabled()) return;
    g_interpolation_trace.camera_root_failures.fetch_add(
        1U, std::memory_order_relaxed);
    if (g_interpolation_trace_events.fetch_add(
            1U, std::memory_order_relaxed) < 32U) {
        std::fprintf(stderr,
            "[trace][interpolation][camera-root-fail] reason=%s "
            "reference=0x%08X matrix=0x%08X frame=%llu\n",
            reason, reference, address,
            static_cast<unsigned long long>(
                g_authored_frame_sequence.load(std::memory_order_relaxed)));
    }
}

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

bool ValidObjectAddress(std::uint32_t address) {
    return address >= 0x80000000U && address <= 0x807FFF00U;
}

std::uint32_t Physical(std::uint32_t address) {
    return address & kRdramMask;
}

std::uint32_t ReadU32(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::uint32_t>(MEM_W(0, RdramAddress(address)));
}

std::uint8_t ReadU8(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::uint8_t>(MEM_B(0, RdramAddress(address)));
}

std::uint16_t ReadU16(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::uint16_t>(MEM_H(0, RdramAddress(address)));
}

std::int16_t ReadS16(std::uint8_t* rdram, std::uint32_t address) {
    return static_cast<std::int16_t>(MEM_H(0, RdramAddress(address)));
}

float ReadF32(std::uint8_t* rdram, std::uint32_t address) {
    return std::bit_cast<float>(ReadU32(rdram, address));
}

bool ValidRange(std::uint32_t address, std::uint32_t size) {
    const std::uint32_t physical = Physical(address);
    return address >= 0x80000000U && address <= 0x807FFFFFU &&
        physical <= kRdramMask && size <= kRdramMask + 1U - physical;
}

dkr::runtime::presentation::WorldProjection ReadWorldProjection(std::uint8_t* rdram) {
    using namespace dkr::runtime::revision_addresses;
    const std::uint32_t header = ReadU32(rdram, CurrentLevelHeader);
    if (!ValidRange(header, 0xA0U)) return {};
    const std::uint32_t layout = ReadU32(rdram, ViewportLayout);
    const float wx = ReadF32(rdram, ViewProjectionMatrix + 0x0CU);
    const float wy = ReadF32(rdram, ViewProjectionMatrix + 0x1CU);
    const float wz = ReadF32(rdram, ViewProjectionMatrix + 0x2CU);
    const bool perspective = std::isfinite(wx) && std::isfinite(wy) &&
        std::isfinite(wz) && (wx * wx + wy * wy + wz * wz) > 0.01F;
    if (!dkr_world_projection_eligible(static_cast<std::int32_t>(ReadU32(rdram, GameMode)),
        ReadU8(rdram, header + 0x4CU), layout, perspective)) return {};
    const std::uint32_t authored = ReadU8(rdram, header + 0x9CU);
    const std::uint32_t effective = ReadU32(rdram, CurrentCameraFov);
    if (!authored || !dkr_world_projection_metadata_valid(authored, effective, layout)) return {};
    return {static_cast<float>(authored), std::bit_cast<float>(effective),
        static_cast<std::uint8_t>(layout)};
}

bool ActiveLogicalCamera(std::uint8_t* rdram, std::uint32_t& camera_id) {
    const std::uint32_t raw_camera_id =
        ReadU32(rdram, kActiveCameraIdAddress);
    const bool cutscene_camera =
        ReadU8(rdram, kCutsceneCameraActiveAddress) != 0U;
    if (raw_camera_id >= g_camera_continuity.size() ||
        (cutscene_camera && raw_camera_id >= 4U)) {
        return false;
    }
    camera_id = raw_camera_id + (cutscene_camera ? 4U : 0U);
    return camera_id < g_camera_continuity.size();
}

bool ShadowTopologySignature(std::uint8_t* rdram,
                             std::uint32_t object,
                             std::uint32_t shadow,
                             std::uint64_t& signature,
                             ShadowGeometrySnapshot& geometry) {
    if (!ValidObjectAddress(object) || !ValidRange(shadow, 0x10U)) {
        return false;
    }

    const std::int32_t mesh_start = ReadS16(
        rdram, shadow + kShadowMeshStartOffset);
    const std::int32_t mesh_end = ReadS16(
        rdram, shadow + kShadowMeshEndOffset);
    if (mesh_start < 0 || mesh_end < mesh_start ||
        mesh_end > kMaximumShadowBatches) {
        return false;
    }

    const std::uint32_t header = ReadU32(
        rdram, object + kObjectHeaderOffset);
    if (!ValidRange(header + kObjectHeaderShadowGroupOffset, 2U)) {
        return false;
    }
    const std::int16_t shadow_group = ReadS16(
        rdram, header + kObjectHeaderShadowGroupOffset);
    std::int32_t heap_index = static_cast<std::int32_t>(ReadU32(
        rdram, kShadowHeapFlipAddress));
    if (shadow_group == 1) {
        heap_index += 2;
    }
    if (heap_index < 0 || heap_index >= 4) {
        return false;
    }

    const std::uint32_t heap_data = ReadU32(
        rdram, kShadowHeapDataAddress +
            static_cast<std::uint32_t>(heap_index) * 4U);
    const std::uint32_t heap_triangles = ReadU32(
        rdram, kShadowHeapTrianglesAddress +
            static_cast<std::uint32_t>(heap_index) * 4U);
    const std::uint32_t heap_vertices = ReadU32(
        rdram, kShadowHeapVerticesAddress +
            static_cast<std::uint32_t>(heap_index) * 4U);
    const std::uint32_t property_bytes =
        (static_cast<std::uint32_t>(mesh_end) + 1U) *
        kShadowHeapPropertySize;
    if (!ValidRange(heap_data, property_bytes) ||
        !ValidRange(heap_triangles,
                    kMaximumShadowTriangles * kTriangleSize) ||
        !ValidRange(heap_vertices,
                    kMaximumShadowVertices * kVertexSize)) {
        return false;
    }

    geometry = {};
    geometry.object_x = ReadF32(rdram, object + 0x0CU);
    geometry.object_y = ReadF32(rdram, object + 0x10U);
    geometry.object_z = ReadF32(rdram, object + 0x14U);
    geometry.footprint = std::abs(ReadF32(rdram, shadow)) * 10.0F;
    geometry.object_yaw = ReadS16(rdram, object + 0x02U);
    if (!std::isfinite(geometry.object_x) ||
        !std::isfinite(geometry.object_y) ||
        !std::isfinite(geometry.object_z) ||
        !std::isfinite(geometry.footprint)) {
        return false;
    }

    std::uint64_t hash =
        dkr::runtime::presentation::kShadowTopologyHashOffset;
    hash = dkr::runtime::presentation::shadow_topology_hash_value(
        hash, static_cast<std::uint32_t>(mesh_end - mesh_start));
    for (std::int32_t batch = mesh_start; batch < mesh_end; ++batch) {
        const std::uint32_t current = heap_data +
            static_cast<std::uint32_t>(batch) * kShadowHeapPropertySize;
        const std::uint32_t next = current + kShadowHeapPropertySize;
        const std::int32_t tri_start = ReadS16(rdram, current + 4U);
        const std::int32_t vert_start = ReadS16(rdram, current + 6U);
        const std::int32_t tri_end = ReadS16(rdram, next + 4U);
        const std::int32_t vert_end = ReadS16(rdram, next + 6U);
        if (tri_start < 0 || tri_end < tri_start ||
            tri_end > kMaximumShadowTriangles || vert_start < 0 ||
            vert_end < vert_start || vert_end > kMaximumShadowVertices) {
            return false;
        }

        // Absolute offsets depend on shadows generated earlier in this heap.
        // Hash only this object's geometry shape and triangle index topology.
        // The material pointer is transient draw state, while moving
        // coordinates and UV animation are validated through correspondence.
        hash = dkr::runtime::presentation::shadow_batch_topology_hash(hash, {
            ReadU32(rdram, current),
            static_cast<std::uint32_t>(tri_end - tri_start),
            static_cast<std::uint32_t>(vert_end - vert_start),
        });
        const std::int32_t vertex_count = vert_end - vert_start;
        geometry.batch_vertex_counts.push_back(
            static_cast<std::uint16_t>(vertex_count));
        geometry.vertices.reserve(
            geometry.vertices.size() + static_cast<std::size_t>(vertex_count));
        for (std::int32_t vertex = vert_start; vertex < vert_end; ++vertex) {
            const std::uint32_t address = heap_vertices +
                static_cast<std::uint32_t>(vertex) * kVertexSize;
            geometry.vertices.push_back({
                static_cast<float>(ReadS16(rdram, address)),
                static_cast<float>(ReadS16(rdram, address + 2U)),
                static_cast<float>(ReadS16(rdram, address + 4U)),
            });
        }
        for (std::int32_t tri = tri_start; tri < tri_end; ++tri) {
            hash = dkr::runtime::presentation::shadow_topology_hash_value(
                hash, ReadU32(rdram, heap_triangles +
                    static_cast<std::uint32_t>(tri) * kTriangleSize));
        }
    }
    signature = hash;
    return true;
}

std::uint32_t NextLifetimeGeneration() {
    for (;;) {
        const std::uint32_t generation =
            g_next_lifetime.fetch_add(1U, std::memory_order_relaxed);
        if (generation != 0U) {
            return generation;
        }
    }
}

std::uint32_t EnsureLifetimeLocked(std::uint32_t object) {
    Lifetime& lifetime = g_lifetimes[object];
    if (!lifetime.alive || lifetime.generation == 0U) {
        lifetime = {};
        lifetime.generation = NextLifetimeGeneration();
        lifetime.presentation_token = g_next_presentation_token <= 0xFFFFU
            ? static_cast<std::uint16_t>(g_next_presentation_token++)
            : 0U;
        lifetime.alive = true;
    }
    return lifetime.generation;
}

std::uint32_t ObjectIdentityLocked(std::uint8_t* rdram,
                                   std::uint32_t object) {
    const std::uint32_t generation = EnsureLifetimeLocked(object);
    const std::uint16_t object_id = ReadU16(rdram, object + kObjectIdOffset);
    const std::uint16_t behaviour_id =
        ReadU16(rdram, object + kObjectBehaviourOffset);
    const std::uint32_t scene =
        g_scene_generation.load(std::memory_order_relaxed);
    const std::uint32_t identity =
        dkr::runtime::presentation::make_object_identity(
            scene, Physical(object), generation, object_id, behaviour_id);
    if (g_collided_identities.contains(identity)) {
        return dkr::runtime::presentation::kIgnoredIdentity;
    }
    const ObjectOwner owner{scene, Physical(object), generation};
    const auto [it, inserted] = g_identity_owners.emplace(identity, owner);
    if (!inserted && it->second != owner) {
        g_identity_collisions.fetch_add(1U, std::memory_order_relaxed);
        g_collided_identities.insert(identity);
        // Disable both owners of a collision, including bindings authored by
        // the first owner earlier in this frame.
        for (auto& map : g_matrix_maps) {
            std::erase_if(map, [identity](const auto& item) {
                return item.second.object_identity == identity;
            });
        }
        return dkr::runtime::presentation::kIgnoredIdentity;
    }
    return identity;
}

void NoteSpawn(std::uint32_t object) {
    if (!ValidObjectAddress(object)) {
        return;
    }
    std::scoped_lock lock(g_identity_mutex);
    Lifetime& lifetime = g_lifetimes[object];
    lifetime = {};
    lifetime.generation = NextLifetimeGeneration();
    lifetime.presentation_token = g_next_presentation_token <= 0xFFFFU
        ? static_cast<std::uint16_t>(g_next_presentation_token++)
        : 0U;
    lifetime.alive = true;
}

void NoteFree(std::uint32_t object) {
    if (!ValidObjectAddress(object)) {
        return;
    }
    std::scoped_lock lock(g_identity_mutex);
    const auto it = g_lifetimes.find(object);
    if (it != g_lifetimes.end()) {
        it->second.alive = false;
    }
}

void RegisterCameraMatrix(std::uint8_t* rdram,
                          std::uint32_t matrix_reference,
                          std::uint8_t matrix_role) {
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    if (!ValidRange(matrix_reference, 4U)) {
        TraceCameraRootFailure("invalid-reference", matrix_reference, 0U);
        return;
    }

    const std::uint32_t matrix_address = ReadU32(rdram, matrix_reference);
    if (!ValidRange(matrix_address, 64U)) {
        TraceCameraRootFailure(
            "invalid-matrix", matrix_reference, matrix_address);
        return;
    }

    std::scoped_lock lock(g_identity_mutex);
    const std::uint32_t scene =
        g_scene_generation.load(std::memory_order_relaxed);
    std::uint32_t camera_id = 0U;
    if (!ActiveLogicalCamera(rdram, camera_id)) {
        TraceCameraRootFailure(
            "invalid-logical-camera", matrix_reference, matrix_address);
        g_matrix_maps[g_recording_buffer & 1U].insert_or_assign(
            Physical(matrix_address), MatrixBinding{});
        g_current_camera_identity =
            dkr::runtime::presentation::kIgnoredIdentity;
        return;
    }
    const std::uint32_t camera =
        kCamerasAddress + camera_id * kCameraSize;
    if (!ValidRange(camera, kCameraSize)) {
        TraceCameraRootFailure(
            "invalid-camera-state", matrix_reference, matrix_address);
        g_matrix_maps[g_recording_buffer & 1U].insert_or_assign(
            Physical(matrix_address), MatrixBinding{});
        g_current_camera_identity =
            dkr::runtime::presentation::kIgnoredIdentity;
        return;
    }

    const std::uint16_t combined_pitch = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(ReadS16(rdram, camera + 0x02U)) +
        static_cast<std::uint16_t>(ReadS16(rdram, camera + 0x38U)));
    const dkr::runtime::presentation::CameraContinuitySample sample{
        ReadF32(rdram, camera + 0x0CU),
        ReadF32(rdram, camera + 0x10U),
        ReadF32(rdram, camera + 0x14U),
        ReadF32(rdram, kCurrentCameraFovAddress),
        ReadS16(rdram, camera + 0x00U),
        static_cast<std::int16_t>(combined_pitch),
        ReadS16(rdram, camera + 0x04U),
    };
    const std::uint32_t task = ReadU32(rdram, kSpTaskNumberAddress);
    CameraContinuityState& continuity = g_camera_continuity[camera_id];
    const bool forced_discontinuity =
        g_finish_camera_shots[camera_id].sample(
            ReadS16(rdram, camera + kCameraModeOffset));
    if (!continuity.valid || continuity.scene != scene) {
        continuity = CameraContinuityState{sample, scene, task, 1U, true};
        if (forced_discontinuity) {
            ++continuity.epoch;
        }
    } else {
        // gSpTaskNumber counts submitted graphics tasks, not authored game
        // frames. DKR may emit more than one task for a frame, so a numeric
        // gap is not a camera discontinuity. Only the camera transform itself
        // is allowed to start a new interpolation epoch.
        const bool new_task = continuity.task != task;
        if (forced_discontinuity ||
            (new_task &&
             dkr::runtime::presentation::camera_sample_discontinuous(
                 continuity.sample, sample))) {
            ++continuity.epoch;
            if (continuity.epoch == 0U) {
                continuity.epoch = 1U;
            }
        }
        if (new_task || forced_discontinuity) {
            continuity.sample = sample;
            continuity.task = task;
        }
    }

    g_current_camera_identity =
        dkr::runtime::presentation::make_camera_continuity_identity(
            scene, camera_id, continuity.epoch);

    const std::uint32_t identity =
        dkr::runtime::presentation::make_camera_matrix_identity(
            scene, camera_id, matrix_role, continuity.epoch);
    g_matrix_maps[g_recording_buffer & 1U].insert_or_assign(
        Physical(matrix_address), MatrixBinding{identity, 0U, false, false,
            false, false, 0U, matrix_role == 1U ? ReadWorldProjection(rdram) :
                dkr::runtime::presentation::WorldProjection{}});
    if (InterpolationTraceEnabled()) {
        g_interpolation_trace.camera_roots.fetch_add(
            1U, std::memory_order_relaxed);
    }
}

} // namespace

dkr::runtime::presentation::MatrixInterpolation
dkr::runtime::presentation::matrix_interpolation(
    std::uint32_t physical_matrix_address) {
    const std::uint32_t address = physical_matrix_address & kRdramMask;
    const MatrixBinding* binding = nullptr;
    if (g_active_owned_task) binding = g_active_owned_matrices.find(address);
    else {
        const auto it = g_active_matrix_map.find(address);
        if (it != g_active_matrix_map.end()) binding = &it->second;
    }
    if (InterpolationTraceEnabled()) {
        g_interpolation_trace.matrix_lookups.fetch_add(
            1U, std::memory_order_relaxed);
        if (binding == nullptr) {
            g_interpolation_trace.matrix_misses.fetch_add(
                1U, std::memory_order_relaxed);
        }
    }
    if (binding != nullptr) {
        return {
            binding->matrix_identity,
            binding->interpolate_vertices,
            binding->interpolate_texcoords,
            binding->interpolate_tiles,
            binding->procedural_water,
            binding->water_scroll_tag,
            binding->world_projection,
        };
    }
    return {};
}

std::uint32_t dkr::runtime::presentation::matrix_identity(
    std::uint32_t physical_matrix_address) {
    return matrix_interpolation(physical_matrix_address).identity;
}

dkr::runtime::presentation::PresentationKey
dkr::runtime::presentation::surface_presentation_key(
    std::uint32_t batch_address) {
    if (!ValidRange(batch_address, 0x0CU)) return {};
    std::uint32_t packed = normalise_identity(
        0x53555246U ^ Physical(batch_address) ^
        (g_scene_generation.load(std::memory_order_relaxed) * 0x9E3779B9U));
    packed &= 0x001FFFFFU;
    std::uint16_t token = static_cast<std::uint16_t>(packed & 0xFFFFU);
    if (token == 0U) token = 1U;
    return {token, static_cast<std::uint8_t>((packed >> 16U) & 0x1FU)};
}

std::uint16_t dkr::runtime::presentation::presentation_token_for_object(
    std::uint8_t*, std::uint32_t object_address) {
    if (!ValidObjectAddress(object_address)) return 0U;
    std::scoped_lock lock(g_identity_mutex);
    EnsureLifetimeLocked(object_address);
    return g_lifetimes[object_address].presentation_token;
}

std::uint16_t
dkr::runtime::presentation::presentation_token_for_registered_object(
    std::uint8_t*, std::uint32_t object_address) {
    if (!ValidObjectAddress(object_address)) return 0U;
    std::scoped_lock lock(g_identity_mutex);
    const auto it = g_lifetimes.find(object_address);
    return it != g_lifetimes.end() && it->second.alive
        ? it->second.presentation_token : 0U;
}

std::uint16_t
dkr::runtime::presentation::presentation_token_for_active_capture() {
    if (g_capture_depth == 0U || g_capture_overflow_depth != 0U) return 0U;
    const ObjectCapture& capture = g_capture_stack[g_capture_depth - 1U];
    return capture.object != 0U ? capture.presentation_token : 0U;
}

dkr::runtime::presentation::VehiclePartPresentationKey
dkr::runtime::presentation::active_vehicle_part_presentation_key(
    std::uint32_t attachment_matrix_address) {
    if (g_capture_depth == 0U || g_capture_overflow_depth != 0U) return {};
    const ObjectCapture& capture = g_capture_stack[g_capture_depth - 1U];
    if (capture.object == 0U || capture.presentation_token == 0U ||
        capture.first_matrix == 0U) return {};
    const std::uint8_t slot = vehicle_part_attachment_slot(
        capture.first_matrix, Physical(attachment_matrix_address));
    if (slot == kInvalidVehiclePartSlot) return {};
    std::scoped_lock lock(g_identity_mutex);
    const auto lifetime_it = g_lifetimes.find(capture.object);
    if (lifetime_it == g_lifetimes.end() || !lifetime_it->second.alive) return {};
    std::uint16_t& attachment_token =
        lifetime_it->second.vehicle_part_tokens[slot];
    if (attachment_token == 0U && g_next_presentation_token <= 0xFFFFU) {
        attachment_token =
            static_cast<std::uint16_t>(g_next_presentation_token++);
    }
    return {attachment_token, slot};
}

std::uint32_t dkr::runtime::presentation::register_active_vehicle_part_matrix(
    std::uint32_t attachment_transform_address,
    std::uint32_t attachment_matrix_address,
    bool mirrored) {
    if (g_capture_depth == 0U || g_capture_overflow_depth != 0U ||
        !ValidObjectAddress(attachment_transform_address) ||
        !ValidObjectAddress(attachment_matrix_address)) return kIgnoredIdentity;
    const ObjectCapture& capture = g_capture_stack[g_capture_depth - 1U];
    const std::uint32_t matrix = Physical(attachment_matrix_address);
    const std::uint8_t slot = vehicle_part_attachment_slot(
        capture.first_matrix, matrix);
    if (capture.identity == kIgnoredIdentity ||
        slot == kInvalidVehiclePartSlot) return kIgnoredIdentity;

    const std::uint32_t identity = make_vehicle_part_matrix_identity(
        capture.identity, Physical(attachment_transform_address), mirrored);
    std::scoped_lock lock(g_identity_mutex);
    g_matrix_maps[capture.buffer & 1U].insert_or_assign(
        matrix, MatrixBinding{
            with_camera_continuity(identity, capture.camera_identity),
            capture.identity, false, false, false, false, 0U, capture.world_projection});
    return identity;
}

dkr::runtime::presentation::ShadowPresentationKey
dkr::runtime::presentation::shadow_presentation_key(
    std::uint8_t* rdram, std::uint32_t object_address,
    std::uint32_t shadow_address) {
    std::uint64_t topology_signature = 0U;
    ShadowGeometrySnapshot geometry{};
    if (!ShadowTopologySignature(rdram, object_address, shadow_address,
                                 topology_signature, geometry)) {
        return {};
    }

    std::scoped_lock lock(g_identity_mutex);
    EnsureLifetimeLocked(object_address);
    Lifetime& lifetime = g_lifetimes[object_address];
    // Topology is diagnostic data, not an interpolation kill switch. The DKR
    // bridge submits a fixed canonical vertex stream for this owner, so an
    // edge crossing or batch split must retain the same lifetime identity.
    // The immutable task's full scene generation separately prevents a reused
    // token from inheriting history after a level transition.
    const std::uint8_t batch_count = static_cast<std::uint8_t>(std::min(
        geometry.batch_vertex_counts.size(),
        dkr::runtime::presentation::kMaximumCanonicalShadowBatches));
    const std::uint32_t scene =
        g_scene_generation.load(std::memory_order_relaxed);
    const std::uint64_t history_key =
        (static_cast<std::uint64_t>(scene) << 16U) |
        lifetime.presentation_token;
    // Gameplay racers, title-sequence vehicle actors and Taj all move a
    // projected ground shadow over changing terrain topology. MagicCarpet is
    // a separate BHV_ANIMATED_OBJECT and is admitted only by its exact object
    // ID. Keep this list deliberately narrow so scenery and the other animated
    // cutscene actors retain their existing terrain-clipped presentation.
    const std::uint16_t behaviour =
        ReadU16(rdram, object_address + kObjectBehaviourOffset);
    const std::uint16_t object_id = static_cast<std::uint16_t>(
        ReadU16(rdram, object_address + kObjectIdOffset) &
        dkr::runtime::presentation::kObjectAssetIdMask);
    std::int8_t vehicle = -1;
    if (behaviour == 1U) {
        const std::uint32_t racer = ReadU32(
            rdram, object_address +
                       dkr::runtime::vehicle_context::kObjectRacerOffset);
        if (racer != 0U && ValidRange(
                racer + dkr::runtime::vehicle_context::kVehicleIdOffset, 1U)) {
            vehicle = static_cast<std::int8_t>(ReadU8(
                rdram,
                racer + dkr::runtime::vehicle_context::kVehicleIdOffset));
            if (!dkr::runtime::vehicle_context::valid_vehicle_id(vehicle)) {
                vehicle = static_cast<std::int8_t>(ReadU8(
                    rdram,
                    racer + dkr::runtime::vehicle_context::
                                kPreviousVehicleIdOffset));
            }
            if (!dkr::runtime::vehicle_context::valid_vehicle_id(vehicle)) {
                vehicle = -1;
            }
        }
    }
    std::uint8_t texture_width = 0U;
    if (ValidRange(shadow_address + kShadowTextureOffset, 4U)) {
        const std::uint32_t texture = ReadU32(
            rdram, shadow_address + kShadowTextureOffset);
        if (ValidRange(texture + kTextureWidthOffset, 1U)) {
            texture_width = ReadU8(
                rdram, texture + kTextureWidthOffset);
        }
    }
    const auto rigid_policy =
        dkr::runtime::presentation::rigid_shadow_owner_policy(
            behaviour, vehicle, texture_width, object_id);
    if (rigid_policy.required()) {
        g_rigid_actor_shadow_keys.insert(history_key);
    } else {
        g_rigid_actor_shadow_keys.erase(history_key);
    }
    if (rigid_policy.magic_carpet()) {
        g_taj_carpet_shadow_keys.insert(history_key);
    } else {
        g_taj_carpet_shadow_keys.erase(history_key);
    }
    if (rigid_policy.required()) {
        g_rigid_shadow_owner_policies.insert_or_assign(
            history_key, rigid_policy);
        g_shadow_owner_motion_maps[g_recording_buffer & 1U]
            .insert_or_assign(history_key,
                dkr::runtime::presentation::ShadowOwnerMotionSample{
                    {geometry.object_x, geometry.object_y, geometry.object_z},
                    geometry.object_yaw, true});
    } else {
        g_rigid_shadow_owner_policies.erase(history_key);
        g_shadow_owner_motion_maps[g_recording_buffer & 1U].erase(history_key);
    }
    lifetime.shadow_topology_valid = true;
    lifetime.shadow_topology_signature = topology_signature;
    lifetime.shadow_geometry = std::move(geometry);
    return {
        lifetime.presentation_token,
        batch_count,
    };
}

bool dkr::runtime::presentation::shadow_owner_uses_rigid_actor_proxy(
    std::uint64_t shadow_history_key) {
    if (shadow_history_key == 0U) return false;
    if(g_active_owned_task){const auto* found=g_active_owned_shadow_policies.find(shadow_history_key);return found&&found->required();}
    std::scoped_lock lock(g_identity_mutex);
    return g_rigid_actor_shadow_keys.contains(shadow_history_key);
}

bool dkr::runtime::presentation::shadow_owner_is_taj_carpet_actor(
    std::uint64_t shadow_history_key) {
    if (shadow_history_key == 0U) return false;
    if(g_active_owned_task){const auto* found=g_active_owned_shadow_policies.find(shadow_history_key);return found&&found->terrain_conforming_actor();}
    std::scoped_lock lock(g_identity_mutex);
    return g_taj_carpet_shadow_keys.contains(shadow_history_key);
}

dkr::runtime::presentation::RigidShadowOwnerPolicy
dkr::runtime::presentation::shadow_owner_rigid_policy(
    std::uint64_t shadow_history_key) {
    if (shadow_history_key == 0U) return {};
    if(g_active_owned_task){const auto* found=g_active_owned_shadow_policies.find(shadow_history_key);return found?*found:RigidShadowOwnerPolicy{};}
    std::scoped_lock lock(g_identity_mutex);
    const auto found =
        g_rigid_shadow_owner_policies.find(shadow_history_key);
    return found != g_rigid_shadow_owner_policies.end()
        ? found->second
        : dkr::runtime::presentation::RigidShadowOwnerPolicy{};
}

dkr::runtime::presentation::ShadowOwnerMotionSample
dkr::runtime::presentation::shadow_owner_motion_sample(
    std::uint64_t shadow_history_key) {
    if (shadow_history_key == 0U) return {};
    if (g_active_owned_task) {
        const auto* found = g_active_owned_shadow_motion.find(shadow_history_key);
        return found ? *found : ShadowOwnerMotionSample{};
    }
    const auto found =
        g_active_shadow_owner_motion.find(shadow_history_key);
    return found != g_active_shadow_owner_motion.end()
        ? found->second
        : dkr::runtime::presentation::ShadowOwnerMotionSample{};
}

dkr::runtime::presentation::TaskIdentityScope::TaskIdentityScope(
    std::uint8_t* rdram_snapshot, std::uint32_t display_list_address) {
    g_active_matrix_map.clear();
    g_active_marker_map.clear();
    g_active_shadow_owner_motion.clear();
    g_active_task_interpolation_allowed = false;
    g_active_task_scene_generation = 0U;
    g_active_owned_task=false;g_active_owned_shadow_policies.clear();
    if (rdram_snapshot == nullptr) {
        return;
    }
    const std::uint32_t task_number =
        ReadU32(rdram_snapshot, kSpTaskNumberAddress) & 1U;
    const std::uint32_t scene =
        g_scene_generation.load(std::memory_order_relaxed);
    const std::uint32_t actual = display_list_address & kRdramMask;
    std::scoped_lock lock(g_identity_mutex);
    if (g_submitted_frames.empty()) {
        return;
    }
    const auto matching = std::find_if(
        g_submitted_frames.begin(), g_submitted_frames.end(),
        [actual, scene](const SubmittedFrame& candidate) {
            return dkr::runtime::presentation::submitted_task_matches(
                candidate.scene_generation, candidate.display_list_address,
                scene, actual);
        });
    if (matching == g_submitted_frames.end()) {
        if (InterpolationTraceEnabled()) {
            g_interpolation_trace.sidecar_mismatches.fetch_add(
                1U, std::memory_order_relaxed);
        }
        const SubmittedFrame& expected = g_submitted_frames.front();
        std::fprintf(stderr,
                     "[boot][presentation] identity sidecar task mismatch "
                     "expected=(scene=%u task=%u dl=0x%06X seq=%llu) "
                     "actual=(scene=%u snapshot-task=%u dl=0x%06X); "
                     "interpolation "
                     "disabled for this task\n",
                     expected.scene_generation, expected.task_number,
                     expected.display_list_address & kRdramMask,
                     static_cast<unsigned long long>(
                         expected.submission_sequence),
                     scene, task_number, actual);
        return;
    }
    // Each immutable OSTask owns its data_ptr. Match the oldest pending entry
    // for that address in the active scene; this stays correct even when DKR
    // has already flipped gSPTaskNum in live/snapshotted RDRAM. Any older
    // entries cannot belong to a future decode once this task has arrived.
    SubmittedFrame frame = std::move(*matching);
    g_submitted_frames.erase(g_submitted_frames.begin(),
                             std::next(matching));
    g_active_matrix_map = std::move(frame.matrices);
    g_active_marker_map = std::move(frame.markers);
    g_active_shadow_owner_motion = std::move(frame.shadow_owner_motion);
    g_active_task_interpolation_allowed = frame.interpolation_allowed;
    g_active_task_scene_generation = frame.scene_generation;
    if (InterpolationTraceEnabled()) {
        g_interpolation_trace.sidecar_matches.fetch_add(
            1U, std::memory_order_relaxed);
    }
}

dkr::runtime::presentation::TaskIdentityScope::~TaskIdentityScope() {
    g_active_matrix_map.clear();
    g_active_marker_map.clear();
    g_active_shadow_owner_motion.clear();
    g_active_task_interpolation_allowed = false;
    g_active_task_scene_generation = 0U;
    g_active_owned_task=false;g_active_owned_shadow_policies.clear();
    g_active_owned_matrices.clear();g_active_owned_markers.clear();g_active_owned_shadow_motion.clear();
}

dkr::runtime::presentation::TaskIdentityScope::TaskIdentityScope(
    std::span<const LocatedPresentationMarker> markers)
    :TaskIdentityScope(markers,{},{},0,false) {}

dkr::runtime::presentation::TaskIdentityScope::TaskIdentityScope(
    std::span<const LocatedPresentationMarker> markers,std::span<const OwnedMatrixBinding> matrices,
    std::span<const OwnedShadowBinding> shadows,std::uint32_t scene,bool allowed) {
    dkr::runtime::netplay::experimental::performance::Scope identity_timing(
        dkr::runtime::netplay::experimental::performance::Stage::Identity);
    g_active_matrix_map.clear();g_active_marker_map.clear();g_active_shadow_owner_motion.clear();
    g_active_owned_task=true;
    if(markers.size()>8192 || matrices.size()>8192 || shadows.size()>8192) {
        g_active_owned_matrices.clear();g_active_owned_markers.clear();
        g_active_owned_shadow_policies.clear();g_active_owned_shadow_motion.clear();
        g_active_task_interpolation_allowed=false;g_active_task_scene_generation=0;
        std::fprintf(stderr,"[rollback][presentation] oversized owned metadata; discrete task fallback\n");
        return;
    }
    g_active_owned_matrices.begin(matrices.size());
    g_active_owned_markers.begin(markers.size());
    g_active_owned_shadow_policies.begin(shadows.size());
    g_active_owned_shadow_motion.begin(shadows.size());
    g_active_task_interpolation_allowed=allowed&&scene!=0;g_active_task_scene_generation=scene;
    for(const auto& binding:matrices) {
        if(!ValidRange(binding.address,64))continue;
        const auto& i=binding.interpolation;
        const MatrixBinding value{i.identity,0,i.interpolate_vertices,i.interpolate_texcoords,i.interpolate_tiles,i.procedural_water,i.water_scroll_tag,i.world_projection};
        if(binding.weak)g_active_owned_matrices.try_emplace(Physical(binding.address),value);
        else g_active_owned_matrices.insert_or_assign(Physical(binding.address),value);
    }
    for(const auto& binding:shadows) {
        if(!binding.token||!scene)continue;
        const auto key=(std::uint64_t(scene)<<16)|binding.token;
        if(binding.policy.required())g_active_owned_shadow_policies.insert_or_assign(key,binding.policy);
        if(binding.motion.valid)g_active_owned_shadow_motion.insert_or_assign(key,binding.motion);
    }
    for(const auto& located:markers) {
        const auto& marker=located.marker;
        if(!ValidRange(located.address,8) ||
           !valid_presentation_marker(marker.kind,marker.mode,marker.variant)) {
            // A malformed optional sidecar must not tear down a healthy
            // multiplayer simulation. Drop this whole task's metadata,
            // including its matrix/shadow identities, and render discrete.
            g_active_owned_markers.clear();g_active_owned_matrices.clear();
            g_active_owned_shadow_motion.clear();g_active_owned_shadow_policies.clear();
            g_active_task_interpolation_allowed=false;g_active_task_scene_generation=0;
            std::fprintf(stderr,"[rollback][presentation] invalid owned marker transaction; metadata disabled for this task\n");
            return;
        }
        auto* destination=g_active_owned_markers.try_emplace(Physical(located.address)).first;
        if(destination->count==kMaximumMarkersPerCommand) {
            g_active_owned_markers.clear();g_active_owned_matrices.clear();
            g_active_owned_shadow_motion.clear();g_active_owned_shadow_policies.clear();
            g_active_task_interpolation_allowed=false;g_active_task_scene_generation=0;
            std::fprintf(stderr,"[rollback][presentation] invalid owned marker transaction; metadata disabled for this task\n");
            return;
        }
        destination->markers[destination->count++]=marker;
    }
}

bool dkr::runtime::presentation::task_interpolation_allowed() {
    return g_active_task_interpolation_allowed;
}

bool dkr::runtime::presentation::task_is_owned() {
    return g_active_owned_task;
}

std::uint32_t dkr::runtime::presentation::task_scene_generation() {
    return g_active_task_scene_generation;
}

std::uint64_t dkr::runtime::presentation::authored_frame_sequence() {
    return g_authored_frame_sequence.load(std::memory_order_relaxed);
}

std::uint32_t dkr::runtime::presentation::recording_scene_generation() {
    return g_scene_generation.load(std::memory_order_relaxed);
}

std::uint32_t
dkr::runtime::presentation::current_camera_continuity_identity() {
    return g_recording_interpolation_allowed
        ? g_current_camera_identity
        : dkr::runtime::presentation::kIgnoredIdentity;
}

bool dkr::runtime::presentation::interpolation_trace_enabled() {
    return InterpolationTraceEnabled();
}

void dkr::runtime::presentation::interpolation_trace_segment_region(
    std::uint8_t* rdram, std::uint32_t segment_id, int race_type,
    bool authored_region_visible, bool effective_region_visible,
    bool retention_active) {
    if (!InterpolationTraceEnabled() || rdram == nullptr) return;
    std::uint32_t camera_id = 0xFFU;
    ActiveLogicalCamera(rdram, camera_id);
    const std::uint64_t key =
        (static_cast<std::uint64_t>(
             g_scene_generation.load(std::memory_order_relaxed)) << 32U) |
        (static_cast<std::uint64_t>(camera_id & 0xFFU) << 24U) |
        static_cast<std::uint64_t>(segment_id & 0x00FFFFFFU);
    {
        std::scoped_lock lock(g_interpolation_trace_mutex);
        SegmentTraceState& state = g_interpolation_segment_visibility[key];
        state.race_type = race_type;
        state.authored_region_visible = authored_region_visible;
        state.effective_region_visible = effective_region_visible;
        state.retention_active = retention_active;
    }
    g_interpolation_trace.segment_region_checks.fetch_add(
        1U, std::memory_order_relaxed);
    if (!authored_region_visible && effective_region_visible) {
        g_interpolation_trace.segment_region_relaxed.fetch_add(
            1U, std::memory_order_relaxed);
    }
}

void dkr::runtime::presentation::interpolation_trace_segment_block(
    std::uint8_t* rdram, std::uint32_t segment_id, bool block_visible) {
    if (!InterpolationTraceEnabled() || rdram == nullptr) return;
    std::uint32_t camera_id = 0xFFU;
    ActiveLogicalCamera(rdram, camera_id);
    const std::uint32_t scene =
        g_scene_generation.load(std::memory_order_relaxed);
    const std::uint64_t key =
        (static_cast<std::uint64_t>(scene) << 32U) |
        (static_cast<std::uint64_t>(camera_id & 0xFFU) << 24U) |
        static_cast<std::uint64_t>(segment_id & 0x00FFFFFFU);
    SegmentTraceState snapshot{};
    bool dropped = false;
    {
        std::scoped_lock lock(g_interpolation_trace_mutex);
        SegmentTraceState& state = g_interpolation_segment_visibility[key];
        dropped = state.has_block_sample && state.block_visible &&
            !block_visible;
        state.block_visible = block_visible;
        state.has_block_sample = true;
        snapshot = state;
    }
    g_interpolation_trace.segment_block_checks.fetch_add(
        1U, std::memory_order_relaxed);
    if (!block_visible) {
        g_interpolation_trace.segment_block_rejects.fetch_add(
            1U, std::memory_order_relaxed);
    }
    if (!dropped) return;
    g_interpolation_trace.segment_visibility_drops.fetch_add(
        1U, std::memory_order_relaxed);
    if (g_interpolation_trace_events.fetch_add(
            1U, std::memory_order_relaxed) < 32U) {
        std::fprintf(stderr,
            "[trace][interpolation][segment-drop] frame=%llu scene=%u "
            "camera=%u segment=%u race=%d authored-region=%d "
            "effective-region=%d retention=%d block=0\n",
            static_cast<unsigned long long>(
                g_authored_frame_sequence.load(std::memory_order_relaxed)),
            scene, camera_id, segment_id, snapshot.race_type,
            snapshot.authored_region_visible ? 1 : 0,
            snapshot.effective_region_visible ? 1 : 0,
            snapshot.retention_active ? 1 : 0);
    }
}

void dkr::runtime::presentation::interpolation_trace_billboard(
    bool has_owner_token, bool has_sprite, bool marker_recorded) {
    if (!InterpolationTraceEnabled()) return;
    if (!has_owner_token) {
        g_interpolation_trace.billboard_owner_misses.fetch_add(
            1U, std::memory_order_relaxed);
    }
    if (!has_sprite) {
        g_interpolation_trace.billboard_sprite_misses.fetch_add(
            1U, std::memory_order_relaxed);
    }
    if (marker_recorded) {
        g_interpolation_trace.billboard_markers.fetch_add(
            1U, std::memory_order_relaxed);
    } else if (has_owner_token && has_sprite) {
        g_interpolation_trace.billboard_marker_failures.fetch_add(
            1U, std::memory_order_relaxed);
    }
}

bool dkr::runtime::presentation::record_presentation_marker(
    std::uint32_t command_address, std::uint8_t mode,
    std::uint16_t token, std::uint8_t variant, PresentationMarkerKind kind,
    hud::groups::Transform hud_transform) {
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return false;
    }
    const std::uint32_t physical = Physical(command_address);
    std::scoped_lock lock(g_identity_mutex);
    auto& integrity = g_marker_integrity[g_recording_buffer & 1U];
    if (!integrity.valid) return false;
    auto& markers = g_marker_maps[g_recording_buffer & 1U][physical];
    if (!integrity.accept(ValidRange(command_address, 8U) &&
                          valid_presentation_marker(kind, mode, variant) &&
                          (kind != PresentationMarkerKind::HudWidget || hud_transform.valid()),
                          markers.size(), kMaximumMarkersPerCommand)) {
        if (g_marker_rejections.record(g_next_submission_sequence.load(
                std::memory_order_relaxed))) {
            std::fprintf(stderr,
                "[boot][presentation] rejected marker transaction "
                "address=0x%08X kind=%u mode=%u variant=%u count=%zu "
                "total=%llu since-report=%llu; partial task metadata discarded\n",
                command_address, static_cast<unsigned>(kind), mode, variant,
                markers.size(), static_cast<unsigned long long>(g_marker_rejections.total),
                static_cast<unsigned long long>(g_marker_rejections.take_pending()));
        }
        return false;
    }
    markers.push_back(PresentationMarker{mode, token, variant, kind, hud_transform});
    return true;
}

dkr::runtime::presentation::PresentationMarkerList
dkr::runtime::presentation::active_presentation_markers(
    std::uint32_t command_address) {
    PresentationMarkerList result{};
    if (g_active_owned_task) {
        const auto* found = g_active_owned_markers.find(Physical(command_address));
        return found ? *found : result;
    }
    const auto found = g_active_marker_map.find(Physical(command_address));
    if (found == g_active_marker_map.end()) {
        return result;
    }
    result.count = std::min(found->second.size(), result.markers.size());
    std::copy_n(found->second.begin(), result.count, result.markers.begin());
    return result;
}

extern "C" void dkr_presentation_scene_begin(std::uint8_t*, recomp_context*) {
    std::scoped_lock lock(g_identity_mutex);
    std::uint32_t next =
        g_scene_generation.fetch_add(1U, std::memory_order_relaxed) + 1U;
    if (next == 0U) {
        g_scene_generation.store(1U, std::memory_order_relaxed);
    }
    g_lifetimes.clear();
    g_identity_owners.clear();
    g_collided_identities.clear();
    g_rigid_actor_shadow_keys.clear();
    g_taj_carpet_shadow_keys.clear();
    g_rigid_shadow_owner_policies.clear();
    g_next_presentation_token = 1U;
    g_camera_continuity = {};
    g_finish_camera_shots = {};
    g_current_camera_identity =
        dkr::runtime::presentation::kIgnoredIdentity;
    g_recording_interpolation_allowed = false;
    g_active_task_interpolation_allowed = false;
    g_active_task_scene_generation = 0U;
    g_wave_capture_active = false;
    g_wave_capture_viewport = 0U;
    g_wave_block_address = 0U;
    g_wave_block_valid = false;
    g_submission_overflowed = false;
    g_authored_frame_sequence.store(0U, std::memory_order_relaxed);
    g_submitted_frames.clear();
    g_active_matrix_map.clear();
    g_active_marker_map.clear();
    g_active_shadow_owner_motion.clear();
    for (auto& map : g_matrix_maps) {
        map.clear();
        map.reserve(1024U);
    }
    g_marker_integrity = {};
    for (auto& map : g_marker_maps) {
        map.clear();
        map.reserve(256U);
    }
    for (auto& map : g_shadow_owner_motion_maps) {
        map.clear();
        map.reserve(64U);
    }
    if (InterpolationTraceEnabled()) {
        std::scoped_lock trace_lock(g_interpolation_trace_mutex);
        g_interpolation_segment_visibility.clear();
        g_interpolation_trace_events.store(0U, std::memory_order_relaxed);
        std::fprintf(stderr,
                     "[trace][interpolation][scene] generation=%u\n",
                     next == 0U ? 1U : next);
    }
}

extern "C" void dkr_presentation_frame_begin(std::uint8_t* rdram,
                                              recomp_context*) {
    dkr::runtime::hud::begin_authored_frame(rdram);
    dkr::runtime::presentation::postrace_presentation_begin_frame();
    dkr::runtime::custom_music::tick(rdram);
    const std::uint64_t frame =
        g_authored_frame_sequence.fetch_add(
            1U, std::memory_order_relaxed) + 1U;
    if (InterpolationTraceEnabled()) {
        PrintInterpolationTraceSummary(frame);
    }
    g_recording_buffer = ReadU32(rdram, kSpTaskNumberAddress) & 1U;
    g_capture_depth = 0U;
    g_capture_overflow_depth = 0U;
    g_current_camera_identity =
        dkr::runtime::presentation::kIgnoredIdentity;
    g_recording_interpolation_allowed =
        dkr::runtime::enhancements::interpolation_allowed(
            dkr::runtime::enhancements::presentation_profile());
    std::scoped_lock lock(g_identity_mutex);
    g_matrix_maps[g_recording_buffer].clear();
    g_marker_maps[g_recording_buffer].clear();
    g_marker_integrity[g_recording_buffer] = {};
    g_shadow_owner_motion_maps[g_recording_buffer].clear();
}

extern "C" void dkr_presentation_perspective_matrix(
    std::uint8_t* rdram, recomp_context* context) {
    // At 0x80068108, a0 has been restored to the caller's Mtx ** and still
    // points at the matrix just written by mtx_perspective.
    RegisterCameraMatrix(rdram, static_cast<std::uint32_t>(context->r4), 0U);
}

extern "C" void dkr_presentation_world_origin_matrix(
    std::uint8_t* rdram, recomp_context* context) {
    // At 0x800684D8, s0 still contains the caller's Mtx ** and has not yet
    // advanced past the matrix produced by mtx_world_origin.
    RegisterCameraMatrix(rdram, static_cast<std::uint32_t>(context->r16), 1U);
}

extern "C" void dkr_presentation_wave_begin(std::uint8_t* rdram,
                                               recomp_context* context) {
    g_wave_capture_active =
        dkr::runtime::enhancements::modern_presentation_enabled();
    g_wave_capture_viewport = static_cast<std::uint32_t>(context->r6);
    g_wave_block_address = 0U;
    g_wave_block_valid = false;
    g_wave_selection_pattern = 0U;
    g_wave_selection_valid = false;

}

extern "C" void dkr_presentation_wave_end(std::uint8_t*, recomp_context*) {
    g_wave_capture_active = false;
    g_wave_capture_viewport = 0U;
    g_wave_block_address = 0U;
    g_wave_block_valid = false;
    g_wave_selection_pattern = 0U;
    g_wave_selection_valid = false;
}

extern "C" void dkr_presentation_wave_block(std::uint8_t*,
                                              recomp_context* context) {
    if (!g_wave_capture_active) {
        return;
    }

    // At 0x800BAE30 in waves_render, v0/r2 still holds the current
    // WaveBlockModel pointer before execution branches to the normal- or
    // double-density draw path. The model entry is stable for the lifetime of
    // the scene, unlike the alternating wave vertex buffers and matrix heap.
    const std::uint32_t block_address =
        static_cast<std::uint32_t>(context->r2);
    g_wave_block_valid = ValidRange(block_address, 0x1CU);
    g_wave_block_address =
        g_wave_block_valid ? Physical(block_address) : 0U;
}

extern "C" void dkr_presentation_wave_selection(
    std::uint8_t* rdram, recomp_context* context) {
    if (!g_wave_capture_active) {
        return;
    }

    // This hook runs in waves_render immediately before each of its two
    // static mtx_cam_push call sites. The caller's 0x120-byte frame is still
    // current here, so 0x104(sp) is the decompiled local `sp104`: a packed
    // sequence of grid selectors in the inclusive range 0..25. At
    // mtx_cam_push entry, by contrast, $sp belongs to the callee and this
    // offset is unrelated stack data.
    const std::uint32_t packed_selection =
        static_cast<std::uint32_t>(MEM_W(0x104, context->r29));
    const std::uint8_t selection_pattern =
        static_cast<std::uint8_t>(packed_selection & 0xFFU);

    g_wave_selection_valid =
        dkr::runtime::presentation::valid_wave_selection_pattern(
            selection_pattern);
    g_wave_selection_pattern = selection_pattern;
}

extern "C" void dkr_presentation_wave_matrix(
    std::uint8_t* rdram, recomp_context* context) {
    if (!g_wave_capture_active) {
        return;
    }

    // mtx_cam_push receives Mtx ** in a1 and ObjectTransform * in a2. This
    // hook runs at function entry, before either argument is overwritten.
    const std::uint32_t matrix_reference =
        static_cast<std::uint32_t>(context->r5);
    const std::uint32_t transform = static_cast<std::uint32_t>(context->r6);
    if (!ValidRange(matrix_reference, 4U) || !ValidRange(transform, 0x10U)) {
        return;
    }
    const std::uint32_t matrix_address = ReadU32(rdram, matrix_reference);
    if (!ValidRange(matrix_address, 64U)) {
        return;
    }

    const std::uint32_t subdivisions = ReadU32(
        rdram, kWaveControllerAddress + kWaveSubdivisionsOffset);
    const bool double_density = ReadU32(
        rdram, kWaveControllerAddress + kWaveDoubleDensityOffset);
    // The caller-side hook captured waves_render's `sp104` before this call.
    // Consume it exactly once so an unrelated mtx_cam_push can never reuse a
    // stale selector. If the call-site hook was missed or the decompiled
    // invariant is violated, retain the authored matrix without registering
    // an unsafe interpolation identity.
    const bool selection_valid = g_wave_selection_valid;
    const std::uint8_t selection_pattern = g_wave_selection_pattern;
    g_wave_selection_pattern = 0U;
    g_wave_selection_valid = false;
    if (!selection_valid || !g_wave_block_valid) {
        return;
    }
    const std::uint32_t scale_bits = ReadU32(rdram, transform + 0x0CU);
    const std::uint32_t topology =
        dkr::runtime::presentation::make_wave_topology_variant(
            subdivisions, double_density, selection_pattern, scale_bits);
    const std::uint32_t identity =
        dkr::runtime::presentation::make_wave_matrix_identity(
        g_scene_generation.load(std::memory_order_relaxed),
        g_wave_capture_viewport,
        g_wave_block_address,
        ReadU32(rdram, transform + 0x00U),
        ReadU32(rdram, transform + 0x04U),
        ReadU32(rdram, transform + 0x08U), topology);

    std::scoped_lock lock(g_identity_mutex);
    g_matrix_maps[g_recording_buffer & 1U].insert_or_assign(
        Physical(matrix_address),
        MatrixBinding{
            dkr::runtime::presentation::with_camera_continuity(
                identity, g_current_camera_identity),
            0U, true, true, true, true,
            dkr::runtime::water::scroll_tag(
                ReadU32(rdram, dkr::runtime::revision_addresses::WaveTexUVMaskX),
                ReadU32(rdram, dkr::runtime::revision_addresses::WaveTexUVMaskY)),
            ReadWorldProjection(rdram)});
}

extern "C" void dkr_presentation_object_spawned(std::uint8_t*,
                                                 recomp_context* context) {
    NoteSpawn(static_cast<std::uint32_t>(context->r2));
}

extern "C" void dkr_presentation_object_freed(std::uint8_t*,
                                               recomp_context* context) {
    NoteFree(static_cast<std::uint32_t>(context->r4));
}

extern "C" void dkr_presentation_task_submitted(std::uint8_t* rdram,
                                                  recomp_context* context) {
    dkr::runtime::presentation::postrace_presentation_submit_frame(
        dkr::runtime::netplay::external_side_effects_allowed());
    if (!dkr::runtime::netplay::external_side_effects_allowed()) {
        std::scoped_lock lock(g_identity_mutex);
        g_matrix_maps[g_recording_buffer & 1U].clear();
        g_marker_maps[g_recording_buffer & 1U].clear();
        g_shadow_owner_motion_maps[g_recording_buffer & 1U].clear();
        return;
    }
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    SubmittedFrame frame{};
    frame.display_list_address = static_cast<std::uint32_t>(context->r4);
    frame.task_number = ReadU32(rdram, kSpTaskNumberAddress) & 1U;
    frame.scene_generation =
        g_scene_generation.load(std::memory_order_relaxed);
    frame.submission_sequence =
        g_next_submission_sequence.fetch_add(1U, std::memory_order_relaxed);
    frame.interpolation_allowed = g_recording_interpolation_allowed;
    std::scoped_lock lock(g_identity_mutex);
    if (g_submission_overflowed) {
        return;
    }
    if (g_submitted_frames.size() >= kMaximumPendingFrames) {
        g_submitted_frames.clear();
        for (auto& map : g_marker_maps) {
            map.clear();
        }
        for (auto& map : g_shadow_owner_motion_maps) {
            map.clear();
        }
        g_submission_overflowed = true;
        if (InterpolationTraceEnabled()) {
            g_interpolation_trace.sidecar_overflows.fetch_add(
                1U, std::memory_order_relaxed);
        }
        std::fprintf(stderr,
                     "[boot][presentation] semantic sidecar queue exceeded "
                     "%zu tasks; interpolation identities disabled until "
                     "the next scene\n",
                     kMaximumPendingFrames);
        return;
    }
    frame.matrices = std::move(g_matrix_maps[g_recording_buffer & 1U]);
    g_matrix_maps[g_recording_buffer & 1U].reserve(1024U);
    frame.markers = std::move(g_marker_maps[g_recording_buffer & 1U]);
    g_marker_maps[g_recording_buffer & 1U].reserve(256U);
    frame.shadow_owner_motion =
        std::move(g_shadow_owner_motion_maps[g_recording_buffer & 1U]);
    g_shadow_owner_motion_maps[g_recording_buffer & 1U].reserve(64U);
    if (!g_marker_integrity[g_recording_buffer & 1U].valid) {
        // Use the existing no-sidecar fallback for this malformed task only.
        // Never publish a truncated begin/end stream, or retain the failure
        // into the next frame/scene. Valid tasks take the unchanged path.
        frame.markers.clear();
        frame.matrices.clear();
        frame.shadow_owner_motion.clear();
        frame.interpolation_allowed = false;
    }
    g_submitted_frames.emplace_back(std::move(frame));
}

extern "C" void dkr_presentation_object_begin(std::uint8_t* rdram,
                                               recomp_context* context) {
    if (!dkr::runtime::enhancements::modern_presentation_enabled()) {
        return;
    }
    if (g_capture_depth >= g_capture_stack.size()) {
        ++g_capture_overflow_depth;
        return;
    }
    ObjectCapture& capture = g_capture_stack[g_capture_depth++];
    capture = {};
    capture.buffer = g_recording_buffer;
    const std::uint32_t object = static_cast<std::uint32_t>(context->r7);
    if (!ValidObjectAddress(object)) {
        return;
    }
    const gpr matrix_reference = MEM_W(context->r29, 0x24);
    const std::uint32_t first_matrix =
        static_cast<std::uint32_t>(MEM_W(0, matrix_reference));
    if (!ValidObjectAddress(first_matrix)) {
        return;
    }
    std::scoped_lock lock(g_identity_mutex);
    capture.object = object;
    capture.identity = ObjectIdentityLocked(rdram, object);
    capture.camera_identity = g_current_camera_identity;
    capture.world_projection = ReadWorldProjection(rdram);
    capture.first_matrix = Physical(first_matrix);
    const auto lifetime = g_lifetimes.find(object);
    capture.presentation_token =
        lifetime != g_lifetimes.end() && lifetime->second.alive
            ? lifetime->second.presentation_token
            : 0U;
}

extern "C" void dkr_presentation_object_end(std::uint8_t* rdram,
                                             recomp_context*) {
    if (g_capture_overflow_depth != 0U) {
        --g_capture_overflow_depth;
        return;
    }
    if (g_capture_depth == 0U) {
        return;
    }
    const ObjectCapture capture = g_capture_stack[--g_capture_depth];
    const std::uint32_t end_matrix =
        Physical(ReadU32(rdram, kObjectCurrentMatrixAddress));
    if (capture.identity == dkr::runtime::presentation::kIgnoredIdentity ||
        end_matrix < capture.first_matrix ||
        ((end_matrix - capture.first_matrix) & 0x3FU) != 0U) {
        return;
    }
    const std::uint32_t matrix_count =
        (end_matrix - capture.first_matrix) / 64U;
    if (matrix_count == 0U || matrix_count > 256U) {
        return;
    }

    std::scoped_lock lock(g_identity_mutex);
    auto& map = g_matrix_maps[capture.buffer & 1U];
    for (std::uint32_t ordinal = 0; ordinal < matrix_count; ++ordinal) {
        const std::uint32_t address = capture.first_matrix + ordinal * 64U;
        // A nested render_object completes first. Preserve its more-specific
        // ownership when the outer object's wider range is closed.
        // The object's lifetime and the logical camera epoch are both required
        // continuity boundaries. The camera epoch no longer changes for
        // ordinary steering (orientation alone is not a cut), so this remains
        // stable through high-speed turns while preventing a real camera cut
        // or viewport change from pairing two unrelated combined MVP poses.
        map.try_emplace(address, MatrixBinding{
            dkr::runtime::presentation::with_camera_continuity(
                dkr::runtime::presentation::make_matrix_identity(
                    capture.identity, ordinal),
                capture.camera_identity),
            capture.identity, false, false, false, false, 0U, capture.world_projection});
    }
    g_matrix_ranges.fetch_add(1U, std::memory_order_relaxed);
    if (InterpolationTraceEnabled()) {
        g_interpolation_trace.object_ranges.fetch_add(
            1U, std::memory_order_relaxed);
        g_interpolation_trace.object_matrices.fetch_add(
            matrix_count, std::memory_order_relaxed);
    }
}

extern "C" void dkr_presentation_finish_camera_node(
    std::uint8_t* rdram, recomp_context* context) {
    // update_camera_finish_race, after spectate_nearest returns a node:
    // s0=&gCameraObject, a0=node, a3=racer object (verified in BOTH revisions).
    // Observe the final shot here; advance its epoch only when it is rendered.
    if (!rdram || !context) return;
    const auto reference = static_cast<std::uint32_t>(context->r16);
    if (!ValidRange(reference, 4U)) return;
    const auto camera = ReadU32(rdram, reference);
    if (camera < kCamerasAddress || (camera - kCamerasAddress) % kCameraSize != 0U) return;
    const auto slot = (camera - kCamerasAddress) / kCameraSize;
    const auto node = static_cast<std::uint32_t>(context->r4);
    const auto owner = static_cast<std::uint32_t>(context->r7);
    if (slot >= g_finish_camera_shots.size() ||
        !ValidObjectAddress(node) || !ValidObjectAddress(owner)) return;
    std::scoped_lock lock(g_identity_mutex);
    g_finish_camera_shots[slot].observe_node(owner, node);
}
