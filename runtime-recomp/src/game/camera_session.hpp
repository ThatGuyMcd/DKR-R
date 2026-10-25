#pragma once
#include "camera_settings.hpp"
#include "camera_actions.hpp"
#include "camera_snapshot.hpp"
#include "camera_scene.hpp"
#include "mods/magic_code_library.hpp"

namespace dkr::runtime::camera {
struct ViewOwner {
    unsigned view=0;             // Actual native racer/controller identity.
    unsigned physical_profile=0; // Device owner; not assumed to equal view.
    unsigned settings_profile=0; // Local saved profile; never a remote slot.
    float aspect=4.F/3.F;
};
struct ViewBatch {
    std::array<std::optional<DkrViewPose>,4> poses;
    std::array<std::optional<DkrViewSnapshot>,4> snapshots;
    bool xr_recenter=false;
    std::string diagnostic;
};
// Presentation-thread owner, one local program and one state set per session.
// evaluate() reads a retained image, never the live/rewindable CPU. Evaluate
// ONCE before rendering a batch; both eyes consume the same submitted anchor.
// No mutable decoder is created by this owner and no GPU lifetime is changed.
class PresentationSession final {
public:
    PresentationSession(const mods::code::FrozenSet& presentation,rom::Revision,
        const SettingsChannel&,Actions&);
    ViewBatch evaluate(std::span<const std::uint8_t> retained,const FrameStamp&,
        std::span<const ViewOwner> owners);
    bool active()const{return bool(program_)&&!program_->quarantined();}
private:
    rom::Revision revision_;
    const SettingsChannel& settings_;
    Actions& actions_;
    SnapshotReader reader_;
    std::unique_ptr<Program> program_;
    std::shared_ptr<const CollisionScene> collision_;
    std::uint32_t scene_=0,epoch_=0,model_=0;
    bool scene_attempted_=false;
    std::string scene_error_;
};
}
