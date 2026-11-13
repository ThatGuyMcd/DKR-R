#pragma once
#include "service.hpp"
#include "camera_snapshot.hpp"
#include "race_details.hpp"
namespace dkr::feedback {
inline void capture(std::span<const std::uint8_t> retained,runtime::rom::Revision revision,
                    runtime::camera::FrameStamp stamp,unsigned owner,unsigned profile) {
    static thread_local runtime::camera::SnapshotReader reader;
    if(retained.size()<0x800000U)return;
    for(unsigned view=0;view<4;++view) {
        if(stamp.online&&(owner>=4||view!=owner))continue;
        const unsigned p=stamp.online?profile:view;if(p>=4)continue;
        if(service().status(p).settings.mode==0)continue;
        Sample sample{};sample.scene=stamp.scene;sample.epoch=stamp.epoch;sample.frame=stamp.frame;
        if(const auto state=reader.capture(retained,revision,view,stamp)) {
            sample.generation=state->racer_generation;sample.vehicle=state->vehicle;sample.speed=state->speed;sample.steer=state->steer;
            sample.driving=(state->flags&DKR_VIEW_DRIVING)&&!(state->flags&DKR_VIEW_SCRIPTED);
            sample.grounded=(state->flags&DKR_VIEW_GROUNDED)!=0;sample.boosting=(state->flags&DKR_VIEW_BOOSTING)!=0;
            sample.impact=(state->flags&DKR_VIEW_IMPACT)!=0;sample.skidding=(state->flags&DKR_VIEW_DRIFTING)!=0;
            race_details(retained,revision,view,sample);
        }
        service().sample(p,sample,service().confirmed(stamp.epoch),stamp.online);
    }
}
}
