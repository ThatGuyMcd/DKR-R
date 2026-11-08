#include "camera_rt64.hpp"
#include "revision_addresses.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dkr::runtime::camera {
namespace {
std::uint32_t word(std::span<const std::uint8_t> bytes,std::uint32_t p) {
    p&=0xFFFFFFU;if(p>0x800000U-4)return 0;
    std::uint32_t out;std::memcpy(&out,bytes.data()+p,4);return out;
}
std::uint32_t racer_object(std::span<const std::uint8_t> bytes,rom::Revision revision,unsigned view) {
    const auto& a=revision==rom::Revision::UsV77?revision_addresses::kUsV77:revision_addresses::kUsV80;
    auto count=word(bytes,a.NumberOfRacers),list=word(bytes,a.RacersByPort);
    if(count>8||list<0x80000000U||list>0x807FFFE0U)return 0;
    for(unsigned i=0;i<count;++i) {
        auto object=word(bytes,list+i*4);
        if(object<0x80000000U||object>0x807FFF80U)continue;
        auto state=word(bytes,object+0x64);
        if(state<0x80000000U||state>0x807FFDDCU)continue;
        std::int16_t player;std::memcpy(&player,bytes.data()+((state-0x80000000U)^2U),2);
        if(player==int(view))return object;
    }
    return 0;
}
}
void RenderPass::prepare(std::span<const std::uint8_t> retained,std::span<std::uint8_t> decoder,
        FrameStamp stamp,std::span<const dkr_owned_draw_event> events,unsigned online_owner,
        unsigned physical_profile,bool full_screen) {
    finish();ranges_.clear();retained_=retained;decoder_=decoder;ended_=0;
    const auto next=activation();
    try {
        if(next!=active_){session_.reset();active_=next;failed_views_=0;if(next)session_=std::make_unique<PresentationSession>(next->frozen,next->revision,*next->settings,actions());}
        if(!session_||retained.size()<0x800000||decoder.size()<DKR_CAMERA_KERNEL_BYTES)return;
        if(failed_scene_!=stamp.scene){failed_scene_=stamp.scene;failed_views_=0;}
        struct Boundary{std::uint32_t address;unsigned view;bool end;};
        std::vector<Boundary> boundaries;
        if(!events.empty()) {
            for(const auto& e:events)if(e.kind==DKR_OWNED_ONLINE_VIEW||e.kind==DKR_OWNED_CAMERA_WORLD_END) {
                if(!dkr_owned_draw_event_valid(&e))throw mods::Error("Invalid retained camera boundary.");
                boundaries.push_back({e.address&0xFFFFFFU,e.token,e.kind==DKR_OWNED_CAMERA_WORLD_END});
            }
        } else {
            for(const auto& marker:presentation::camera_world_boundaries())
                boundaries.push_back({marker.address&0xFFFFFFU,marker.marker.token,
                    marker.marker.kind==presentation::PresentationMarkerKind::CameraWorldEnd});
        }
        if(boundaries.size()>8)return;
        std::sort(boundaries.begin(),boundaries.end(),[](auto a,auto b){return a.address<b.address;});
        std::vector<ViewOwner> owners;
        for(std::size_t i=0;i+1<boundaries.size();i+=2) {
            const auto b=boundaries[i],e=boundaries[i+1];
            if(b.end||!e.end||b.view!=e.view||b.view>=4||b.address>=e.address||e.address>0x7FFFF8U)return;
            if(stamp.online&&b.view!=online_owner)continue;
            const auto profile=stamp.online?physical_profile:b.view;
            owners.push_back({b.view,profile,profile,stamp.aspect});
            ranges_.push_back({b.address,e.address,b.view,profile,{}});
        }
        stamp.capabilities=DKR_VIEW_POSE|DKR_VIEW_PROJECTION|DKR_VIEW_REAR_COVERAGE|
            DKR_VIEW_HIDE_LOCAL_MODEL|DKR_VIEW_STATIC_COLLISION;
        const auto batch=session_->evaluate(retained,stamp,owners);
        if(!batch.diagnostic.empty())report(batch.diagnostic);
        for(auto& range:ranges_)if(batch.poses[range.view])range.pose=*batch.poses[range.view];
        ranges_.erase(std::remove_if(ranges_.begin(),ranges_.end(),[&](const Range& r){return !r.pose.api||r.pose.mode==DKR_VIEW_ORIGINAL||(failed_views_&(1U<<r.view));}),ranges_.end());
        if(!ranges_.empty()&&prepared_.size()!=0x800000U)prepared_.resize(0x800000U);
        scene_=stamp.scene;full_screen_=full_screen;aspect_=stamp.aspect;
    }catch(const std::exception& e){ranges_.clear();report(e.what());}
}
void RenderPass::before_command(std::uint32_t physical) {
    if(ended_!=physical)ended_=0;
    if(drawing_&&physical==drawing_->end) {
        // Decoders synchronously own/copy all geometry and TMEM bytes. Restore
        // before the original HUD/weather or another player's view resumes.
        std::memcpy(decoder_.data(),prepared_.data(),0x800000);
        ended_=physical;drawing_=nullptr;
    }
    if(drawing_||ended_==physical)return;
    const auto range=std::find_if(ranges_.begin(),ranges_.end(),[&](const Range& r){return r.begin==physical;});
    if(range==ranges_.end())return;
    // Preserve local HUD/sky/aspect corrections and readbacks before this
    // view. The immutable simulation lease remains read-only throughout.
    std::memcpy(prepared_.data(),decoder_.data(),0x800000);
    dkr_camera_draw_request request{};request.view=range->view;request.scene=scene_;
    request.return_address=range->end|0x80000000U;request.full_screen=full_screen_;request.aspect=aspect_;
    const auto& pose=range->pose;
    if(pose.visibility==DKR_VIEW_HIDE_VEHICLE)request.hide_object=racer_object(retained_,active_->revision,range->view);
    std::copy_n(pose.position,3,request.position);request.yaw=pose.yaw;request.pitch=pose.pitch;request.roll=pose.roll;
    request.fov=pose.vertical_fov;request.near_plane=pose.near_plane;request.far_plane=pose.far_plane;
    const bool okay=active_->revision==rom::Revision::UsV77?
        dkr_camera_v77_draw(decoder_.data(),decoder_.size(),&request,&draw_):
        dkr_camera_v80_draw(decoder_.data(),decoder_.size(),&request,&draw_);
    if(!okay) {
        std::memcpy(decoder_.data(),prepared_.data(),0x800000);
        failed_views_|=1U<<range->view;
        report(draw_.failure?draw_.failure:"Camera scene could not be drawn.");presented(range->profile,false);return;
    }
    const std::uint32_t branch[2]={0x06010000U,draw_.start&0xFFFFFFU};
    std::memcpy(decoder_.data()+range->begin,branch,8);drawing_=&*range;
    presented(range->profile,true);
}
bool RenderPass::replaced_command(std::uint32_t physical)const {
    return (ended_&&ended_==physical) || (drawing_&&physical>=drawing_->begin&&physical<drawing_->end);
}
bool RenderPass::matrix(std::uint32_t physical,presentation::MatrixInterpolation& result)const {
    if(!drawing_||physical<0x900000U||physical>=0x980000U)return false;
    result={};
    // No address-only vertex matching: the native draw closure can change
    // topology from one camera frame to the next.
    for(unsigned i=0;i<draw_.matrix_count;++i)if((draw_.matrices[i].address&0xFFFFFFU)==physical) {
        const auto& m=draw_.matrices[i];result.identity=drawing_->pose.cut?0U:m.identity;
        result.camera_root=(m.root&1U)!=0;
        result.procedural_water=(m.root&2U)!=0;
        result.interpolate_vertices=result.interpolate_texcoords=result.interpolate_tiles=result.procedural_water;
        break;
    }
    return true;
}
void RenderPass::finish()noexcept {
    if(drawing_&&decoder_.size()>=0x800000&&prepared_.size()>=0x800000)
        std::memcpy(decoder_.data(),prepared_.data(),0x800000);
    drawing_=nullptr;ended_=0;
}
}
