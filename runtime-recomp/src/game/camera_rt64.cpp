#include "party_camera.hpp"
#include "camera_scene.hpp"
#include "game_payload.hpp"
#include "camera_rt64.hpp"
#include "revision_addresses.hpp"
#include <algorithm>
#include <atomic>
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
    const bool director=stamp.online&&dkr::runtime::spectators::spectator_director.load(std::memory_order_relaxed);
    try {
        if(next!=active_){session_.reset();active_=next;failed_views_=0;shadow_owners_.clear();if(next)session_=std::make_unique<PresentationSession>(next->frozen,next->revision,*next->settings,actions());}
        if((!session_&&!director)||retained.size()<0x800000||decoder.size()<DKR_CAMERA_KERNEL_BYTES)return;
        if(failed_scene_!=stamp.scene){failed_scene_=stamp.scene;failed_views_=0;shadow_owners_.clear();shadow_continuity_={};shadow_cut_frame_.fill(UINT64_MAX);}
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
        if(director) {
            static thread_local SnapshotReader reader;
            static thread_local std::shared_ptr<const CollisionScene> collision;
            static thread_local std::uint64_t collision_key=0;
            const auto* payload=dkr::runtime::active_payload();if(!payload){ranges_.clear();return;}
            const auto revision=payload->revision;
            const auto address=revision==rom::Revision::UsV77?0x800DC918U:0x800DCE88U;
            const auto model=word(retained,address);
            const auto identity=(std::uint64_t(stamp.epoch)<<32)^stamp.scene^model;
            if(identity!=collision_key) {collision_key=identity;collision.reset();try{collision=CollisionScene::capture(retained,model,identity);}catch(const std::exception&) {}}
            for(auto& range:ranges_) {
                const auto state=reader.capture(retained,revision,range.view,stamp);
                if(!state||!(state->flags&DKR_VIEW_DRIVING)||(state->flags&DKR_VIEW_SCRIPTED))continue;
                const auto& s=*state;auto& p=range.pose;p.api=1;p.size=sizeof(p);p.view=range.view;p.mode=DKR_VIEW_CINEMATIC;
                p.racer_id=s.racer_id;p.racer_generation=s.racer_generation;p.visibility=DKR_VIEW_SHOW_MODEL;p.cut=bool(s.flags&DKR_VIEW_CUT);
                const float side=(stamp.frame/240U)%2U?1.F:-1.F;
                p.position[0]=s.position[0]+std::sin(s.yaw)*160.F+std::cos(s.yaw)*240.F*side;
                p.position[1]=s.position[1]+100.F;
                p.position[2]=s.position[2]+std::cos(s.yaw)*160.F-std::sin(s.yaw)*240.F*side;
                if(collision) {
                    DkrViewQuery q{};q.api=1;q.size=sizeof(q);q.view=range.view;q.radius=8;
                    std::copy_n(s.position,3,q.origin);q.origin[1]+=30;std::copy_n(p.position,3,q.target);
                    const auto hit=collision->query(q);
                    if(hit.status==DKR_VIEW_QUERY_HIT)for(unsigned axis=0;axis<3;++axis)p.position[axis]=q.origin[axis]+(q.target[axis]-q.origin[axis])*std::max(0.F,hit.fraction-.04F);
                    else if(hit.status!=DKR_VIEW_QUERY_NONE){std::copy_n(s.original_position,3,p.position);}
                } else std::copy_n(s.original_position,3,p.position);
                const float dx=s.position[0]-p.position[0],dy=s.position[1]+20.F-p.position[1],dz=s.position[2]-p.position[2];
                p.yaw=std::atan2(-dx,-dz);p.pitch=std::atan2(-dy,std::sqrt(dx*dx+dz*dz));p.roll=0;
                p.vertical_fov=s.original_fov;p.near_plane=s.original_near;p.far_plane=s.original_far;
            }
        } else {
        const auto batch=session_->evaluate(retained,stamp,owners);
        if(!batch.diagnostic.empty())report(batch.diagnostic);
        for(auto& range:ranges_)if(batch.poses[range.view])range.pose=*batch.poses[range.view];
        }

        ranges_.erase(std::remove_if(ranges_.begin(),ranges_.end(),[&](const Range& r){return !r.pose.api||r.pose.mode==DKR_VIEW_ORIGINAL||(failed_views_&(1U<<r.view));}),ranges_.end());
        if(!ranges_.empty()&&prepared_.size()!=0x800000U)prepared_.resize(0x800000U);
        for(const auto& range:ranges_)if(range.pose.cut) {
            const auto frame=(std::uint64_t(stamp.epoch)<<32)|stamp.frame;
            if(shadow_cut_frame_[range.view]!=frame) {
                shadow_cut_frame_[range.view]=frame;++shadow_continuity_[range.view];
            }
        }
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
    const auto revision=active_?active_->revision:(dkr::runtime::active_payload()?dkr::runtime::active_payload()->revision:rom::Revision::UsV77);
    bool okay=revision==rom::Revision::UsV77?
        dkr_camera_v77_shadow_draw(decoder_.data(),decoder_.size(),&request,&draw_,&shadows_):
        dkr_camera_v80_shadow_draw(decoder_.data(),decoder_.size(),&request,&draw_,&shadows_);
    if(okay)try {capture_shadows(*range);}catch(const std::exception& e) {
        report(e.what());okay=false;draw_.failure="Camera shadow metadata was rejected.";
    }
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
// Shadow metadata belongs to this renderer, scene and viewport. Reuse the
// established canonical/rigid-shadow renderer, not arbitrary vertex matching.
namespace {
std::uint64_t new_camera_shadow_history() {
    // A renderer/session restart can reuse the same scene/object addresses.
    // Allocate an owner lifetime in a host-local namespace, never game state.
    static std::atomic<std::uint64_t> next{1};
    const auto serial=next.fetch_add(1,std::memory_order_relaxed);
    return serial<0x0100000000000000ULL ? 0xC600000000000000ULL|serial : 0ULL;
}
}
void RenderPass::capture_shadows(const Range& range) {
    shadow_markers_.clear();
    const auto valid=[](std::uint32_t p,std::uint32_t size) {
        return p>=0x80000000U&&p<=0x80800000U-size;
    };
    const auto half=[&](std::uint32_t p) {
        std::uint16_t v=0; if(valid(p,2)&&!(p&1))std::memcpy(&v,decoder_.data()+((p&0xFFFFFFU)^2U),2);return v;
    };
    const auto byte=[&](std::uint32_t p) {
        return valid(p,1)?decoder_[(p&0xFFFFFFU)^3U]:std::uint8_t(0);
    };
    const auto number=[&](std::uint32_t p) {
        const auto bits=word(decoder_,p);float v;std::memcpy(&v,&bits,4);return v;
    };
    for(unsigned i=0;i<shadows_.count;++i) {
        const auto& source=shadows_.entries[i];
        if(source.begin<0x80800000U||source.end<source.begin||source.end>0x80900000U||
           (source.begin&7)||(source.end&7)||!valid(source.object,0x68)||!valid(source.shadow,0x10))
            throw mods::Error("Camera shadow observation is invalid.");
        const auto object=source.object,header=word(decoder_,object+0x40);
        const auto kind=half(object+0x48),asset=half(object+0x4A);
        const auto start=std::int16_t(half(source.shadow+8)),end=std::int16_t(half(source.shadow+10));
        ShadowMarker marker{};marker.address=source.begin&0xFFFFFFU;marker.begin=true;
        marker.batches=std::uint8_t(start>=0&&end>=start?
            std::min<int>(end-start,presentation::kMaximumCanonicalShadowBatches):0);
        auto owner=std::find_if(shadow_owners_.begin(),shadow_owners_.end(),[&](const ShadowOwner& o) {
            return o.object==object&&o.header==header&&o.asset==asset&&o.kind==kind&&
                o.view==range.view&&o.continuity==shadow_continuity_[range.view];
        });
        if(owner==shadow_owners_.end()&&shadow_owners_.size()<512U) {
            shadow_owners_.push_back({object,header,asset,kind,range.view,shadow_continuity_[range.view],new_camera_shadow_history()});
            owner=shadow_owners_.end()-1;
        }
        if(owner!=shadow_owners_.end()&&owner->history&&!range.pose.cut) {
            marker.identity=presentation::normalise_identity(std::uint32_t(owner->history)^std::uint32_t(owner->history>>32)^0x43534844U);
            marker.history=owner->history;
        }
        int vehicle=-1;
        if(kind==1) {
            const auto racer=word(decoder_,object+0x64);
            if(valid(racer,0x1D8)) {
                vehicle=byte(racer+0x1D6);
                if(vehicle>2)vehicle=byte(racer+0x1D7);
                if(vehicle>2)vehicle=-1;
            }
        }
        const auto texture=word(decoder_,source.shadow+4);
        marker.policy=presentation::rigid_shadow_owner_policy(kind,std::int8_t(vehicle),byte(texture),asset&presentation::kObjectAssetIdMask);
        marker.motion={{number(object+12),number(object+16),number(object+20)},std::int16_t(half(object+2)),true};
        marker.motion.valid=std::isfinite(marker.motion.position.x)&&std::isfinite(marker.motion.position.y)&&
            std::isfinite(marker.motion.position.z);
        shadow_markers_.push_back(marker);
        marker.address=source.end&0xFFFFFFU;marker.begin=false;shadow_markers_.push_back(marker);
    }
    std::stable_sort(shadow_markers_.begin(),shadow_markers_.end(),[](const auto& a,const auto& b){return a.address<b.address;});
}
std::span<const RenderPass::ShadowMarker> RenderPass::shadows_at(std::uint32_t physical)const {
    if(!drawing_||physical<0x800000U||physical>=0x900000U)return {};
    const auto first=std::lower_bound(shadow_markers_.begin(),shadow_markers_.end(),physical,
        [](const ShadowMarker& marker,std::uint32_t address){return marker.address<address;});
    auto last=first;while(last!=shadow_markers_.end()&&last->address==physical)++last;
    return {first,std::size_t(last-first)};
}

void RenderPass::finish()noexcept {
    shadow_markers_.clear();
    if(drawing_&&decoder_.size()>=0x800000&&prepared_.size()>=0x800000)
        std::memcpy(decoder_.data(),prepared_.data(),0x800000);
    drawing_=nullptr;ended_=0;
}
}
