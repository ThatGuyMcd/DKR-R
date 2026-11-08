"""Explicit local camera handoff over the accepted private draw provider."""
FILES=('src/game/camera_runtime.hpp','src/game/camera_runtime.cpp','src/game/camera_session.cpp','src/game/f3ddkr_rt64.cpp')


def transform(name,text,once):
    def one(a,b):
        nonlocal text
        text=once(text,a,b)
    if name.endswith('camera_runtime.hpp'):
        one('rom::Revision,bool online)noexcept;', 'rom::Revision,bool online,bool development_tools=false)noexcept;')
    elif name.endswith('camera_runtime.cpp'):
        one('rom::Revision revision,bool online)noexcept {','rom::Revision revision,bool online,bool development_tools)noexcept {')
        one('        if(set.ordered.empty())return;', '''        if(set.ordered.empty()) {
            if(!development_tools)return;
            auto next=std::make_shared<Activation>();next->revision=revision;next->online=online;
            next->settings=std::make_shared<SettingsChannel>();std::array<Json,4> defaults;for(auto& value:defaults)value=Json::object();next->settings->publish(defaults);
            current.store(std::move(next));return;
        }''')
    elif name.endswith('camera_session.cpp'):
        one('#include "camera_session.hpp"','#include "camera_session.hpp"\n#include "mods/sdk2_bridge.hpp"')
        one('    if(!program_||owners.size()>4)return batch;', '    if(owners.size()>4)return batch;')
        one('    if(program_->quarantined()){batch.diagnostic=program_->error();return batch;}', '    if(program_&&program_->quarantined())batch.diagnostic=program_->error();')
        one('            if(!program_->settings(configuration->views[owner.settings_profile]))', '''            // Explicit tool ownership temporarily suspends Camera++ only for
            // this local viewport. Releasing it restores the existing provider.
            const auto local_camera=dkr::sdk2::camera_state(owner.view);
            if(local_camera.cut&&!local_camera.enabled)state->flags|=DKR_VIEW_CUT;
            if(auto pose=dkr::sdk2::camera_frame(*state,input,retained)){batch.poses[owner.view]=*pose;dkr::sdk2::debug_frame(*state,batch.poses[owner.view],unsigned(owners.size()));continue;}
            if(!program_||program_->quarantined()){dkr::sdk2::debug_frame(*state,{},unsigned(owners.size()));continue;}
            if(!program_->settings(configuration->views[owner.settings_profile]))''')
        one('            batch.poses[owner.view]=program_->frame(*state,input,query);','            batch.poses[owner.view]=program_->frame(*state,input,query);\n            dkr::sdk2::debug_frame(*state,batch.poses[owner.view],unsigned(owners.size()));')
    elif name.endswith('f3ddkr_rt64.cpp'):
        one('#include "f3ddkr_rt64.hpp"','#include "f3ddkr_rt64.hpp"\n#include "mods/sdk2_bridge.hpp"')
        one('            state->rdp->setScissorAlign(hud_alignment);\n            scope.changed_scissor = true;', '''            // Screenshot visibility is local to the decoder HUD scope. Keep
            // every native HUD update, DMA, state command and completion intact.
            // The existing scope pop restores the world/next viewport scissor.
            if(dkr::sdk2::hud_hidden.load(std::memory_order_relaxed)) {
                rect.ulx=rect.uly=rect.lrx=rect.lry=0;
                left_origin=right_origin=G_EX_ORIGIN_NONE;
                hud_alignment.leftOrigin=hud_alignment.rightOrigin=G_EX_ORIGIN_NONE;
            }
            state->rdp->setScissorAlign(hud_alignment);
            scope.changed_scissor = true;''')
    else:raise ValueError('Unreviewed local presentation source: '+name)
    return text
