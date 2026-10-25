#include "online_view_rt64.hpp"
#include "online_nameplates_rt64.hpp"
#include "hud_projection_rt64.hpp"
#include "hud_reference_layout.hpp"
#include "presentation_identity.hpp"
#include "hle/rt64_state.h"
#include <bit>
#include <cmath>

namespace dkr::runtime::online_view {
struct Pass::Impl {
    Plan plan;
    struct Observation {dkr_owned_draw_event event;unsigned order;};
    std::array<Observation,2048> events{};
    unsigned count=0,view=4;
    enum class Phase {Shared,World,Hud};Phase phase=Phase::Shared;
    bool divider=false,projection=false,camera_valid=false;
    std::uint32_t scene=0,framebuffer=0;
    float aspect=4.F/3.F;
    hud::LayoutMode hud_mode=hud::LayoutMode::Original;
    hlslpp::float4x4 camera{};
    Nameplates names;
    IdentitySelector select=nullptr;
    std::array<hud::groups::Transform,4> widgets{};
    std::array<unsigned,4> widget_elements{};
    unsigned widget_depth=0,rejected_widgets=0;
    bool suppress() const {return plan.focus&&(divider||(phase!=Phase::Shared&&view!=plan.owner));}
    bool main_image(const RT64::State& s) const {return (s.rdp->colorImage.address&0xFFFFFFU)==framebuffer;}
    void projection_end(RT64::RSP& r) {
        if(projection){hud::pop_hud_projection(r);projection=false;}
    }
    void scissor(RT64::State& s,bool native_change=false) {
        if(!plan.focus||!main_image(s))return;
        auto& rect=s.rdp->scissorRectStack[s.rdp->scissorStackSize-1];
        if(suppress())rect.ulx=rect.uly=rect.lrx=rect.lry=0;
        else if(phase!=Phase::Shared||native_change) {
            const int gutter=int(std::lround(640.F*std::max(0.F,aspect/(4.F/3.F)-1.F)));
            rect.ulx=-gutter;rect.uly=0;rect.lrx=1280+gutter;rect.lry=960;
        } else return;
        auto i=s.rdp->scissorStackSize-1;
        s.rdp->extended.scissorLeftOriginStack[i]=G_EX_ORIGIN_NONE;
        s.rdp->extended.scissorRightOriginStack[i]=G_EX_ORIGIN_NONE;
        s.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
    }
};
Pass::Pass():impl_(std::make_unique<Impl>()) {}
Pass::~Pass()=default;
void Pass::prepare(const Session& session,std::span<const dkr_owned_draw_event> events,
        std::span<std::uint8_t> decoder,std::uint32_t scene,float aspect,std::uint32_t framebuffer,
        hud::LayoutMode hud_mode) noexcept {
    auto& p=*impl_;p.plan=online_view::plan(session,events);p.count=0;p.view=4;
    p.phase=Impl::Phase::Shared;p.divider=p.projection=p.camera_valid=false;
    p.widget_depth=p.rejected_widgets=0;
    p.scene=scene;p.aspect=std::isfinite(aspect)?std::clamp(aspect,1.F,8.F):4.F/3.F;
    p.framebuffer=framebuffer&0xFFFFFFU;
    p.hud_mode=hud_mode;
    if(!p.plan.eligible)return;
    for(unsigned i=0;i<events.size();++i) {
        const auto& e=events[i];
        if(e.kind<DKR_OWNED_ONLINE_VIEW || e.kind==DKR_OWNED_LOCAL_MODEL || e.kind==DKR_OWNED_LOCAL_OBJECT || e.kind==DKR_OWNED_CAMERA_WORLD_END)continue;
        if(p.count==p.events.size()||!dkr_owned_draw_event_valid(&e)) {p.plan={};p.count=0;return;}
        p.events[p.count++]={e,i};
    }
    std::sort(p.events.begin(),p.events.begin()+p.count,[](const auto& a,const auto& b){
        return a.event.address!=b.event.address?a.event.address<b.event.address:a.order<b.order;
    });
    p.names.prepare(session,p.plan,decoder,p.plan.focus?
        presentation::normalise_identity(scene^0x46554C4CU^session.owner):scene);
}
void Pass::selector(IdentitySelector value) noexcept{impl_->select=value;}
void Pass::begin(RT64::State& state) {
    auto& p=*impl_;
    // One outer task-local aspect scope. Native world/HUD scopes nest ABOVE
    // it and are retired by their existing owners before finish() pops ours.
    // This avoids interleaving pops at shared world-end/HUD-begin addresses.
    if(p.plan.focus){hud::push_hud_projection(*state.rsp);p.projection=true;}
}
bool Pass::owns_framebuffer(std::uint32_t address) const noexcept {
    return impl_->plan.focus&&(address&0xFFFFFFU)==impl_->framebuffer;
}
bool Pass::suppress_rectangle(const RT64::State& s) const noexcept {
    // The native border and coverage erase are real pixel-writing commands.
    // Discard only their marked draws, not state, branches, clears or syncs.
    return impl_->suppress()&&impl_->main_image(s);
}
bool Pass::focused() const noexcept{return impl_->plan.focus;}
bool Pass::focused_hud() const noexcept {
    return impl_->plan.focus&&impl_->phase==Impl::Phase::Hud&&impl_->view==impl_->plan.owner;
}
bool Pass::widget_active() const noexcept {
    const auto& p=*impl_;
    return p.plan.focus&&p.widget_depth&&(focused_hud()||
        (p.phase==Impl::Phase::Shared&&p.widget_elements[p.widget_depth-1]==59));
}
hud::groups::Transform Pass::widget_transform() const noexcept {
    return widget_active()?impl_->widgets[impl_->widget_depth-1]:hud::groups::Transform{};
}
void Pass::before_command(RT64::State& s,std::uint32_t command) {
    auto& p=*impl_;if(!p.plan.eligible)return;
    const auto address=command|0x80000000U;
    auto at=std::lower_bound(p.events.begin(),p.events.begin()+p.count,address,
        [](const auto& a,auto b){return a.event.address<b;});
    for(;at!=p.events.begin()+p.count&&at->event.address==address;++at) {
        const auto& e=at->event;
        if(e.kind==DKR_OWNED_ONLINE_RACER) {
            if(p.phase==Impl::Phase::World&&p.camera_valid&&p.main_image(s))p.names.draw(s,e,p.camera,p.select);
            continue;
        }
        s.flush();
        switch(e.kind) {
        case DKR_OWNED_ONLINE_HUD_WIDGET_BEGIN:
            if(p.widget_depth==p.widgets.size()||p.rejected_widgets) {++p.rejected_widgets;break;}
            {
                hud::groups::Transform transform;
                transform.x=std::bit_cast<float>(e.parameters[0]);transform.y=std::bit_cast<float>(e.parameters[1]);
                transform.scale=std::bit_cast<float>(e.parameters[3]);
                transform.scale_y=std::bit_cast<float>(e.parameters[4]);
                if(!std::isfinite(transform.x)||!std::isfinite(transform.y)||
                   std::abs(transform.x)>4096||std::abs(transform.y)>1024||
                   !transform.valid(2.5F)) {
                    ++p.rejected_widgets;break;
                }
                const auto element=e.parameters[2];
                const auto widget=element<59?hud::kElements[element].widget:hud::Widget::Minimap;
                const auto anchor=hud::reference::anchor(element,widget,hud::groups::Scenario::Race,false,false);
                // Same single-player edge policy as offline. The native
                // multiplayer source already contains its current animation.
                transform.x+=hud::reference::transform(p.hud_mode,p.aspect,anchor,0,false).x;
                p.widget_elements[p.widget_depth]=element;
                p.widgets[p.widget_depth++]=transform;
            }
            break;
        case DKR_OWNED_ONLINE_HUD_WIDGET_END:
            if(p.rejected_widgets)--p.rejected_widgets;else if(p.widget_depth)--p.widget_depth;break;
        case DKR_OWNED_ONLINE_VIEW:
            p.widget_depth=p.rejected_widgets=0;
            p.view=e.token;p.phase=Impl::Phase::World;p.camera_valid=false;
            p.names.begin_view(p.view);
            break;
        case DKR_OWNED_ONLINE_HUD:
            p.widget_depth=p.rejected_widgets=0;
            p.view=e.token;p.phase=Impl::Phase::Hud;
            break;
        case DKR_OWNED_ONLINE_SHARED:
            p.widget_depth=p.rejected_widgets=0;
            p.phase=Impl::Phase::Shared;p.view=4;break;
        case DKR_OWNED_ONLINE_DIVIDER_BEGIN:p.divider=true;break;
        case DKR_OWNED_ONLINE_DIVIDER_END:p.divider=false;break;
        default:break;
        }
        p.scissor(s,true);
    }
    // RDP pop/scoped alignment operations can restore a previous scissor.
    // Keep hidden *main-image* draws clipped, without skipping state/loads,
    // branches, auxiliary targets, native camera updates or full syncs.
    if(p.suppress())p.scissor(s);
}
void Pass::after_command(RT64::State& s,bool scissor,bool viewport) {
    auto& p=*impl_;if(!p.plan.focus||!p.main_image(s))return;
    if(scissor||p.suppress())p.scissor(s);
    // Native mtx_ortho already installs a full HUD canvas with its deliberate
    // 160/160 scale. Do not replace that with the world's 160/120 scale.
    if(viewport&&p.phase==Impl::Phase::World&&p.view==p.plan.owner) {
        auto& v=s.rsp->viewportStack[s.rsp->viewportStackSize-1];
        v.scale.x=std::copysign(160.F,float(v.scale.x));v.translate.x=160.F;
        v.scale.y=std::copysign(120.F,float(v.scale.y));v.translate.y=120.F;
        s.rsp->viewportChanged=true;
    }
}
void Pass::camera_root(RT64::RSP& rsp,unsigned matrix) {
    auto& p=*impl_;if(!p.plan.eligible||p.phase!=Impl::Phase::World||matrix>2)return;
    p.camera=rsp.modelMatrixStack[matrix];p.camera_valid=true;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)p.camera_valid&=std::isfinite(float(p.camera[r][c]));
}
void Pass::finish(RT64::State& s) {
    auto& p=*impl_;if(!p.plan.eligible)return;
    s.flush();p.projection_end(*s.rsp);p.phase=Impl::Phase::Shared;p.view=4;p.divider=false;
}
}
