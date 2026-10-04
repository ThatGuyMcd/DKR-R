#include "presentation_identity.hpp"
#include "retained_metadata_table.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace dkr::runtime::presentation;

constexpr auto kObjectA = make_object_identity(3U, 0x12340U, 7U, 12U, 5U);
constexpr auto kObjectARepeat =
    make_object_identity(3U, 0x12340U, 7U, 12U, 5U);
constexpr auto kObjectNextLife =
    make_object_identity(3U, 0x12340U, 8U, 12U, 5U);
constexpr auto kObjectNextScene =
    make_object_identity(4U, 0x12340U, 7U, 12U, 5U);

static_assert(kObjectA == kObjectARepeat);
static_assert(kObjectA != kObjectNextLife);
static_assert(kObjectA != kObjectNextScene);
static_assert(kObjectA != kIgnoredIdentity && kObjectA != kAutomaticIdentity);
static_assert(make_matrix_identity(kObjectA, 0U) !=
              make_matrix_identity(kObjectA, 1U));
static_assert(make_matrix_identity(kObjectA, 0U) != kIgnoredIdentity);
static_assert(make_matrix_identity(kObjectA, 0U) != kAutomaticIdentity);
static_assert(make_camera_matrix_identity(3U, 0U, 0U) ==
              make_camera_matrix_identity(3U, 0U, 0U));
static_assert(make_camera_matrix_identity(3U, 0U, 0U) !=
              make_camera_matrix_identity(4U, 0U, 0U));
static_assert(make_camera_matrix_identity(3U, 0U, 0U) !=
              make_camera_matrix_identity(3U, 1U, 0U));
static_assert(make_camera_matrix_identity(3U, 0U, 0U) !=
              make_camera_matrix_identity(3U, 0U, 1U));
static_assert(make_camera_matrix_identity(3U, 0U, 0U, 1U) !=
              make_camera_matrix_identity(3U, 0U, 0U, 2U));
static_assert(make_camera_matrix_identity(3U, 0U, 0U) !=
              kIgnoredIdentity);
static_assert(make_camera_matrix_identity(3U, 0U, 0U) !=
              kAutomaticIdentity);
constexpr auto kCameraEpoch1 = make_camera_continuity_identity(3U, 0U, 1U);
constexpr auto kCameraEpoch2 = make_camera_continuity_identity(3U, 0U, 2U);
constexpr auto kCameraViewport1 = make_camera_continuity_identity(3U, 1U, 1U);
static_assert(kCameraEpoch1 != kCameraEpoch2);
static_assert(kCameraEpoch1 != kCameraViewport1);
static_assert(with_camera_continuity(kObjectA, kCameraEpoch1) ==
              with_camera_continuity(kObjectA, kCameraEpoch1));
static_assert(with_camera_continuity(kObjectA, kCameraEpoch1) !=
              with_camera_continuity(kObjectA, kCameraEpoch2));
static_assert(with_camera_continuity(kObjectA, kCameraEpoch1) !=
              with_camera_continuity(kObjectA, kCameraViewport1));
static_assert(with_camera_continuity(kIgnoredIdentity, kCameraEpoch1) ==
              kIgnoredIdentity);
static_assert(make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 4U) ==
              make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 4U));
static_assert(make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 4U) !=
              make_wave_matrix_identity(3U, 1U, 0x100U, 1U, 2U, 3U, 4U));
static_assert(make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 4U) !=
              make_wave_matrix_identity(3U, 0U, 0x11CU, 1U, 2U, 3U, 4U));
static_assert(make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 4U) !=
              make_wave_matrix_identity(3U, 0U, 0x100U, 1U, 2U, 3U, 5U));
static_assert(valid_wave_selection_pattern(0U));
static_assert(valid_wave_selection_pattern(9U));
static_assert(valid_wave_selection_pattern(10U));
static_assert(valid_wave_selection_pattern(25U));
static_assert(!valid_wave_selection_pattern(26U));
static_assert(!valid_wave_selection_pattern(255U));
static_assert(make_wave_topology_variant(4U, false, 1U, 0x3F800000U) ==
              make_wave_topology_variant(4U, false, 7U, 0x3F800000U));
static_assert(make_wave_topology_variant(4U, true, 1U, 0x3F000000U) ==
              make_wave_topology_variant(4U, true, 25U, 0x3F000000U));
static_assert(make_wave_topology_variant(4U, true, 0U, 0x3F000000U) !=
              make_wave_topology_variant(4U, true, 7U, 0x3F000000U));
static_assert(make_wave_topology_variant(4U, true, 7U, 0x3F000000U) !=
              make_wave_topology_variant(5U, true, 7U, 0x3F000000U));
static_assert(make_vehicle_part_matrix_identity(kObjectA, 0x123400U, false) ==
              make_vehicle_part_matrix_identity(kObjectA, 0x123400U, false));
static_assert(make_vehicle_part_matrix_identity(kObjectA, 0x123400U, false) !=
              make_vehicle_part_matrix_identity(kObjectA, 0x123440U, false));
static_assert(make_vehicle_part_matrix_identity(kObjectA, 0x123400U, false) !=
              make_vehicle_part_matrix_identity(kObjectA, 0x123400U, true));
static_assert(make_vehicle_part_matrix_identity(kObjectA, 0x123400U, true) ==
              make_vehicle_part_matrix_identity(kObjectA, 0x123400U, true));
static_assert(make_vehicle_part_matrix_identity(kIgnoredIdentity,
                                                 0x123400U, false) ==
              kIgnoredIdentity);
static_assert(make_vehicle_part_matrix_identity(kIgnoredIdentity,
                                                 0x123400U, true) ==
              kIgnoredIdentity);
static_assert(make_vehicle_part_matrix_identity(kObjectA, 0x123400U, false) !=
              make_matrix_identity(kObjectA, 0U));
static_assert(make_shadow_group_identity(0U) == kIgnoredIdentity);
static_assert(make_shadow_group_identity(1U) != kIgnoredIdentity);
static_assert(make_shadow_group_identity(1U) != kAutomaticIdentity);
static_assert(make_shadow_group_identity(1U) !=
              make_shadow_group_identity(2U));
static_assert(make_shadow_group_identity(1U, 1U) ==
              make_shadow_group_identity(1U, 1U));
static_assert(make_shadow_group_identity(1U, 1U) !=
              make_shadow_group_identity(1U, 2U));
static_assert(make_shadow_page_group_identity(kIgnoredIdentity, 0U) ==
              kIgnoredIdentity);
static_assert(make_shadow_page_group_identity(
                  make_shadow_group_identity(1U, 1U), 0U) !=
              make_shadow_page_group_identity(
                  make_shadow_group_identity(1U, 1U), 1U));
static_assert(make_shadow_page_group_identity(
                  make_shadow_group_identity(1U, 1U), 0U, 1U) !=
              make_shadow_page_group_identity(
                  make_shadow_group_identity(1U, 1U), 0U, 2U));
static_assert(make_vehicle_part_group_identity(0U) == kIgnoredIdentity);
static_assert(vehicle_part_attachment_slot(0x1000U, 0x1000U) == 0U);
static_assert(vehicle_part_attachment_slot(0x1000U, 0x1040U) == 1U);
static_assert(vehicle_part_attachment_slot(0x1000U, 0x17C0U) == 31U);
static_assert(vehicle_part_attachment_slot(0x1000U, 0x1800U) ==
              kInvalidVehiclePartSlot);
static_assert(vehicle_part_attachment_slot(0x1040U, 0x1000U) ==
              kInvalidVehiclePartSlot);
static_assert(vehicle_part_attachment_slot(0x1000U, 0x1010U) ==
              kInvalidVehiclePartSlot);
static_assert(vehicle_part_frame_variant(0U, 16U) == 0U);
static_assert(vehicle_part_frame_variant(255U, 16U) == 15U);
static_assert(vehicle_part_frame_variant(128U, 16U) == 8U);
static_assert(vehicle_part_frame_variant(128U, 0U) == 0U);
static_assert(make_vehicle_part_group_identity(1U) != kIgnoredIdentity);
static_assert(make_vehicle_part_group_identity(1U) != kAutomaticIdentity);
static_assert(make_vehicle_part_group_identity(1U) !=
              make_vehicle_part_group_identity(2U));
static_assert(make_vehicle_part_group_identity(1U) !=
              make_shadow_group_identity(1U));
static_assert(make_billboard_group_identity(0U) == kIgnoredIdentity);
static_assert(make_billboard_group_identity(1U, 3U) != kIgnoredIdentity);
static_assert(make_billboard_group_identity(1U, 3U) !=
              make_billboard_group_identity(1U, 4U));
static_assert(make_billboard_group_identity(1U, 3U) !=
              make_vehicle_part_group_identity(1U));
static_assert(with_camera_continuity(
                  make_billboard_group_identity(1U, 3U), kCameraEpoch1) !=
              with_camera_continuity(
                  make_billboard_group_identity(1U, 3U), kCameraEpoch2));
static_assert(make_surface_group_identity(0U, 0U) == kIgnoredIdentity);
static_assert(make_surface_group_identity(1U, 3U) != kIgnoredIdentity);
static_assert(make_surface_group_identity(1U, 3U) !=
              make_surface_group_identity(1U, 4U));
constexpr auto kOpaqueSegment = level_segment_presentation_key(17U, false);
constexpr auto kTransparentSegment = level_segment_presentation_key(17U, true);
static_assert(kOpaqueSegment.token == 18U && kOpaqueSegment.variant == 0U);
static_assert(kTransparentSegment.token == 18U &&
              kTransparentSegment.variant == 1U);
static_assert(level_segment_presentation_key(512U, false).token == 0U);
static_assert(make_level_segment_group_identity(
                  kOpaqueSegment.token, kOpaqueSegment.variant, 3U) ==
              make_level_segment_group_identity(
                  kOpaqueSegment.token, kOpaqueSegment.variant, 3U));
static_assert(make_level_segment_group_identity(
                  kOpaqueSegment.token, kOpaqueSegment.variant, 3U) !=
              make_level_segment_group_identity(
                  kTransparentSegment.token, kTransparentSegment.variant, 3U));
static_assert(make_level_segment_group_identity(
                  kOpaqueSegment.token, kOpaqueSegment.variant, 3U) !=
              make_level_segment_group_identity(
                  kOpaqueSegment.token, kOpaqueSegment.variant, 4U));
static_assert(make_level_segment_group_identity(0U, 0U, 3U) ==
              kIgnoredIdentity);
static_assert(submitted_task_matches(7U, 0x80123400U,
                                     7U, 0x00123400U));
static_assert(!submitted_task_matches(7U, 0x80123400U,
                                      8U, 0x00123400U));
static_assert(!submitted_task_matches(7U, 0x80123400U,
                                      7U, 0x00124400U));
int main() {
    // Compile-only regression coverage in this pass; user qualification owns
    // execution. First weak wins, strong replaces, duplicate marker order and
    // cold task reset must match the previous unordered_map implementation.
    RetainedMetadataTable<std::uint32_t, unsigned> retained;
    retained.begin(8192);
    for(unsigned i=0;i<8192;++i)assert(retained.try_emplace(i*64,i).second);
    for(unsigned i=0;i<8192;++i)assert(*retained.find(i*64)==i);
    assert(!retained.find(8192*64));
    assert(!retained.try_emplace(0,99).second && *retained.find(0)==0);
    retained.insert_or_assign(0,99);assert(*retained.find(0)==99);
    bool exhausted=false;
    try {retained.try_emplace(8192*64,0);}catch(const std::length_error&){exhausted=true;}
    assert(exhausted);
    const auto entries_capacity=retained.entry_capacity(),slots_capacity=retained.slot_capacity();
    retained.begin(2);
    assert(!retained.find(0) && retained.size()==0);
    assert(retained.entry_capacity()==entries_capacity && retained.slot_capacity()==slots_capacity);
    retained.try_emplace(0,10);retained.try_emplace(64,11);
    retained.clear();assert(!retained.find(0));
    RetainedMetadataTable<std::uint64_t,PresentationMarkerList> marker_table;
    marker_table.begin(3);
    const auto key=(std::uint64_t(0xABCDEFFF)<<16)|2;
    auto* list=marker_table.try_emplace(key).first;
    list->markers[list->count++].token=7;
    marker_table.try_emplace(key).first->markers[list->count++].token=8;
    assert(marker_table.find(key)->count==2 && marker_table.find(key)->markers[0].token==7 &&
           marker_table.find(key)->markers[1].token==8 && !marker_table.find(key+1));
    constexpr std::array<ShadowTexcoordSample,
                         kCanonicalShadowBatchVertices> empty_shadow_slots{};
    constexpr std::array<ShadowTexcoordSample, 4> initial_shadow_uvs{{
        {-100, -100, true}, {100, -100, true},
        {100, 100, true}, {-100, 100, true},
    }};
    const auto initial_shadow_slots = canonical_shadow_slot_map(
        empty_shadow_slots, initial_shadow_uvs);
    std::array<ShadowTexcoordSample,
               kCanonicalShadowBatchVertices> established_shadow_slots{};
    for (std::size_t slot = 0U; slot < established_shadow_slots.size(); ++slot) {
        const auto source = initial_shadow_slots.source_for_slot[slot];
        if (source != kInvalidShadowVertexSlot) {
            established_shadow_slots[slot] = initial_shadow_uvs[source];
        }
    }
    const std::array<ShadowTexcoordSample, 4> reordered_shadow_uvs{{
        {100, 100, true}, {-100, 100, true},
        {-100, -100, true}, {100, -100, true},
    }};
    const auto reordered_shadow_slots = canonical_shadow_slot_map(
        established_shadow_slots, reordered_shadow_uvs);
    assert(reordered_shadow_slots.slot_for_source[0] ==
           initial_shadow_slots.slot_for_source[2]);
    assert(reordered_shadow_slots.slot_for_source[1] ==
           initial_shadow_slots.slot_for_source[3]);
    assert(reordered_shadow_slots.slot_for_source[2] ==
           initial_shadow_slots.slot_for_source[0]);
    assert(reordered_shadow_slots.slot_for_source[3] ==
           initial_shadow_slots.slot_for_source[1]);

    const std::array<ShadowTexcoordSample, 5> split_shadow_uvs{{
        {-100, -100, true}, {100, -100, true},
        {100, 100, true}, {-100, 100, true}, {0, -100, true},
    }};
    const auto split_shadow_slots = canonical_shadow_slot_map(
        established_shadow_slots, split_shadow_uvs);
    for (std::size_t source = 0U; source < initial_shadow_uvs.size(); ++source) {
        assert(split_shadow_slots.slot_for_source[source] ==
               initial_shadow_slots.slot_for_source[source]);
    }
    assert(split_shadow_slots.slot_for_source[4] != kInvalidShadowVertexSlot);

    std::array<ShadowVertexSample,
               kCanonicalShadowBatchVertices> previous_shadow_positions{};
    for (std::size_t slot = 0U; slot < initial_shadow_uvs.size(); ++slot) {
        previous_shadow_positions[
            initial_shadow_slots.slot_for_source[slot]] = {
                static_cast<float>(initial_shadow_uvs[slot].s), 2.0F,
                static_cast<float>(initial_shadow_uvs[slot].t)};
    }
    // Dormant canonical slots are deliberately kept near the current page.
    // A recycled UV must not select a remote active slot when a safe dormant
    // slot exists beside the new polygon.
    previous_shadow_positions[4] = {900.0F, 2.0F, 900.0F};
    established_shadow_slots[4] = {-100, -100, true};
    previous_shadow_positions[5] = {-96.0F, 2.0F, -103.0F};
    const std::array<ShadowTexcoordSample, 1> recycled_shadow_uv{{
        {-100, -100, true},
    }};
    const std::array<ShadowVertexSample, 1> recycled_shadow_position{{
        {-95.0F, 2.0F, -105.0F},
    }};
    const auto safe_recycled_slot = canonical_shadow_motion_slot_map(
        established_shadow_slots, previous_shadow_positions,
        recycled_shadow_uv, recycled_shadow_position, {});
    assert(safe_recycled_slot.slot_for_source[0] == 5U);
    // Slot 5 is a deliberately dormant local anchor. It prevents an exploding
    // wedge. Correspondence remains a diagnostic because the canonical page
    // stays interpolated; the submitted vertex is bounded separately.
    assert(!canonical_shadow_slot_map_corresponds(
        established_shadow_slots, previous_shadow_positions,
        recycled_shadow_position, safe_recycled_slot, {}, 8.0F));

    // A dormant point can be geometrically closer than the established active
    // vertex during ordinary motion. The safe active semantic must retain its
    // slot or the triangle topology changes every authored frame.
    std::array<ShadowTexcoordSample,
               kCanonicalShadowBatchVertices> active_preference_uvs{};
    std::array<ShadowVertexSample,
               kCanonicalShadowBatchVertices> active_preference_positions{};
    active_preference_uvs[0] = {0, 0, true};
    active_preference_positions[0] = {0.0F, 0.0F, 0.0F};
    active_preference_positions[1] = {5.0F, 0.0F, 0.0F};
    const std::array<ShadowTexcoordSample, 1> active_preference_current_uv{{
        {0, 0, true},
    }};
    const std::array<ShadowVertexSample, 1> active_preference_current_position{{
        {5.0F, 0.0F, 0.0F},
    }};
    const auto active_preference_map = canonical_shadow_motion_slot_map(
        active_preference_uvs, active_preference_positions,
        active_preference_current_uv, active_preference_current_position, {});
    assert(active_preference_map.slot_for_source[0] == 0U);

    std::array<bool, 4> dormant_coverage{};
    for (std::size_t slot = 0U; slot < kCanonicalShadowBatchVertices; ++slot) {
        const std::size_t source = canonical_shadow_dormant_source(slot, 4U);
        assert(source < dormant_coverage.size());
        dormant_coverage[source] = true;
    }
    assert(std::all_of(dormant_coverage.begin(), dormant_coverage.end(),
                       [](bool covered) { return covered; }));
    assert(canonical_shadow_dormant_source(0U, 0U) == 0U);
    assert(shadow_page_vertex_interpolation(make_shadow_group_identity(1U)));
    assert(!shadow_page_vertex_interpolation(kIgnoredIdentity));

    constexpr ShadowTexcoordBounds rigid_bounds{
        -100, -80, 100, 80, true};
    constexpr std::array<ShadowTexcoordSample, 4> rigid_base_uvs{{
        {-100, -80, true}, {100, -80, true},
        {100, 80, true}, {-100, 80, true},
    }};
    const auto rigid_position = [](const ShadowTexcoordSample& uv) {
        return ShadowVertexSample{
            10.0F + 0.5F * uv.s + 0.1F * uv.t,
            20.0F + 0.02F * uv.s - 0.03F * uv.t,
            -5.0F - 0.2F * uv.s + 0.4F * uv.t,
        };
    };
    std::array<ShadowVertexSample, 4> rigid_base_vertices{};
    for (std::size_t i = 0U; i < rigid_base_uvs.size(); ++i) {
        rigid_base_vertices[i] = rigid_position(rigid_base_uvs[i]);
    }
    const auto rigid_base_quad = rigid_shadow_quad(
        rigid_base_uvs, rigid_base_vertices, rigid_bounds);
    assert(rigid_base_quad.valid);

    // Crossing a terrain seam can add four clipped vertices to the authored
    // mesh. The Modern racer proxy must still resolve to the exact same four
    // corners instead of changing topology or silhouette.
    constexpr std::array<ShadowTexcoordSample, 8> rigid_split_uvs{{
        {-100, -80, true}, {100, -80, true},
        {100, 80, true}, {-100, 80, true},
        {0, -80, true}, {100, 0, true},
        {0, 80, true}, {-100, 0, true},
    }};
    std::array<ShadowVertexSample, 8> rigid_split_vertices{};
    for (std::size_t i = 0U; i < rigid_split_uvs.size(); ++i) {
        rigid_split_vertices[i] = rigid_position(rigid_split_uvs[i]);
    }
    const auto rigid_split_quad = rigid_shadow_quad(
        rigid_split_uvs, rigid_split_vertices, rigid_bounds);
    assert(rigid_split_quad.valid);
    for (std::size_t corner = 0U; corner < 4U; ++corner) {
        assert(std::abs(rigid_split_quad.vertices[corner].x -
                        rigid_base_quad.vertices[corner].x) < 0.001F);
        assert(std::abs(rigid_split_quad.vertices[corner].y -
                        rigid_base_quad.vertices[corner].y) < 0.001F);
        assert(std::abs(rigid_split_quad.vertices[corner].z -
                        rigid_base_quad.vertices[corner].z) < 0.001F);
        assert(rigid_split_quad.texcoords[corner].s ==
               rigid_base_quad.texcoords[corner].s);
        assert(rigid_split_quad.texcoords[corner].t ==
               rigid_base_quad.texcoords[corner].t);
    }

    // func_8002F440 stores yaw in each triangle-corner UV. A tunnel may submit
    // a floor and roof with identical UV coverage; the Modern proxy must retain
    // that exact X/Z orientation while selecting one real ground receiver,
    // rather than averaging the two heights into a nonexistent plane.
    constexpr ShadowTexcoordBounds decomp_receiver_bounds{
        0, 0, 1024, 1024, true};
    const auto decomp_receiver_position = [](
        const ShadowTexcoordSample& uv, float y) {
        return ShadowVertexSample{
            100.0F + 0.25F * uv.t,
            y,
            50.0F - 0.25F * uv.s,
        };
    };
    const std::array<ShadowTexcoordSample, 3> decomp_triangle_a_uv{{
        {0, 0, true}, {1024, 0, true}, {1024, 1024, true},
    }};
    const std::array<ShadowTexcoordSample, 3> decomp_triangle_b_uv{{
        {0, 0, true}, {1024, 1024, true}, {0, 1024, true},
    }};
    std::array<ShadowTriangleSample, 4> stacked_decomp_triangles{};
    stacked_decomp_triangles[0].texcoords = decomp_triangle_a_uv;
    stacked_decomp_triangles[1].texcoords = decomp_triangle_b_uv;
    stacked_decomp_triangles[2].texcoords = decomp_triangle_a_uv;
    stacked_decomp_triangles[3].texcoords = decomp_triangle_b_uv;
    for (std::size_t triangle = 0U;
         triangle < stacked_decomp_triangles.size(); ++triangle) {
        const float receiver_y = triangle < 2U ? 82.0F : 2.0F;
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            stacked_decomp_triangles[triangle].vertices[corner] =
                decomp_receiver_position(
                    stacked_decomp_triangles[triangle].texcoords[corner],
                    receiver_y);
        }
    }
    std::array<ShadowTexcoordSample, 12> stacked_decomp_uvs{};
    std::array<ShadowVertexSample, 12> stacked_decomp_vertices{};
    std::size_t stacked_corner = 0U;
    for (const auto& triangle : stacked_decomp_triangles) {
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            stacked_decomp_uvs[stacked_corner] = triangle.texcoords[corner];
            stacked_decomp_vertices[stacked_corner] =
                triangle.vertices[corner];
            ++stacked_corner;
        }
    }
    const auto decomp_ground_receiver = decomp_shadow_receiver_quad(
        stacked_decomp_uvs, stacked_decomp_vertices,
        stacked_decomp_triangles, decomp_receiver_bounds,
        200.0F, true, 2.0F, true);
    assert(decomp_ground_receiver.valid);
    assert(decomp_ground_receiver.centre_covered);
    assert(decomp_ground_receiver.triangle_index >= 2U);
    for (const auto& vertex : decomp_ground_receiver.quad.vertices) {
        assert(std::abs(vertex.y - 2.0F) < 0.001F);
    }
    // S increases toward negative world Z and T toward positive world X in
    // this authored sample. The selected receiver must not replace that yaw.
    assert(std::abs(decomp_ground_receiver.quad.vertices[1].x -
                    decomp_ground_receiver.quad.vertices[0].x) < 0.001F);
    assert(std::abs((decomp_ground_receiver.quad.vertices[1].z -
                     decomp_ground_receiver.quad.vertices[0].z) + 256.0F) <
           0.001F);
    assert(std::abs((decomp_ground_receiver.quad.vertices[3].x -
                     decomp_ground_receiver.quad.vertices[0].x) - 256.0F) <
           0.001F);
    assert(std::abs(decomp_ground_receiver.quad.vertices[3].z -
                    decomp_ground_receiver.quad.vertices[0].z) < 0.001F);
    const auto first_decomp_receiver = decomp_shadow_receiver_quad(
        stacked_decomp_uvs, stacked_decomp_vertices,
        stacked_decomp_triangles, decomp_receiver_bounds,
        200.0F, true);
    assert(first_decomp_receiver.valid);
    assert(std::abs(first_decomp_receiver.centre_y - 2.0F) < 0.001F);
    const auto roof_above_owner_receiver = decomp_shadow_receiver_quad(
        stacked_decomp_uvs, stacked_decomp_vertices,
        stacked_decomp_triangles, decomp_receiver_bounds,
        40.0F, true, 2.0F, true);
    assert(roof_above_owner_receiver.valid);
    assert(std::abs(roof_above_owner_receiver.centre_y - 2.0F) < 0.001F);
    const auto only_overhead_receiver = decomp_shadow_receiver_quad(
        std::span<const ShadowTexcoordSample>(
            stacked_decomp_uvs.data(), 6U),
        std::span<const ShadowVertexSample>(
            stacked_decomp_vertices.data(), 6U),
        std::span<const ShadowTriangleSample>(
            stacked_decomp_triangles.data(), 2U),
        decomp_receiver_bounds, 40.0F, true, 2.0F, true);
    assert(!only_overhead_receiver.valid);
    assert(only_overhead_receiver.only_above_owner);
    static_assert(rigid_shadow_source_history_is_fresh(108U, 100U));
    static_assert(!rigid_shadow_source_history_is_fresh(109U, 100U));
    static_assert(!rigid_shadow_source_history_is_fresh(100U, 0U));
    static_assert(taj_shadow_source_history_is_fresh(101U, 100U));
    static_assert(!taj_shadow_source_history_is_fresh(102U, 100U));
    static_assert(!taj_shadow_source_history_is_fresh(100U, 0U));

    // A segment-order change at a seam must not change Taj's chosen ground
    // receiver or the authored X/Z footprint.
    const std::array<ShadowTriangleSample, 4> reordered_decomp_triangles{{
        stacked_decomp_triangles[3], stacked_decomp_triangles[0],
        stacked_decomp_triangles[2], stacked_decomp_triangles[1],
    }};
    std::array<ShadowTexcoordSample, 12> reordered_decomp_uvs{};
    std::array<ShadowVertexSample, 12> reordered_decomp_vertices{};
    std::size_t reordered_corner = 0U;
    for (const auto& triangle : reordered_decomp_triangles) {
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            reordered_decomp_uvs[reordered_corner] =
                triangle.texcoords[corner];
            reordered_decomp_vertices[reordered_corner] =
                triangle.vertices[corner];
            ++reordered_corner;
        }
    }
    const auto reordered_ground_receiver = decomp_shadow_receiver_quad(
        reordered_decomp_uvs, reordered_decomp_vertices,
        reordered_decomp_triangles, decomp_receiver_bounds,
        200.0F, true, 2.0F, true);
    assert(reordered_ground_receiver.valid);
    assert(std::abs(reordered_ground_receiver.centre_y - 2.0F) < 0.001F);
    for (std::size_t corner = 0U;
         corner < reordered_ground_receiver.quad.vertices.size(); ++corner) {
        assert(std::abs(reordered_ground_receiver.quad.vertices[corner].x -
                        decomp_ground_receiver.quad.vertices[corner].x) <
               0.001F);
        assert(std::abs(reordered_ground_receiver.quad.vertices[corner].y -
                        decomp_ground_receiver.quad.vertices[corner].y) <
               0.001F);
        assert(std::abs(reordered_ground_receiver.quad.vertices[corner].z -
                        decomp_ground_receiver.quad.vertices[corner].z) <
               0.001F);
    }

    // A rigid Taj proxy must retain the selected receiver's slope. Replacing
    // these four authored heights with one centre Y makes the rectangle
    // intersect the terrain at opposite corners and reproduces the observed
    // depth fighting on uneven ground.
    std::array<ShadowTriangleSample, 2> sloped_decomp_triangles{};
    sloped_decomp_triangles[0].texcoords = decomp_triangle_a_uv;
    sloped_decomp_triangles[1].texcoords = decomp_triangle_b_uv;
    std::array<ShadowTexcoordSample, 6> sloped_decomp_uvs{};
    std::array<ShadowVertexSample, 6> sloped_decomp_vertices{};
    std::size_t sloped_corner = 0U;
    for (auto& triangle : sloped_decomp_triangles) {
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const auto& uv = triangle.texcoords[corner];
            const float receiver_y = 2.0F +
                0.01F * static_cast<float>(uv.s) +
                0.005F * static_cast<float>(uv.t);
            triangle.vertices[corner] =
                decomp_receiver_position(uv, receiver_y);
            sloped_decomp_uvs[sloped_corner] = uv;
            sloped_decomp_vertices[sloped_corner] =
                triangle.vertices[corner];
            ++sloped_corner;
        }
    }
    const auto sloped_decomp_receiver = decomp_shadow_receiver_quad(
        sloped_decomp_uvs, sloped_decomp_vertices,
        sloped_decomp_triangles, decomp_receiver_bounds,
        200.0F, true);
    assert(sloped_decomp_receiver.valid);
    float minimum_sloped_y =
        sloped_decomp_receiver.quad.vertices.front().y;
    float maximum_sloped_y = minimum_sloped_y;
    for (const auto& vertex : sloped_decomp_receiver.quad.vertices) {
        minimum_sloped_y = std::min(minimum_sloped_y, vertex.y);
        maximum_sloped_y = std::max(maximum_sloped_y, vertex.y);
    }
    assert(maximum_sloped_y - minimum_sloped_y > 15.0F);

    // Taj's footprint can cover two non-coplanar terrain sections at once. A
    // single selected plane necessarily passes through one of them, whereas
    // the fixed 13-point surface must follow both sections without accepting
    // an overlapping roof or changing topology when segment order changes.
    const std::array<std::array<ShadowTexcoordSample, 3>, 4>
        creased_ground_uvs{{
            {{{0, 0, true}, {512, 0, true}, {512, 1024, true}}},
            {{{0, 0, true}, {512, 1024, true}, {0, 1024, true}}},
            {{{512, 0, true}, {1024, 0, true}, {1024, 1024, true}}},
            {{{512, 0, true}, {1024, 1024, true}, {512, 1024, true}}},
        }};
    const auto creased_ground_y = [](std::int16_t s) {
        const float coordinate = static_cast<float>(s);
        return coordinate <= 512.0F
            ? 2.0F + 0.01F * coordinate
            : 7.12F + 0.03F * (coordinate - 512.0F);
    };
    std::array<ShadowTriangleSample, 8> creased_triangles{};
    for (std::size_t layer = 0U; layer < 2U; ++layer) {
        for (std::size_t triangle = 0U;
             triangle < creased_ground_uvs.size(); ++triangle) {
            auto& destination =
                creased_triangles[layer * creased_ground_uvs.size() +
                                  triangle];
            destination.texcoords = creased_ground_uvs[triangle];
            for (std::size_t corner = 0U; corner < 3U; ++corner) {
                const auto uv = destination.texcoords[corner];
                destination.vertices[corner] = decomp_receiver_position(
                    uv, creased_ground_y(uv.s) +
                        (layer == 0U ? 80.0F : 0.0F));
            }
        }
    }
    std::array<ShadowTexcoordSample, 24> creased_corner_uvs{};
    std::array<ShadowVertexSample, 24> creased_corner_vertices{};
    std::size_t creased_corner = 0U;
    for (const auto& triangle : creased_triangles) {
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            creased_corner_uvs[creased_corner] = triangle.texcoords[corner];
            creased_corner_vertices[creased_corner] =
                triangle.vertices[corner];
            ++creased_corner;
        }
    }
    const auto creased_receiver = decomp_shadow_receiver_quad(
        creased_corner_uvs, creased_corner_vertices, creased_triangles,
        decomp_receiver_bounds, 200.0F, true);
    assert(creased_receiver.valid);
    const auto creased_surface = taj_shadow_receiver_surface(
        creased_receiver, creased_triangles, decomp_receiver_bounds,
        200.0F, true);
    assert(creased_surface.valid);
    static_assert(kTajShadowSurfaceVertexCount == 13U);
    static_assert(kTajShadowSurfaceTriangleCount == 16U);
    for (std::size_t point = 0U; point < creased_surface.vertices.size();
         ++point) {
        const auto uv = creased_surface.texcoords[point];
        assert(uv.valid);
        assert(std::abs(creased_surface.vertices[point].y -
                        creased_ground_y(uv.s)) < 0.001F);
        assert(creased_surface.vertices[point].y < 30.0F);
    }
    assert(creased_surface.vertices[2].y >
           creased_receiver.quad.vertices[1].y + 9.0F);

    const std::array<ShadowTriangleSample, 8> reordered_creased_triangles{{
        creased_triangles[7], creased_triangles[2],
        creased_triangles[4], creased_triangles[1],
        creased_triangles[6], creased_triangles[0],
        creased_triangles[5], creased_triangles[3],
    }};
    std::array<ShadowTexcoordSample, 24> reordered_creased_uvs{};
    std::array<ShadowVertexSample, 24> reordered_creased_vertices{};
    creased_corner = 0U;
    for (const auto& triangle : reordered_creased_triangles) {
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            reordered_creased_uvs[creased_corner] =
                triangle.texcoords[corner];
            reordered_creased_vertices[creased_corner] =
                triangle.vertices[corner];
            ++creased_corner;
        }
    }
    const auto reordered_creased_receiver = decomp_shadow_receiver_quad(
        reordered_creased_uvs, reordered_creased_vertices,
        reordered_creased_triangles, decomp_receiver_bounds,
        200.0F, true);
    const auto reordered_creased_surface = taj_shadow_receiver_surface(
        reordered_creased_receiver, reordered_creased_triangles,
        decomp_receiver_bounds, 200.0F, true);
    assert(reordered_creased_surface.valid);
    for (std::size_t point = 0U; point < creased_surface.vertices.size();
         ++point) {
        assert(std::abs(reordered_creased_surface.vertices[point].x -
                        creased_surface.vertices[point].x) < 0.001F);
        assert(std::abs(reordered_creased_surface.vertices[point].y -
                        creased_surface.vertices[point].y) < 0.001F);
        assert(std::abs(reordered_creased_surface.vertices[point].z -
                        creased_surface.vertices[point].z) < 0.001F);
    }

    constexpr std::array<ShadowTexcoordSample, 3> rigid_degenerate_uvs{{
        {0, 0, true}, {10, 0, true}, {20, 0, true},
    }};
    constexpr std::array<ShadowVertexSample, 3> rigid_degenerate_vertices{{
        {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
        {2.0F, 0.0F, 0.0F},
    }};
    assert(!rigid_shadow_quad(rigid_degenerate_uvs,
                              rigid_degenerate_vertices).valid);
    static_assert(shadow_behaviour_uses_rigid_actor_proxy(1U));
    static_assert(shadow_behaviour_uses_rigid_actor_proxy(56U));
    static_assert(shadow_behaviour_uses_rigid_actor_proxy(62U));
    static_assert(shadow_behaviour_uses_rigid_actor_proxy(80U));
    static_assert(!shadow_behaviour_uses_rigid_actor_proxy(2U));
    // BHV_ANIMATED_OBJECT is deliberately too broad: MagicCarpet shares it
    // with 24 other shadow-bearing cutscene actors. Exact object ownership is
    // required before admitting behaviour 50 to the rigid proxy.
    static_assert(!shadow_behaviour_uses_rigid_actor_proxy(50U));
    static_assert(!shadow_behaviour_is_park_warden_actor(1U));
    static_assert(!shadow_behaviour_is_park_warden_actor(56U));
    static_assert(shadow_behaviour_is_park_warden_actor(62U));
    static_assert(shadow_behaviour_is_park_warden_actor(80U));
    static_assert(shadow_owner_is_magic_carpet(50U, 120U));
    static_assert(shadow_owner_is_magic_carpet(50U, 120U | 0x200U));
    static_assert(!shadow_owner_is_magic_carpet(50U, 119U));
    static_assert(!shadow_owner_is_magic_carpet(50U, 150U));
    static_assert(!shadow_owner_is_magic_carpet(50U, 203U));
    static_assert(!shadow_owner_is_magic_carpet(50U, 300U));
    static_assert(!shadow_owner_is_magic_carpet(62U, 120U));
    static_assert(rigid_shadow_owner_policy(1U, 2).plane());
    static_assert(!rigid_shadow_owner_policy(1U, 0).plane());
    static_assert(rigid_shadow_owner_policy(1U, 2, 32U)
                      .authored_texcoord_bounds()
                      .maximum_s == 1024);
    static_assert(rigid_shadow_owner_policy(1U, 2, 32U)
                      .authored_texcoord_bounds()
                      .maximum_t == 1024);
    static_assert(rigid_shadow_owner_policy(56U).required());
    static_assert(rigid_shadow_owner_policy(62U).taj());
    static_assert(rigid_shadow_owner_policy(80U).taj());
    constexpr auto magic_carpet_policy =
        rigid_shadow_owner_policy(50U, -1, 64U, 120U);
    static_assert(magic_carpet_policy.required());
    static_assert(magic_carpet_policy.magic_carpet());
    static_assert(!magic_carpet_policy.taj());
    static_assert(magic_carpet_policy.terrain_conforming_actor());
    static_assert(magic_carpet_policy.object_id == 120U);
    static_assert(magic_carpet_policy.authored_texcoord_bounds().maximum_s ==
                  2048);
    static_assert(magic_carpet_policy.authored_texcoord_bounds().maximum_t ==
                  2048);
    static_assert(!rigid_shadow_owner_policy(50U, -1, 64U, 119U).required());
    static_assert(!rigid_shadow_owner_policy(50U, -1, 64U, 150U).required());
    static_assert(!rigid_shadow_owner_policy(50U, -1, 64U, 203U).required());
    static_assert(!rigid_shadow_owner_policy(50U, -1, 64U, 300U).required());
    static_assert(!rigid_shadow_owner_policy(2U).required());
    static_assert(rigid_shadow_policy_inspects_decomp_receiver(
        rigid_shadow_owner_policy(1U, 0)));
    static_assert(!rigid_shadow_policy_uses_decomp_receiver(
        rigid_shadow_owner_policy(1U, 0)));
    static_assert(rigid_shadow_policy_uses_decomp_receiver(
        rigid_shadow_owner_policy(1U, 2)));
    static_assert(rigid_shadow_policy_inspects_decomp_receiver(
        rigid_shadow_owner_policy(56U)));
    static_assert(rigid_shadow_policy_uses_decomp_receiver(
        rigid_shadow_owner_policy(56U)));
    static_assert(rigid_shadow_policy_inspects_decomp_receiver(
        rigid_shadow_owner_policy(62U)));
    static_assert(rigid_shadow_policy_uses_decomp_receiver(
        rigid_shadow_owner_policy(62U)));
    static_assert(rigid_shadow_policy_inspects_decomp_receiver(
        rigid_shadow_owner_policy(80U)));
    static_assert(rigid_shadow_policy_uses_decomp_receiver(
        rigid_shadow_owner_policy(80U)));
    static_assert(rigid_shadow_policy_inspects_decomp_receiver(
        magic_carpet_policy));
    static_assert(rigid_shadow_policy_uses_decomp_receiver(
        magic_carpet_policy));
    static_assert(!rigid_shadow_policy_inspects_decomp_receiver(
        rigid_shadow_owner_policy(2U)));

    auto rounded_rigid_quad = rigid_base_quad;
    for (auto& vertex : rounded_rigid_quad.vertices) {
        vertex.y -= 1.0F;
    }
    const float rounded_clearance = rigid_shadow_vertical_clearance(
        rigid_base_uvs, rigid_base_vertices, rounded_rigid_quad);
    assert(rounded_clearance > 0.99F && rounded_clearance < 1.01F);

    const auto rigid_reference_quad = rigid_shadow_fixed_shape_quad(
        rigid_base_quad, rigid_base_quad);
    assert(rigid_reference_quad.valid);
    auto rigid_deformed_quad = rigid_split_quad;
    rigid_deformed_quad.vertices[0].x -= 2.5F;
    rigid_deformed_quad.vertices[1].y += 1.75F;
    rigid_deformed_quad.vertices[2].z += 3.25F;
    rigid_deformed_quad.vertices[3].y -= 1.25F;
    const auto deformed_metrics = rigid_shadow_shape_metrics(
        rigid_deformed_quad);
    const auto rigid_locked_quad = rigid_shadow_fixed_shape_quad(
        rigid_deformed_quad, rigid_reference_quad);
    const auto rigid_locked_metrics = rigid_shadow_shape_metrics(
        rigid_locked_quad);
    const auto rigid_reference_metrics = rigid_shadow_shape_metrics(
        rigid_reference_quad);
    assert(rigid_locked_metrics.valid);
    for (std::size_t edge = 0U;
         edge < rigid_locked_metrics.edge_lengths.size(); ++edge) {
        assert(std::abs(rigid_locked_metrics.edge_lengths[edge] -
                        rigid_reference_metrics.edge_lengths[edge]) < 0.001F);
    }
    for (std::size_t diagonal = 0U;
         diagonal < rigid_locked_metrics.diagonal_lengths.size(); ++diagonal) {
        assert(std::abs(rigid_locked_metrics.diagonal_lengths[diagonal] -
                        rigid_reference_metrics.diagonal_lengths[diagonal]) <
               0.001F);
    }
    assert(std::abs(rigid_locked_metrics.centre.x -
                    deformed_metrics.centre.x) < 0.001F);
    assert(std::abs(rigid_locked_metrics.centre.y -
                    deformed_metrics.centre.y) < 0.001F);
    assert(std::abs(rigid_locked_metrics.centre.z -
                    deformed_metrics.centre.z) < 0.001F);
    assert(std::abs(rigid_locked_metrics.adjacent_edge_cosine) < 0.00001F);
    const auto rigid_translated_quad = translated_rigid_shadow_quad(
        rigid_locked_quad, {7.0F, -3.0F, 11.0F});
    const auto rigid_translated_metrics = rigid_shadow_shape_metrics(
        rigid_translated_quad);
    assert(std::abs(rigid_translated_metrics.centre.x -
                    (rigid_locked_metrics.centre.x + 7.0F)) < 0.001F);
    assert(std::abs(rigid_translated_metrics.centre.y -
                    (rigid_locked_metrics.centre.y - 3.0F)) < 0.001F);
    assert(std::abs(rigid_translated_metrics.centre.z -
                    (rigid_locked_metrics.centre.z + 11.0F)) < 0.001F);
    assert(std::abs(rigid_translated_metrics.area -
                    rigid_locked_metrics.area) < 0.001F);
    const float trusted_horizontal_size =
        rigid_shadow_horizontal_size(rigid_translated_quad);
    const auto rigid_resized_quad = rigid_shadow_horizontal_resized_quad(
        rigid_translated_quad, trusted_horizontal_size * 1.125F);
    const auto rigid_resized_metrics =
        rigid_shadow_shape_metrics(rigid_resized_quad);
    assert(rigid_resized_metrics.valid);
    assert(std::abs(rigid_shadow_horizontal_size(rigid_resized_quad) -
                    trusted_horizontal_size * 1.125F) < 0.001F);
    assert(std::abs(rigid_resized_metrics.centre.x -
                    rigid_translated_metrics.centre.x) < 0.001F);
    assert(std::abs(rigid_resized_metrics.centre.y -
                    rigid_translated_metrics.centre.y) < 0.001F);
    assert(std::abs(rigid_resized_metrics.centre.z -
                    rigid_translated_metrics.centre.z) < 0.001F);
    assert(std::abs(rigid_resized_metrics.adjacent_edge_cosine -
                    rigid_translated_metrics.adjacent_edge_cosine) < 0.00001F);
    const auto rigid_authored_scale_quad =
        rigid_shadow_authored_scale_quad(rigid_deformed_quad);
    assert(rigid_authored_scale_quad.valid);
    const auto authored_s0 = ShadowVertexSample{
        rigid_authored_scale_quad.vertices[1].x -
            rigid_authored_scale_quad.vertices[0].x,
        0.0F,
        rigid_authored_scale_quad.vertices[1].z -
            rigid_authored_scale_quad.vertices[0].z};
    const auto authored_t0 = ShadowVertexSample{
        rigid_authored_scale_quad.vertices[3].x -
            rigid_authored_scale_quad.vertices[0].x,
        0.0F,
        rigid_authored_scale_quad.vertices[3].z -
            rigid_authored_scale_quad.vertices[0].z};
    const float authored_s_length = std::sqrt(
        authored_s0.x * authored_s0.x + authored_s0.z * authored_s0.z);
    const float authored_t_length = std::sqrt(
        authored_t0.x * authored_t0.x + authored_t0.z * authored_t0.z);
    assert(std::abs(authored_s_length - authored_t_length) < 0.001F);
    constexpr float taj_receiver_y = 37.25F;
    const auto taj_horizontal_quad = rigid_shadow_horizontal_receiver_quad(
        rigid_authored_scale_quad, taj_receiver_y);
    assert(taj_horizontal_quad.valid);
    const auto taj_horizontal_metrics =
        rigid_shadow_shape_metrics(taj_horizontal_quad);
    assert(taj_horizontal_metrics.valid);
    for (const auto& vertex : taj_horizontal_quad.vertices) {
        assert(std::abs(vertex.y - taj_receiver_y) < 0.001F);
    }
    assert(std::abs(taj_horizontal_metrics.centre.x -
                    rigid_shadow_shape_metrics(rigid_authored_scale_quad)
                        .centre.x) < 0.001F);
    assert(std::abs(taj_horizontal_metrics.centre.z -
                    rigid_shadow_shape_metrics(rigid_authored_scale_quad)
                        .centre.z) < 0.001F);
    assert(std::abs(taj_horizontal_metrics.adjacent_edge_cosine) < 0.00001F);
    assert(std::abs(taj_horizontal_metrics.edge_lengths[0] -
                    taj_horizontal_metrics.edge_lengths[1]) < 0.001F);
    for (std::size_t corner = 0U;
         corner < taj_horizontal_quad.texcoords.size(); ++corner) {
        assert(taj_horizontal_quad.texcoords[corner].s ==
               rigid_authored_scale_quad.texcoords[corner].s);
        assert(taj_horizontal_quad.texcoords[corner].t ==
               rigid_authored_scale_quad.texcoords[corner].t);
        assert(taj_horizontal_quad.texcoords[corner].valid ==
               rigid_authored_scale_quad.texcoords[corner].valid);
    }
    const auto taj_integer_square =
        rigid_shadow_integer_square_quad(taj_horizontal_quad);
    const auto taj_integer_metrics =
        rigid_shadow_shape_metrics(taj_integer_square);
    assert(taj_integer_metrics.valid);
    for (const auto& vertex : taj_integer_square.vertices) {
        assert(vertex.x == std::round(vertex.x));
        assert(vertex.y == std::round(vertex.y));
        assert(vertex.z == std::round(vertex.z));
    }
    for (std::size_t edge = 1U;
         edge < taj_integer_metrics.edge_lengths.size(); ++edge) {
        assert(std::abs(taj_integer_metrics.edge_lengths[edge] -
                        taj_integer_metrics.edge_lengths[0]) < 0.001F);
    }
    assert(std::abs(taj_integer_metrics.adjacent_edge_cosine) < 0.00001F);
    assert(!rigid_shadow_scale_is_discontinuous(
        rigid_authored_scale_quad,
        translated_rigid_shadow_quad(
            rigid_authored_scale_quad, {20.0F, 5.0F, -30.0F})));
    auto oversized_shadow_quad = rigid_authored_scale_quad;
    const auto authored_centre =
        rigid_shadow_shape_metrics(rigid_authored_scale_quad).centre;
    for (auto& vertex : oversized_shadow_quad.vertices) {
        vertex.x = authored_centre.x +
            (vertex.x - authored_centre.x) * 1.5F;
        vertex.z = authored_centre.z +
            (vertex.z - authored_centre.z) * 1.5F;
    }
    assert(rigid_shadow_scale_is_discontinuous(
        rigid_authored_scale_quad, oversized_shadow_quad));
    auto implausible_receiver =
        rigid_shadow_shape_metrics(rigid_authored_scale_quad);
    implausible_receiver.edge_lengths = {30.0F, 80.0F, 30.0F, 80.0F};
    assert(rigid_shadow_receiver_fit_is_implausible(implausible_receiver));
    implausible_receiver.edge_lengths = {42.0F, 42.0F, 42.0F, 42.0F};
    implausible_receiver.adjacent_edge_cosine = 0.48F;
    assert(rigid_shadow_receiver_fit_is_implausible(implausible_receiver));
    const auto plausible_receiver =
        rigid_shadow_shape_metrics(rigid_authored_scale_quad);
    assert(!rigid_shadow_receiver_fit_is_implausible(plausible_receiver));
    assert(rigid_shadow_receiver_fit_is_near_square(plausible_receiver));
    auto non_square_receiver = plausible_receiver;
    non_square_receiver.edge_lengths = {41.4F, 30.2F, 41.4F, 30.2F};
    assert(!rigid_shadow_receiver_fit_is_near_square(non_square_receiver));
    auto nearby_receiver = plausible_receiver;
    nearby_receiver.centre.y += 3.0F;
    assert(rigid_shadow_receiver_can_reacquire(
        plausible_receiver, nearby_receiver, 2.0F));
    nearby_receiver.centre.y += 20.0F;
    assert(!rigid_shadow_receiver_can_reacquire(
        plausible_receiver, nearby_receiver, 2.0F));
    assert(!plane_shadow_receiver_is_upward_discontinuity(
        20.0F, 23.0F, 8.0F));
    assert(plane_shadow_receiver_is_upward_discontinuity(
        20.0F, 34.0F, 8.0F));
    assert(!plane_shadow_receiver_is_upward_discontinuity(
        34.0F, 20.0F, 1.0F));
    RigidShadowQuad yaw_zero_quad{};
    yaw_zero_quad.vertices = {{{0.0F, 2.0F, 0.0F},
                               {10.0F, 2.0F, 0.0F},
                               {10.0F, 2.0F, 10.0F},
                               {0.0F, 2.0F, 10.0F}}};
    yaw_zero_quad.texcoords = {{{0, 0, true}, {1024, 0, true},
                                {1024, 1024, true}, {0, 1024, true}}};
    yaw_zero_quad.valid = true;
    const auto plane_gap_quad = plane_shadow_missing_frame_quad(
        yaw_zero_quad, {3.0F, 4.0F, 5.0F}, 0,
        static_cast<std::int16_t>(0x4000U));
    const auto plane_gap_metrics = rigid_shadow_shape_metrics(plane_gap_quad);
    assert(plane_gap_metrics.valid);
    assert(std::abs(plane_gap_metrics.centre.x - 8.0F) < 0.001F);
    // Plane altitude is not receiver motion: a missing tunnel sample must keep
    // the last authored ground height while following horizontal motion/yaw.
    assert(std::abs(plane_gap_metrics.centre.y - 2.0F) < 0.001F);
    assert(std::abs(plane_gap_metrics.centre.z - 10.0F) < 0.001F);
    assert(std::abs(plane_gap_metrics.area - 100.0F) < 0.001F);
    assert(std::abs(rigid_shadow_horizontal_size(plane_gap_quad) - 10.0F) <
           0.001F);
    assert(std::abs(plane_gap_quad.vertices[1].x -
                    plane_gap_quad.vertices[0].x) < 0.001F);
    assert(std::abs((plane_gap_quad.vertices[1].z -
                     plane_gap_quad.vertices[0].z) + 10.0F) < 0.001F);

    const ShadowVertexSample bounded_previous{0.0F, 0.0F, 0.0F};
    const ShadowVertexSample bounded_current{210.0F, 0.0F, 0.0F};
    const ShadowVertexSample bounded_translation{10.0F, 0.0F, 0.0F};
    const auto bounded_birth = bounded_shadow_presentation_vertex(
        bounded_previous, bounded_current, bounded_translation, 50.0F);
    assert(std::abs(bounded_birth.x - 60.0F) < 0.001F);
    assert(std::abs(bounded_birth.y) < 0.001F);
    assert(std::abs(bounded_birth.z) < 0.001F);
    const auto bounded_near = bounded_shadow_presentation_vertex(
        bounded_previous, {40.0F, 3.0F, -2.0F}, bounded_translation, 50.0F);
    assert(std::abs(bounded_near.x - 40.0F) < 0.001F);
    assert(std::abs(bounded_near.y - 3.0F) < 0.001F);
    assert(std::abs(bounded_near.z + 2.0F) < 0.001F);

    std::array<ShadowTexcoordSample, 4> moving_previous_uvs =
        initial_shadow_uvs;
    const std::array<ShadowVertexSample, 4> moving_previous_positions{{
        {-100.0F, 2.0F, -100.0F}, {100.0F, 2.0F, -100.0F},
        {100.0F, 2.0F, 100.0F}, {-100.0F, 2.0F, 100.0F},
    }};
    const std::array<ShadowVertexSample, 4> moving_current_positions{{
        {140.0F, 5.0F, -170.0F}, {340.0F, 5.0F, -170.0F},
        {340.0F, 5.0F, 30.0F}, {140.0F, 5.0F, 30.0F},
    }};
    const auto measured_shadow_motion = shadow_cloud_translation(
        moving_previous_uvs, moving_previous_positions,
        moving_current_positions);
    assert(measured_shadow_motion.x == 240.0F);
    assert(measured_shadow_motion.y == 3.0F);
    assert(measured_shadow_motion.z == -70.0F);

    std::array<ShadowTexcoordSample,
               kCanonicalShadowBatchVertices> moving_previous_page{};
    std::array<ShadowVertexSample,
               kCanonicalShadowBatchVertices> moving_previous_page_positions{};
    for (std::size_t source = 0U; source < initial_shadow_uvs.size(); ++source) {
        const std::uint8_t slot = initial_shadow_slots.slot_for_source[source];
        moving_previous_page[slot] = moving_previous_uvs[source];
        moving_previous_page_positions[slot] = moving_previous_positions[source];
    }
    const auto moving_shadow_slots = canonical_shadow_motion_slot_map(
        moving_previous_page, moving_previous_page_positions,
        moving_previous_uvs, moving_current_positions, measured_shadow_motion);
    assert(canonical_shadow_slot_map_corresponds(
        moving_previous_page, moving_previous_page_positions,
        moving_current_positions, moving_shadow_slots,
        measured_shadow_motion, 8.0F));

    auto seam_outlier_positions = moving_current_positions;
    seam_outlier_positions[3].y += 48.0F;
    const auto seam_outlier_slots = canonical_shadow_motion_slot_map(
        moving_previous_page, moving_previous_page_positions,
        moving_previous_uvs, seam_outlier_positions, measured_shadow_motion);
    assert(!canonical_shadow_slot_map_corresponds(
        moving_previous_page, moving_previous_page_positions,
        seam_outlier_positions, seam_outlier_slots,
        measured_shadow_motion, 8.0F));

    std::array<ShadowCanonicalPage, kMaximumCanonicalShadowBatches>
        previous_shadow_pages{};
    previous_shadow_pages[0][0] = {-120, -20, true};
    previous_shadow_pages[0][1] = {-80, 20, true};
    previous_shadow_pages[1][0] = {80, -20, true};
    previous_shadow_pages[1][1] = {120, 20, true};
    std::array<ShadowCanonicalPage, 2> reordered_shadow_batches{};
    reordered_shadow_batches[0][0] = {82, -18, true};
    reordered_shadow_batches[0][1] = {118, 18, true};
    reordered_shadow_batches[1][0] = {-118, -18, true};
    reordered_shadow_batches[1][1] = {-82, 18, true};
    const auto reordered_shadow_pages = canonical_shadow_page_map(
        previous_shadow_pages, reordered_shadow_batches);
    assert(reordered_shadow_pages.page_for_batch[0] == 1U);
    assert(reordered_shadow_pages.page_for_batch[1] == 0U);

    std::array<ShadowCanonicalPage, 3> split_shadow_batches_by_page{};
    split_shadow_batches_by_page[0][0] = {-118, -18, true};
    split_shadow_batches_by_page[0][1] = {-82, 18, true};
    split_shadow_batches_by_page[1][0] = {0, 0, true};
    split_shadow_batches_by_page[2][0] = {82, -18, true};
    split_shadow_batches_by_page[2][1] = {118, 18, true};
    const auto split_shadow_pages = canonical_shadow_page_map(
        previous_shadow_pages, split_shadow_batches_by_page);
    assert(split_shadow_pages.page_for_batch[0] == 0U);
    assert(split_shadow_pages.page_for_batch[2] == 1U);
    assert(split_shadow_pages.page_for_batch[1] == 2U);

    std::array<ShadowCanonicalPage, kMaximumCanonicalShadowBatches>
        empty_shadow_pages{};
    std::array<ShadowCanonicalPage, 2> first_seen_shadow_batches{};
    first_seen_shadow_batches[0][0] = {100, 0, true};
    first_seen_shadow_batches[1][0] = {-100, 0, true};
    const auto first_seen_shadow_pages = canonical_shadow_page_map(
        empty_shadow_pages, first_seen_shadow_batches);
    assert(first_seen_shadow_pages.page_for_batch[1] == 0U);
    assert(first_seen_shadow_pages.page_for_batch[0] == 1U);

    const CameraContinuitySample camera_origin{
        0.0F, 100.0F, 200.0F, 60.0F, 0, 0, 0};
    const CameraContinuitySample camera_smooth{
        24.0F, 105.0F, 180.0F, 62.0F, 0x0200, -0x0100, 0x0080};
    const CameraContinuitySample camera_teleport{
        2000.0F, 100.0F, 200.0F, 60.0F, 0, 0, 0};
    const CameraContinuitySample camera_hard_turn{
        0.0F, 100.0F, 200.0F, 60.0F, 0x4000, 0, 0};
    const CameraContinuitySample camera_fov_cut{
        0.0F, 100.0F, 200.0F, 90.0F, 0, 0, 0};
    assert(!camera_sample_discontinuous(camera_origin, camera_smooth));
    assert(camera_sample_discontinuous(camera_origin, camera_teleport));
    // A fast authored turn is normal gameplay motion, not a camera cut. It
    // must preserve ordinary scenery interpolation identity.
    assert(!camera_sample_discontinuous(camera_origin, camera_hard_turn));
    assert(camera_sample_discontinuous(camera_origin, camera_fov_cut));
    assert(camera_angle_distance(static_cast<std::int16_t>(0x7F00),
                                 static_cast<std::int16_t>(0x8100)) ==
           0x0200U);

    constexpr ShadowBatchTopology shadow_material_a{0x001000U, 2U, 4U};
    constexpr ShadowBatchTopology shadow_material_b{0x002000U, 2U, 4U};
    constexpr ShadowBatchTopology shadow_shape_change{0x001000U, 3U, 4U};
    constexpr std::uint64_t shadow_hash_a = shadow_batch_topology_hash(
        kShadowTopologyHashOffset, shadow_material_a);
    constexpr std::uint64_t shadow_hash_b = shadow_batch_topology_hash(
        kShadowTopologyHashOffset, shadow_material_b);
    constexpr std::uint64_t shadow_hash_shape = shadow_batch_topology_hash(
        kShadowTopologyHashOffset, shadow_shape_change);
    static_assert(shadow_hash_a == shadow_hash_b,
                  "shadow material changes are not topology changes");
    static_assert(shadow_hash_a != shadow_hash_shape,
                  "shadow geometry changes must alter the topology hash");
    static_assert(!shadow_topology_epoch_should_advance(
        false, shadow_hash_a, shadow_hash_shape));
    static_assert(!shadow_topology_epoch_should_advance(
        true, shadow_hash_a, shadow_hash_a));
    static_assert(shadow_topology_epoch_should_advance(
        true, shadow_hash_a, shadow_hash_shape));

    constexpr std::array<ShadowVertexSample, 4> previous{{
        {0.0F, 2.0F, 0.0F}, {10.0F, 2.0F, 0.0F},
        {10.0F, 2.0F, 10.0F}, {0.0F, 2.0F, 10.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> translated{{
        {4.0F, 2.0F, -3.0F}, {14.0F, 2.0F, -3.0F},
        {14.0F, 2.0F, 7.0F}, {4.0F, 2.0F, 7.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> reordered{{
        {14.0F, 2.0F, -3.0F}, {4.0F, 2.0F, -3.0F},
        {14.0F, 2.0F, 7.0F}, {4.0F, 2.0F, 7.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> rotated{{
        {14.0F, 2.0F, -3.0F}, {14.0F, 2.0F, 7.0F},
        {4.0F, 2.0F, 7.0F}, {4.0F, 2.0F, -3.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> gently_deformed{{
        {4.0F, 2.0F, -3.0F}, {13.5F, 2.0F, -2.0F},
        {13.0F, 2.0F, 7.0F}, {4.0F, 2.0F, 6.5F},
    }};
    constexpr std::array<ShadowVertexSample, 4> scaled{{
        {-1.0F, 2.0F, -8.0F}, {19.0F, 2.0F, -8.0F},
        {19.0F, 2.0F, 12.0F}, {-1.0F, 2.0F, 12.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> terrain_jump{{
        {4.0F, 12.0F, -3.0F}, {14.0F, 12.0F, -3.0F},
        {14.0F, 12.0F, 7.0F}, {4.0F, 12.0F, 7.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> coherent_slope_projection{{
        {5.0F, 4.0F, -3.0F}, {15.0F, 4.5F, -3.0F},
        {15.0F, 5.0F, 7.0F}, {5.0F, 4.5F, 7.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> fast_steering_projection{{
        {6.0F, 14.0F, -5.0F}, {16.0F, 15.0F, -4.0F},
        {15.0F, 16.0F, 6.0F}, {5.0F, 15.0F, 5.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> unrelated_projection_jump{{
        {4.0F, 66.0F, -3.0F}, {14.0F, 66.0F, -3.0F},
        {14.0F, 66.0F, 7.0F}, {4.0F, 66.0F, 7.0F},
    }};
    constexpr std::array<ShadowVertexSample, 4> isolated_outlier{{
        {4.0F, 2.0F, -3.0F}, {14.0F, 2.0F, -3.0F},
        {14.0F, 2.0F, 7.0F}, {4.0F, 14.0F, 7.0F},
    }};
    constexpr std::array<std::uint16_t, 1> batches{{4U}};
    constexpr std::array<std::uint16_t, 2> split_batches{{2U, 2U}};
    constexpr std::array<std::uint16_t, 1> incomplete_batches{{3U}};
    assert(shadow_geometry_corresponds(previous, translated, batches,
                                       4.0F, -3.0F, 8.0F));
    assert(shadow_geometry_corresponds(previous, gently_deformed, batches,
                                       4.0F, -3.0F, 3.0F));
    assert(shadow_geometry_corresponds(previous, coherent_slope_projection,
                                       batches, 4.0F, -3.0F, 4.0F));
    assert(shadow_geometry_corresponds(previous, coherent_slope_projection,
                                       split_batches, 4.0F, -3.0F, 4.0F));
    assert(shadow_geometry_corresponds(previous, fast_steering_projection,
                                       batches, 4.0F, -3.0F, 16.0F));
    assert(!shadow_geometry_corresponds(previous, rotated, batches,
                                        4.0F, -3.0F, 8.0F));
    assert(!shadow_geometry_corresponds(previous, reordered, batches,
                                         4.0F, -3.0F, 8.0F));
    assert(!shadow_geometry_corresponds(previous, scaled, batches,
                                         4.0F, -3.0F, 6.0F));
    assert(!shadow_geometry_corresponds(previous, terrain_jump, batches,
                                         4.0F, -3.0F, 8.0F));
    assert(!shadow_geometry_corresponds(previous, unrelated_projection_jump,
                                         batches, 4.0F, -3.0F, 24.0F));
    assert(!shadow_geometry_corresponds(previous, isolated_outlier, batches,
                                         4.0F, -3.0F, 4.0F));
    assert(!shadow_geometry_corresponds(previous, translated,
                                         incomplete_batches,
                                         4.0F, -3.0F, 8.0F));
    std::puts("[test][presentation-identity] PASS");
    return 0;
}
