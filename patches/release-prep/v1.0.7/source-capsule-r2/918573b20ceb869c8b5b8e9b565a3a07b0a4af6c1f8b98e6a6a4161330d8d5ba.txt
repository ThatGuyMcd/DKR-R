#pragma once
#include "camera_runtime.hpp"
#include "camera_kernel.h"
#include "camera_shadow_kernel.h"
#include "presentation_identity.hpp"
#include "netplay/experimental_draw_events.h"

namespace dkr::runtime::camera {
// One decoder owner, reused sequentially for all local views. Never borrows
// live CPU memory and never creates another rollback DecodeWorkspace.
class RenderPass final {
public:
    void prepare(std::span<const std::uint8_t> retained,std::span<std::uint8_t> decoder,
        FrameStamp,std::span<const dkr_owned_draw_event> events={},unsigned online_owner=4,
        unsigned physical_profile=0,bool full_screen=false);
    struct ShadowMarker {
        std::uint32_t address=0,identity=0;
        std::uint64_t history=0;
        std::uint8_t batches=0;
        bool begin=false;
        presentation::RigidShadowOwnerPolicy policy{};
        presentation::ShadowOwnerMotionSample motion{};
    };
    std::span<const ShadowMarker> shadows_at(std::uint32_t physical)const;
    void before_command(std::uint32_t physical);
    bool replaced_command(std::uint32_t physical)const;
    bool matrix(std::uint32_t physical,presentation::MatrixInterpolation&)const;
    bool enabled()const{return !ranges_.empty();}
    void finish()noexcept;
private:
    struct Range {std::uint32_t begin,end;unsigned view,profile;DkrViewPose pose;};
    std::shared_ptr<const Activation> active_;
    std::unique_ptr<PresentationSession> session_;
    std::span<const std::uint8_t> retained_;
    std::span<std::uint8_t> decoder_;
    // Immutable restoration journal for the prepared retail bytes, not
    // another mutable DecodeWorkspace or an eye/simulation image.
    std::vector<std::uint8_t> prepared_;
    std::vector<Range> ranges_;
    struct ShadowOwner {
        std::uint32_t object,header,asset,kind,view,continuity;
        std::uint64_t history;
    };
    void capture_shadows(const Range&);
    std::vector<ShadowOwner> shadow_owners_;
    std::vector<ShadowMarker> shadow_markers_;
    std::array<std::uint32_t,4> shadow_continuity_{};
    std::array<std::uint64_t,4> shadow_cut_frame_{{UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX}};
    dkr_camera_shadows shadows_{};
    dkr_camera_draw_result draw_{};
    const Range* drawing_=nullptr;
    std::uint32_t ended_=0,scene_=0;
    std::uint32_t failed_scene_=0,failed_views_=0;
    bool full_screen_=false;
    float aspect_=4.F/3.F;
};
}
