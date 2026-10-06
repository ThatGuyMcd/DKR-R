#include "modern_camera_policy.hpp"
#include "camera_clearance_policy.hpp"
#include "camera_obstruction_math.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace dkr::runtime::enhancements;

static_assert(dkr_cam_follow_mode(0) && dkr_cam_follow_mode(1) && dkr_cam_follow_mode(4));
static_assert(!dkr_cam_follow_mode(3) && !dkr_cam_follow_mode(5) && !dkr_cam_follow_mode(6) && !dkr_cam_follow_mode(7));
static_assert(dkr_cam_solid_batch(0,0) && dkr_cam_solid_batch(0,10)); // Stone/frozen water.
static_assert(!dkr_cam_solid_batch(0x200,0) && !dkr_cam_solid_batch(0x100,0));
static_assert(!dkr_cam_solid_batch(0x2000,0) && !dkr_cam_solid_batch(0,11));
static_assert(!dkr_cam_solid_batch(0,14) && !dkr_cam_solid_batch(0,15));
static_assert(dkr_cam_radius(0)==15 && dkr_cam_radius(1)==21 && dkr_cam_radius(3)==15);

// Compiled fixtures only unless the user authorises runtime tests separately.
bool CameraObstructionMath() {
    const auto a=dkr_cam_v(-100,-100,50),b=dkr_cam_v(100,-100,50),c=dkr_cam_v(0,100,50);
    const auto origin=dkr_cam_v(0,0,0),target=dkr_cam_v(0,0,100);
    const auto close=[](double x,double y){return std::abs(x-y)<1e-7;};
    if(!close(dkr_cam_triangle_hit(origin,target,a,b,c,15),0.35) ||
       !close(dkr_cam_triangle_hit(origin,target,c,b,a,15),0.35) ||
       dkr_cam_triangle_hit(origin,dkr_cam_v(0,0,25),a,b,c,15)<=1 ||
       dkr_cam_triangle_hit(origin,target,a,a,a,15)<=1)return false;
    // Walls, ground and ceilings share the same 3D clearance volume.
    const auto ground_a=dkr_cam_v(-100,0,-100),ground_b=dkr_cam_v(100,0,-100),ground_c=dkr_cam_v(0,0,100);
    if(!close(dkr_cam_triangle_hit(dkr_cam_v(0,50,0),dkr_cam_v(0,-50,0),
        ground_a,ground_b,ground_c,15),0.35))return false;
    // The centre ray misses this triangle; the sphere still catches its edge.
    const auto e0=dkr_cam_v(10,-100,50),e1=dkr_cam_v(10,100,50),e2=dkr_cam_v(100,0,50);
    if(dkr_cam_triangle_hit(origin,target,e0,e1,e2,15)>1)return false;
    dkr_cam_vec push;
    if(dkr_cam_triangle_push(origin,dkr_cam_v(0,0,40),a,b,c,15,&push)<=0 ||
       push.z>=0 || !dkr_cam_finite(push))return false;
    if(dkr_cam_triangle_push(origin,dkr_cam_v(0,0,30),a,b,c,15,&push)!=0)return false;
    return true;
}

static_assert(clamp_fov_offset(-99) == -20);
static_assert(clamp_fov_offset(99) == 20);
static_assert(effective_gameplay_fov(PresentationProfile::Accurate, 60, 20) == 60);
static_assert(effective_gameplay_fov(PresentationProfile::Modern, 60, 10) == 70);
static_assert(effective_gameplay_fov(PresentationProfile::Modern, 45, -20) == 40);

static_assert(clamp_view_distance_multiplier(99) == 8);
static_assert(effective_view_distance(PresentationProfile::Accurate, 1000, 8) == 1000);
static_assert(effective_view_distance(PresentationProfile::Modern, 1000, 8) == 8000);
static_assert(effective_view_distance(PresentationProfile::Modern, 10000, 8) == 32767);
static_assert(effective_view_distance(PresentationProfile::Modern, -1, 8) == -1);

static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, 5, kRaceTypeDefault, 3) == 5);
static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, 4, kRaceTypeDefault, 3) == 3);
static_assert(effective_wave_view_distance(
    PresentationProfile::Accurate, 5, kRaceTypeDefault, 3) == 3);
static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, 5, 6, 3) == 3);
static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, 5, 7, 3) == 3);

static_assert(persistent_water_override_enabled(
    PresentationProfile::Modern, 2, kRaceTypeDefault));
static_assert(!persistent_water_override_enabled(
    PresentationProfile::Modern, 1, kRaceTypeDefault));
static_assert(!persistent_water_override_enabled(
    PresentationProfile::Modern, 5, 6));
static_assert(persistent_water_hq_fade(
    PresentationProfile::Modern, 2, true, 0x80) == 0);
static_assert(persistent_water_hq_fade(
    PresentationProfile::Modern, 2, false, 0x80) == 0x80);
static_assert(persistent_water_hq_fade(
    PresentationProfile::Accurate, 5, true, 0x20) == 0x20);

static_assert(scenery_retention_enabled_for_race_type(
    PresentationProfile::Modern, kRaceTypeHubWorld, true, false, false));
static_assert(scenery_retention_enabled_for_race_type(
    PresentationProfile::Modern, kRaceTypeBoss, false, true, false));
static_assert(scenery_retention_enabled_for_race_type(
    PresentationProfile::Modern, 0x40, false, false, true));
static_assert(!scenery_retention_enabled_for_race_type(
    PresentationProfile::Accurate, kRaceTypeHubWorld, true, true, true));
static_assert(!scenery_retention_enabled_for_race_type(
    PresentationProfile::Modern, 6, true, true, true));

static_assert(!relax_scenery_segment_bitfield(
    PresentationProfile::Modern, kRaceTypeHubWorld,
    SceneryRetentionMode::CurrentRegion, true, false, false));
static_assert(relax_scenery_segment_bitfield(
    PresentationProfile::Modern, kRaceTypeHubWorld,
    SceneryRetentionMode::VisibleAndAdjacent, true, false, false));
static_assert(relax_scenery_segment_bitfield(
    PresentationProfile::Modern, kRaceTypeDefault,
    SceneryRetentionMode::FullForwardView, false, true, false));
static_assert(!relax_scenery_segment_bitfield(
    PresentationProfile::Modern, kRaceTypeDefault,
    SceneryRetentionMode::FullForwardView, true, false, false));

static_assert(is_animated_scenery_behaviour(12));
static_assert(!is_animated_scenery_behaviour(4));
static_assert(is_billboard_or_effect_behaviour(3));
static_assert(!is_billboard_or_effect_behaviour(4));
static_assert(is_water_or_lava_behaviour(59));
static_assert(!is_water_or_lava_behaviour(4));
static_assert(object_view_distance_multiplier(12, 2, 4, 1, 1) == 4);
static_assert(object_view_distance_multiplier(3, 2, 1, 4, 1) == 4);
static_assert(object_view_distance_multiplier(59, 2, 1, 1, 5) == 5);
static_assert(object_view_distance_multiplier(4, 2, 4, 4, 5) == 2);

static_assert(active_viewport_aspect(16.0F / 9.0F, 0) == 16.0F / 9.0F);
static_assert(active_viewport_aspect(16.0F / 9.0F, 1) == 32.0F / 9.0F);
static_assert(active_viewport_aspect(16.0F / 9.0F, 3) == 16.0F / 9.0F);
static_assert(frustum_horizontal_scale(PresentationProfile::Accurate, true, true,
                                       32.0F / 9.0F, 0, 5) == 1.0F);
static_assert(frustum_horizontal_scale(PresentationProfile::Modern, false, true,
                                       32.0F / 9.0F, 0, 5) == 1.0F);

static_assert(dkr_world_projection_eligible(0, 0, 0, 1));
static_assert(dkr_world_projection_eligible(0, 8, 1, 1));
static_assert(dkr_world_projection_eligible(0, 5, 3, 1));
static_assert(!dkr_world_projection_eligible(1, 0, 0, 1)); // Menus/stage/previews.
static_assert(!dkr_world_projection_eligible(0, 0, 0, 0)); // Orthographic HUD.
static_assert(!dkr_world_projection_eligible(0, 6, 0, 1));
static_assert(!dkr_world_projection_eligible(0, 0, 4, 1));
static_assert(dkr_world_projection_metadata_valid(60, 0x42700000, 3));
static_assert(dkr_world_projection_metadata_valid(0, 0, 0));
static_assert(!dkr_world_projection_metadata_valid(0, 0x42700000, 0));
static_assert(!dkr_world_projection_metadata_valid(60, 0x7FC00000, 0));

bool CameraClearanceMath() {
    using namespace dkr::runtime::presentation;
    const auto close = [](double a, double b) { return std::abs(a - b) < 0.0001; };
    if (!close(world_near_distance({60,60,0},4.0F/3),10) ||
        !close(world_near_distance({60,60,0},16.0F/9),7.5) ||
        !close(world_near_distance({60,60,1},16.0F/9),7.5) ||
        !close(world_near_distance({60,60,3},16.0F/9),7.5) ||
        !close(world_near_distance({60,40,0},4.0F/3),10) ||
        !close(world_near_distance({60,100,0},1000),0.5) ||
        world_depth_remap({},16.0F/9).active ||
        world_depth_remap({60,60,0},std::numeric_limits<float>::quiet_NaN()).active)
        return false;
    using Matrix = std::array<std::array<float,4>,4>;
    // Retail row-vector perspective plus translation: preserve the X/Y/W
    // columns and map the new near/far planes to -1/+1 clip depth.
    constexpr double old_a = 15010.0/-14990.0, old_b = 300000.0/-14990.0;
    Matrix m{{{1,0,0,0},{0,1,0,0},{0,0,float(old_a),-1},{3,4,float(old_b),0}}};
    const Matrix original=m;
    const auto remap=world_depth_remap({60,80,0},16.0F/9);
    if (!apply_world_depth_remap(m,remap)) return false;
    for(unsigned row=0;row<4;++row)
        for(unsigned column: {0U,1U,3U}) if(m[row][column]!=original[row][column]) return false;
    const double near=world_near_distance({60,80,0},16.0F/9);
    for(const double distance: {near,15000.0}) {
        const double ndc=(-distance*m[2][2]+m[3][2])/distance;
        if(!close(ndc,distance==near?-1.0:1.0)) return false;
    }
    Matrix invalid=original;
    invalid[3][3]=std::numeric_limits<float>::infinity();
    if(apply_world_depth_remap(invalid,remap)) return false;
    for(unsigned row=0;row<4;++row) if(invalid[row][2]!=original[row][2]) return false;
    Matrix unchanged=original;
    if(apply_world_depth_remap(unchanged,world_depth_remap({60,60,0},4.0F/3)) ||
        unchanged!=original) return false;
    Matrix affine{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    const auto corners=affine;
    if(apply_world_depth_remap(affine,remap) || affine!=corners) return false;
    return true;
}

int main() {
    if (!CameraClearanceMath() || !CameraObstructionMath()) {
        std::fputs("[test][camera-clearance] FAIL\n", stderr);
        return 1;
    }
    const float retention = scenery_retention_frustum_scale(
        PresentationProfile::Modern, kRaceTypeDefault,
        SceneryRetentionMode::FullForwardView, 8, false, true, false);
    const float retention_disabled = scenery_retention_frustum_scale(
        PresentationProfile::Modern, kRaceTypeDefault,
        SceneryRetentionMode::VisibleAndAdjacent, 8, false, true, false);
    const float scale_4_3 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 4.0F / 3.0F, 0, 5);
    const float scale_16_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 16.0F / 9.0F, 0, 5);
    const float scale_21_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 21.0F / 9.0F, 0, 5);
    const float scale_32_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 32.0F / 9.0F, 0, 5);
    const float scale_split = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 16.0F / 9.0F, 1, 5);
    if (std::abs(retention - 1.56F) > 0.0001F ||
        std::abs(retention_disabled - 1.0F) > 0.0001F ||
        std::abs(scale_4_3 - 1.05F) > 0.0001F ||
        std::abs(scale_16_9 - 1.4F) > 0.0001F ||
        std::abs(scale_21_9 - 1.8375F) > 0.0001F ||
        std::abs(scale_32_9 - 2.8F) > 0.0001F ||
        std::abs(scale_split - 2.8F) > 0.0001F) {
        std::fputs("[test][modern-camera-policy] FAIL\n", stderr);
        return 1;
    }
    std::puts("[test][modern-camera-policy] PASS");
    return 0;
}
