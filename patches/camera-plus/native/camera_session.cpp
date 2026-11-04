#include "camera_session.hpp"
#include "look_behind/policy.hpp"
#include <cmath>
#include <cstring>

namespace dkr::runtime::camera {
PresentationSession::PresentationSession(const mods::code::FrozenSet& selected,rom::Revision revision,
        const SettingsChannel& settings,Actions& actions):revision_(revision),settings_(settings),actions_(actions) {
    if(selected.ordered.empty())return; // Original DKR, no local provider.
    if(selected.ordered.size()!=1)throw mods::Error("Enable only one camera provider.");
    if(revision!=rom::Revision::UsV77&&revision!=rom::Revision::UsV80)
        throw mods::Error("Camera presentation does not support this Game Pak.");
    const auto& package=selected.ordered[0];
    const auto configured=selected.settings.find(package->id);
    program_=std::make_unique<Program>(package,configured==selected.settings.end()?Json::object():configured->second,
        revision==rom::Revision::UsV77?"us.v77":"us.v80");
}
ViewBatch PresentationSession::evaluate(std::span<const std::uint8_t> retained,const FrameStamp& stamp,
        std::span<const ViewOwner> owners) {
    ViewBatch batch;
    if(owners.size()>4)return batch;
    const bool provider=program_&&!program_->quarantined();
    if(program_&&!provider)batch.diagnostic=program_->error();
    try {
        const auto configuration=settings_.current();
        if(provider&&!configuration)throw mods::Error("Camera profiles are not ready.");
        unsigned seen_views=0,seen_physical=0;
        for(const auto& owner:owners) {
            if(owner.view>=4||owner.physical_profile>=4||owner.settings_profile>=4||
               !std::isfinite(owner.aspect)||owner.aspect<0.1F||owner.aspect>16||
               (seen_views&(1U<<owner.view))||(seen_physical&(1U<<owner.physical_profile)))
                throw mods::Error("Camera local-view ownership is invalid.");
            seen_views|=1U<<owner.view;seen_physical|=1U<<owner.physical_profile;
        }
        FrameStamp frame=stamp;
        std::array<DkrViewInput,4> inputs{};
        bool driving=false,rear_requested=false;
        for(const auto& owner:owners) {
            frame.aspect=owner.aspect;
            inputs[owner.view]=actions_.consume(owner.physical_profile,frame.dt);
            auto state=reader_.capture(retained,revision_,owner.view,frame);
            batch.snapshots[owner.view]=state;
            driving|=state&&(state->flags&DKR_VIEW_DRIVING)&&!(state->flags&DKR_VIEW_SCRIPTED);
            rear_requested|=!inputs[owner.view].blocked&&(inputs[owner.view].held&DKR_VIEW_LOOK_BACK)&&look_behind::enabled.load();
        }
        // No mesh rebuilding or mod execution in Track Select/title menus.
        // Consume their local taps instead of replaying them in the race.
        if(!driving||(!provider&&!rear_requested))return batch;
        if(frame.capabilities&DKR_VIEW_STATIC_COLLISION) {
            if(retained.size()<0x800000U)throw mods::Error("Camera presentation requires a complete retained image.");
            const auto address=revision_==rom::Revision::UsV77?0x800DC918U:0x800DCE88U;
            std::uint32_t model;std::memcpy(&model,retained.data()+address-0x80000000U,4);
            if(!scene_attempted_||scene_!=stamp.scene||epoch_!=stamp.epoch||model_!=model) {
                collision_.reset();scene_error_.clear();scene_attempted_=true;
                scene_=stamp.scene;epoch_=stamp.epoch;model_=model;
                try {collision_=CollisionScene::capture(retained,model,(std::uint64_t(epoch_)<<32U)|scene_);}
                catch(const std::exception& e){scene_error_=e.what();}
            }
            if(!collision_)frame.capabilities&=~std::uint32_t(DKR_VIEW_STATIC_COLLISION);
        }
        // This implementation has no retained moving-platform/door adapter.
        // Never accidentally promote static queries to dynamic capability.
        frame.capabilities&=~std::uint32_t(DKR_VIEW_DYNAMIC_COLLISION);
        const Program::Query query=[scene=collision_](const DkrViewQuery& request) {
            if(scene)return scene->query(request);
            DkrViewHit hit{};hit.api=1;hit.size=sizeof(hit);hit.status=DKR_VIEW_QUERY_UNSUPPORTED;return hit;
        };
        for(const auto& owner:owners) {
            frame.aspect=owner.aspect;
            auto state=batch.snapshots[owner.view];
            if(!state)continue; // No racer/menu/scripted fallback, not a fault.
            state->capabilities=frame.capabilities;
            batch.snapshots[owner.view]=state;
            auto input=inputs[owner.view];
            batch.xr_recenter|=(input.pressed&DKR_VIEW_XR_RECENTER)!=0;
            if(look_behind::enabled.load()&&!input.blocked&&(input.held&DKR_VIEW_LOOK_BACK)) {
                DkrViewPose rear{};
                if(look_behind::pose(*state,rear)) {
                    if(collision_) {
                        DkrViewQuery cast{};cast.api=1;cast.size=sizeof(cast);cast.view=owner.view;
                        cast.exclude_racer=state->racer_id;cast.radius=12;
                        for(unsigned i=0;i<3;++i){cast.origin[i]=state->position[i];cast.target[i]=rear.position[i];}
                        cast.origin[1]+=35;
                        const auto hit=query(cast);
                        if(hit.status==DKR_VIEW_QUERY_HIT)for(unsigned i=0;i<3;++i)rear.position[i]=hit.position[i]+hit.normal[i]*12;
                    }
                    batch.poses[owner.view]=rear;continue;
                }
            }
            if(!provider||program_->quarantined())continue;
            if(!look_behind::enabled.load())input.held&=~DKR_VIEW_LOOK_BACK;
            if(!program_->settings(configuration->views[owner.settings_profile]))
                throw mods::Error(program_->error());
            batch.poses[owner.view]=program_->frame(*state,input,query);
            if(program_->quarantined()) {
                batch.diagnostic=program_->error();continue;
            }
        }
        if(batch.diagnostic.empty())batch.diagnostic=scene_error_;
    }catch(const std::exception& e){batch.poses={};batch.diagnostic=e.what();}
    return batch;
}
}
