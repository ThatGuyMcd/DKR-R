#include "f3ddkr_rt64.hpp"
#include "postrace_viewport_rt64.hpp"

#include "interpolation_state_policy.hpp"
#include "hud_layout_policy.hpp"
#include "hud_group_rt64.hpp"
#include "hud_rectangle_layout.hpp"
#include "hud_projection_rt64.hpp"
#include "hud_reference_layout.hpp"
#include "presentation_identity.hpp"
#include "revision_addresses.hpp"
#include "runtime_enhancements.hpp"
#include "widescreen_policy.hpp"
#include "split_screen_rt64.hpp"
#include "water_uv_rt64.hpp"
#include "water_performance.hpp"
#include "water_scroll_policy.hpp"

#include "gbi/rt64_f3d.h"
#include "gbi/rt64_gbi_f3d.h"
#include "gbi/rt64_gbi_rdp.h"
#include "hle/rt64_application.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_state.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <unordered_map>

namespace {

constexpr std::uint8_t kMatrixOpcode = 0x01;
constexpr std::uint8_t kTextureOffsetOpcode = 0x02;
constexpr std::uint8_t kMoveMemOpcode = 0x03;
constexpr std::uint8_t kVertexOpcode = 0x04;
constexpr std::uint8_t kTriangleOpcode = 0x05;
constexpr std::uint8_t kDisplayListOpcode = 0x06;
constexpr std::uint8_t kCountedDisplayListOpcode = 0x07;
constexpr std::uint8_t kDMAOffsetsOpcode = 0xBF;
constexpr std::uint8_t kMoveWordOpcode = 0xBC;
constexpr std::uint8_t kSetTextureImageOpcode = 0xFD;
constexpr std::uint8_t kLoadBlockOpcode = 0xF3;
constexpr std::uint8_t kFillRectOpcode = 0xF6;
constexpr std::uint8_t kEndDisplayListOpcode = 0xB8;
constexpr std::uint8_t kMoveWordBillboard = 0x02;
constexpr std::uint8_t kMoveWordMVPMatrix = 0x0A;
constexpr std::uint8_t kMoveWordPresentationGroup = 0xFE;
constexpr std::uint32_t kPresentationGroupMagic = 0x444B5200U;
constexpr std::uint32_t kPresentationGroupMetadataMask = 0xFFU;
constexpr std::uint32_t kPresentationGroupModeMask = 7U;
constexpr std::uint32_t kPresentationGroupShadowMode =
    dkr::runtime::interpolation::kShadowScopeMode;
constexpr std::uint32_t kPresentationGroupVehiclePartMode =
    dkr::runtime::interpolation::kVehiclePartScopeMode;
constexpr std::uint32_t kPresentationGroupAspectAdjustMode =
    dkr::runtime::interpolation::kAspectAdjustScopeMode;
constexpr std::uint32_t kPresentationGroupAspectOriginalMode =
    dkr::runtime::interpolation::kAspectOriginalScopeMode;
constexpr std::uint32_t kPresentationGroupBillboardMode =
    dkr::runtime::interpolation::kBillboardScopeMode;
constexpr std::uint32_t kPresentationGroupSurfaceMode =
    dkr::runtime::interpolation::kSurfaceScopeMode;
constexpr std::uint32_t kPresentationGroupLevelSegmentMode =
    dkr::runtime::interpolation::kLevelSegmentScopeMode;
constexpr std::uint8_t kSetScissorOpcode = 0xEDU;
constexpr std::uint8_t kFullSyncOpcode = 0xE9U;
constexpr std::uint8_t kViewportMoveMemType = 0x80U;
using dkr::runtime::presentation::PresentationMarkerKind;
constexpr std::uint32_t kHudPassMarkerBeginMode = 1U;
constexpr float kSplitViewportCoverQuantisation = 1024.0F;
constexpr std::uint32_t kRDRAMAddressMask = 0x00FFFFFFU;
constexpr std::uint32_t kRDRAMSize = 0x00800000U;
const std::uint32_t& kCurrentMenuIdAddress =
    dkr::runtime::revision_addresses::CurrentMenuId;
constexpr std::uint32_t kScratchVertexAddress = 0x007FE000U;
constexpr std::uint32_t kMaxDKRVertices = 32;
constexpr std::uint32_t kMaxNestedDisplayLists = 32;
constexpr float kRigidActorShadowBaseLift = 0.025F;
std::atomic<std::uint64_t> g_completed_tasks{0};
std::uint32_t g_logged_counted_errors = 0;

struct CanonicalShadowSlotHistory {
    dkr::runtime::presentation::ShadowTexcoordSample texcoord{};
    RT64::RSP::Vertex vertex{};
    RT64::RSP::Vertex authored_vertex{};
    bool positioned = false;
};

struct CanonicalShadowBatchHistory {
    std::array<CanonicalShadowSlotHistory,
               dkr::runtime::presentation::kCanonicalShadowBatchVertices>
        slots{};
    std::uint64_t topology_signature = 0U;
    bool topology_valid = false;
};

struct CanonicalShadowHistory {
    std::array<CanonicalShadowBatchHistory,
               dkr::runtime::presentation::kMaximumCanonicalShadowBatches>
        batches{};
    dkr::runtime::presentation::ShadowTexcoordBounds rigid_actor_bounds{};
    dkr::runtime::presentation::RigidShadowOwnerPolicy rigid_owner_policy{};
    dkr::runtime::presentation::RigidShadowQuad rigid_reference_quad{};
    dkr::runtime::presentation::RigidShadowQuad last_rigid_quad{};
    dkr::runtime::presentation::RigidShadowShapeMetrics last_affine_shape{};
    std::array<RT64::RSP::Vertex, 4> rigid_vertex_templates{};
    dkr::runtime::presentation::ShadowVertexSample last_source_centre{};
    dkr::runtime::presentation::ShadowOwnerMotionSample last_owner_motion{};
    bool rigid_vertex_templates_valid = false;
    bool last_source_centre_valid = false;
    bool title_receiver_rejection_active = false;
    float title_receiver_rejection_size = 0.0F;
    float taj_vertical_clearance = 0.0F;
    std::uint32_t interpolation_epoch = 0U;
    std::uint64_t last_rigid_draw_task = 0U;
    std::uint64_t last_rigid_source_task = 0U;
    std::uint64_t last_task = 0U;
};

std::unordered_map<std::uint64_t, CanonicalShadowHistory>
    g_canonical_shadow_histories{};
constexpr std::uint64_t kShadowHistoryRetentionTasks = 600U;
std::uint64_t g_last_shadow_history_prune_task = 0U;

bool ShadowTraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("DKR_SHADOW_TRACE");
        return value != nullptr && value[0] != '\0' &&
            !(value[0] == '0' && value[1] == '\0');
    }();
    return enabled;
}

bool SplitTraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("DKR_SPLIT_TRACE");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

// The completed decomp shows every G_DMADL payload is either a two-command
// material-mode block or a texture/TLUT upload produced by the libultra GBI
// macros. In particular, framebuffer, depth-image and full-sync commands are
// never legal inside G_DMADL. Treating recycled texels as a permissive Fast3D
// list can otherwise create bogus framebuffer pairs and corrupt RT64 at the
// next real full sync.
bool IsSafeCountedOpcode(std::uint8_t opcode) {
    switch (opcode) {
        case 0xE6: // G_RDPLOADSYNC
        case 0xE7: // G_RDPPIPESYNC
        case 0xE8: // G_RDPTILESYNC
        case 0xEF: // G_RDPSETOTHERMODE
        case 0xF0: // G_LOADTLUT
        case 0xF2: // G_SETTILESIZE
        case 0xF3: // G_LOADBLOCK
        case 0xF4: // G_LOADTILE
        case 0xF5: // G_SETTILE
        case 0xF8: // G_SETFOGCOLOR
        case 0xF9: // G_SETBLENDCOLOR
        case 0xFA: // G_SETPRIMCOLOR
        case 0xFB: // G_SETENVCOLOR
        case 0xFC: // G_SETCOMBINE
        case 0xFD: // G_SETTIMG
            return true;
        default:
            return false;
    }
}

std::int16_t ReadS16(const std::uint8_t* rdram, std::uint32_t address) {
    std::int16_t value = 0;
    std::memcpy(&value, rdram + ((address & kRDRAMAddressMask) ^ 2U), sizeof(value));
    return value;
}

std::uint16_t ReadU16(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint16_t value = 0;
    std::memcpy(&value, rdram + ((address & kRDRAMAddressMask) ^ 2U), sizeof(value));
    return value;
}

std::uint8_t ReadU8(const std::uint8_t* rdram, std::uint32_t address) {
    return rdram[(address & kRDRAMAddressMask) ^ 3U];
}

std::uint32_t ReadU32(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint32_t value = 0;
    std::memcpy(&value, rdram + (address & kRDRAMAddressMask), sizeof(value));
    return value;
}

std::uint32_t PhysicalAddress(RT64::RSP& rsp, std::uint32_t address) {
    return rsp.fromSegmented(address) & kRDRAMAddressMask;
}

void SelectInterpolationGroup(RT64::RSP& rsp, std::uint32_t id,
                              bool interpolate_vertices = false,
                              bool interpolate_texcoords = false,
                              bool interpolate_tiles = false,
                              std::uint8_t aspect_mode = G_EX_ASPECT_AUTO,
                              bool preserve_vertex_interpolation = false,
                              std::uint8_t water_scroll_tag = 0U) {
    auto current_menu = ReadU32(rsp.state->RDRAM, kCurrentMenuIdAddress);
#if defined(DKR_WATER_QUALIFICATION)
    // The automatic preview is hosted by the title frontend, which normally
    // disables tile interpolation. Exercise the GAMEPLAY matching policy in
    // this private probe without changing guest menu/save/gameplay state.
    static const bool qualify_gameplay_tiles = std::getenv("DKR_WATER_TEST_MAP") != nullptr;
    if (qualify_gameplay_tiles) current_menu = 15U;
#endif
    interpolate_tiles =
        dkr::runtime::interpolation::effective_tile_interpolation(
            interpolate_tiles,
            current_menu);
    const bool task_interpolation_allowed =
        dkr::runtime::presentation::task_interpolation_allowed();
    if (!task_interpolation_allowed && !preserve_vertex_interpolation) {
        id = G_EX_ID_IGNORE;
        interpolate_vertices = false;
        interpolate_texcoords = false;
        interpolate_tiles = false;
    }
    const bool interpolation_disabled = id == G_EX_ID_IGNORE;
    // A finish-camera cut must snap its combined MVP, but a canonical shadow
    // page still owns a safe, fixed vertex stream. Preserve only that stream's
    // interpolation identity so camera/scenery policy cannot make the shadow
    // alternate between authored and interpolated frames.
    const std::uint8_t transform_component =
        interpolation_disabled || !task_interpolation_allowed
        ? G_EX_COMPONENT_SKIP
        : G_EX_COMPONENT_INTERPOLATE;
    // Generic tile matching is deliberately disabled: repeated materials in
    // a busy race create a large ambiguous candidate set. Procedural water is
    // different because its draw scope/matrix has an exact semantic identity;
    // only those explicitly marked groups may interpolate authored tile
    // scrolling between simulation ticks.
    const std::uint8_t tile_component =
        !interpolation_disabled && interpolate_tiles
            ? G_EX_COMPONENT_INTERPOLATE
            : G_EX_COMPONENT_SKIP;
    const std::uint8_t vertex_component =
        !interpolation_disabled && interpolate_vertices
            ? G_EX_COMPONENT_INTERPOLATE
            : G_EX_COMPONENT_SKIP;
    static_assert(G_EX_COMPONENT_AUTO < 0x80 && G_EX_COMPONENT_SKIP < 0x80 &&
                  G_EX_COMPONENT_INTERPOLATE < 0x80);
    static const bool baseline_scroll = [] {
        const char* value = std::getenv("DKR_WATER_SCROLL_BASELINE");
        return value != nullptr && value[0] == '1';
    }();
    const std::uint8_t texcoord_component =
        !interpolation_disabled && interpolate_texcoords
            ? (!baseline_scroll && dkr::runtime::water::is_scroll_tag(water_scroll_tag)
                ? water_scroll_tag : G_EX_COMPONENT_INTERPOLATE)
            : G_EX_COMPONENT_SKIP;
    // F3DDKR submits an already-combined model/view/projection matrix through
    // its matrix command. It is not an affine model transform and cannot be
    // safely decomposed into a rigid body. Interpolate all matrix regions
    // component-wise so the current 30 Hz pose remains exact at weight 1 and
    // RT64 can generate valid intermediate camera/object poses.
    rsp.matrixId(id, false, false, false,
                 transform_component, transform_component,
                 G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                 transform_component,
                 vertex_component, texcoord_component,
                 tile_component, G_EX_COMPONENT_SKIP,
                 // Every non-ignored ID is an immutable object-lifetime plus
                 // matrix-ordinal identity captured by the decomp patch. Tell
                 // RT64 to pair equal IDs directly and in submission order.
                 // AUTO ordering discards that guarantee and falls back to a
                 // geometric candidate search whose cost grows rapidly in
                 // scenes with many repeated draw calls.
                 G_EX_ORDER_LINEAR,
                 aspect_mode,
                 G_EX_EDIT_NONE, false, false);
}

std::uint8_t ActiveAspectMode(
    const dkr::runtime::interpolation::GroupState& groups,
    bool explicit_override_active, std::uint8_t explicit_override) {
    if (explicit_override_active) {
        return explicit_override;
    }
    if (groups.contains_mode(kPresentationGroupAspectOriginalMode)) {
        // Legacy markers from an older patch set must remain aspect preserving.
        // Never map them to STRETCH: that is the path which non-uniformly
        // widened HUD glyphs and menu rectangles.
        return G_EX_ASPECT_ADJUST;
    }
    return groups.contains_mode(kPresentationGroupAspectAdjustMode)
        ? G_EX_ASPECT_ADJUST
        : G_EX_ASPECT_AUTO;
}

} // namespace

std::uint64_t dkr::runtime::completed_f3ddkr_task_count() {
    return g_completed_tasks.load(std::memory_order_acquire);
}

struct dkr::runtime::F3DDKRRT64Bridge::StateData {
    struct HudAlignmentScope {
        RT64::ExtendedAlignment previous_scissor{};
        RT64::ExtendedAlignment previous_rect_alignment{};
        bool changed_rect_alignment = false;
        std::array<std::int16_t, 4> previous_clip_ratios{};
        bool changed_scissor = false;
        bool changed_clip_ratios = false;
        bool pushed_scissor = false;
        bool pushed_hud_projection = false;
        bool previous_matrix_aspect_override_active = false;
        std::uint8_t previous_matrix_aspect_override = G_EX_ASPECT_AUTO;
        std::uint8_t previous_rect_aspect = G_EX_ASPECT_AUTO;
        std::uint8_t previous_background_fill_stretch = 0U;
    };

    struct AspectScope {
        PresentationMarkerKind kind = PresentationMarkerKind::Geometry;
        bool previous_matrix_aspect_override_active = false;
        std::uint8_t previous_matrix_aspect_override = G_EX_ASPECT_AUTO;
        std::uint8_t previous_rect_aspect = G_EX_ASPECT_AUTO;
        std::uint8_t previous_background_fill_stretch = 0U;
    };

    struct PendingShadowBatch {
        struct Triangle {
            std::uint8_t flag = 0U;
            std::array<std::uint8_t, 3> vertices{};
            std::array<std::int16_t, 3> s{};
            std::array<std::int16_t, 3> t{};
        };

        std::array<RT64::RSP::Vertex, kMaxDKRVertices> vertices{};
        dkr::runtime::presentation::ShadowCanonicalPage texcoords{};
        std::array<Triangle, 16> triangles{};
        std::uint32_t count = 0U;
        std::uint32_t triangle_count = 0U;
        std::uint32_t texcoord_conflict_count = 0U;
        std::uint32_t destination = 0U;
        bool valid = false;
    };

    std::uint32_t matrix_offset = 0;
    std::uint32_t vertex_offset = 0;
    std::uint32_t texture_offset = 0;
    std::uint32_t texture_shift = 0;
    std::uint32_t texture_count = 0;
    std::uint32_t vertex_cursor = 0;
    std::uint32_t selected_matrix = 0;
    bool billboard = false;
    std::uint32_t nested_depth = 0;
    std::uint64_t task_count = 0;
    std::uint32_t presentation_group_begins = 0;
    std::uint32_t presentation_group_ends = 0;
    std::uint8_t background_fill_stretch_active = 0U;
    bool matrix_aspect_override_active = false;
    std::uint8_t matrix_aspect_override = G_EX_ASPECT_AUTO;
    std::uint8_t rect_aspect_mode = G_EX_ASPECT_AUTO;
    bool task_rejected = false;
    bool split_viewport_fill_pending = false;
    bool postrace_full_viewport_pending = false;
    bool split_viewport_scissor_adjusted = false;
    bool split_viewport_rsp_adjusted = false;
    std::uint8_t split_viewport_camera = 0U;
    std::uint8_t split_viewport_commands_remaining = 0U;
    float split_viewport_cover = 1.0F;
    bool split_world_projection_active = false;
    std::array<std::uint32_t, 8> split_color_images{};
    std::size_t split_color_image_count = 0U;
    std::array<std::uint32_t, 8> hud_color_images{};
    std::size_t hud_color_image_count = 0U;
    std::array<HudAlignmentScope, 8> hud_alignment_scopes{};
    std::size_t hud_alignment_depth = 0U;
    struct HudWidgetScope {
        hud::groups::Transform transform;
    };
    std::array<HudWidgetScope,4> hud_widgets{};
    std::size_t hud_widget_depth = 0;
    unsigned hud_rect_bias = 0;
    presentation::RejectedMarkerScopes rejected_hud_widgets{};
    dkr::runtime::presentation::RejectedMarkerScopes rejected_hud_scopes{};
    std::array<AspectScope, 16> aspect_scopes{};
    std::size_t aspect_scope_depth = 0U;
    dkr::runtime::presentation::RejectedMarkerScopes rejected_aspect_scopes{};
    PendingShadowBatch pending_shadow_batch{};
    std::uint64_t shadow_history_key = 0U;
    std::size_t shadow_expected_batch_count = 0U;
    std::size_t shadow_batch_count = 0U;
    std::array<PendingShadowBatch,
               dkr::runtime::presentation::kMaximumCanonicalShadowBatches>
        pending_shadow_batches{};
    bool shadow_scope_active = false;
    dkr::runtime::interpolation::GroupState interpolation_groups{};
    std::array<RT64::DisplayList*, kMaxNestedDisplayLists> return_stack{};
    std::uint32_t return_depth = 0;
};

dkr::runtime::F3DDKRRT64Bridge* dkr::runtime::F3DDKRRT64Bridge::active_ = nullptr;

dkr::runtime::F3DDKRRT64Bridge::F3DDKRRT64Bridge()
    : gbi_(new RT64::GBI{}), data_(new StateData{}) {
    RT64::GBI_RDP::setup(gbi_, true);
    RT64::GBI_F3D::setup(gbi_);
    gbi_->ucode = RT64::GBIUCode::F3D;
    gbi_->map[kMatrixOpcode] = &Matrix;
    gbi_->map[kTextureOffsetOpcode] = &TextureOffset;
    gbi_->map[kMoveMemOpcode] = &MoveMem;
    gbi_->map[kVertexOpcode] = &Vertex;
    gbi_->map[kTriangleOpcode] = &Triangle;
    gbi_->map[kDisplayListOpcode] = &DisplayListBranch;
    gbi_->map[kEndDisplayListOpcode] = &EndDisplayList;
    gbi_->map[kCountedDisplayListOpcode] = &CountedDisplayList;
    gbi_->map[kDMAOffsetsOpcode] = &DMAOffsets;
    gbi_->map[kMoveWordOpcode] = &MoveWord;
    gbi_->map[kSetTextureImageOpcode] = &SetTextureImage;
    gbi_->map[kLoadBlockOpcode] = &LoadBlock;
    gbi_->map[kFillRectOpcode] = &FillRect;
    gbi_->map[G_TEXRECT] = &HudTextureRect;
    gbi_->map[G_TEXRECTFLIP] = &HudTextureRect;
    // Interpolation scopes are recorded out-of-band at the address of the
    // next authored command. Route every opcode through one bounded dispatcher
    // so the renderer can apply that metadata without appending bytes to DKR's
    // fixed-size display-list heaps.
    std::copy(std::begin(gbi_->map), std::end(gbi_->map),
              original_handlers_.begin());
    std::fill(std::begin(gbi_->map), std::end(gbi_->map), &Dispatch);
}

dkr::runtime::F3DDKRRT64Bridge::~F3DDKRRT64Bridge() {
    for (std::size_t index = 0U; index < diagnostic_budgets_.size(); ++index) {
        const auto& budget = diagnostic_budgets_[index];
        if (budget.total != 0U) {
            std::fprintf(stderr,
                "[boot][f3ddkr] presentation diagnostic summary category=%zu "
                "total=%llu unreported=%llu\n", index,
                static_cast<unsigned long long>(budget.total),
                static_cast<unsigned long long>(budget.pending));
        }
    }
    if (active_ == this) {
        active_ = nullptr;
    }
    delete data_;
    delete gbi_;
}

void dkr::runtime::F3DDKRRT64Bridge::FinishShadowScope(
    RT64::State* state) {
    if (active_ == nullptr || state == nullptr) return;
    StateData& data = *active_->data_;
    if (!data.shadow_scope_active) return;

    RT64::RSP& rsp = *state->rsp;
    data.selected_matrix = std::min(data.selected_matrix, 2U);
    rsp.modelMatrixStackSize = static_cast<int>(data.selected_matrix + 1U);
    const auto rounded_shadow_coordinate = [](float value) {
        return static_cast<std::int16_t>(std::clamp(
            std::lround(value), -32768L, 32767L));
    };

    CanonicalShadowHistory transient_history{};
    CanonicalShadowHistory* history = &transient_history;
    if (data.shadow_history_key != 0U) {
        auto& entry = g_canonical_shadow_histories[data.shadow_history_key];
        entry.last_task = data.task_count;
        history = &entry;
    }

    bool rigid_actor_shadow_active = false;
    bool magic_carpet_shadow_active = false;
    float rigid_actor_shadow_lift = kRigidActorShadowBaseLift;
    const std::size_t rigid_source_batch_count = data.shadow_batch_count;
    const auto rigid_policy =
        dkr::runtime::presentation::shadow_owner_rigid_policy(
            data.shadow_history_key);
    const auto owner_motion =
        dkr::runtime::presentation::shadow_owner_motion_sample(
            data.shadow_history_key);
    if (rigid_policy.required() &&
        (history->rigid_owner_policy.kind != rigid_policy.kind ||
         history->rigid_owner_policy.vehicle != rigid_policy.vehicle ||
         history->rigid_owner_policy.texture_width !=
             rigid_policy.texture_width ||
         history->rigid_owner_policy.object_id != rigid_policy.object_id)) {
        // A racer can legitimately switch between car, hovercraft and plane
        // while retaining its object identity. Seed a new rigid silhouette for
        // the new vehicle instead of carrying the previous vehicle's footprint
        // or receiver history across the change.
        history->rigid_actor_bounds = {};
        history->rigid_reference_quad = {};
        history->last_rigid_quad = {};
        history->last_affine_shape = {};
        history->rigid_vertex_templates = {};
        history->last_source_centre = {};
        history->last_owner_motion = {};
        history->rigid_vertex_templates_valid = false;
        history->last_source_centre_valid = false;
        history->title_receiver_rejection_active = false;
        history->title_receiver_rejection_size = 0.0F;
        history->taj_vertical_clearance = 0.0F;
        history->batches = {};
        ++history->interpolation_epoch;
        if (history->interpolation_epoch == 0U) {
            history->interpolation_epoch = 1U;
        }
        history->last_rigid_draw_task = 0U;
        history->last_rigid_source_task = 0U;
        history->rigid_owner_policy = rigid_policy;
    }
    const bool rigid_source_history_is_fresh =
        dkr::runtime::presentation::rigid_shadow_source_history_is_fresh(
            data.task_count, history->last_rigid_source_task);
    const bool taj_source_history_is_fresh =
        dkr::runtime::presentation::taj_shadow_source_history_is_fresh(
            data.task_count, history->last_rigid_source_task);
    const bool can_bridge_missing_plane =
        data.shadow_batch_count == 0U && rigid_policy.plane() &&
        history->last_rigid_quad.valid &&
        history->rigid_vertex_templates_valid && owner_motion.valid &&
        history->last_owner_motion.valid && rigid_source_history_is_fresh;
    const bool can_bridge_missing_taj_receiver =
        data.shadow_batch_count == 0U &&
        rigid_policy.terrain_conforming_actor() &&
        history->last_rigid_quad.valid &&
        history->rigid_vertex_templates_valid && owner_motion.valid &&
        history->last_owner_motion.valid && taj_source_history_is_fresh;
    if (rigid_policy.required() &&
        (data.shadow_batch_count != 0U || can_bridge_missing_plane ||
         can_bridge_missing_taj_receiver)) {
        constexpr std::size_t kMaximumRigidSourceVertices =
            dkr::runtime::presentation::kMaximumCanonicalShadowBatches *
            dkr::runtime::presentation::kCanonicalShadowBatchVertices;
        constexpr std::size_t kMaximumRigidSourceTriangles =
            dkr::runtime::presentation::kMaximumCanonicalShadowBatches * 16U;
        constexpr std::size_t kMaximumRigidTriangleCorners =
            kMaximumRigidSourceTriangles * 3U;
        std::array<dkr::runtime::presentation::ShadowTexcoordSample,
                   kMaximumRigidSourceVertices>
            source_texcoords{};
        std::array<dkr::runtime::presentation::ShadowVertexSample,
                   kMaximumRigidSourceVertices>
            source_vertices{};
        std::array<RT64::RSP::Vertex, kMaximumRigidSourceVertices>
            source_rsp_vertices{};
        std::array<dkr::runtime::presentation::ShadowTexcoordSample,
                   kMaximumRigidTriangleCorners>
            triangle_corner_texcoords{};
        std::array<dkr::runtime::presentation::ShadowVertexSample,
                   kMaximumRigidTriangleCorners>
            triangle_corner_vertices{};
        std::array<dkr::runtime::presentation::ShadowTriangleSample,
                   kMaximumRigidSourceTriangles>
            source_triangles{};
        std::size_t source_count = 0U;
        std::size_t source_triangle_count = 0U;
        std::size_t triangle_corner_count = 0U;
        for (std::size_t batch_index = 0U;
             batch_index < data.shadow_batch_count; ++batch_index) {
            const auto& source_batch =
                data.pending_shadow_batches[batch_index];
            for (std::size_t source = 0U; source < source_batch.count;
                 ++source) {
                source_texcoords[source_count] =
                    source_batch.texcoords[source];
                source_rsp_vertices[source_count] =
                    source_batch.vertices[source];
                source_vertices[source_count] = {
                    static_cast<float>(source_batch.vertices[source].x),
                    static_cast<float>(source_batch.vertices[source].y),
                    static_cast<float>(source_batch.vertices[source].z),
                };
                ++source_count;
            }
            for (std::size_t triangle_index = 0U;
                 triangle_index < source_batch.triangle_count &&
                 source_triangle_count < source_triangles.size();
                 ++triangle_index) {
                const auto& source_triangle =
                    source_batch.triangles[triangle_index];
                auto& triangle = source_triangles[source_triangle_count];
                bool triangle_valid = true;
                for (std::size_t corner = 0U;
                     corner < source_triangle.vertices.size(); ++corner) {
                    const std::size_t vertex =
                        source_triangle.vertices[corner];
                    if (vertex >= source_batch.count) {
                        triangle_valid = false;
                        break;
                    }
                    triangle.texcoords[corner] = {
                        source_triangle.s[corner],
                        source_triangle.t[corner], true};
                    triangle.vertices[corner] = {
                        static_cast<float>(source_batch.vertices[vertex].x),
                        static_cast<float>(source_batch.vertices[vertex].y),
                        static_cast<float>(source_batch.vertices[vertex].z),
                    };
                }
                if (!triangle_valid) continue;
                for (std::size_t corner = 0U;
                     corner < triangle.vertices.size(); ++corner) {
                    triangle_corner_texcoords[triangle_corner_count] =
                        triangle.texcoords[corner];
                    triangle_corner_vertices[triangle_corner_count] =
                        triangle.vertices[corner];
                    ++triangle_corner_count;
                }
                ++source_triangle_count;
            }
        }
        const auto source_texcoord_span = std::span<const
            dkr::runtime::presentation::ShadowTexcoordSample>(
                source_texcoords.data(), source_count);
        const auto source_vertex_span = std::span<const
            dkr::runtime::presentation::ShadowVertexSample>(
                source_vertices.data(), source_count);
        dkr::runtime::presentation::ShadowVertexSample source_centre{};
        bool source_centre_valid = source_count != 0U;
        if (source_centre_valid) {
            const float inverse_source_count =
                1.0F / static_cast<float>(source_count);
            for (const auto& vertex : source_vertex_span) {
                source_centre.x += vertex.x * inverse_source_count;
                source_centre.y += vertex.y * inverse_source_count;
                source_centre.z += vertex.z * inverse_source_count;
            }
            source_centre_valid = std::isfinite(source_centre.x) &&
                std::isfinite(source_centre.y) &&
                std::isfinite(source_centre.z);
        }
        const auto authored_bounds =
            rigid_policy.authored_texcoord_bounds();
        if (authored_bounds.valid) {
            history->rigid_actor_bounds = authored_bounds;
        } else {
            const auto current_bounds =
                dkr::runtime::presentation::shadow_texcoord_bounds(
                    source_texcoord_span);
            if (!history->rigid_actor_bounds.valid) {
                history->rigid_actor_bounds = current_bounds;
            } else if (current_bounds.valid) {
                // Unknown/custom textures retain an expanding envelope so a
                // partially clipped first frame can never permanently shrink
                // the proxy.
                history->rigid_actor_bounds.minimum_s = std::min(
                    history->rigid_actor_bounds.minimum_s,
                    current_bounds.minimum_s);
                history->rigid_actor_bounds.minimum_t = std::min(
                    history->rigid_actor_bounds.minimum_t,
                    current_bounds.minimum_t);
                history->rigid_actor_bounds.maximum_s = std::max(
                    history->rigid_actor_bounds.maximum_s,
                    current_bounds.maximum_s);
                history->rigid_actor_bounds.maximum_t = std::max(
                    history->rigid_actor_bounds.maximum_t,
                    current_bounds.maximum_t);
            }
        }
        dkr::runtime::presentation::RigidShadowQuad affine_quad{};
        dkr::runtime::presentation::DecompShadowReceiverQuad
            decomp_receiver{};
        const bool use_decomp_receiver = dkr::runtime::presentation::
            rigid_shadow_policy_uses_decomp_receiver(rigid_policy);
        const bool inspect_decomp_receiver = dkr::runtime::presentation::
            rigid_shadow_policy_inspects_decomp_receiver(rigid_policy);
        if (inspect_decomp_receiver && source_triangle_count != 0U) {
            const auto previous_receiver_shape =
                dkr::runtime::presentation::rigid_shadow_shape_metrics(
                    history->last_rigid_quad);
            decomp_receiver =
                dkr::runtime::presentation::decomp_shadow_receiver_quad(
                    std::span<const dkr::runtime::presentation::
                        ShadowTexcoordSample>(
                            triangle_corner_texcoords.data(),
                            triangle_corner_count),
                    std::span<const dkr::runtime::presentation::
                        ShadowVertexSample>(
                            triangle_corner_vertices.data(),
                            triangle_corner_count),
                    std::span<const dkr::runtime::presentation::
                        ShadowTriangleSample>(
                            source_triangles.data(), source_triangle_count),
                    history->rigid_actor_bounds,
                    owner_motion.position.y, owner_motion.valid,
                    previous_receiver_shape.centre.y,
                    previous_receiver_shape.valid);
            if (use_decomp_receiver && decomp_receiver.valid) {
                affine_quad = decomp_receiver.quad;
            }
        }
        const bool only_above_owner_receiver =
            decomp_receiver.only_above_owner;
        const bool untrusted_taj_receiver =
            rigid_policy.terrain_conforming_actor() && !decomp_receiver.valid;
        if (!affine_quad.valid && !only_above_owner_receiver &&
            !untrusted_taj_receiver) {
            const auto fit_texcoords = triangle_corner_count != 0U
                ? std::span<const dkr::runtime::presentation::
                      ShadowTexcoordSample>(
                          triangle_corner_texcoords.data(),
                          triangle_corner_count)
                : source_texcoord_span;
            const auto fit_vertices = triangle_corner_count != 0U
                ? std::span<const dkr::runtime::presentation::
                      ShadowVertexSample>(
                          triangle_corner_vertices.data(),
                          triangle_corner_count)
                : source_vertex_span;
            affine_quad =
                dkr::runtime::presentation::rigid_shadow_quad(
                    fit_texcoords, fit_vertices,
                    history->rigid_actor_bounds);
        }
        if (decomp_receiver.valid) {
            const auto receiver_shape =
                dkr::runtime::presentation::rigid_shadow_shape_metrics(
                    affine_quad);
            if (receiver_shape.valid) {
                source_centre = receiver_shape.centre;
                source_centre_valid = true;
            }
        }
        const auto affine_shape =
            dkr::runtime::presentation::rigid_shadow_shape_metrics(
                affine_quad);
        float maximum_edge_delta = 0.0F;
        float maximum_diagonal_delta = 0.0F;
        float centre_y_delta = 0.0F;
        if (affine_shape.valid && history->last_affine_shape.valid) {
            for (std::size_t edge = 0U;
                 edge < affine_shape.edge_lengths.size(); ++edge) {
                maximum_edge_delta = std::max(
                    maximum_edge_delta,
                    std::abs(affine_shape.edge_lengths[edge] -
                             history->last_affine_shape.edge_lengths[edge]));
            }
            for (std::size_t diagonal = 0U;
                 diagonal < affine_shape.diagonal_lengths.size(); ++diagonal) {
                maximum_diagonal_delta = std::max(
                    maximum_diagonal_delta,
                    std::abs(affine_shape.diagonal_lengths[diagonal] -
                             history->last_affine_shape
                                 .diagonal_lengths[diagonal]));
            }
            centre_y_delta =
                affine_shape.centre.y - history->last_affine_shape.centre.y;
        }
        if (ShadowTraceEnabled()) {
            std::fprintf(
                stderr,
                "[trace][shadow-rigid-fit] task=%llu key=%llu kind=%u "
                "vehicle=%d texture-width=%u bounds=(%d,%d,%d,%d) "
                "required=1 valid=%u source-batches=%zu "
                "source-vertices=%zu source-triangles=%zu "
                "receiver-valid=%u receiver-triangle=%zu "
                "receiver-centre-y=%.3f receiver-centre-covered=%u "
                "edges=(%.3f,%.3f,%.3f,%.3f) "
                "diagonals=(%.3f,%.3f) cosine=%.6f area=%.3f "
                "edge-delta=%.3f diagonal-delta=%.3f centre-y=%.3f "
                "centre-y-delta=%.3f\n",
                static_cast<unsigned long long>(data.task_count),
                static_cast<unsigned long long>(data.shadow_history_key),
                static_cast<unsigned int>(rigid_policy.kind),
                static_cast<int>(rigid_policy.vehicle),
                static_cast<unsigned int>(rigid_policy.texture_width),
                static_cast<int>(history->rigid_actor_bounds.minimum_s),
                static_cast<int>(history->rigid_actor_bounds.minimum_t),
                static_cast<int>(history->rigid_actor_bounds.maximum_s),
                static_cast<int>(history->rigid_actor_bounds.maximum_t),
                affine_quad.valid ? 1U : 0U,
                rigid_source_batch_count, source_count,
                source_triangle_count,
                decomp_receiver.valid ? 1U : 0U,
                decomp_receiver.triangle_index,
                decomp_receiver.centre_y,
                decomp_receiver.centre_covered ? 1U : 0U,
                affine_shape.edge_lengths[0],
                affine_shape.edge_lengths[1],
                affine_shape.edge_lengths[2],
                affine_shape.edge_lengths[3],
                affine_shape.diagonal_lengths[0],
                affine_shape.diagonal_lengths[1],
                affine_shape.adjacent_edge_cosine, affine_shape.area,
                maximum_edge_delta, maximum_diagonal_delta,
                affine_shape.centre.y, centre_y_delta);
        }
        if (affine_shape.valid) {
            history->last_affine_shape = affine_shape;
        }

        dkr::runtime::presentation::RigidShadowQuad presentation_quad{};
        bool used_sticky_history = false;
        bool scale_sample_rejected = false;
        bool receiver_rejected = false;
        bool receiver_reacquired = false;
        if (affine_quad.valid) {
            // The decomp receiver already contains the exact X/Z UV axes and
            // footprint authored by func_8002F440. Do not orthogonalise or
            // resize that result: doing so can shrink the plane silhouette and
            // perturb its direction on sloped tunnel receivers.
            presentation_quad = decomp_receiver.valid
                ? affine_quad
                : dkr::runtime::presentation::
                      rigid_shadow_authored_scale_quad(affine_quad);
            if (presentation_quad.valid &&
                !decomp_receiver.valid &&
                rigid_policy.kind == dkr::runtime::presentation::
                    RigidShadowOwnerKind::TitleVehicle &&
                history->last_rigid_quad.valid) {
                const auto current_shape =
                    dkr::runtime::presentation::rigid_shadow_shape_metrics(
                        presentation_quad);
                const auto previous_shape =
                    dkr::runtime::presentation::rigid_shadow_shape_metrics(
                        history->last_rigid_quad);
                const float dx =
                    current_shape.centre.x - previous_shape.centre.x;
                const float dz =
                    current_shape.centre.z - previous_shape.centre.z;
                const float horizontal_motion = std::sqrt(dx * dx + dz * dz);
                const bool upward_discontinuity =
                    dkr::runtime::presentation::
                        plane_shadow_receiver_is_upward_discontinuity(
                            previous_shape.centre.y,
                            current_shape.centre.y, horizontal_motion);
                const bool implausible_fit =
                    dkr::runtime::presentation::
                        rigid_shadow_receiver_fit_is_implausible(
                            affine_shape);
                if (history->title_receiver_rejection_active &&
                    dkr::runtime::presentation::
                        rigid_shadow_receiver_can_reacquire(
                            previous_shape, affine_shape,
                            horizontal_motion)) {
                    history->title_receiver_rejection_active = false;
                    history->title_receiver_rejection_size = 0.0F;
                    receiver_reacquired = true;
                } else if (implausible_fit || upward_discontinuity ||
                           history->title_receiver_rejection_active) {
                    // Title-sequence vehicles do not expose their racer vehicle
                    // subtype. When the Banjo plane flies below a roof, the
                    // terrain mesh can combine ground, roof and wall samples in
                    // one affine fit. Retain the last trusted ground receiver,
                    // following only the authored horizontal motion, until a
                    // plausible fit returns near that ground height.
                    if (!history->title_receiver_rejection_active) {
                        const float trusted_size =
                            dkr::runtime::presentation::
                                rigid_shadow_horizontal_size(
                                    history->last_rigid_quad);
                        history->title_receiver_rejection_size = trusted_size;
                        if (dkr::runtime::presentation::
                                rigid_shadow_receiver_fit_is_near_square(
                                    previous_shape)) {
                            // The Banjo fly-under enters the roof with a valid,
                            // square footprint that reads slightly undersized
                            // while held. Apply one bounded correction to that
                            // trusted size; never chase the degenerate samples.
                            history->title_receiver_rejection_size *= 1.1F;
                        }
                    }
                    presentation_quad = dkr::runtime::presentation::
                        translated_rigid_shadow_quad(
                            history->last_rigid_quad, {dx, 0.0F, dz});
                    if (presentation_quad.valid &&
                        history->title_receiver_rejection_size > 0.0F) {
                        // Ground height, orientation and the corrected target
                        // size all remain fixed for the rejection interval.
                        presentation_quad = dkr::runtime::presentation::
                            rigid_shadow_horizontal_resized_quad(
                                presentation_quad,
                                history->title_receiver_rejection_size);
                    }
                    history->title_receiver_rejection_active = true;
                    used_sticky_history = presentation_quad.valid;
                    receiver_rejected = presentation_quad.valid;
                }
            }
            if (presentation_quad.valid &&
                history->last_rigid_quad.valid &&
                !decomp_receiver.valid &&
                !receiver_rejected && !receiver_reacquired &&
                dkr::runtime::presentation::
                    rigid_shadow_scale_is_discontinuous(
                        history->last_rigid_quad, presentation_quad)) {
                const auto current_shape =
                    dkr::runtime::presentation::rigid_shadow_shape_metrics(
                        presentation_quad);
                const auto previous_shape =
                    dkr::runtime::presentation::rigid_shadow_shape_metrics(
                        history->last_rigid_quad);
                presentation_quad = dkr::runtime::presentation::
                    translated_rigid_shadow_quad(
                        history->last_rigid_quad,
                        {current_shape.centre.x - previous_shape.centre.x,
                         current_shape.centre.y - previous_shape.centre.y,
                         current_shape.centre.z - previous_shape.centre.z});
                used_sticky_history = true;
                scale_sample_rejected = true;
            }
            if (presentation_quad.valid &&
                rigid_policy.terrain_conforming_actor()) {
                // Keep the selected decomp receiver plane intact. Flattening
                // this rigid quad to one Y value makes its far corners pass
                // through sloped or uneven terrain even though receiver
                // ownership is correct. Vehicle shadows already preserve this
                // same planar result and receive the shared 0.025 matrix lift.
                history->taj_vertical_clearance = 0.0F;
            }
            if (presentation_quad.valid &&
                !history->rigid_reference_quad.valid) {
                history->rigid_reference_quad = presentation_quad;
            }
        }
        const bool rigid_history_can_bridge_receiver =
            rigid_policy.terrain_conforming_actor()
            ? taj_source_history_is_fresh
            : (!only_above_owner_receiver ||
               rigid_source_history_is_fresh);
        if (!presentation_quad.valid && history->last_rigid_quad.valid &&
            rigid_history_can_bridge_receiver) {
            dkr::runtime::presentation::ShadowVertexSample translation{};
            if (owner_motion.valid && history->last_owner_motion.valid) {
                translation.x = owner_motion.position.x -
                    history->last_owner_motion.position.x;
                translation.y = owner_motion.position.y -
                    history->last_owner_motion.position.y;
                translation.z = owner_motion.position.z -
                    history->last_owner_motion.position.z;
            } else if (source_centre_valid &&
                       history->last_source_centre_valid) {
                translation.x =
                    source_centre.x - history->last_source_centre.x;
                translation.y =
                    source_centre.y - history->last_source_centre.y;
                translation.z =
                    source_centre.z - history->last_source_centre.z;
            }
            if (rigid_policy.plane() ||
                rigid_policy.terrain_conforming_actor()) {
                // DKR has supplied no receiver mesh for this authored instant,
                // so preserve the last real ground height and exact footprint.
                // Its UV direction still follows the owner's wrapped yaw delta,
                // matching func_8002F440 until authored geometry returns.
                translation.y = 0.0F;
            }
            if ((rigid_policy.plane() ||
                 rigid_policy.terrain_conforming_actor()) &&
                owner_motion.valid &&
                history->last_owner_motion.valid) {
                presentation_quad = dkr::runtime::presentation::
                    plane_shadow_missing_frame_quad(
                        history->last_rigid_quad, translation,
                        history->last_owner_motion.yaw, owner_motion.yaw);
            } else {
                presentation_quad = dkr::runtime::presentation::
                    translated_rigid_shadow_quad(
                        history->last_rigid_quad, translation);
            }
            used_sticky_history = presentation_quad.valid;
        }

        if (presentation_quad.valid) {
            StateData::PendingShadowBatch proxy{};
            dkr::runtime::presentation::TajShadowReceiverSurface
                taj_receiver_surface{};
            if (rigid_policy.terrain_conforming_actor()) {
                // Keep Taj's broad footprint and topology permanent, but let
                // its fixed lattice follow each real terrain section. This is
                // the smallest surface that fits all four quarter-cells in one
                // canonical DKR page and prevents one extrapolated plane from
                // passing through uneven ground.
                auto surface_receiver = decomp_receiver;
                surface_receiver.quad = presentation_quad;
                surface_receiver.valid = presentation_quad.valid;
                const auto trusted_triangles = decomp_receiver.valid
                    ? std::span<const dkr::runtime::presentation::
                          ShadowTriangleSample>(
                              source_triangles.data(), source_triangle_count)
                    : std::span<const dkr::runtime::presentation::
                          ShadowTriangleSample>{};
                taj_receiver_surface = dkr::runtime::presentation::
                    taj_shadow_receiver_surface(
                        surface_receiver, trusted_triangles,
                        history->rigid_actor_bounds,
                        owner_motion.position.y, owner_motion.valid);
            }
            const bool use_taj_receiver_surface =
                rigid_policy.terrain_conforming_actor() &&
                taj_receiver_surface.valid;
            proxy.count = use_taj_receiver_surface
                ? static_cast<std::uint32_t>(dkr::runtime::presentation::
                      kTajShadowSurfaceVertexCount)
                : 4U;
            proxy.triangle_count = use_taj_receiver_surface
                ? static_cast<std::uint32_t>(dkr::runtime::presentation::
                      kTajShadowSurfaceTriangleCount)
                : 2U;
            proxy.destination = 0U;
            proxy.valid = true;
            for (std::size_t point = 0U; point < proxy.count; ++point) {
                const auto& presentation_vertex = use_taj_receiver_surface
                    ? taj_receiver_surface.vertices[point]
                    : presentation_quad.vertices[point];
                const auto& presentation_texcoord = use_taj_receiver_surface
                    ? taj_receiver_surface.texcoords[point]
                    : presentation_quad.texcoords[point];
                std::size_t nearest_source = 0U;
                std::uint64_t nearest_distance =
                    std::numeric_limits<std::uint64_t>::max();
                for (std::size_t source = 0U; source < source_count; ++source) {
                    const auto& texcoord = source_texcoords[source];
                    if (!texcoord.valid) continue;
                    const std::int64_t ds =
                        static_cast<std::int64_t>(texcoord.s) -
                        presentation_texcoord.s;
                    const std::int64_t dt =
                        static_cast<std::int64_t>(texcoord.t) -
                        presentation_texcoord.t;
                    const std::uint64_t distance =
                        static_cast<std::uint64_t>(ds * ds + dt * dt);
                    if (distance < nearest_distance) {
                        nearest_distance = distance;
                        nearest_source = source;
                    }
                }
                if (nearest_distance !=
                    std::numeric_limits<std::uint64_t>::max()) {
                    proxy.vertices[point] =
                        source_rsp_vertices[nearest_source];
                } else if (history->rigid_vertex_templates_valid) {
                    // One preserved RSP template is sufficient for every Taj
                    // lattice point during its one-task receiver bridge; UV
                    // and position are replaced immediately below.
                    proxy.vertices[point] = history->rigid_vertex_templates[
                        use_taj_receiver_surface ? 0U : point];
                } else if (source_count != 0U) {
                    proxy.vertices[point] = source_rsp_vertices[0];
                } else {
                    proxy.valid = false;
                    break;
                }
                proxy.vertices[point].x = rounded_shadow_coordinate(
                    presentation_vertex.x);
                proxy.vertices[point].y = rounded_shadow_coordinate(
                    presentation_vertex.y);
                proxy.vertices[point].z = rounded_shadow_coordinate(
                    presentation_vertex.z);
                proxy.texcoords[point] = presentation_texcoord;
            }
            if (proxy.valid && !use_taj_receiver_surface) {
                // Round three corners, then derive the opposite corner. This
                // preserves an exact parallelogram in the integer RSP stream;
                // four independent roundings can otherwise introduce a visible
                // one-unit seam pulse in a wide shadow such as Taj's carpet.
                proxy.vertices[2].x = rounded_shadow_coordinate(
                    static_cast<float>(proxy.vertices[1].x) +
                    static_cast<float>(proxy.vertices[3].x) -
                    static_cast<float>(proxy.vertices[0].x));
                proxy.vertices[2].y = rounded_shadow_coordinate(
                    static_cast<float>(proxy.vertices[1].y) +
                    static_cast<float>(proxy.vertices[3].y) -
                    static_cast<float>(proxy.vertices[0].y));
                proxy.vertices[2].z = rounded_shadow_coordinate(
                    static_cast<float>(proxy.vertices[1].z) +
                    static_cast<float>(proxy.vertices[3].z) -
                    static_cast<float>(proxy.vertices[0].z));
            }
            auto submitted_quad = presentation_quad;
            constexpr std::array<std::size_t, 4> kTajSurfaceCorners{
                0U, 2U, 8U, 6U};
            for (std::size_t corner = 0U;
                 corner < submitted_quad.vertices.size(); ++corner) {
                const std::size_t source = use_taj_receiver_surface
                    ? kTajSurfaceCorners[corner]
                    : corner;
                submitted_quad.vertices[corner] = {
                    static_cast<float>(proxy.vertices[source].x),
                    static_cast<float>(proxy.vertices[source].y),
                    static_cast<float>(proxy.vertices[source].z),
                };
            }
            magic_carpet_shadow_active =
                rigid_policy.magic_carpet() && proxy.valid;
            if (use_taj_receiver_surface) {
                for (std::size_t triangle = 0U;
                     triangle < dkr::runtime::presentation::
                         kTajShadowSurfaceTriangles.size(); ++triangle) {
                    proxy.triangles[triangle].flag = 0x40U;
                    proxy.triangles[triangle].vertices =
                        dkr::runtime::presentation::
                            kTajShadowSurfaceTriangles[triangle];
                }
            } else {
                proxy.triangles[0].flag = 0x40U;
                proxy.triangles[0].vertices = {0U, 1U, 2U};
                proxy.triangles[1].flag = 0x40U;
                proxy.triangles[1].vertices = {0U, 2U, 3U};
            }
            for (std::size_t triangle_index = 0U;
                 triangle_index < proxy.triangle_count; ++triangle_index) {
                auto& triangle = proxy.triangles[triangle_index];
                for (std::size_t corner = 0U;
                     corner < triangle.vertices.size(); ++corner) {
                    const auto& texcoord =
                        proxy.texcoords[triangle.vertices[corner]];
                    triangle.s[corner] = texcoord.s;
                    triangle.t[corner] = texcoord.t;
                }
            }
            if (proxy.valid) {
                const bool rigid_history_gap =
                    history->last_rigid_draw_task != 0U &&
                    data.task_count > history->last_rigid_draw_task + 1U;
                if (rigid_history_gap) {
                    // No geometry was submitted for this owner during the
                    // missing interval. A new RT64 identity prevents the first
                    // returning quad from blending with stale, distant corners.
                    history->batches = {};
                    ++history->interpolation_epoch;
                    if (history->interpolation_epoch == 0U) {
                        history->interpolation_epoch = 1U;
                    }
                }
                // Terrain clipping may split a large racer shadow (notably
                // Taj's carpet) across several temporary 24-vertex batches.
                // They all describe one projected texture, so the Modern copy
                // is collapsed to one mandatory fixed-topology proxy: a quad
                // for vehicles or the terrain-following Taj lattice.
                data.pending_shadow_batches = {};
                data.pending_shadow_batches[0] = proxy;
                data.shadow_batch_count = 1U;
                rigid_actor_shadow_active = true;
                history->last_rigid_quad = submitted_quad;
                history->last_rigid_draw_task = data.task_count;
                if (source_triangle_count != 0U &&
                    !only_above_owner_receiver &&
                    (!rigid_policy.terrain_conforming_actor() ||
                     decomp_receiver.valid)) {
                    history->last_rigid_source_task = data.task_count;
                }
                std::copy_n(proxy.vertices.begin(),
                            history->rigid_vertex_templates.size(),
                            history->rigid_vertex_templates.begin());
                history->rigid_vertex_templates_valid = true;
                if (source_centre_valid &&
                    (!rigid_policy.terrain_conforming_actor() ||
                     decomp_receiver.valid)) {
                    history->last_source_centre = source_centre;
                    history->last_source_centre_valid = true;
                }
                if (owner_motion.valid) {
                    history->last_owner_motion = owner_motion;
                }
                if (ShadowTraceEnabled()) {
                    const auto submitted_shape =
                        dkr::runtime::presentation::
                            rigid_shadow_shape_metrics(submitted_quad);
                    const auto reference_shape =
                        dkr::runtime::presentation::
                            rigid_shadow_shape_metrics(
                                history->rigid_reference_quad);
                    float maximum_shape_edge_drift = 0.0F;
                    float maximum_shape_diagonal_drift = 0.0F;
                    if (submitted_shape.valid && reference_shape.valid) {
                        for (std::size_t edge = 0U;
                             edge < submitted_shape.edge_lengths.size();
                             ++edge) {
                            maximum_shape_edge_drift = std::max(
                                maximum_shape_edge_drift,
                                std::abs(
                                    submitted_shape.edge_lengths[edge] -
                                    reference_shape.edge_lengths[edge]));
                        }
                        for (std::size_t diagonal = 0U;
                             diagonal <
                                 submitted_shape.diagonal_lengths.size();
                             ++diagonal) {
                            maximum_shape_diagonal_drift = std::max(
                                maximum_shape_diagonal_drift,
                                std::abs(
                                    submitted_shape
                                            .diagonal_lengths[diagonal] -
                                    reference_shape
                                            .diagonal_lengths[diagonal]));
                        }
                    }
                    std::fprintf(
                        stderr,
                        "[trace][shadow-rigid] task=%llu key=%llu "
                        "source-batches=%zu source-vertices=%zu "
                        "proxy-vertices=%u kind=%u object=%u vehicle=%d "
                        "plane=%u magic-carpet=%u sticky=%u "
                        "scale-rejected=%u "
                        "receiver-rejected=%u bridged-missing=%u "
                        "history-gap=%u epoch=%u "
                        "clearance=%.3f surface-lift=%.3f "
                        "edges=(%.3f,%.3f,%.3f,%.3f) "
                        "diagonals=(%.3f,%.3f) cosine=%.6f area=%.3f "
                        "edge-drift=%.3f diagonal-drift=%.3f fallback=0\n",
                        static_cast<unsigned long long>(data.task_count),
                        static_cast<unsigned long long>(
                            data.shadow_history_key),
                        rigid_source_batch_count, source_count,
                        proxy.count,
                        static_cast<unsigned int>(rigid_policy.kind),
                        static_cast<unsigned int>(rigid_policy.object_id),
                        static_cast<int>(rigid_policy.vehicle),
                        rigid_policy.plane() ? 1U : 0U,
                        magic_carpet_shadow_active ? 1U : 0U,
                        used_sticky_history ? 1U : 0U,
                        scale_sample_rejected ? 1U : 0U,
                        receiver_rejected ? 1U : 0U,
                        rigid_source_batch_count == 0U ? 1U : 0U,
                        rigid_history_gap ? 1U : 0U,
                        history->interpolation_epoch,
                        history->taj_vertical_clearance,
                        rigid_actor_shadow_lift,
                        submitted_shape.edge_lengths[0],
                        submitted_shape.edge_lengths[1],
                        submitted_shape.edge_lengths[2],
                        submitted_shape.edge_lengths[3],
                        submitted_shape.diagonal_lengths[0],
                        submitted_shape.diagonal_lengths[1],
                        submitted_shape.adjacent_edge_cosine,
                        submitted_shape.area,
                        maximum_shape_edge_drift,
                        maximum_shape_diagonal_drift);
                }
            }
        }
        if (!rigid_actor_shadow_active && ShadowTraceEnabled()) {
            std::fprintf(
                stderr,
                "[trace][shadow-rigid-fallback] task=%llu key=%llu "
                "kind=%u object=%u vehicle=%d plane=%u magic-carpet=%u "
                "source-batches=%zu "
                "source-vertices=%zu reason=no-rigid-history-or-template "
                "fallback=1\n",
                static_cast<unsigned long long>(data.task_count),
                static_cast<unsigned long long>(data.shadow_history_key),
                static_cast<unsigned int>(rigid_policy.kind),
                static_cast<unsigned int>(rigid_policy.object_id),
                static_cast<int>(rigid_policy.vehicle),
                rigid_policy.plane() ? 1U : 0U,
                rigid_policy.magic_carpet() ? 1U : 0U,
                rigid_source_batch_count, source_count);
        }
    }

    std::array<dkr::runtime::presentation::ShadowCanonicalPage,
               dkr::runtime::presentation::kMaximumCanonicalShadowBatches>
        previous_pages{};
    std::array<dkr::runtime::presentation::ShadowCanonicalPage,
               dkr::runtime::presentation::kMaximumCanonicalShadowBatches>
        current_batches{};
    for (std::size_t page = 0U; page < previous_pages.size(); ++page) {
        for (std::size_t slot = 0U;
             slot < dkr::runtime::presentation::kCanonicalShadowBatchVertices;
             ++slot) {
            previous_pages[page][slot] =
                history->batches[page].slots[slot].texcoord;
        }
    }
    for (std::size_t batch = 0U; batch < data.shadow_batch_count; ++batch) {
        current_batches[batch] =
            data.pending_shadow_batches[batch].texcoords;
    }

    std::array<dkr::runtime::presentation::ShadowTexcoordSample,
               dkr::runtime::presentation::kMaximumCanonicalShadowBatches *
                   dkr::runtime::presentation::kCanonicalShadowBatchVertices>
        previous_motion_texcoords{};
    std::array<dkr::runtime::presentation::ShadowVertexSample,
               dkr::runtime::presentation::kMaximumCanonicalShadowBatches *
                   dkr::runtime::presentation::kCanonicalShadowBatchVertices>
        previous_motion_vertices{};
    std::array<dkr::runtime::presentation::ShadowVertexSample,
               dkr::runtime::presentation::kMaximumCanonicalShadowBatches *
                   dkr::runtime::presentation::kCanonicalShadowBatchVertices>
        current_motion_vertices{};
    std::array<RT64::RSP::Vertex,
               dkr::runtime::presentation::kMaximumCanonicalShadowBatches *
                   dkr::runtime::presentation::kCanonicalShadowBatchVertices>
        current_motion_rsp_vertices{};
    std::size_t current_motion_count = 0U;
    for (std::size_t page = 0U; page < previous_pages.size(); ++page) {
        for (std::size_t slot = 0U;
             slot < dkr::runtime::presentation::kCanonicalShadowBatchVertices;
             ++slot) {
            const std::size_t index =
                page * dkr::runtime::presentation::kCanonicalShadowBatchVertices +
                slot;
            previous_motion_texcoords[index] = previous_pages[page][slot];
            const auto& vertex =
                history->batches[page].slots[slot].authored_vertex;
            previous_motion_vertices[index] = {
                static_cast<float>(vertex.x), static_cast<float>(vertex.y),
                static_cast<float>(vertex.z)};
        }
    }
    for (std::size_t batch = 0U; batch < data.shadow_batch_count; ++batch) {
        const auto& pending = data.pending_shadow_batches[batch];
        for (std::size_t source = 0U; source < pending.count; ++source) {
            const auto& vertex = pending.vertices[source];
            current_motion_vertices[current_motion_count++] = {
                static_cast<float>(vertex.x), static_cast<float>(vertex.y),
                static_cast<float>(vertex.z)};
            current_motion_rsp_vertices[current_motion_count - 1U] = vertex;
        }
    }
    const auto shadow_translation =
        dkr::runtime::presentation::shadow_cloud_translation(
            previous_motion_texcoords, previous_motion_vertices,
            std::span<const dkr::runtime::presentation::ShadowVertexSample>(
                current_motion_vertices.data(), current_motion_count));
    const auto page_map =
        dkr::runtime::presentation::canonical_shadow_page_map(
            previous_pages,
            std::span<const dkr::runtime::presentation::ShadowCanonicalPage>(
                current_batches.data(), data.shadow_batch_count));
    const auto shadow_group = data.interpolation_groups.active_group();
    const std::uint8_t shadow_aspect = ActiveAspectMode(
        data.interpolation_groups, data.matrix_aspect_override_active,
        data.matrix_aspect_override);

    // Every undrawn slot follows a current shadow anchor instead of retaining
    // a stale world-space point. If topology later repopulates that slot, its
    // first interpolation grows from inside the moving footprint rather than
    // producing a long triangle from an obsolete terrain region.
    RT64::RSP::Vertex anchor{};
    for (std::size_t batch = 0U; batch < data.shadow_batch_count; ++batch) {
        if (data.pending_shadow_batches[batch].count != 0U) {
            anchor = data.pending_shadow_batches[batch].vertices[0];
            break;
        }
    }

    // Submit canonical pages in stable page order only after every temporary
    // DKR batch has been seen. The fixed sixteen-by-twenty-four stream keeps
    // RT64's vertex history continuous while the page matcher prevents a
    // terrain clipping split from shifting every later batch by one.
    for (std::size_t page = 0U;
         page < dkr::runtime::presentation::kMaximumCanonicalShadowBatches;
         ++page) {
        auto& page_history = history->batches[page];
        const std::uint8_t source_batch = page_map.batch_for_page[page];
        const bool page_drawn =
            source_batch != dkr::runtime::presentation::kInvalidShadowPage &&
            source_batch < data.shadow_batch_count;
        dkr::runtime::presentation::ShadowCanonicalSlotMap slot_map{};
        slot_map.source_for_slot.fill(
            dkr::runtime::presentation::kInvalidShadowVertexSlot);
        slot_map.slot_for_source.fill(
            dkr::runtime::presentation::kInvalidShadowVertexSlot);
        bool correspondence_safe = false;
        bool topology_complete = page_drawn;
        float maximum_residual = 0.0F;
        float maximum_mapped_residual = 0.0F;
        std::size_t newly_mapped_slots = 0U;
        std::uint64_t topology_signature =
            dkr::runtime::presentation::kShadowTopologyHashOffset;
        const StateData::PendingShadowBatch* batch = nullptr;
        if (page_drawn) {
            batch = &data.pending_shadow_batches[source_batch];
            std::array<dkr::runtime::presentation::ShadowVertexSample,
                       dkr::runtime::presentation::kCanonicalShadowBatchVertices>
                previous_vertices{};
            std::array<dkr::runtime::presentation::ShadowVertexSample,
                       dkr::runtime::presentation::kCanonicalShadowBatchVertices>
                current_vertices{};
            for (std::size_t slot = 0U; slot < previous_vertices.size(); ++slot) {
                const auto& vertex =
                    page_history.slots[slot].authored_vertex;
                previous_vertices[slot] = {
                    static_cast<float>(vertex.x), static_cast<float>(vertex.y),
                    static_cast<float>(vertex.z)};
            }
            for (std::size_t source = 0U; source < batch->count; ++source) {
                const auto& vertex = batch->vertices[source];
                current_vertices[source] = {
                    static_cast<float>(vertex.x), static_cast<float>(vertex.y),
                    static_cast<float>(vertex.z)};
            }
            if (rigid_actor_shadow_active) {
                // Rigid proxies already have a permanent UV order: four
                // corners for vehicles, or Taj's fixed 13-point lattice.
                // Preserve that order directly. A generic nearest-motion
                // remap is useful for clipped terrain but can permute the
                // presentation surface after a gap and rotate its texture.
                for (std::uint8_t point = 0U; point < batch->count; ++point) {
                    slot_map.source_for_slot[point] = point;
                    slot_map.slot_for_source[point] = point;
                }
            } else {
                slot_map = dkr::runtime::presentation::
                    canonical_shadow_motion_slot_map(
                        previous_pages[page], previous_vertices,
                        std::span<const dkr::runtime::presentation::
                            ShadowTexcoordSample>(
                                batch->texcoords.data(), batch->count),
                        std::span<const dkr::runtime::presentation::
                            ShadowVertexSample>(
                                current_vertices.data(), batch->count),
                        shadow_translation);
            }

            auto current_min = current_vertices[0];
            auto current_max = current_vertices[0];
            for (std::size_t source = 1U; source < batch->count; ++source) {
                current_min.x = std::min(current_min.x, current_vertices[source].x);
                current_min.y = std::min(current_min.y, current_vertices[source].y);
                current_min.z = std::min(current_min.z, current_vertices[source].z);
                current_max.x = std::max(current_max.x, current_vertices[source].x);
                current_max.y = std::max(current_max.y, current_vertices[source].y);
                current_max.z = std::max(current_max.z, current_vertices[source].z);
            }
            const float extent_x = current_max.x - current_min.x;
            const float extent_y = current_max.y - current_min.y;
            const float extent_z = current_max.z - current_min.z;
            maximum_residual = std::max(
                6.0F, std::sqrt(extent_x * extent_x + extent_y * extent_y +
                                 extent_z * extent_z) * 0.25F);
            correspondence_safe =
                dkr::runtime::presentation::canonical_shadow_slot_map_corresponds(
                    previous_pages[page], previous_vertices,
                    std::span<const
                        dkr::runtime::presentation::ShadowVertexSample>(
                            current_vertices.data(), batch->count),
                    slot_map, shadow_translation, maximum_residual);

            for (std::size_t source = 0U; source < batch->count; ++source) {
                const std::uint8_t slot = slot_map.slot_for_source[source];
                if (slot == dkr::runtime::presentation::
                                kInvalidShadowVertexSlot ||
                    slot >= previous_vertices.size()) {
                    continue;
                }
                newly_mapped_slots += previous_pages[page][slot].valid
                    ? 0U : 1U;
                const float dx = current_vertices[source].x -
                    (previous_vertices[slot].x + shadow_translation.x);
                const float dy = current_vertices[source].y -
                    (previous_vertices[slot].y + shadow_translation.y);
                const float dz = current_vertices[source].z -
                    (previous_vertices[slot].z + shadow_translation.z);
                maximum_mapped_residual = std::max(
                    maximum_mapped_residual,
                    std::sqrt(dx * dx + dy * dy + dz * dz));
            }

            topology_signature =
                dkr::runtime::presentation::shadow_topology_hash_value(
                    topology_signature, batch->triangle_count);
            for (std::size_t triangle_index = 0U;
                 triangle_index < batch->triangle_count; ++triangle_index) {
                const auto& triangle = batch->triangles[triangle_index];
                topology_signature =
                    dkr::runtime::presentation::shadow_topology_hash_value(
                        topology_signature, triangle.flag & 0x40U);
                for (std::size_t corner = 0U;
                     corner < triangle.vertices.size(); ++corner) {
                    const std::uint8_t source = triangle.vertices[corner];
                    const std::uint8_t slot = source < batch->count
                        ? slot_map.slot_for_source[source]
                        : dkr::runtime::presentation::kInvalidShadowVertexSlot;
                    topology_complete = topology_complete &&
                        slot != dkr::runtime::presentation::kInvalidShadowVertexSlot;
                    topology_signature =
                        dkr::runtime::presentation::shadow_topology_hash_value(
                            topology_signature, slot);
                }
            }
        }

        // A terrain seam can create or destroy clipped triangles even though
        // the shadow owner remains the same. The fixed canonical stream and
        // distributed dormant positions make that a bounded geometry morph;
        // topology diagnostics must never toggle interpolation for the page.
        const bool interpolate_page_vertices =
            dkr::runtime::presentation::shadow_page_vertex_interpolation(
                shadow_group.identity);

        if (ShadowTraceEnabled() && page_drawn &&
            (!interpolate_page_vertices ||
             page_history.topology_signature != topology_signature)) {
            std::size_t previous_active_slots = 0U;
            std::size_t retained_slots = 0U;
            for (std::size_t slot = 0U;
                 slot < dkr::runtime::presentation::
                     kCanonicalShadowBatchVertices;
                 ++slot) {
                previous_active_slots += previous_pages[page][slot].valid
                    ? 1U : 0U;
                retained_slots +=
                    slot_map.source_for_slot[slot] !=
                        dkr::runtime::presentation::
                            kInvalidShadowVertexSlot
                    ? 1U : 0U;
            }
            std::fprintf(
                stderr,
                "[trace][shadow] task=%llu key=%llu page=%zu batch=%u "
                "previous-active=%zu current=%u retained=%zu "
                "new=%zu max-mapped-residual=%.1f "
                "previous-topology-valid=%u topology-complete=%u "
                "topology-same=%u correspondence=%u interpolate=%u "
                "task-allowed=%u "
                "translation=(%.1f,%.1f,%.1f) residual-limit=%.1f\n",
                static_cast<unsigned long long>(data.task_count),
                static_cast<unsigned long long>(data.shadow_history_key),
                page, static_cast<unsigned int>(source_batch),
                previous_active_slots,
                batch != nullptr ? batch->count : 0U, retained_slots,
                newly_mapped_slots, maximum_mapped_residual,
                page_history.topology_valid ? 1U : 0U,
                topology_complete ? 1U : 0U,
                page_history.topology_signature == topology_signature ? 1U : 0U,
                correspondence_safe ? 1U : 0U,
                interpolate_page_vertices ? 1U : 0U,
                dkr::runtime::presentation::task_interpolation_allowed()
                    ? 1U : 0U,
                shadow_translation.x, shadow_translation.y,
                shadow_translation.z,
                maximum_residual);
        }

        float maximum_submitted_residual = 0.0F;
        for (std::size_t slot = 0U;
             slot < dkr::runtime::presentation::kCanonicalShadowBatchVertices;
             ++slot) {
            RT64::RSP::Vertex vertex = anchor;
            RT64::RSP::Vertex authored_vertex = anchor;
            page_history.slots[slot].texcoord = {};
            const std::uint8_t source_vertex =
                slot_map.source_for_slot[slot];
            if (batch != nullptr &&
                source_vertex !=
                    dkr::runtime::presentation::kInvalidShadowVertexSlot &&
                source_vertex < batch->count) {
                authored_vertex = batch->vertices[source_vertex];
                vertex = authored_vertex;
                page_history.slots[slot].texcoord =
                    batch->texcoords[source_vertex];
                if (page_history.slots[slot].positioned &&
                    !rigid_actor_shadow_active) {
                    const auto& previous = page_history.slots[slot].vertex;
                    const auto bounded = dkr::runtime::presentation::
                        bounded_shadow_presentation_vertex(
                             {static_cast<float>(previous.x),
                              static_cast<float>(previous.y),
                             static_cast<float>(previous.z)},
                             {static_cast<float>(authored_vertex.x),
                              static_cast<float>(authored_vertex.y),
                              static_cast<float>(authored_vertex.z)},
                             shadow_translation, maximum_residual);
                    vertex.x = rounded_shadow_coordinate(bounded.x);
                    vertex.y = rounded_shadow_coordinate(bounded.y);
                    vertex.z = rounded_shadow_coordinate(bounded.z);

                    const float dx = static_cast<float>(vertex.x) -
                        (static_cast<float>(previous.x) + shadow_translation.x);
                    const float dy = static_cast<float>(vertex.y) -
                        (static_cast<float>(previous.y) + shadow_translation.y);
                    const float dz = static_cast<float>(vertex.z) -
                        (static_cast<float>(previous.z) + shadow_translation.z);
                    maximum_submitted_residual = std::max(
                        maximum_submitted_residual,
                        std::sqrt(dx * dx + dy * dy + dz * dz));
                }
            } else if (batch != nullptr && batch->count != 0U) {
                // Cover the whole active page with its dormant slots. Keeping
                // all spare history at one nearest point left the opposite seam
                // edge with no safe slot when a clipped vertex was born there.
                const std::size_t dormant_source =
                    dkr::runtime::presentation::
                        canonical_shadow_dormant_source(slot, batch->count);
                vertex = batch->vertices[dormant_source];
                authored_vertex = vertex;
            } else if (current_motion_count != 0U) {
                // An inactive page is a reserve for a future DKR cache split.
                // Seed every reserve page across the complete current shadow
                // cloud so a new page never grows from one remote anchor.
                const std::size_t dormant_source =
                    dkr::runtime::presentation::
                        canonical_shadow_dormant_source(
                            slot, current_motion_count);
                vertex = current_motion_rsp_vertices[dormant_source];
                authored_vertex = vertex;
            } else if (page_history.slots[slot].positioned) {
                // An unused page still follows the owner's measured motion.
                // This keeps its dormant slots close if terrain clipping makes
                // the page visible again on the next authored frame.
                const auto& previous = page_history.slots[slot].vertex;
                vertex = previous;
                vertex.x = rounded_shadow_coordinate(
                    static_cast<float>(previous.x) + shadow_translation.x);
                vertex.y = rounded_shadow_coordinate(
                    static_cast<float>(previous.y) + shadow_translation.y);
                vertex.z = rounded_shadow_coordinate(
                    static_cast<float>(previous.z) + shadow_translation.z);
                authored_vertex = vertex;
            }
            page_history.slots[slot].vertex = vertex;
            page_history.slots[slot].authored_vertex = authored_vertex;
            page_history.slots[slot].positioned = true;
            std::memcpy(state->RDRAM + kScratchVertexAddress +
                            slot * sizeof(vertex),
                        &vertex, sizeof(vertex));
        }
        if (ShadowTraceEnabled() && page_drawn &&
            (!interpolate_page_vertices ||
             page_history.topology_signature != topology_signature)) {
            std::fprintf(
                stderr,
                "[trace][shadow-submit] task=%llu key=%llu page=%zu "
                "max-submitted-residual=%.1f residual-limit=%.1f\n",
                static_cast<unsigned long long>(data.task_count),
                static_cast<unsigned long long>(data.shadow_history_key),
                page, maximum_submitted_residual, maximum_residual);
        }
        SelectInterpolationGroup(
            rsp,
            dkr::runtime::presentation::make_shadow_page_group_identity(
                shadow_group.identity, page,
                history->interpolation_epoch),
            interpolate_page_vertices, false, false, shadow_aspect,
            interpolate_page_vertices);
        rsp.modelViewProjChanged = true;
        auto& workload = state->ext.workloadQueue
                             ->workloads[state->ext.workloadQueue->writeCursor];
        const std::uint32_t page_vertex_start =
            workload.drawData.vertexCount();
        const auto original_shadow_matrix =
            rsp.modelMatrixStack[data.selected_matrix];
        if (rigid_actor_shadow_active && page_drawn) {
            // RSP vertices use integer world coordinates, so a fractional lift
            // cannot be represented in the vertex itself. Apply the fractional
            // separation in the temporary presentation matrix used only while
            // these vertices are transformed, then restore the authored matrix.
            auto lifted_shadow_matrix = original_shadow_matrix;
            lifted_shadow_matrix[3] += hlslpp::float4(
                0.0F, rigid_actor_shadow_lift, 0.0F, 0.0F);
            rsp.modelMatrixStack[data.selected_matrix] = lifted_shadow_matrix;
            rsp.modelViewProjChanged = true;
        }
        rsp.setVertex(
            kScratchVertexAddress,
            static_cast<std::uint32_t>(
                dkr::runtime::presentation::kCanonicalShadowBatchVertices),
            0U);
        if (rigid_actor_shadow_active && page_drawn) {
            rsp.modelMatrixStack[data.selected_matrix] = original_shadow_matrix;
            rsp.modelViewProjChanged = true;
        }
        // DKR's triangle-corner UV command bypasses the ordinary texture scale,
        // so writing the same values into RSP::Vertex::s/t is not equivalent and
        // can sample transparent texels. Preserve the original modifyVertex
        // semantics, but apply every canonical slot exactly once before any
        // triangle uses it. RT64 only clones an already-used vertex, therefore
        // this ordering keeps the visible shadow and the invariant 24-vertex
        // stream at the same time.
        for (std::size_t slot = 0U;
             slot < dkr::runtime::presentation::kCanonicalShadowBatchVertices;
             ++slot) {
            const auto& texcoord = page_history.slots[slot].texcoord;
            const std::uint32_t packed_texcoord =
                (static_cast<std::uint32_t>(static_cast<std::uint16_t>(
                     texcoord.valid ? texcoord.s : 0))
                 << 16U) |
                static_cast<std::uint16_t>(texcoord.valid ? texcoord.t : 0);
            rsp.modifyVertex(static_cast<std::uint16_t>(slot),
                             G_MWO_POINT_ST, packed_texcoord);
        }

        page_history.topology_signature = topology_signature;
        page_history.topology_valid = page_drawn && topology_complete;
        if (batch != nullptr) {
            for (std::size_t triangle_index = 0U;
                 triangle_index < batch->triangle_count; ++triangle_index) {
                const auto& triangle = batch->triangles[triangle_index];
                std::array<std::uint8_t, 3> vertices{};
                bool valid = true;
                for (std::size_t corner = 0U; corner < vertices.size(); ++corner) {
                    vertices[corner] =
                        slot_map.slot_for_source[triangle.vertices[corner]];
                    valid = valid &&
                        vertices[corner] !=
                            dkr::runtime::presentation::kInvalidShadowVertexSlot;
                }
                if (!valid) continue;

                rsp.clearGeometryMode(rsp.cullBothMask);
                if ((triangle.flag & 0x40U) == 0U) {
                    const bool positive_x =
                        rsp.viewportStack[rsp.viewportStackSize - 1].scale.x >
                        0.0F;
                    rsp.setGeometryMode(
                        positive_x
                            ? active_->gbi_->constants[F3DENUM::G_CULL_BACK]
                            : active_->gbi_->constants[F3DENUM::G_CULL_FRONT]);
                }
                rsp.drawIndexedTri(vertices[0], vertices[1], vertices[2]);
            }
        }
        const std::uint32_t page_vertex_count =
            workload.drawData.vertexCount() - page_vertex_start;
        if (ShadowTraceEnabled() &&
            (page_vertex_count !=
                 dkr::runtime::presentation::kCanonicalShadowBatchVertices ||
             (batch != nullptr && batch->texcoord_conflict_count != 0U))) {
            std::fprintf(
                stderr,
                "[trace][shadow-cardinality] task=%llu key=%llu page=%zu "
                "vertices=%u expected=%zu uv-conflicts=%u triangles=%u\n",
                static_cast<unsigned long long>(data.task_count),
                static_cast<unsigned long long>(data.shadow_history_key),
                page, page_vertex_count,
                dkr::runtime::presentation::kCanonicalShadowBatchVertices,
                batch != nullptr ? batch->texcoord_conflict_count : 0U,
                batch != nullptr ? batch->triangle_count : 0U);
        }
    }

    // Restore the owner scope before its closing marker is decoded. This keeps
    // any following command in the shadow scope from inheriting the final page
    // identity.
    SelectInterpolationGroup(
        rsp, shadow_group.identity, shadow_group.interpolate_vertices,
        shadow_group.interpolate_texcoords, shadow_group.interpolate_tiles,
        shadow_aspect);
    rsp.modelViewProjChanged = true;

    data.pending_shadow_batch = {};
    data.pending_shadow_batches = {};
    data.shadow_scope_active = false;
    data.shadow_history_key = 0U;
    data.shadow_expected_batch_count = 0U;
    data.shadow_batch_count = 0U;
}

void dkr::runtime::F3DDKRRT64Bridge::process(RT64::Application& application,
                                             const OSTask& task) {
    active_ = this;
    StateData fresh{};
    fresh.task_count = data_->task_count + 1;
    *data_ = fresh;

    RT64::State* state = application.state.get();
    RT64::RSP& rsp = *state->rsp;
    rsp.reset();

    application.interpreter->hleGBI = gbi_;
    application.interpreter->UCode.textAddress = task.t.ucode & 0x00FFFFF8U;
    application.interpreter->UCode.dataAddress = task.t.ucode_data & 0x00FFFFF8U;
    rsp.setGBI(gbi_);
    RT64::GBI_F3D::reset(state);
    SelectInterpolationGroup(rsp, G_EX_ID_IGNORE);

    const auto identity = hlslpp::float4x4::identity();
    rsp.viewMatrixStack[0] = identity;
    rsp.projMatrixStack[0] = identity;
    rsp.viewProjMatrixStack[0] = identity;
    rsp.invViewProjMatrixStack[0] = identity;
    rsp.extended.viewMatrix = identity;
    rsp.extended.projMatrix = identity;
    rsp.extended.viewProjMatrix = identity;
    rsp.extended.invViewMatrix = identity;
    rsp.extended.invProjMatrix = identity;
    rsp.extended.invViewProjMatrix = identity;
    rsp.projectionMatrixChanged = true;
    rsp.modelViewProjChanged = true;

    const std::uint32_t start = PhysicalAddress(rsp, task.t.data_ptr);
    application.processDisplayLists(application.core.RDRAM, start, 0, true);
    // Malformed/aborted tasks must not leave presentation state in RT64.
    while (data_->hud_widget_depth > 0) {
        state->flush();
        --data_->hud_widget_depth;
        state->rdp->popScissor();
    }
    dkr::runtime::presentation::select_split_world_projection(
        rsp, false, data_->split_world_projection_active);
    if (data_->hud_alignment_depth != 0U || data_->aspect_scope_depth != 0U ||
        data_->rejected_hud_scopes.depth != 0U ||
        data_->rejected_aspect_scopes.depth != 0U ||
        data_->interpolation_groups.has_active_scope() ||
        data_->interpolation_groups.rejected_scope_begins() != 0U) {
        ReportPresentationError(0U, "unbalanced presentation group");
    }
    while (data_->hud_alignment_depth > 0U) {
        const auto scope = data_->hud_alignment_scopes[
            --data_->hud_alignment_depth];
        if (scope.pushed_scissor) state->rdp->popScissor();
        if (scope.pushed_hud_projection) hud::pop_hud_projection(rsp);
        if (scope.changed_scissor) {
            state->rdp->setScissorAlign(scope.previous_scissor);
        }
        if (scope.changed_rect_alignment) {
            state->rdp->setRectAlign(scope.previous_rect_alignment);
        }
        if (scope.changed_clip_ratios) {
            for (std::size_t edge = 0U;
                 edge < scope.previous_clip_ratios.size(); ++edge) {
                state->rsp->setClipRatioEdge(
                    static_cast<std::uint8_t>(edge),
                    scope.previous_clip_ratios[edge]);
            }
        }
        state->flush();
        state->rdp->setRectAspect(scope.previous_rect_aspect);
        data_->rect_aspect_mode = scope.previous_rect_aspect;
        data_->matrix_aspect_override_active =
            scope.previous_matrix_aspect_override_active;
        data_->matrix_aspect_override =
            scope.previous_matrix_aspect_override;
        data_->background_fill_stretch_active =
            scope.previous_background_fill_stretch;
    }
    while (data_->aspect_scope_depth > 0U) {
        const auto scope = data_->aspect_scopes[--data_->aspect_scope_depth];
        state->flush();
        state->rdp->setRectAspect(scope.previous_rect_aspect);
        data_->rect_aspect_mode = scope.previous_rect_aspect;
        data_->matrix_aspect_override_active =
            scope.previous_matrix_aspect_override_active;
        data_->matrix_aspect_override = scope.previous_matrix_aspect_override;
        data_->background_fill_stretch_active =
            scope.previous_background_fill_stretch;
    }
    if (data_->rect_aspect_mode != G_EX_ASPECT_AUTO) {
        state->flush();
        state->rdp->setRectAspect(G_EX_ASPECT_AUTO);
        data_->rect_aspect_mode = G_EX_ASPECT_AUTO;
    }
    data_->matrix_aspect_override_active = false;
    data_->matrix_aspect_override = G_EX_ASPECT_AUTO;
    data_->background_fill_stretch_active = false;
    g_completed_tasks.store(data_->task_count, std::memory_order_release);
    if (water::enabled()) {
        water::end_task(presentation::task_scene_generation(),
            ReadU32(state->RDRAM, revision_addresses::CurrentMapId & 0x007FFFFCU),
            ReadU32(state->RDRAM, kCurrentMenuIdAddress & 0x007FFFFCU));
    }
}

void dkr::runtime::F3DDKRRT64Bridge::RejectTask(
    RT64::DisplayList** display_list) {
    if (active_ != nullptr) {
        active_->data_->task_rejected = true;
    }
    if (display_list != nullptr) {
        *display_list = nullptr;
    }
}

void dkr::runtime::F3DDKRRT64Bridge::MoveMem(
    RT64::State* state, RT64::DisplayList** display_list) {
    RT64::GBI_F3D::moveMem(state, display_list);
}

void dkr::runtime::F3DDKRRT64Bridge::Dispatch(
    RT64::State* state, RT64::DisplayList** display_list) {
    if (active_ == nullptr || state == nullptr || display_list == nullptr ||
        *display_list == nullptr) {
        if (display_list != nullptr) {
            *display_list = nullptr;
        }
        return;
    }
    RT64::DisplayList* command = *display_list;
    ApplyPresentationMarkers(state, command);
    const std::uint8_t opcode =
        static_cast<std::uint8_t>(command->w0 >> 24U);
    const Handler handler = active_->original_handlers_[opcode];
    if (handler == nullptr || handler == &Dispatch) {
        if (g_logged_counted_errors++ < 64U) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected unsupported opcode=0x%02X "
                         "task=%llu\n",
                         opcode,
                         static_cast<unsigned long long>(
                             active_->data_->task_count));
        }
        RejectTask(display_list);
        return;
    }
    if (opcode == kFullSyncOpcode && (active_->data_->split_color_image_count != 0U ||
                                     active_->data_->hud_color_image_count != 0U)) {
        // Finish pending calls before normalising the producer-owned aggregate;
        // never access or mutate a workload after fullSync hands it to RT64.
        state->flush();
        auto& workload = state->ext.workloadQueue->workloads[
            state->ext.workloadQueue->writeCursor];
        const auto& data = *active_->data_;
        static std::uint64_t last_trace_task = 0U;
        static unsigned trace_count = 0U;
        const bool trace = SplitTraceEnabled() && trace_count < 24U &&
            (trace_count == 0U || data.task_count >= last_trace_task + 120U);
        for (std::uint32_t i = 0; i < workload.fbPairCount; ++i) {
            auto& pair = workload.fbPairs[i];
            const auto end = data.split_color_images.begin() + data.split_color_image_count;
            const auto hud_end = data.hud_color_images.begin() + data.hud_color_image_count;
            if (std::find(data.split_color_images.begin(), end,
                          pair.colorImage.address) != end ||
                std::find(data.hud_color_images.begin(), hud_end,
                          pair.colorImage.address) != hud_end) {
                const auto authored_union = pair.scissorRect;
                dkr::runtime::presentation::normalise_split_framebuffer_extent(pair);
                if (trace) {
                    unsigned adjusted = 0U;
                    unsigned rectangles = 0U;
                    for (unsigned j = 0; j < pair.projectionCount; ++j) {
                        const auto& projection = pair.projections[j];
                        if (projection.type == RT64::Projection::Type::Rectangle) ++rectangles;
                        if (projection.transformsIndex < workload.drawData.viewProjTransformGroups.size()) {
                            const auto group = workload.drawData.viewProjTransformGroups[projection.transformsIndex];
                            if (workload.drawData.transformGroups[group].aspectMode == G_EX_ASPECT_ADJUST) ++adjusted;
                        }
                    }
                    std::fprintf(stderr, "[split-screen] task=%llu pair=%u cover=%.4f native-width=%u scissor=[%d,%d,%d,%d]->[%d,%d,%d,%d] projections=%u adjusted=%u rects=%u projection-restored=%u\n",
                        static_cast<unsigned long long>(data.task_count), i, data.split_viewport_cover,
                        pair.colorImage.width, authored_union.ulx, authored_union.uly, authored_union.lrx, authored_union.lry,
                        pair.scissorRect.ulx, pair.scissorRect.uly, pair.scissorRect.lrx, pair.scissorRect.lry,
                        pair.projectionCount, adjusted, rectangles, !data.split_world_projection_active);
                }
            }
        }
        if (trace) { last_trace_task = data.task_count; ++trace_count; }
    }
    handler(state, display_list);
    AdjustSplitViewportCommand(state, command, opcode);
}

void dkr::runtime::F3DDKRRT64Bridge::AdjustSplitViewportCommand(
    RT64::State* state, RT64::DisplayList* command, std::uint8_t opcode) {
    StateData& data = *active_->data_;
    if ((!data.split_viewport_fill_pending && !data.postrace_full_viewport_pending) || command == nullptr) {
        return;
    }

    if (opcode == kSetScissorOpcode &&
        !data.split_viewport_scissor_adjusted) {
        auto& scissor = state->rdp->scissorRectStack[
            state->rdp->scissorStackSize - 1];
        const auto expanded =
            dkr::runtime::enhancements::expand_split_viewport_horizontal_range(
                static_cast<float>(scissor.ulx),
                static_cast<float>(scissor.lrx),
                data.split_viewport_cover,
                static_cast<int>(data.split_viewport_camera));
        if (data.postrace_full_viewport_pending) {
            dkr::runtime::presentation::fill_postrace_scissor(scissor);
        } else {
            scissor.ulx = static_cast<std::int32_t>(std::lround(expanded.left));
            scissor.lrx = static_cast<std::int32_t>(std::lround(expanded.right));
        }
        state->updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
        data.split_viewport_scissor_adjusted = true;
    } else if (opcode == kMoveMemOpcode &&
               command->p0(16, 8) == kViewportMoveMemType &&
               !data.split_viewport_rsp_adjusted) {
        auto& viewport = state->rsp->viewportStack[
            state->rsp->viewportStackSize - 1];
        const float left = viewport.translate.x - viewport.scale.x;
        const float right = viewport.translate.x + viewport.scale.x;
        const auto expanded =
            dkr::runtime::enhancements::expand_split_viewport_horizontal_range(
                left, right, data.split_viewport_cover,
                static_cast<int>(data.split_viewport_camera));
        if (data.postrace_full_viewport_pending) {
            dkr::runtime::presentation::fill_postrace_viewport(viewport);
        } else {
            viewport.scale.x = (expanded.right - expanded.left) * 0.5F;
            viewport.translate.x = (expanded.left + expanded.right) * 0.5F;
        }
        state->rsp->viewportChanged = true;
        data.split_viewport_rsp_adjusted = true;
    }

    if (data.split_viewport_scissor_adjusted &&
        data.split_viewport_rsp_adjusted) {
        data.split_viewport_fill_pending = false;
        data.postrace_full_viewport_pending = false;
        return;
    }

    if (data.split_viewport_commands_remaining > 0U) {
        --data.split_viewport_commands_remaining;
    }
    if (data.split_viewport_commands_remaining == 0U) {
        data.split_viewport_fill_pending = false;
        data.postrace_full_viewport_pending = false;
    }
}

void dkr::runtime::F3DDKRRT64Bridge::ApplyPresentationMarkers(
    RT64::State* state, RT64::DisplayList* display_list) {
    if (active_ == nullptr || state == nullptr || display_list == nullptr ||
        state->RDRAM == nullptr) {
        return;
    }
    const std::uintptr_t rdram_begin =
        reinterpret_cast<std::uintptr_t>(state->RDRAM);
    const std::uintptr_t command =
        reinterpret_cast<std::uintptr_t>(display_list);
    if (command < rdram_begin ||
        command > rdram_begin + kRDRAMSize - sizeof(RT64::DisplayList)) {
        return;
    }
    const auto markers =
        dkr::runtime::presentation::active_presentation_markers(
            static_cast<std::uint32_t>(command - rdram_begin));
    for (std::size_t index = 0U; index < markers.count; ++index) {
        const auto& marker = markers.markers[index];
        if (marker.kind == PresentationMarkerKind::HudRect) {
            state->flush();
            active_->data_->hud_rect_bias=marker.mode!=0 ? (marker.variant&3U) : 0U;
            continue;
        }
        if (marker.kind == PresentationMarkerKind::HudWidget) {
            auto& data = *active_->data_;
            auto& rsp = *state->rsp;
            state->flush();
            if (marker.mode == 1) {
                if (data.rejected_hud_widgets.reject_begin(data.hud_widget_depth,data.hud_widgets.size())) continue;
                if (!marker.hud_transform.valid()) { ++data.rejected_hud_widgets.depth; continue; }
                auto& scope = data.hud_widgets[data.hud_widget_depth++];
                scope.transform = marker.hud_transform;
                const auto& t = scope.transform;
                state->rdp->pushScissor();
                auto& clip = state->rdp->scissorRectStack[state->rdp->scissorStackSize-1];
                clip.uly = std::max(clip.uly,static_cast<int>(t.clip_top*4));
                clip.lry = std::min(clip.lry,static_cast<int>(t.clip_bottom*4));
                state->updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
            } else if (!data.rejected_hud_widgets.consume_end() && data.hud_widget_depth > 0) {
                --data.hud_widget_depth;
                state->rdp->popScissor();
                state->updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
            }
            continue;
        }
        ApplyPresentationGroup(state, marker.mode, marker.token,
                               marker.variant, marker.kind);
    }
}

void dkr::runtime::F3DDKRRT64Bridge::PresentationGroup(
    RT64::State* state, RT64::DisplayList** display_list) {
    const std::uint32_t mode =
        (*display_list)->w1 & kPresentationGroupModeMask;
    const std::uint16_t presentation_token = static_cast<std::uint16_t>(
        ((*display_list)->w0 >> 8U) & 0xFFFFU);
    const std::uint8_t presentation_variant = static_cast<std::uint8_t>(
        ((*display_list)->w1 >> 3U) & 0x1FU);
    ApplyPresentationGroup(state, mode, presentation_token,
                           presentation_variant,
                           dkr::runtime::presentation::legacy_presentation_marker_kind(
                               static_cast<std::uint8_t>(mode), presentation_variant));
}

void dkr::runtime::F3DDKRRT64Bridge::ReportPresentationError(
    std::size_t index, const char* message, PresentationMarkerKind kind,
    std::uint32_t mode, std::uint8_t variant) {
    auto& budget = diagnostic_budgets_.at(index);
    if (!budget.record(data_->task_count)) return;
    std::fprintf(stderr,
        "[boot][f3ddkr] %s task=%llu kind=%u mode=%u variant=%u "
        "total=%llu since-report=%llu begins=%u ends=%u "
        "depth=%zu aspect-depth=%zu hud-depth=%zu rejected=%u\n",
        message, static_cast<unsigned long long>(data_->task_count),
        static_cast<unsigned>(kind), mode, variant,
        static_cast<unsigned long long>(budget.total),
        static_cast<unsigned long long>(budget.take_pending()),
        data_->presentation_group_begins, data_->presentation_group_ends,
        data_->interpolation_groups.scope_depth(), data_->aspect_scope_depth,
        data_->hud_alignment_depth,
        data_->interpolation_groups.rejected_scope_begins());
}

void dkr::runtime::F3DDKRRT64Bridge::ApplyPresentationGroup(
    RT64::State* state, std::uint32_t mode,
    std::uint16_t presentation_token,
    std::uint8_t presentation_variant, PresentationMarkerKind kind) {
    StateData& data = *active_->data_;
    if (mode > 9U || !dkr::runtime::presentation::valid_presentation_marker(
            kind, static_cast<std::uint8_t>(mode), presentation_variant)) {
        active_->ReportPresentationError(7U, "invalid presentation marker",
                                         kind, mode, presentation_variant);
        return;
    }
    const auto apply_active_group = [&]() {
        const auto active_group = data.interpolation_groups.active_group();
        SelectInterpolationGroup(
            *state->rsp, active_group.identity,
            active_group.interpolate_vertices,
            active_group.interpolate_texcoords,
            active_group.interpolate_tiles,
            ActiveAspectMode(data.interpolation_groups,
                             data.matrix_aspect_override_active,
                             data.matrix_aspect_override), false,
            active_group.procedural_water ? active_group.water_scroll_tag : 0U);
        state->rsp->modelViewProjChanged = true;
        dkr::runtime::presentation::select_split_world_projection(
            *state->rsp,
            data.split_color_image_count != 0U &&
                data.interpolation_groups.contains_mode(kPresentationGroupAspectAdjustMode),
            data.split_world_projection_active);
    };

    if (kind == PresentationMarkerKind::PostraceFullViewport) {
        data.postrace_full_viewport_pending = true;
        data.split_viewport_fill_pending = false;
        data.split_viewport_scissor_adjusted = false;
        data.split_viewport_rsp_adjusted = false;
        data.split_viewport_commands_remaining = 32U;
        return;
    }
    if (kind == PresentationMarkerKind::SplitViewport) {
        data.postrace_full_viewport_pending = false;
        const auto address = state->rdp->colorImage.address;
        const auto end = data.split_color_images.begin() + data.split_color_image_count;
        if (std::find(data.split_color_images.begin(), end, address) == end &&
            data.split_color_image_count < data.split_color_images.size()) {
            data.split_color_images[data.split_color_image_count++] = address;
        }
        data.split_viewport_fill_pending = true;
        data.split_viewport_scissor_adjusted = false;
        data.split_viewport_rsp_adjusted = false;
        data.split_viewport_camera =
            static_cast<std::uint8_t>(presentation_token & 3U);
        data.split_viewport_cover =
            static_cast<float>(presentation_token >> 2U) /
            kSplitViewportCoverQuantisation;
        data.split_viewport_commands_remaining = 32U;
        return;
    }

    if (dkr::runtime::presentation::is_aspect_marker(kind)) {
        state->flush();
        if (mode == kHudPassMarkerBeginMode) {
            if (data.rejected_aspect_scopes.reject_begin(
                    data.aspect_scope_depth, data.aspect_scopes.size())) {
                active_->ReportPresentationError(1U, "aspect scope overflow",
                                                 kind, mode, presentation_variant);
                return;
            }
            auto& scope = data.aspect_scopes[data.aspect_scope_depth++];
            scope.kind = kind;
            scope.previous_matrix_aspect_override_active =
                data.matrix_aspect_override_active;
            scope.previous_matrix_aspect_override =
                data.matrix_aspect_override;
            scope.previous_rect_aspect = data.rect_aspect_mode;
            scope.previous_background_fill_stretch =
                data.background_fill_stretch_active;

            if (kind == PresentationMarkerKind::BackgroundAspect) {
                // Background-only rectangles may cover the host width. They do
                // not alter the matrix aspect used by world or HUD geometry.
                data.rect_aspect_mode = G_EX_ASPECT_STRETCH;
                data.background_fill_stretch_active =
                    presentation_token <= 2U ? presentation_token : 0U;
            } else {
                // Framed views and fixed game UI preserve their own aspect.
                data.matrix_aspect_override_active = true;
                data.matrix_aspect_override = G_EX_ASPECT_ADJUST;
                data.rect_aspect_mode = G_EX_ASPECT_ADJUST;
                data.background_fill_stretch_active = false;
            }
            state->rdp->setRectAspect(data.rect_aspect_mode);
        } else if (data.rejected_aspect_scopes.consume_end()) {
            return;
        } else if (data.aspect_scope_depth > 0U &&
                   data.aspect_scopes[data.aspect_scope_depth - 1U].kind == kind) {
            const auto scope = data.aspect_scopes[--data.aspect_scope_depth];
            data.matrix_aspect_override_active =
                scope.previous_matrix_aspect_override_active;
            data.matrix_aspect_override =
                scope.previous_matrix_aspect_override;
            data.rect_aspect_mode = scope.previous_rect_aspect;
            data.background_fill_stretch_active =
                scope.previous_background_fill_stretch;
            state->rdp->setRectAspect(data.rect_aspect_mode);
        } else {
            active_->ReportPresentationError(2U, "aspect scope mismatch",
                                             kind, mode, presentation_variant);
            return;
        }
        apply_active_group();
        return;
    }

    if (kind == PresentationMarkerKind::HudPass) {
        state->flush();
        if (mode == kHudPassMarkerBeginMode) {
            if (data.rejected_hud_scopes.reject_begin(
                    data.hud_alignment_depth, data.hud_alignment_scopes.size())) {
                active_->ReportPresentationError(3U, "HUD alignment scope overflow",
                                                 kind, mode, presentation_variant);
                return;
            }
            if (presentation_variant == hud::reference::kPassVariant &&
                static_cast<std::size_t>(state->rsp->extended.viewProjMatrixIdStackSize) >=
                    state->rsp->extended.viewProjMatrixIdStack.size()) {
                ++data.rejected_hud_scopes.depth;
                active_->ReportPresentationError(3U, "HUD projection scope overflow",
                                                 kind, mode, presentation_variant);
                return;
            }

            auto& scope = data.hud_alignment_scopes[
                data.hud_alignment_depth++];
            scope.previous_scissor = state->rdp->extended.global.scissor;
            scope.previous_rect_alignment = state->rdp->extended.global.rect;
            scope.changed_rect_alignment =
                presentation_variant == hud::kHudAnchorLeft ||
                presentation_variant == hud::kHudAnchorRight;
            if (scope.changed_rect_alignment) {
                state->rdp->setRectAlign(presentation::split_counter_alignment(
                    scope.previous_rect_alignment, presentation_variant,
                    state->rdp->colorImage.width,
                    hud::decode_hud_viewport_cover(presentation_token)));
            }
            scope.previous_clip_ratios = state->rsp->clipRatios;
            scope.previous_matrix_aspect_override_active =
                data.matrix_aspect_override_active;
            scope.previous_matrix_aspect_override =
                data.matrix_aspect_override;
            scope.previous_rect_aspect = data.rect_aspect_mode;
            scope.previous_background_fill_stretch =
                data.background_fill_stretch_active;

            // The accepted quadrant variants remain clip/alignment-only.
            // The 1/2-player reference pass explicitly preserves proportions
            // on BOTH RDP rectangles and the independent RSP projection stack.
            if (presentation_variant == hud::reference::kPassVariant) {
                hud::push_hud_projection(*state->rsp);
                scope.pushed_hud_projection = true;
                state->rdp->setRectAspect(G_EX_ASPECT_ADJUST);
                data.rect_aspect_mode = G_EX_ASPECT_ADJUST;
                data.background_fill_stretch_active = false;
                const auto address = state->rdp->colorImage.address;
                const auto end = data.hud_color_images.begin() + data.hud_color_image_count;
                if (std::find(data.hud_color_images.begin(), end, address) == end &&
                    data.hud_color_image_count < data.hud_color_images.size()) {
                    data.hud_color_images[data.hud_color_image_count++] = address;
                }
            }

            state->rdp->pushScissor();
            scope.pushed_scissor = true;

            const std::int16_t horizontal_clip =
                dkr::runtime::hud::decode_hud_viewport_clip_ratio(
                    presentation_token);
            state->rsp->setClipRatioEdge(0U, horizontal_clip);
            state->rsp->setClipRatioEdge(
                2U, static_cast<std::int16_t>(-horizontal_clip));
            scope.changed_clip_ratios = true;

            const std::size_t stack_index =
                state->rdp->scissorStackSize - 1U;
            auto& rect = state->rdp->scissorRectStack[stack_index];
            auto& left_origin =
                state->rdp->extended.scissorLeftOriginStack[stack_index];
            auto& right_origin =
                state->rdp->extended.scissorRightOriginStack[stack_index];
            RT64::ExtendedAlignment hud_alignment = scope.previous_scissor;
            hud_alignment.leftOrigin = G_EX_ORIGIN_LEFT;
            hud_alignment.rightOrigin = G_EX_ORIGIN_RIGHT;
            rect.ulx = dkr::runtime::hud::rebase_hud_scissor_edge(
                rect.ulx, left_origin, hud_alignment.leftOrigin,
                state->rdp->colorImage.width);
            rect.lrx = dkr::runtime::hud::rebase_hud_scissor_edge(
                rect.lrx, right_origin, hud_alignment.rightOrigin,
                state->rdp->colorImage.width);
            left_origin = hud_alignment.leftOrigin;
            right_origin = hud_alignment.rightOrigin;
            if (scope.pushed_hud_projection) {
                // Signed overscan is a clip, not a larger native framebuffer.
                // Use NONE origins because the global extended-origin policy
                // intentionally stays at Original for the rest of the game.
                const auto gutter = static_cast<std::int32_t>(std::lround(
                    state->rdp->colorImage.width * 2.F *
                    (hud::decode_hud_viewport_cover(presentation_token) - 1.F)));
                rect.ulx = -gutter;
                rect.lrx = state->rdp->colorImage.width * 4 + gutter;
                left_origin = right_origin = G_EX_ORIGIN_NONE;
                hud_alignment.leftOrigin = hud_alignment.rightOrigin = G_EX_ORIGIN_NONE;
            }
            state->rdp->setScissorAlign(hud_alignment);
            scope.changed_scissor = true;
            state->updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
        } else if (data.rejected_hud_scopes.consume_end()) {
            return;
        } else if (data.hud_alignment_depth > 0U) {
            const auto scope = data.hud_alignment_scopes[
                --data.hud_alignment_depth];
            if (scope.pushed_scissor) {
                state->rdp->popScissor();
            }
            if (scope.pushed_hud_projection) {
                hud::pop_hud_projection(*state->rsp);
                state->rdp->setRectAspect(scope.previous_rect_aspect);
            }
            if (scope.changed_scissor) {
                state->rdp->setScissorAlign(scope.previous_scissor);
            }
            if (scope.changed_rect_alignment) {
                state->rdp->setRectAlign(scope.previous_rect_alignment);
            }
            if (scope.changed_clip_ratios) {
                for (std::size_t edge = 0U;
                     edge < scope.previous_clip_ratios.size(); ++edge) {
                    state->rsp->setClipRatioEdge(
                        static_cast<std::uint8_t>(edge),
                        scope.previous_clip_ratios[edge]);
                }
            }
            // Restore all scoped state before dialogue, menus or world draws.
            data.matrix_aspect_override_active =
                scope.previous_matrix_aspect_override_active;
            data.matrix_aspect_override =
                scope.previous_matrix_aspect_override;
            data.rect_aspect_mode = scope.previous_rect_aspect;
            data.background_fill_stretch_active =
                scope.previous_background_fill_stretch;
        } else {
            active_->ReportPresentationError(4U, "HUD alignment scope underflow",
                                             kind, mode, presentation_variant);
        }
        apply_active_group();
        return;
    }

    const bool presentation_scoped = mode != 0U;

    // Pad while the shadow identity is still selected. Once the scope is
    // popped these samples would belong to the world matrix and could not keep
    // the shadow transform's vertex count continuous.
    if (!presentation_scoped && !data.interpolation_groups.has_rejected_scope() &&
        data.shadow_scope_active &&
        data.interpolation_groups.active_group().mode ==
            kPresentationGroupShadowMode) {
        FinishShadowScope(state);
    }

    if (presentation_scoped) {
        ++data.presentation_group_begins;
    } else {
        ++data.presentation_group_ends;
    }
    const std::uint32_t raw_scoped_identity =
        mode == kPresentationGroupShadowMode
            ? dkr::runtime::presentation::make_shadow_group_identity(
                  presentation_token,
                  dkr::runtime::presentation::task_scene_generation())
            : mode == kPresentationGroupVehiclePartMode
                ? dkr::runtime::presentation::normalise_identity(
                      dkr::runtime::presentation::make_vehicle_part_group_identity(
                          presentation_token) ^
                      (static_cast<std::uint32_t>(presentation_variant) *
                       0x9E3779B9U))
            : mode == kPresentationGroupBillboardMode
                ? dkr::runtime::presentation::make_billboard_group_identity(
                      presentation_token, presentation_variant)
            : mode == kPresentationGroupSurfaceMode
                ? dkr::runtime::presentation::make_surface_group_identity(
                      presentation_token, presentation_variant)
            : mode == kPresentationGroupLevelSegmentMode
                ? dkr::runtime::presentation::make_level_segment_group_identity(
                      presentation_token, presentation_variant,
                      dkr::runtime::presentation::task_scene_generation())
            : G_EX_ID_IGNORE;
    const std::uint32_t selected_matrix_identity =
        data.interpolation_groups.matrix_group(
            data.interpolation_groups.selected_matrix()).identity;
    // A shadow's semantic owner is deliberately independent of the selected
    // matrix because its vertices are already projected into world space.
    // Billboards, vehicle parts, surfaces and level segments remain
    // matrix-relative: folding in the selected identity distinguishes their
    // semantic owners and carries the logical camera continuity boundary.
    const std::uint32_t scoped_identity =
        dkr::runtime::interpolation::scope_identity_uses_selected_matrix(
            static_cast<std::uint8_t>(mode))
            ? dkr::runtime::presentation::with_camera_continuity(
                  raw_scoped_identity, selected_matrix_identity)
            : raw_scoped_identity;
    // Shadow and billboard commands generate scratch vertices every authored
    // frame. Their scoped identity must own those vertices so RT64 can match
    // the same ordered geometry between authored frames. Vehicle parts remain
    // matrix-owned, while animated surfaces own texcoord/tile interpolation.
    // Level segment vertices are static and therefore inherit only the
    // interpolated selected world matrix, never cross-segment vertex motion.
    const bool scoped_vertices =
        mode == kPresentationGroupShadowMode ||
        mode == kPresentationGroupBillboardMode;
    const bool scoped_texcoords = mode == kPresentationGroupSurfaceMode;
    const bool scoped_tiles = mode == kPresentationGroupSurfaceMode;
    if (presentation_scoped) {
        const bool began = data.interpolation_groups.begin_scope(
                static_cast<std::uint8_t>(mode), scoped_identity,
                scoped_vertices, scoped_texcoords, scoped_tiles);
        if (!began) {
            active_->ReportPresentationError(5U, "presentation scope overflow",
                                             kind, mode, presentation_variant);
        } else if (mode == kPresentationGroupShadowMode) {
            data.pending_shadow_batch = {};
            data.pending_shadow_batches = {};
            const std::uint32_t scene_generation =
                dkr::runtime::presentation::task_scene_generation();
            data.shadow_history_key =
                presentation_token != 0U && scene_generation != 0U
                    ? (static_cast<std::uint64_t>(scene_generation) << 16U) |
                          presentation_token
                    : 0U;
            data.shadow_expected_batch_count = std::min<std::size_t>(
                presentation_variant,
                dkr::runtime::presentation::kMaximumCanonicalShadowBatches);
            data.shadow_batch_count = 0U;
            data.shadow_scope_active = true;

            if (data.task_count - g_last_shadow_history_prune_task >=
                kShadowHistoryRetentionTasks) {
                for (auto it = g_canonical_shadow_histories.begin();
                     it != g_canonical_shadow_histories.end();) {
                    if (it->second.last_task + kShadowHistoryRetentionTasks <
                        data.task_count) {
                        it = g_canonical_shadow_histories.erase(it);
                    } else {
                        ++it;
                    }
                }
                g_last_shadow_history_prune_task = data.task_count;
            }
        }
    } else {
        const auto ended = data.interpolation_groups.end_scope();
        if (!ended.had_scope && !ended.rejected_begin) {
            active_->ReportPresentationError(6U, "presentation scope underflow",
                                             kind, mode, presentation_variant);
        }
    }
    apply_active_group();
}

void dkr::runtime::F3DDKRRT64Bridge::Matrix(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    const std::uint32_t w0 = (*display_list)->w0;
    if ((w0 & 0xFFFFU) != 64U) {
        RejectTask(display_list);
        return;
    }

    StateData& data = *active_->data_;
    std::uint32_t index = (w0 >> 16U) & 0xFU;
    if (index == 0U) {
        index = (w0 >> 22U) & 0x3U;
    }
    index = std::min(index, 2U);
    data.selected_matrix = index;

    RT64::RSP& rsp = *state->rsp;
    rsp.modelMatrixStackSize = static_cast<int>(index + 1U);
    const std::uint32_t address =
        (data.matrix_offset + PhysicalAddress(rsp, (*display_list)->w1)) &
        kRDRAMAddressMask;
    if (address > kRDRAMSize - 64U) {
        if (g_logged_counted_errors++ < 64U) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected matrix address=0x%06X "
                         "task=%llu\n",
                         address,
                         static_cast<unsigned long long>(data.task_count));
        }
        RejectTask(display_list);
        return;
    }
    dkr::runtime::presentation::MatrixInterpolation matrix_group{};
    if (dkr::runtime::enhancements::modern_presentation_enabled()) {
        matrix_group =
            dkr::runtime::presentation::matrix_interpolation(address);
    }
    data.interpolation_groups.load_matrix(
        index, matrix_group.identity, matrix_group.interpolate_vertices,
        matrix_group.interpolate_texcoords, matrix_group.interpolate_tiles,
        matrix_group.procedural_water, matrix_group.water_scroll_tag);
    const auto active_group = data.interpolation_groups.active_group();
    SelectInterpolationGroup(rsp, active_group.identity,
                             active_group.interpolate_vertices,
                             active_group.interpolate_texcoords,
                             active_group.interpolate_tiles,
                              ActiveAspectMode(
                                  data.interpolation_groups,
                                  data.matrix_aspect_override_active,
                                  data.matrix_aspect_override), false,
                             active_group.procedural_water ? active_group.water_scroll_tag : 0U);
    rsp.matrix(address, static_cast<std::uint8_t>(
        active_->gbi_->constants[F3DENUM::G_MTX_LOAD]));
}

void dkr::runtime::F3DDKRRT64Bridge::FillRect(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    if (!data.background_fill_stretch_active) {
        RT64::GBI_RDP::fillRect(state, display_list);
        return;
    }

    const std::int32_t uly = (*display_list)->p1(0, 12);
    const std::int32_t lry = (*display_list)->p0(0, 12);
    if (data.background_fill_stretch_active == 2U) {
        // The optional three-player panel ends at the expanded right edge.
        // Explicit origins are disabled by the runtime's global HUD policy;
        // extend this fill in signed coordinates without touching other fills.
        state->rdp->fillRect((*display_list)->p1(12, 12), uly,
            (*display_list)->p0(12, 12), lry,
            presentation::split_panel_alignment(
                state->rdp->colorImage.width, data.split_viewport_cover));
        return;
    }
    RT64::ExtendedAlignment alignment{};
    // Preserve the existing full-width background policy.
    alignment.leftOrigin = G_EX_ORIGIN_LEFT;
    alignment.rightOrigin = G_EX_ORIGIN_RIGHT;
    state->rdp->fillRect(0, uly, 0, lry, alignment);
}

void dkr::runtime::F3DDKRRT64Bridge::HudTextureRect(
    RT64::State* state, RT64::DisplayList** display_list) {
    const bool flip = ((*display_list)->w0 >> 24U) == G_TEXRECTFLIP;
    const auto& data = *active_->data_;
    if (data.hud_widget_depth == 0) {
        if (flip) RT64::GBI_RDP::texrectFlip(state,display_list);
        else RT64::GBI_RDP::texrect(state,display_list);
        return;
    }
    // Do not repack to unsigned 12-bit command coordinates. Widescreen HUD
    // positions may be negative, and RT64 accepts signed rectangle bounds.
    const auto& t = data.hud_widgets[data.hud_widget_depth-1].transform;
    auto* command = *display_list;
    const int ulx=command->p1(12,12), uly=command->p1(0,12);
    const int lrx=command->p0(12,12), lry=command->p0(0,12);
    const auto tile=static_cast<std::uint8_t>(command->p1(24,3));
    ++command;
    const auto s=static_cast<std::int16_t>(command->p1(16,16));
    const auto uv=static_cast<std::int16_t>(command->p1(0,16));
    ++command;
    const auto rect=hud::groups::transform_rectangle(t,ulx,uly,lrx,lry,
        static_cast<std::int16_t>(command->p1(16,16)),static_cast<std::int16_t>(command->p1(0,16)),data.hud_rect_bias);
    state->rdp->drawTexRect(rect.ulx,rect.uly,rect.lrx,rect.lry,tile,s,uv,rect.dsdx,rect.dtdy,flip);
    *display_list=command;
}

void dkr::runtime::F3DDKRRT64Bridge::TextureOffset(
    RT64::State*, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    data.texture_offset = (*display_list)->w1 & kRDRAMAddressMask;
    data.texture_shift = 0U;
    data.texture_count = 0U;
}

void dkr::runtime::F3DDKRRT64Bridge::Vertex(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    RT64::RSP& rsp = *state->rsp;
    const std::uint32_t w0 = (*display_list)->w0;
    const bool append = (w0 & 0x00010000U) != 0U;
    const std::uint32_t count = ((w0 >> 19U) & 0x1FU) + 1U;
    if (append) {
        if (data.billboard) {
            data.vertex_cursor = 1U;
        }
    } else {
        data.vertex_cursor = 0U;
    }
    const std::uint32_t destination =
        data.vertex_cursor + ((w0 >> 9U) & 0x1FU);
    if (count > kMaxDKRVertices ||
        destination + count > RSP_MAX_VERTICES) {
        std::fprintf(stderr,
                     "[boot][f3ddkr] rejected vertex range dst=%u count=%u\n",
                     destination, count);
        RejectTask(display_list);
        return;
    }

    const std::uint32_t source =
        (data.vertex_offset + PhysicalAddress(rsp, (*display_list)->w1)) &
        kRDRAMAddressMask;
    const std::uint64_t source_end =
        static_cast<std::uint64_t>(source) +
        static_cast<std::uint64_t>(count) * 10U;
    if (count == 0U || count > kMaxDKRVertices || source_end > kRDRAMSize ||
        destination > kMaxDKRVertices || count > kMaxDKRVertices - destination) {
        if (g_logged_counted_errors++ < 64) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected vertex range task=%llu "
                         "source=0x%06X count=%u destination=%u append=%u\n",
                         static_cast<unsigned long long>(data.task_count), source,
                         count, destination, append ? 1U : 0U);
        }
        RejectTask(display_list);
        return;
    }
    const bool canonical_shadow =
        data.shadow_scope_active &&
        data.interpolation_groups.active_group().mode ==
            kPresentationGroupShadowMode;
    if (canonical_shadow &&
        (append || destination != 0U ||
         count >
             dkr::runtime::presentation::kCanonicalShadowBatchVertices ||
         data.pending_shadow_batch.valid)) {
        if (g_logged_counted_errors++ < 64U) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected noncanonical shadow vertex "
                         "batch task=%llu count=%u destination=%u append=%u "
                         "pending=%u\n",
                         static_cast<unsigned long long>(data.task_count),
                         count, destination, append ? 1U : 0U,
                         data.pending_shadow_batch.valid ? 1U : 0U);
        }
        RejectTask(display_list);
        return;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t address = source + i * 10U;
        RT64::RSP::Vertex vertex{};
        vertex.x = ReadS16(state->RDRAM, address + 0U);
        vertex.y = ReadS16(state->RDRAM, address + 2U);
        vertex.z = ReadS16(state->RDRAM, address + 4U);
        vertex.color.r = ReadU8(state->RDRAM, address + 6U);
        vertex.color.g = ReadU8(state->RDRAM, address + 7U);
        vertex.color.b = ReadU8(state->RDRAM, address + 8U);
        vertex.color.a = ReadU8(state->RDRAM, address + 9U);
        if (canonical_shadow) {
            data.pending_shadow_batch.vertices[i] = vertex;
        } else {
            std::memcpy(
                state->RDRAM + kScratchVertexAddress + i * sizeof(vertex),
                &vertex, sizeof(vertex));
        }
    }
    data.vertex_cursor += count;

    data.selected_matrix = std::min(data.selected_matrix, 2U);
    rsp.modelMatrixStackSize = static_cast<int>(data.selected_matrix + 1U);
    if (canonical_shadow) {
        data.pending_shadow_batch.count = count;
        data.pending_shadow_batch.destination = destination;
        data.pending_shadow_batch.valid = true;
        return;
    }
    const auto original_matrix = rsp.modelMatrixStack[data.selected_matrix];
    bool adjusted_billboard = false;
    if (data.billboard && append && rsp.indices[0] <
        state->ext.workloadQueue->workloads[state->ext.workloadQueue->writeCursor]
            .drawData.posTransformed.size()) {
        const auto& workload =
            state->ext.workloadQueue->workloads[state->ext.workloadQueue->writeCursor];
        hlslpp::float4 anchor = workload.drawData.posTransformed[rsp.indices[0]];
        if(data.hud_widget_depth!=0) {
            // The ortho-sprite anchor was loaded earlier inside this same HUD
            // scope. Undo its group affine before adding it to the billboard
            // matrix, otherwise the quad receives the translation/scale twice.
            anchor=hud::groups::untransform_anchor(anchor,rsp.viewportStack[rsp.viewportStackSize-1],
                data.hud_widgets[data.hud_widget_depth-1].transform);
        }
        auto adjusted_matrix = original_matrix;
        adjusted_matrix[3] += anchor;
        rsp.modelMatrixStack[data.selected_matrix] = adjusted_matrix;
        rsp.modelViewProjChanged = true;
        adjusted_billboard = true;
    }

    const auto authored_hud_matrix = rsp.modelMatrixStack[data.selected_matrix];
    if (data.hud_widget_depth != 0) {
        rsp.modelMatrixStack[data.selected_matrix] = hud::groups::transform_mvp(
            authored_hud_matrix, rsp.viewportStack[rsp.viewportStackSize-1],
            data.hud_widgets[data.hud_widget_depth-1].transform);
        rsp.modelViewProjChanged = true;
    }
    rsp.setVertex(kScratchVertexAddress, count, destination);
    if (data.hud_widget_depth != 0) {
        rsp.modelMatrixStack[data.selected_matrix] = authored_hud_matrix;
        rsp.modelViewProjChanged = true;
    }
    if (adjusted_billboard) {
        rsp.modelMatrixStack[data.selected_matrix] = original_matrix;
        rsp.modelViewProjChanged = true;
    }
}

void dkr::runtime::F3DDKRRT64Bridge::Triangle(RT64::State* state,
                                              RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    RT64::RSP& rsp = *state->rsp;
    const std::uint32_t w0 = (*display_list)->w0;
    const std::uint32_t count = ((w0 >> 20U) & 0xFU) + 1U;
    rsp.textureState.on = static_cast<std::uint8_t>((w0 >> 16U) & 0xFU);
    const std::uint32_t source = PhysicalAddress(rsp, (*display_list)->w1);
    const std::uint64_t source_end = static_cast<std::uint64_t>(source) +
                                     static_cast<std::uint64_t>(count) * 16U;
    if (count == 0U || source_end > kRDRAMSize) {
        if (g_logged_counted_errors++ < 64) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected triangle range task=%llu "
                         "source=0x%06X count=%u\n",
                         static_cast<unsigned long long>(data.task_count), source,
                         count);
        }
        RejectTask(display_list);
        return;
    }

    // Validate the complete batch before changing RT64 state. F3DDKR has a
    // 32-entry vertex cache; an index outside it is stale/recycled data, not a
    // drawable triangle.
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t address = source + i * 16U;
        const std::array<std::uint8_t, 3> vertices{
            ReadU8(state->RDRAM, address + 1U),
            ReadU8(state->RDRAM, address + 2U),
            ReadU8(state->RDRAM, address + 3U),
        };
        if (std::any_of(vertices.begin(), vertices.end(), [](std::uint8_t index) {
                return index >= kMaxDKRVertices;
            })) {
            if (g_logged_counted_errors++ < 64) {
                std::fprintf(stderr,
                             "[boot][f3ddkr] rejected triangle vertex index "
                             "task=%llu source=0x%06X triangle=%u "
                             "vertices=(%u,%u,%u)\n",
                             static_cast<unsigned long long>(data.task_count),
                             source, i, vertices[0], vertices[1], vertices[2]);
            }
            RejectTask(display_list);
            return;
        }
    }

    const bool canonical_shadow =
        data.shadow_scope_active &&
        data.interpolation_groups.active_group().mode ==
            kPresentationGroupShadowMode;
    if (canonical_shadow) {
        auto& pending = data.pending_shadow_batch;
        if (!pending.valid || pending.count == 0U ||
            pending.destination != 0U ||
            data.shadow_batch_count >=
                dkr::runtime::presentation::kMaximumCanonicalShadowBatches) {
            if (g_logged_counted_errors++ < 64U) {
                std::fprintf(stderr,
                             "[boot][f3ddkr] rejected shadow polygon without "
                             "canonical vertex batch task=%llu batch=%zu\n",
                             static_cast<unsigned long long>(data.task_count),
                             data.shadow_batch_count);
            }
            RejectTask(display_list);
            return;
        }

        std::array<dkr::runtime::presentation::ShadowTexcoordSample,
                   dkr::runtime::presentation::kCanonicalShadowBatchVertices>
            current_texcoords{};
        for (std::uint32_t i = 0U; i < count; ++i) {
            const std::uint32_t address = source + i * 16U;
            const std::array<std::uint8_t, 3> vertices{
                ReadU8(state->RDRAM, address + 1U),
                ReadU8(state->RDRAM, address + 2U),
                ReadU8(state->RDRAM, address + 3U),
            };
            const std::array<std::int16_t, 3> s{
                ReadS16(state->RDRAM, address + 4U),
                ReadS16(state->RDRAM, address + 8U),
                ReadS16(state->RDRAM, address + 12U),
            };
            const std::array<std::int16_t, 3> t{
                ReadS16(state->RDRAM, address + 6U),
                ReadS16(state->RDRAM, address + 10U),
                ReadS16(state->RDRAM, address + 14U),
            };
            for (std::size_t corner = 0U; corner < vertices.size(); ++corner) {
                const std::size_t vertex = vertices[corner];
                if (vertex >= pending.count) {
                    if (g_logged_counted_errors++ < 64U) {
                        std::fprintf(stderr,
                                     "[boot][f3ddkr] rejected shadow triangle "
                                     "outside pending batch task=%llu vertex=%zu "
                                     "count=%u\n",
                                     static_cast<unsigned long long>(
                                         data.task_count),
                                     vertex, pending.count);
                    }
                    RejectTask(display_list);
                    return;
                }
                auto& sample = current_texcoords[vertex];
                if (sample.valid &&
                    (sample.s != s[corner] || sample.t != t[corner])) {
                    ++pending.texcoord_conflict_count;
                }
                if (!sample.valid || s[corner] < sample.s ||
                    (s[corner] == sample.s && t[corner] < sample.t)) {
                    sample = {s[corner], t[corner], true};
                }
            }
        }
        for (std::size_t vertex = 0U; vertex < pending.count; ++vertex) {
            if (!current_texcoords[vertex].valid) {
                if (g_logged_counted_errors++ < 64U) {
                    std::fprintf(stderr,
                                 "[boot][f3ddkr] rejected unreferenced shadow "
                                 "vertex task=%llu vertex=%zu count=%u\n",
                                 static_cast<unsigned long long>(
                                     data.task_count),
                                 vertex, pending.count);
                }
                RejectTask(display_list);
                return;
            }
        }

        pending.texcoords = current_texcoords;
        pending.triangle_count = count;
        for (std::uint32_t i = 0U; i < count; ++i) {
            const std::uint32_t address = source + i * 16U;
            auto& triangle = pending.triangles[i];
            triangle.flag = ReadU8(state->RDRAM, address + 0U);
            triangle.vertices = {
                ReadU8(state->RDRAM, address + 1U),
                ReadU8(state->RDRAM, address + 2U),
                ReadU8(state->RDRAM, address + 3U),
            };
            triangle.s = {
                ReadS16(state->RDRAM, address + 4U),
                ReadS16(state->RDRAM, address + 8U),
                ReadS16(state->RDRAM, address + 12U),
            };
            triangle.t = {
                ReadS16(state->RDRAM, address + 6U),
                ReadS16(state->RDRAM, address + 10U),
                ReadS16(state->RDRAM, address + 14U),
            };
        }
        data.pending_shadow_batches[data.shadow_batch_count] = pending;
        ++data.shadow_batch_count;
        data.pending_shadow_batch = {};
        data.vertex_cursor = 0U;
        if (data.shadow_expected_batch_count != 0U &&
            data.shadow_batch_count == data.shadow_expected_batch_count) {
            FinishShadowScope(state);
        }
        return;
    }

    const bool procedural_water = data.interpolation_groups.active_group().procedural_water;
    const bool skip_equal_uv = procedural_water && water::equal_uv_enabled();
    const bool coalesce_water = procedural_water && water::coalesce_enabled();
    const bool profile_water = procedural_water && water::enabled();
    if (profile_water) {
        static thread_local std::uint8_t last_scroll_tag = 0U;
        const auto tag = data.interpolation_groups.active_group().water_scroll_tag;
        if (tag != last_scroll_tag) {
            std::fprintf(stderr, "[perf][water-scroll] authored-period=%.0fx%.0f tag=%u\n",
                water::scroll_period(tag,0), water::scroll_period(tag,1), unsigned(tag));
            last_scroll_tag = tag;
        }
    }
    auto& draw = state->ext.workloadQueue->workloads[state->ext.workloadQueue->writeCursor].drawData;
    const auto before_vertices = profile_water ? draw.vertexCount() : 0U;
    const auto started = profile_water ? track_performance::Clock::now() : track_performance::Clock::time_point{};
    std::uint64_t skipped = 0U;
    std::uint64_t skipped_culling = 0U;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t address = source + i * 16U;
        const std::uint8_t flag = ReadU8(state->RDRAM, address + 0U);
        const std::array<std::uint8_t, 3> vertices{
            ReadU8(state->RDRAM, address + 1U),
            ReadU8(state->RDRAM, address + 2U),
            ReadU8(state->RDRAM, address + 3U),
        };
        const std::array<std::int16_t, 3> s{
            ReadS16(state->RDRAM, address + 4U),
            ReadS16(state->RDRAM, address + 8U),
            ReadS16(state->RDRAM, address + 12U),
        };
        const std::array<std::int16_t, 3> t{
            ReadS16(state->RDRAM, address + 6U),
            ReadS16(state->RDRAM, address + 10U),
            ReadS16(state->RDRAM, address + 14U),
        };
        std::uint32_t requested_culling = 0U;
        if ((flag & 0x40U) == 0) {
            const bool positive_x = rsp.viewportStack[rsp.viewportStackSize - 1].scale.x > 0.0F;
            requested_culling = positive_x ?
                active_->gbi_->constants[F3DENUM::G_CULL_BACK] :
                active_->gbi_->constants[F3DENUM::G_CULL_FRONT];
        }
        // Never merge across authored batches, matrices, material changes or
        // changed per-triangle culling. Non-water keeps the exact old path.
        skipped_culling += water::set_culling(rsp, requested_culling, coalesce_water && i != 0U);

        for (std::size_t corner = 0; corner < vertices.size(); ++corner) {
            skipped += water::set_texcoord(rsp, draw, vertices[corner], s[corner], t[corner], skip_equal_uv);
        }
        rsp.drawIndexedTri(vertices[0], vertices[1], vertices[2]);
    }
    if (profile_water) {
        water::bridge.triangles += count;
        water::bridge.skipped += skipped;
        water::bridge.skipped_culling += skipped_culling;
        water::bridge.added_vertices += draw.vertexCount() - before_vertices;
        water::bridge.batch_ms += std::chrono::duration<double, std::milli>(track_performance::Clock::now() - started).count();
    }
    data.vertex_cursor = 0U;
}

void dkr::runtime::F3DDKRRT64Bridge::DisplayListBranch(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    const bool branch = (*display_list)->p0(16, 1) != 0U;
    const std::uint32_t target =
        PhysicalAddress(*state->rsp, (*display_list)->w1) & 0x00FFFFF8U;
    if (target > kRDRAMSize - sizeof(RT64::DisplayList) ||
        (!branch && data.return_depth >= data.return_stack.size())) {
        if (g_logged_counted_errors++ < 64) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected G_DL target=0x%06X "
                         "branch=%u depth=%u task=%llu\n",
                         target, branch ? 1U : 0U, data.return_depth,
                         static_cast<unsigned long long>(data.task_count));
        }
        RejectTask(display_list);
        return;
    }
    if (!branch) {
        data.return_stack[data.return_depth++] = *display_list;
    }
    *display_list = reinterpret_cast<RT64::DisplayList*>(state->fromRDRAM(target)) - 1;
}

void dkr::runtime::F3DDKRRT64Bridge::EndDisplayList(
    RT64::State*, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    if (data.return_depth == 0U) {
        *display_list = nullptr;
    } else {
        *display_list = data.return_stack[--data.return_depth];
    }
}

void dkr::runtime::F3DDKRRT64Bridge::CountedDisplayList(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    if (data.nested_depth >= kMaxNestedDisplayLists) {
        std::fprintf(stderr, "[boot][f3ddkr] counted display-list nesting limit reached\n");
        RejectTask(display_list);
        return;
    }
    const std::uint32_t count = ((*display_list)->w0 >> 16U) & 0xFFU;
    const std::uint32_t address = PhysicalAddress(*state->rsp, (*display_list)->w1);
    const std::uint64_t end = static_cast<std::uint64_t>(address) +
                              static_cast<std::uint64_t>(count) *
                                  sizeof(RT64::DisplayList);
    if (count == 0U || address == 0U || end > kRDRAMSize) {
        if (g_logged_counted_errors++ < 64) {
            std::fprintf(stderr,
                         "[boot][f3ddkr] rejected counted display-list "
                         "address=0x%06X count=%u end=0x%llX\n",
                         address, count, static_cast<unsigned long long>(end));
        }
        RejectTask(display_list);
        return;
    }

    // A G_DMADL is an inline block of already-built GBI commands. Validate the
    // whole block before mutating RT64 state: if DKR has recycled a texture or
    // a bad header points into texels, executing only its coincidentally valid
    // prefix can poison TMEM/TLUT state and crash at the next full sync.
    const auto* commands = reinterpret_cast<const RT64::DisplayList*>(
        state->fromRDRAM(address));
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t opcode =
            static_cast<std::uint8_t>(commands[i].w0 >> 24U);
        if (!IsSafeCountedOpcode(opcode) ||
            active_->original_handlers_[opcode] == nullptr ||
            (opcode == kSetTextureImageOpcode &&
             (commands[i].w1 & ~kRDRAMAddressMask) != 0U)) {
            if (g_logged_counted_errors++ < 64) {
                std::fprintf(stderr,
                             "[boot][f3ddkr] rejected invalid counted block "
                             "opcode=0x%02X task=%llu address=0x%06X "
                             "index=%u/%u word=%08X:%08X\n",
                             opcode,
                             static_cast<unsigned long long>(data.task_count),
                             address, i, count, commands[i].w0, commands[i].w1);
            }
            RejectTask(display_list);
            return;
        }
    }
    ++data.nested_depth;
    RunCommands(state, reinterpret_cast<RT64::DisplayList*>(state->fromRDRAM(address)), count);
    --data.nested_depth;
    if (data.task_rejected) {
        RejectTask(display_list);
    }
}

void dkr::runtime::F3DDKRRT64Bridge::DMAOffsets(
    RT64::State*, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    data.matrix_offset = (*display_list)->w0 & kRDRAMAddressMask;
    data.vertex_offset = (*display_list)->w1 & kRDRAMAddressMask;
}

void dkr::runtime::F3DDKRRT64Bridge::MoveWord(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    const std::uint8_t type = static_cast<std::uint8_t>((*display_list)->w0 & 0xFFU);
    if (type == kMoveWordPresentationGroup &&
        ((*display_list)->w1 & ~kPresentationGroupMetadataMask) ==
            kPresentationGroupMagic) {
        PresentationGroup(state, display_list);
    } else if (type == kMoveWordBillboard) {
        data.billboard = ((*display_list)->w1 & 1U) != 0;
    } else if (type == kMoveWordMVPMatrix) {
        data.selected_matrix = std::min(((*display_list)->w1 >> 6U) & 0x3U, 2U);
        data.interpolation_groups.select_matrix(data.selected_matrix);
        state->rsp->modelMatrixStackSize = static_cast<int>(data.selected_matrix + 1U);
        const auto active_group = data.interpolation_groups.active_group();
        SelectInterpolationGroup(*state->rsp, active_group.identity,
                                 active_group.interpolate_vertices,
                                 active_group.interpolate_texcoords,
                                 active_group.interpolate_tiles,
                                  ActiveAspectMode(
                                      data.interpolation_groups,
                                      data.matrix_aspect_override_active,
                                      data.matrix_aspect_override), false,
                                 active_group.procedural_water ? active_group.water_scroll_tag : 0U);
        state->rsp->modelViewProjChanged = true;
    } else {
        RT64::GBI_F3D::moveWord(state, display_list);
    }
}

void dkr::runtime::F3DDKRRT64Bridge::SetTextureImage(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    const std::uint8_t format = static_cast<std::uint8_t>((*display_list)->p0(21, 3));
    const std::uint8_t size = static_cast<std::uint8_t>((*display_list)->p0(19, 2));
    const std::uint16_t width = static_cast<std::uint16_t>((*display_list)->p0(0, 12) + 1U);
    std::uint32_t address = (*display_list)->w1 & kRDRAMAddressMask;
    if (data.texture_offset != 0) {
        if (format == G_IM_FMT_RGBA) {
            const std::uint64_t shift_address =
                static_cast<std::uint64_t>(data.texture_offset) +
                static_cast<std::uint64_t>(data.texture_count) *
                    sizeof(std::uint16_t);
            if (shift_address > kRDRAMSize - sizeof(std::uint16_t)) {
                if (g_logged_counted_errors++ < 64) {
                    std::fprintf(stderr,
                                 "[boot][f3ddkr] rejected texture-offset read "
                                 "task=%llu offset=0x%06X index=%u\n",
                                 static_cast<unsigned long long>(data.task_count),
                                 data.texture_offset, data.texture_count);
                }
                data.texture_offset = 0;
                data.texture_shift = 0;
                data.texture_count = 0;
                RejectTask(display_list);
                return;
            }
            data.texture_shift =
                ReadU16(state->RDRAM, static_cast<std::uint32_t>(shift_address));
            address = (address + data.texture_shift) & kRDRAMAddressMask;
        } else {
            data.texture_offset = 0;
            data.texture_shift = 0;
            data.texture_count = 0;
        }
    }
    state->rdp->setTextureImage(format, size, width, address);
}

void dkr::runtime::F3DDKRRT64Bridge::LoadBlock(
    RT64::State* state, RT64::DisplayList** display_list) {
    assert(active_ != nullptr);
    StateData& data = *active_->data_;
    const std::uint8_t tile = static_cast<std::uint8_t>((*display_list)->p1(24, 3));
    const std::uint16_t uls = static_cast<std::uint16_t>((*display_list)->p0(12, 12));
    const std::uint16_t ult = static_cast<std::uint16_t>((*display_list)->p0(0, 12));
    const std::uint16_t lrs = static_cast<std::uint16_t>((*display_list)->p1(12, 12));
    const std::uint16_t dxt = static_cast<std::uint16_t>((*display_list)->p1(0, 12));
    if (data.texture_offset != 0) {
        const std::uint32_t block_size = (((lrs >> 2U) + 1U) << 3U);
        if (block_size == 0 || (data.texture_shift % block_size) != 0) {
            state->rdp->texture.address -= data.texture_shift;
            data.texture_offset = 0;
            data.texture_shift = 0;
            data.texture_count = 0;
        } else {
            ++data.texture_count;
        }
    }
    state->rdp->loadBlock(tile, uls, ult, lrs, dxt);
}

void dkr::runtime::F3DDKRRT64Bridge::RunCommands(
    RT64::State* state, RT64::DisplayList* commands, std::uint32_t command_count) {
    assert(active_ != nullptr);
    std::uint32_t consumed = 0;
    RT64::DisplayList* command = commands;
    std::array<RT64::DisplayList*, kMaxNestedDisplayLists> return_stack{};
    std::uint32_t return_depth = 0;
    while (command != nullptr && consumed < command_count) {
        RT64::DisplayList* before = command;
        ApplyPresentationMarkers(state, command);
        const std::uint8_t opcode = static_cast<std::uint8_t>(command->w0 >> 24U);
        if (opcode == kDisplayListOpcode) {
            const bool branch = command->p0(16, 1) != 0U;
            const std::uint32_t target =
                PhysicalAddress(*state->rsp, command->w1) & 0x00FFFFF8U;
            if (target > kRDRAMSize - sizeof(RT64::DisplayList) ||
                (!branch && return_depth >= return_stack.size())) {
                if (g_logged_counted_errors++ < 64) {
                    std::fprintf(stderr,
                                 "[boot][f3ddkr] rejected counted G_DL "
                                 "target=0x%06X branch=%u depth=%u\n",
                                 target, branch ? 1U : 0U, return_depth);
                }
                active_->data_->task_rejected = true;
                break;
            }
            if (!branch) {
                return_stack[return_depth++] = command;
            }
            command = reinterpret_cast<RT64::DisplayList*>(
                          state->fromRDRAM(target)) -
                      1;
        } else if (opcode == kEndDisplayListOpcode) {
            if (return_depth == 0U) {
                break;
            }
            command = return_stack[--return_depth];
        } else {
            RT64::GBIFunction function = active_->original_handlers_[opcode];
            if (function == nullptr) {
                if (g_logged_counted_errors++ < 64) {
                    const auto base_offset = static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(commands) -
                        reinterpret_cast<std::uintptr_t>(state->RDRAM));
                    const auto command_offset = static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(command) -
                        reinterpret_cast<std::uintptr_t>(state->RDRAM));
                    std::fprintf(stderr,
                                 "[boot][f3ddkr] rejected unknown counted opcode=0x%02X "
                                 "task=%llu base=0x%06X command=0x%06X "
                                 "word=%08X:%08X consumed=%u/%u\n",
                                 opcode,
                                 static_cast<unsigned long long>(active_->data_->task_count),
                                 base_offset, command_offset, command->w0, command->w1,
                                 consumed, command_count);
                }
                active_->data_->task_rejected = true;
                break;
            }
            function(state, &command);
        }
        if (command == nullptr) {
            break;
        }
        // Most GBI handlers leave the command pointer in place. A few consume
        // additional words, while G_DL/G_ENDDL can move it to an unrelated
        // display-list allocation. Pointer subtraction across those allocations
        // is undefined and previously let a branch turn the bounded DMA list
        // into an effectively unbounded walk through texture data.
        std::uint32_t command_words = 1U;
        const auto before_address = reinterpret_cast<std::uintptr_t>(before);
        const auto after_address = reinterpret_cast<std::uintptr_t>(command);
        const auto rdram_begin = reinterpret_cast<std::uintptr_t>(state->RDRAM);
        const auto rdram_end = rdram_begin + kRDRAMSize;
        if (after_address < rdram_begin ||
            after_address > rdram_end - sizeof(RT64::DisplayList)) {
            if (g_logged_counted_errors++ < 64) {
                std::fprintf(stderr,
                             "[boot][f3ddkr] counted display-list escaped RDRAM "
                             "opcode=0x%02X consumed=%u/%u\n",
                             opcode, consumed, command_count);
            }
            active_->data_->task_rejected = true;
            break;
        }
        if (after_address >= before_address) {
            const auto byte_advance = after_address - before_address;
            if (byte_advance <= 4U * sizeof(RT64::DisplayList) &&
                (byte_advance % sizeof(RT64::DisplayList)) == 0U) {
                command_words += static_cast<std::uint32_t>(
                    byte_advance / sizeof(RT64::DisplayList));
            }
        }
        consumed += command_words;
        ++command;
    }
}
