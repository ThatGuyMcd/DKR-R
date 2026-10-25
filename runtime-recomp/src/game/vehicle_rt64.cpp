#include "driver_anchor_units.hpp"
#include "vehicle_rt64.hpp"
#include "detail_contract.hpp"
#include "vehicle_wheel_interpolation.hpp"
#include "vehicle_seat_alignment.hpp"
#include "native_draw_scope_rt64.hpp"
#include "presentation_identity.hpp"
#include "runtime_enhancements.hpp"
#include "gbi/rt64_gbi_rdp.h"
#include "hle/rt64_workload_queue.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <numbers>
#include <stdexcept>

namespace dkr::runtime::vehicles {
namespace {
constexpr unsigned Scratch=0x00c00000U;
thread_local const RenderPass* current_pass=nullptr;
// Version-1 Studio exports contain native-sized model coordinates after the
// authored package scale, not world coordinates. Use one quarter-scale native
// reference (the clean Tiptup model's 0.25 scale), never the selected driver's
// individual 0.21..0.28 scale. Keeping this fixed lets any driver use the same
// car without making the car itself larger/smaller on character changes.
constexpr float KartModelToWorld=seating::StudioToWorld;
// Studio's ground/reference frame sits 2.5 local world units below the
// native racer draw root. This common frame conversion follows suspension;
// it is NOT kart_transform.translation and never moves the guest/ground.
// Apply it to the car and driver once, outside their authored transforms.
constexpr float KartRideHeight=-2.5F;
// Legacy shared-seat exports only. Fixed-reference seats never inherit it.
constexpr float DriverSeatAdvance=20.0F;
std::atomic<unsigned> diagnostic_count{0};
unsigned read(const RT64::State& s,unsigned p,unsigned n=4) {
    if(p<0x80000000U||p>0x807fffffU||n>0x80800000U-p)throw mods::Error("Custom car draw has an invalid snapshot address.");
    p-=0x80000000U;unsigned v=0;for(unsigned i=0;i<n;++i)v=v<<8|s.RDRAM[(p+i)^3U];return v;
}
float real(const RT64::State& s,unsigned p){const auto v=std::bit_cast<float>(read(s,p));if(!std::isfinite(v))throw mods::Error("Custom car draw has a non-finite transform.");return v;}
bool finite(const hlslpp::float4x4& m) {for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!std::isfinite(float(m[r][c])))return false;return true;}
hlslpp::float4x4 translate(float x,float y,float z) {auto m=hlslpp::float4x4::identity();m[3]=hlslpp::float4(x,y,z,1);return m;}
hlslpp::float4x4 yaw(float angle) {auto m=hlslpp::float4x4::identity();const auto c=std::cos(angle),s=std::sin(angle);m[0]=hlslpp::float4(c,0,-s,0);m[2]=hlslpp::float4(s,0,c,0);return m;}
constexpr float Tau=2*std::numbers::pi_v<float>;
mods::vehicles::Vec3 seat_target(const mods::vehicles::Package& car,unsigned base) {
    if(car.per_driver()) {
        auto target=seating::destination(car,base);target.y+=KartRideHeight;
        return target; // No kart origin/scale/yaw/translation or legacy trim.
    }
    // Forward is the declared FRONT axle, not the model's arbitrary origin,
    // bounding-box centre or the camera. Apply the same yaw as the car mesh.
    // This also handles an export imported with a different orientation.
    const auto& p=car.pivots;
    const float dx=(p[0].x+p[1].x-p[2].x-p[3].x)*.5F;
    const float dz=(p[0].z+p[1].z-p[2].z-p[3].z)*.5F;
    const float length=std::hypot(dx,dz),angle=car.yaw*Tau/360.F;
    float forward_x=0,forward_z=-1; // Agreed native forward convention.
    if(std::isfinite(length)&&length>1e-6F) {
        const auto cosine=std::cos(angle),sine=std::sin(angle);
        forward_x=(dx*cosine+dz*sine)/length;
        forward_z=(-dx*sine+dz*cosine)/length;
    }
    return {(car.seat.x+car.driver_offset.x)*KartModelToWorld+forward_x*DriverSeatAdvance,
            (car.seat.y+car.driver_offset.y)*KartModelToWorld+KartRideHeight,
            (car.seat.z+car.driver_offset.z)*KartModelToWorld+forward_z*DriverSeatAdvance};
}
// RGB uses vertex shade. Opacity MUST use primitive alpha: the RSP fog
// shader replaces shade alpha with fog density (zero near the camera).
// Multiplying texture alpha by that value made otherwise valid cars vanish.
constexpr std::uint32_t Mux0=(1U<<20)|(4U<<15)|(1U<<12)|(3U<<9)|(1U<<5)|4U;
constexpr std::uint32_t Mux1=(8U<<28)|(8U<<24)|(1U<<21)|(3U<<18)|(7U<<15)|(7U<<12)|(7U<<9)|(7U<<6)|(7U<<3)|7U;
constexpr std::uint64_t TextureShade=(std::uint64_t(Mux1)<<32)|Mux0;
// Untextured RGB uses shade; untextured alpha uses the same primitive opacity.
constexpr std::uint32_t Shade0=(15U<<20)|(31U<<15)|(7U<<12)|(7U<<9)|(15U<<5)|31U;
constexpr std::uint32_t Shade1=(15U<<28)|(15U<<24)|(7U<<21)|(7U<<18)|(4U<<15)|(3U<<12)|(4U<<9)|(7U<<6)|(4U<<3)|3U;
constexpr std::uint64_t Shade=(std::uint64_t(Shade1)<<32)|Shade0;
static_assert(((TextureShade>>9)&7U)==3U && ((TextureShade>>50)&7U)==3U);
static_assert(((Shade>>44)&7U)==3U && ((Shade>>32)&7U)==3U);
}
RenderPass::RenderPass(RT64::State& s,std::shared_ptr<const mods::vehicles::Resources> r,Select select):
    state_(s),resources_(std::move(r)),select_(select) {
    // Snapshot the SAME effective option used by the character renderer once
    // per decoder task. Accurate/profile gating stays identical; neither a
    // host preference nor a requested-but-inactive option can force detail.
    maximum_detail_=enhancements::maximum_detail_enabled();
    auto& workload=s.ext.workloadQueue->workloads[s.ext.workloadQueue->writeCursor];
    if(resources_&&!resources_->library.packages.empty())
        wheel_interpolation::begin(workload,s.workloadId+1,resources_);
    previous_pass_=current_pass;current_pass=this;
}
RenderPass::~RenderPass(){current_pass=previous_pass_;}
const RenderPass* active_render_pass() noexcept{return current_pass;}
bool RenderPass::marker(unsigned kind,unsigned value) {
    if(kind>17)return false;
    if(kind==0){if(object_||scope_||!value)return false;scope_=value;setup_=0;return true;}
    if(kind>=7) {
        if(object_||!scope_)return false;
        if(kind==7){if(setup_&1U)return false;draw_scale_=std::bit_cast<float>(value);setup_|=1U;}
        else if(kind<=11) {
            const auto bit=1U<<(kind-7);if(setup_&bit)return false;
            spin_[kind-8]=(spin_[kind-8]&0xffffffff00000000ULL)|std::uint64_t(value);setup_|=bit;
        }else if(kind==12) {
            if(setup_&32U)return false;
            // Native sprite-wheel yaw and this package-space Y rotation use
            // opposite steering signs. Change only the visual conversion;
            // the native wheel angle/input and checkpoint remain untouched.
            steer_=-float(std::int16_t(value))*Tau/65536.F;setup_|=32U;
        }else if(kind<=16) {
            const auto bit=1U<<(kind-7);if(setup_&bit)return false;
            spin_[kind-13]=(spin_[kind-13]&0xffffffffULL)|(std::uint64_t(value)<<32);setup_|=bit;
        }else {
            if(setup_&1024U)return false;wheel_epoch_=value;setup_|=1024U;
        }
        return true;
    }
    if(kind==1){if(object_||!scope_||setup_!=2047)return false;begin(value);object_=value;return true;}
    if(kind==2) {
        if(!object_||value!=object_||wheel_)return false;
        if(active_&&!replacement_submitted_)diagnostic("no replacement submitted; original retained");
        object_=scope_=0;active_=root_ready_=replacement_submitted_=false;return true;
    }
    if(!object_)return false;
    if(kind==3||kind==5){if(wheel_)return false;wheel_=value;hide_attachment_=kind==3;}
    else {if(wheel_!=value||hide_attachment_!=(kind==4))return false;wheel_=0;hide_attachment_=false;}
    return true;
}
void RenderPass::begin(unsigned object) {
    active_=root_ready_=replacement_submitted_=false;profile_=nullptr;driver_=nullptr;wheel_=0;
    if(!resources_||resources_->library.packages.empty())return;
    try {
        car_=(scope_&255U)-1;player_=(scope_>>24)&7U;view_=(scope_>>27)&3U;original_lod_=(scope_>>8)&255U;
        const auto lod=(scope_>>16)&255U;
        if(car_>=resources_->library.packages.size()||lod>4)return;
        // Driver ownership still matches the submitted native LOD. Only the
        // custom car's presentation mesh is pinned; no guest/simulation write.
        vehicle_lod_=maximum_detail_?0U:std::min(original_lod_,5U);
        driver_=resources_->driver(read(state_,object+0x2c,2));
        if(!driver_||!driver_->compatible())return;profile_=&driver_->lods[lod];
        const auto instances=read(state_,object+0x68),instance=read(state_,instances+lod*4),model=read(state_,instance);
        if(read(state_,model+0x24,2)!=profile_->vertices||read(state_,model+0x26,2)!=profile_->triangles)return;
        triangles_=read(state_,model+8)&0x00ffffffU;
        if(triangles_>0x800000U||profile_->triangles>(0x800000U-triangles_)/16)return;
        scale_=draw_scale_;if(!std::isfinite(scale_)||scale_<.001F||scale_>64)return;
        // Choice, live draw scale, steering and camera-independent wheel phase
        // arrived in the bounded display stream, not a later/recycled object.
        const auto points=read(state_,object+0x60),racer=read(state_,object+0x64);
        if(!points||read(state_,points)<4||read(state_,points)>10||!racer)return;
        auto& addresses=texture_addresses_[car_];
        if(addresses.empty()) {
            const auto& pixels=resources_->texture_pixels.at(car_);std::size_t total=0;for(const auto& b:pixels)total+=(b.bytes.size()+7)&~std::size_t(7);
            if(total>0x00f00000U-texture_cursor_)return;
            for(const auto& b:pixels) {
                addresses.push_back(texture_cursor_);
                for(unsigned i=0;i<b.bytes.size();++i)state_.RDRAM[(texture_cursor_+i)^3U]=b.bytes[i];
                texture_cursor_+=unsigned((b.bytes.size()+7)&~std::size_t(7));
            }
        }
        active_=true;
    }catch(const std::exception& e){active_=false;diagnostic(e.what());}
}
void RenderPass::diagnostic(const char* message)const {
    if(diagnostic_count.fetch_add(1,std::memory_order_relaxed)<24)
        std::fprintf(stderr,"[custom-vehicles][draw] car=%u player=%u view=%u lod=%u object=%08X scale=%.6f %s\n",
            car_,player_,view_,original_lod_,object_,scale_,message);
}
void RenderPass::matrix(unsigned index) {
    if(!active_||wheel_||index==0)return;
    auto& rsp=*state_.rsp;
    if(index==1&&!root_ready_) {
        root_=rsp.modelMatrixStack[1];if(!finite(root_)){active_=false;return;}
        const auto inverse=hlslpp::inverse(root_);if(!finite(inverse)){active_=false;return;}
        const auto& car=*resources_->library.packages[car_];const auto anchor=original_textures::model_anchor(car,driver_->base,driver_->native_scale);
        auto target=seat_target(car,driver_->base);
        const auto reference=seating::reference_placement(car);
        const auto angle=car.driver_yaw*Tau/360.F,c=std::cos(angle),s=std::sin(angle);
        target.x+=reference.x*c+reference.z*s;target.y+=reference.y;target.z+=-reference.x*s+reference.z*c;
        auto local=hlslpp::mul(translate(-anchor.x,-anchor.y,-anchor.z),yaw(car.driver_yaw*Tau/360.F));
        // All post-scale Studio positions now share one unit conversion.
        // Divide the anchor by the driver's immutable native scale once to
        // express it in that driver's model coordinates. Its animation and
        // live linear transform remain unchanged; never shrink the character.
        local=hlslpp::mul(local,translate(target.x/scale_,target.y/scale_,target.z/scale_));
        adjustment_=hlslpp::mul(hlslpp::mul(inverse,local),root_);root_ready_=finite(adjustment_);
        if(!root_ready_){active_=false;return;}
        // Submit at the original object root, BEFORE any native chassis or
        // wheel is hidden. The end marker arrives after native mtx_pop and
        // attachment/material teardown, and must not be our first draw point.
        try{replacement_submitted_=draw();}
        catch(const std::exception& e){diagnostic(e.what());replacement_submitted_=false;}
        if(!replacement_submitted_){active_=root_ready_=false;return;}
    }
    if(root_ready_) {
        rsp.modelMatrixStack[index]=hlslpp::mul(rsp.modelMatrixStack[index],adjustment_);
        rsp.modelViewProjChanged=true;
    }
}
bool RenderPass::suppress(unsigned triangle)const {
    if(!active_||!root_ready_||!replacement_submitted_)return false;
    if(wheel_&&hide_attachment_)return true;
    if(triangle<triangles_||(triangle-triangles_)%16)return false;
    const auto i=(triangle-triangles_)/16;return i<profile_->hide.size()&&profile_->hide[i]!=0;
}
const mods::vehicles::ModelProfile* RenderPass::detail_profile(unsigned object,unsigned instance)const {
    if(object!=object_||!active_||!root_ready_||!replacement_submitted_||wheel_)return nullptr;
    const auto slots=read(state_,object+0x68);
    if(!driver_||!slots||read(state_,slots)!=instance)
        throw std::runtime_error("high-detail car driver has no matching LOD0 instance");
    const auto model=read(state_,instance);const auto& high=driver_->lods[0];
    if(read(state_,model+0x24,2)!=high.vertices||read(state_,model+0x26,2)!=high.triangles)
        throw std::runtime_error("high-detail car driver ownership differs from LOD0");
    return &high;
}
bool RenderPass::draw() {
    auto& s=state_;auto& rsp=*s.rsp;auto& rdp=*s.rdp;
    if(!select_||rdp.colorCombinerStackSize>=RDP_EXTENDED_STACK_SIZE||rdp.primColorStackSize>=RDP_EXTENDED_STACK_SIZE||rdp.envColorStackSize>=RDP_EXTENDED_STACK_SIZE){diagnostic("draw-state stack unavailable; original retained");return false;}
    NativeDrawScope restore(s);
#if defined(DKR_ENHANCED_WATER)
    struct WaterScope {
        RT64::State& state;decltype(s.waterMaterial) previous;
        explicit WaterScope(RT64::State& value):state(value),previous(value.waterMaterial){state.waterMaterial={};state.updateDrawStatusAttribute(RT64::DrawAttribute::WaterMaterial);}
        ~WaterScope(){state.waterMaterial=previous;state.updateDrawStatusAttribute(RT64::DrawAttribute::WaterMaterial);}
    } water(s);
#endif
    auto& workload=s.ext.workloadQueue->workloads[s.ext.workloadQueue->writeCursor];
    auto& draw=workload.drawData;
    const auto first_index=draw.faceIndices.size();
    const auto& car=*resources_->library.packages[car_];
    const float angle=car.yaw*Tau/360.F,car_cos=std::cos(angle),car_sin=std::sin(angle);
    const auto units=resources_->vertex_units.at(car_);
    // Remove only the selected driver's live draw scale and vertex scratch
    // precision, then restore the common native-sized vehicle reference.
    // Previously the last step was missing, rendering model units as world
    // units. Translation/projection and the artist's package scale are kept.
    auto car_root=root_;for(unsigned row=0;row<3;++row)car_root[row]=root_[row]*(KartModelToWorld/(units*scale_));
    // The root's Y basis already contains the native live draw scale. Remove
    // it once to move the entire car (including all four wheels) by the same
    // fixed local distance as its seat. Do not double-apply vertex precision
    // or the package scale, and do not change the canonical guest transform.
    const auto offset=seating::scaled(car.kart_offset,KartModelToWorld);
    car_root[3]=root_[3]+root_[0]*(offset.x/scale_)+root_[1]*((KartRideHeight+offset.y)/scale_)+root_[2]*(offset.z/scale_);
    rsp.modelMatrixStack[1]=car_root;rsp.modelMatrixStackSize=2;
    rsp.modelMatrixPhysicalAddressStack[1]=rsp.modelMatrixSegmentedAddressStack[1]=Scratch;
    rsp.modelViewProjChanged=true;rsp.modelViewProjInserted=false;
    const auto alpha=read(s,object_+0x39,1);
    rdp.setEnvColor(0);
    unsigned mesh_index=0;bool in_front=false;float min_w=1e30F,max_w=-1e30F;
    for(const auto& mesh:car.lods[vehicle_lod_].meshes) {
        int wheel=-1;for(unsigned i=0;i<4;++i)if(car.wheels[i]==mesh.id)wheel=int(i);
        wheel_interpolation::Sample motion;
        if(wheel>=0) {
            const auto pivot=seating::source_point(car,car.pivots[unsigned(wheel)]);const auto precision=car.scale*units;
            motion.pivot={pivot.x*precision,pivot.y*precision,pivot.z*precision};
            motion.phase=spin_[unsigned(wheel)];motion.axle=car.axle;motion.yaw=angle;
            motion.steering=steer_;motion.front=wheel<2;motion.epoch=wheel_epoch_;motion.object=object_;
            motion.scene=presentation::task_scene_generation();motion.allowed=presentation::task_interpolation_allowed();
            motion.base=hlslpp::mul(car_root,rsp.extended.viewProjMatrix);
            rsp.modelMatrixStack[1]=hlslpp::mul(wheel_interpolation::local(motion,wheel_interpolation::phase_angle(motion.phase),steer_),car_root);
        }else rsp.modelMatrixStack[1]=car_root;
        const bool textured=mesh.texture>=0;
        const auto opacity=unsigned(mesh.colour[3])*alpha/255;
        rdp.setPrimColor(0,0,0xffffff00U|opacity);
        // Alpha coverage keeps transparent texels from writing solid depth.
        // No lighting passes, rays, or additional render target are required.
        // These pixel-art material images are already small in the export.
        // Point sampling retains their edges instead of adding another blur.
        // NativeDrawScope restores the caller's filter/material state.
        rdp.setOtherMode(G_TF_POINT|G_TP_PERSP,(alpha<255||mesh.colour[3]<255)?0x005049d8U:textured?0x00553078U:0x00552078U);
        if(textured) {
            const auto& texture=car.textures.at(unsigned(mesh.texture));const auto width=texture.image.width,height=texture.image.height;
            const auto format=resources_->texture_pixels.at(car_).at(unsigned(mesh.texture)).rgba32?G_IM_SIZ_32b:G_IM_SIZ_16b;
            rdp.setTextureImage(G_IM_FMT_RGBA,format,width,texture_addresses_[car_].at(unsigned(mesh.texture)));
            const auto mask=[](unsigned n){unsigned bits=0;while((1U<<bits)<n)++bits;return bits;};
            // RGBA32's per-bank line stride is two bytes per texel too.
            rdp.setTile(0,G_IM_FMT_RGBA,format,(width*2+7)/8,0,0,texture.repeat_t?G_TX_WRAP:G_TX_CLAMP,texture.repeat_s?G_TX_WRAP:G_TX_CLAMP,mask(height),mask(width),0,0);
            rdp.setTileSize(0,0,0,(width-1)*4,(height-1)*4);rdp.loadTile(0,0,0,(width-1)*4,(height-1)*4);
        }
        // Texture * shade RGB, texture * independent primitive opacity.
        rdp.setCombine(textured?TextureShade:Shade);
        rsp.textureState.on=textured?1:0;
        rsp.clearGeometryMode(rsp.cullBothMask);
        if(!mesh.double_sided)rsp.setGeometryMode(float(rsp.viewportStack[rsp.viewportStackSize-1].scale.x)>0.F?
            rsp.cullBothMask&~rsp.cullFrontMask:rsp.cullFrontMask);
        unsigned chunk=0;
        for(std::size_t first=0;first<mesh.faces.size();first+=10,++chunk) {
            const auto count=unsigned(std::min<std::size_t>(10,mesh.faces.size()-first));
            // Disjoint fields: no XOR overlap between a different car, mesh,
            // view or chunk. Vertex correspondence never crosses an LOD.
            const auto identity=0xc0000000U|((presentation::task_scene_generation()&7U)<<27)|
                (view_<<25)|(player_<<22)|(vehicle_lod_<<19)|(car_<<15)|(mesh_index<<9)|chunk;
            // Wheel geometry stays fixed. Interpolate its pivot-local angular
            // motion at presentation time, never chord/morph its vertices.
            select_(rsp,identity,wheel<0);
            // Each chunk owns its transform group, even though it uses the
            // same physical scratch address. Don't inherit the previous
            // chunk's transform index after selecting a new group identity.
            rsp.modelViewProjChanged=true;
            for(unsigned face=0;face<count;++face)for(unsigned corner=0;corner<3;++corner) {
                const auto vi=mesh.faces[first+face][corner];auto v=seating::source_point(car,mesh.vertices[vi]);
                const float x=v.x*car_cos+v.z*car_sin,z=-v.x*car_sin+v.z*car_cos;
                const auto coordinate=[&](float value){return std::int16_t(std::lround(std::clamp(value*car.scale*units,-32767.F,32767.F)));};
                RT64::RSP::Vertex vertex{};vertex.x=coordinate(x);vertex.y=coordinate(v.y);vertex.z=coordinate(z);
                vertex.color.r=mesh.colour[0];vertex.color.g=mesh.colour[1];vertex.color.b=mesh.colour[2];vertex.color.a=std::uint8_t(unsigned(mesh.colour[3])*alpha/255);
                std::memcpy(s.RDRAM+Scratch+(face*3+corner)*sizeof(vertex),&vertex,sizeof(vertex));
            }
            rsp.setVertex(Scratch,std::uint8_t(count*3),0);
            if(wheel>=0) {
                motion.identity=identity;motion.transform=rsp.curTransformIndex;
                wheel_interpolation::record(workload,s.workloadId+1,resources_,motion);
            }
            for(unsigned v=0;v<count*3;++v) {
                const auto& clip=draw.posTransformed.at(rsp.indices[v]);const float w=float(clip.w);
                min_w=std::min(min_w,w);max_w=std::max(max_w,w);
                in_front|=std::isfinite(w)&&w>0;
            }
            for(unsigned face=0;face<count;++face) {
                if(textured)for(unsigned corner=0;corner<3;++corner) {
                    const auto& uv=mesh.uv[mesh.faces[first+face][corner]];const auto& t=car.textures[unsigned(mesh.texture)];
                    const auto st=[](float v,unsigned size){return std::uint16_t(std::int16_t(std::lround(std::clamp(v*float(size)*32.F,-32767.F,32767.F))));};
                    rsp.modifyVertex(std::uint16_t(face*3+corner),G_MWO_POINT_ST,(unsigned(st(uv.x,t.image.width))<<16)|st(uv.y,t.image.height));
                }
                rsp.drawIndexedTri(face*3,face*3+1,face*3+2);
            }
            s.flush();
        }
        ++mesh_index;
    }
    const auto indices=draw.faceIndices.size()-first_index;
    if(diagnostic_count.fetch_add(1,std::memory_order_relaxed)<24)
        std::fprintf(stderr,"[custom-vehicles][draw] car=%u player=%u view=%u lod=%u triangles=%zu alpha=%u clip-w=%.3f..%.3f original-hidden=%u opacity-source=primitive-not-fog kart-unit-scale=%.3f driver-scale=%.6f native-anchor-scale=%.6f ride-height=%.3f seat-advance=%.3f steering-sign=-1 wheel-motion=arc-phase64 seat-anchor-units=%s seating=%s base=%u geometry=origin-scale-yaw-translate-v1 vehicle-lod=%u max-detail=%u detail-policy=character-effective-v1 texture-policy=source-colour-point-v1\n",
            car_,player_,view_,original_lod_,indices/3,alpha,min_w,max_w,unsigned(indices!=0&&in_front),KartModelToWorld,scale_,driver_->native_scale,KartRideHeight,car.per_driver()?0.F:DriverSeatAdvance,(car.per_driver()&&car.authored_anchors)?"raw-native-v1":"studio-to-world",car.per_driver()?"per-base-driver-v1":"legacy-shared",driver_->base,vehicle_lod_,unsigned(maximum_detail_));
    // A degenerate/behind-camera replacement must never make the original
    // disappear. The immutable package already guarantees complete meshes.
    detail::note({presentation::task_scene_generation(),object_,view_,original_lod_,vehicle_lod_,2,maximum_detail_,
        indices!=0&&in_front?"custom vehicle submitted":"custom vehicle not visible; original retained"});
    return indices!=0&&in_front;
}
} // namespace dkr::runtime::vehicles
