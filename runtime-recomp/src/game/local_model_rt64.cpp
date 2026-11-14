#include "local_model_rt64.hpp"
#include "detail_contract.hpp"
#include "detail_metrics.hpp"
#include "vehicle_rt64.hpp"
#include "visual_model_kernel.h"
#include "native_draw_scope_rt64.hpp"
#include "runtime_enhancements.hpp"
#include "revision_addresses.hpp"
#include "presentation_identity.hpp"
#include "netplay/local_scenery.hpp"
#include "local_scenery_policy.hpp"
#include "gbi/rt64_gbi_rdp.h"
#include "hle/rt64_state.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace dkr::runtime {
namespace {
constexpr std::uint32_t kTextureBegin=0xF00000U,kTextureEnd=0x1000000U,kVertices=0xC00100U;
struct Reader {
    std::span<const std::uint8_t> ram;
    std::uint32_t offset(std::uint32_t p,std::size_t bytes,unsigned alignment=1) const {
        if(p>=0x80000000U && p<0x81000000U)p-=0x80000000U;
        if(p>=ram.size() || bytes>ram.size()-p || (p&(alignment-1)))throw std::runtime_error("local model resource span");
        return p;
    }
    std::uint32_t word(std::uint32_t p)const{p=offset(p,4,4);std::uint32_t v;std::memcpy(&v,ram.data()+p,4);return v;}
    std::uint16_t half(std::uint32_t p)const{p=offset(p,2,2);std::uint16_t v;std::memcpy(&v,ram.data()+(p^2U),2);return v;}
    std::uint8_t byte(std::uint32_t p)const{return ram[offset(p,1)^3U];}
    float real(std::uint32_t p)const{const auto v=std::bit_cast<float>(word(p));if(!std::isfinite(v))throw std::runtime_error("local model transform");return v;}
};
using Texture=netplay::experimental::LocalSceneryTexture;
struct Corner {RT64::RSP::Vertex vertex{};std::int16_t s=0,t=0;unsigned matrix=1;};
struct Step {
    enum Kind {Command,Triangle} kind=Command;
    std::uint32_t w0=0,w1=0;
    std::array<Corner,3> corners{};
    unsigned source_triangle=0;
};
struct Packet {std::vector<Step> steps;dkr_visual_model_result model{};unsigned triangles=0;};
bool rdp_opcode(unsigned op) {
    return op==0xE6||op==0xE7||op==0xE8||op==0xF0||op==0xF2||op==0xF3||op==0xF4||op==0xF5||op==0xFD||op==0xFC||op==0xEF;
}
void command(RT64::State& state,std::uint32_t w0,std::uint32_t w1) {
    RT64::DisplayList c{};c.w0=w0;c.w1=w1;auto* p=&c;
    switch(w0>>24) {
    case 0xFD:state.rdp->setTextureImage(c.p0(21,3),c.p0(19,2),c.p0(0,12)+1,w1);break;
    case 0xFC:state.rdp->setCombine((std::uint64_t(w0)<<32)|w1);break;
    case 0xEF:state.rdp->setOtherMode(w0&0xFFFFFFU,w1);break;
    case 0xE6:case 0xE7:case 0xE8:break;
    case 0xF0:RT64::GBI_RDP::loadTLUT(&state,&p);break;
    case 0xF2:RT64::GBI_RDP::setTileSize(&state,&p);break;
    case 0xF3:RT64::GBI_RDP::loadBlock(&state,&p);break;
    case 0xF4:RT64::GBI_RDP::loadTile(&state,&p);break;
    case 0xF5:RT64::GBI_RDP::setTile(&state,&p);break;
    case 0xB6:state.rsp->clearGeometryMode(w1);break;
    case 0xB7:state.rsp->setGeometryMode(w1);break;
    default:throw std::logic_error("Unadmitted local model command");
    }
}
hlslpp::float4x4 head_matrix(const dkr_visual_model_result& model,std::int16_t angle) {
    const float radians=6.2831853071795864769F/65536.F;
    const float c=std::cos(float(angle)*radians),s=std::sin(float(angle)*radians);
    const float ct=std::cos(float(model.head[3])*radians),st=std::sin(float(model.head[3])*radians);
    const float x=model.head[0],y=model.head[1],z=model.head[2];
    auto m=hlslpp::float4x4::identity();
    m[0]=hlslpp::float4(c*ct,c*st,-s,0);
    m[1]=hlslpp::float4(-st,ct,0,0);
    m[2]=hlslpp::float4(s*ct,s*st,c,0);
    m[3]=hlslpp::float4(-x*c*ct+y*st-z*s*ct+x,-x*c*st-y*ct-z*s*st+y,x*s-z*c+z,1);
    return m;
}
hlslpp::float4x4 object_matrix(const Reader& r,std::uint32_t object) {
    const float radians=6.2831853071795864769F/65536.F;
    const float y=float(std::int16_t(r.half(object)))*radians,x=float(std::int16_t(r.half(object+2)))*radians,z=float(std::int16_t(r.half(object+4)))*radians;
    const float sy=std::sin(y),cy=std::cos(y),sx=std::sin(x),cx=std::cos(x),sz=std::sin(z),cz=std::cos(z),scale=r.real(object+8);
    if(scale<=0||scale>128)throw std::runtime_error("local scenery model scale");
    auto m=hlslpp::float4x4::identity();
    m[0]=hlslpp::float4((sx*sy*sz+cz*cy)*scale,sz*cx*scale,(sx*cy*sz-cz*sy)*scale,0);
    m[1]=hlslpp::float4((sx*sy*cz-sz*cy)*scale,cz*cx*scale,(sx*cy*cz+sz*sy)*scale,0);
    m[2]=hlslpp::float4(cx*sy*scale,-sx*scale,cx*cy*scale,0);
    m[3]=hlslpp::float4(r.real(object+12),r.real(object+16),r.real(object+20),1);
    return m;
}
}
struct LocalModelRT64Pass::Impl {
    struct Observation {dkr_owned_draw_event e{};bool consumed=false,replaced=false,extra=false;unsigned alpha=255;float squared_distance=0;};
    struct Image {std::uint32_t source=0,count=0,address=0;std::shared_ptr<const Texture> texture;};
    std::vector<Observation> observations;
    std::vector<Image> images;
    // Sorted, merged successful replacement spans. Query cost is logarithmic
    // instead of scanning every racer/scenery observation per triangle.
    std::vector<std::pair<std::uint32_t,std::uint32_t>> suppressed;
    detail::FrameMetrics profile;
    unsigned kernel_builds=0,vertex_uploads=0,batches=0,texture_hits=0,texture_captures=0;
    void suppress(std::uint32_t first,std::uint32_t last) {
        auto at=std::lower_bound(suppressed.begin(),suppressed.end(),first,[](const auto& row,auto value){return row.second<value;});
        while(at!=suppressed.end()&&at->first<=last){first=std::min(first,at->first);last=std::max(last,at->second);at=suppressed.erase(at);}
        suppressed.insert(at,{first,last});
    }
    #include "detail_texture_cache.inl"
    std::span<std::uint8_t> ram;
    std::uint32_t scene=0,cursor=kTextureBegin;
    unsigned draws=0,logged=0;
    std::size_t cached=0,packet_steps=0;
    unsigned kernel_operations=0;
    unsigned submitted_triangles=0;
    bool enabled=false;
    local_scenery::Settings settings;
    struct CachedPacket {dkr_visual_model_request request{};Packet packet;bool valid=false;};
    // Reuse allocations across frames and poses across split-screen views.
    // No pointer to reusable guest scratch is retained in a compiled packet.
    std::array<CachedPacket,96> cache;
    std::array<CachedPacket,128> racer_cache;
    std::size_t racer_cached=0,racer_steps=0;
    unsigned racer_operations=0;
    std::array<unsigned,4> racer_triangles{};
    std::uint32_t texture(const Reader& r,std::uint32_t source,unsigned count,const dkr_visual_model_result& model) {
        for(unsigned i=0;i<images.size();++i)if(images[i].source==source && images[i].count==count)return i;
        for(unsigned i=0;i<model.texture_count;++i) {
            const auto p=model.textures[i];
            if((r.word(p+0xC)&0xFFFFFFU)!=source || r.half(p+0xA)!=count)continue;
            auto tex=retain_texture(r,p);
            const auto bytes=(tex->bytes.size()+7U)&~std::size_t(7U);
            if(bytes>kTextureEnd-cursor)throw std::runtime_error("local model texture arena full");
            const auto address=cursor;cursor+=std::uint32_t(bytes);
            std::memcpy(ram.data()+address,tex->bytes.data(),tex->bytes.size());
            images.push_back({source,count,address,std::move(tex)});return unsigned(images.size()-1);
        }
        throw std::runtime_error("local model texture ownership");
    }
    const Packet* build(const dkr_owned_draw_event& e,bool extra) {
        auto packets=extra?std::span<CachedPacket>{cache}:std::span<CachedPacket>{racer_cache};
        auto& packet_count=extra?cached:racer_cached;
        auto& operations=extra?kernel_operations:racer_operations;
        auto& prepared_steps=extra?packet_steps:racer_steps;
        const auto operation_limit=extra?12000000U:detail::KernelOperations;
        const auto step_limit=extra?65536U:detail::PreparedSteps;
        const auto& p=e.parameters;
        dkr_visual_model_request request{p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[9]};
        for(std::size_t i=0;i<packet_count;++i)if(std::memcmp(&packets[i].request,&request,sizeof(request))==0)
            return packets[i].valid?&packets[i].packet:nullptr;
        if(packet_count==packets.size()||operations>=operation_limit||prepared_steps>=step_limit)throw std::runtime_error(extra?"scenery preparation capacity":"racer preparation capacity");
        auto& entry=packets[packet_count++];entry.request=request;entry.valid=false;
        auto& packet=entry.packet;
        auto kernel=revision_addresses::selected_revision()==rom::Revision::UsV80?dkr_visual_v80_model:dkr_visual_v77_model;
        packet.steps.clear();packet.triangles=0;
        // Failed packets count too: malformed late commands must not bypass
        // the frame's allocation budget by leaving many large partial vectors.
        struct Charge {std::size_t& total;const Packet& packet;~Charge(){total+=packet.steps.size();}} charge{prepared_steps,packet};
        ++kernel_builds;
        if(!kernel(ram.data(),ram.size(),&request,&packet.model)) {
            if(logged++<8)std::fprintf(stderr,"[graphics][local-detail] retained original mesh: %s\n",packet.model.failure?packet.model.failure:"invalid model");
            operations+=packet.model.operations;
            return nullptr;
        }
        operations+=packet.model.operations;
        Reader r{ram};std::array<Corner,32> vertices{};std::array<bool,32> valid{};
        unsigned matrix=1,vertex_cursor=0,triangles=0;
        for(unsigned i=0;i<packet.model.count;++i) {
            const auto at=packet.model.commands+i*8U,w0=r.word(at),w1=r.word(at+4),op=w0>>24;
            if(op==0x07) {
                const unsigned count=(w0>>16)&255U;const auto source=w1&0xFFFFFFU;
                if(!count || count>32 || source>=0x800000U || count*8>0x800000U-source)throw std::runtime_error("local model counted block");
                const unsigned first=r.word(source)>>24;
                if(first==0xFC && count==2 && (r.word(source+8)>>24)==0xEF) {
                    for(unsigned n=0;n<2;++n)packet.steps.push_back({Step::Command,r.word(source+n*8),r.word(source+n*8+4)});
                } else {
                    const auto& image=images[texture(r,source,count,packet.model)];
                    for(const auto& words:image.texture->commands) {
                        if(!rdp_opcode(words[0]>>24))throw std::runtime_error("local model upload opcode");
                        packet.steps.push_back({Step::Command,words[0],words[0]>>24==0xFD?image.address+words[1]:words[1]});
                    }
                }
            } else if(op==0x04) {
                if(!(w0&0x10000U))vertex_cursor=0;
                const unsigned count=((w0>>19)&31U)+1,dst=vertex_cursor+((w0>>9)&31U);
                const auto source=w1&0xFFFFFFU;r.offset(source,count*10,2);
                if(source<packet.model.vertices || source>packet.model.vertices+packet.model.vertex_count*10 ||
                   count*10>packet.model.vertices+packet.model.vertex_count*10-source)
                    throw std::runtime_error("local model vertex ownership");
                if(dst+count>32)throw std::runtime_error("local model vertex slots");
                for(unsigned v=0;v<count;++v) {
                    const auto a=source+v*10;auto& out=vertices[dst+v];
                    out.vertex.x=std::int16_t(r.half(a));out.vertex.y=std::int16_t(r.half(a+2));out.vertex.z=std::int16_t(r.half(a+4));
                    out.vertex.color.r=r.byte(a+6);out.vertex.color.g=r.byte(a+7);out.vertex.color.b=r.byte(a+8);out.vertex.color.a=r.byte(a+9);
                    out.matrix=matrix;valid[dst+v]=true;
                }
                vertex_cursor+=count;
            } else if(op==0xBC && (w0&255U)==10) {
                matrix=(w1>>6)&3U;if(matrix<1||matrix>2)throw std::runtime_error("local model matrix selector");
            } else if(op==0x05) {
                const unsigned count=((w0>>20)&15U)+1;const auto source=w1&0xFFFFFFU;r.offset(source,count*16,2);
                if(source<packet.model.triangles || source>packet.model.triangles+packet.model.triangle_count*16 ||
                   count*16>packet.model.triangles+packet.model.triangle_count*16-source)
                    throw std::runtime_error("local model triangle ownership");
                if(triangles+count>8192)throw std::runtime_error("local model triangle budget");triangles+=count;
                for(unsigned tri=0;tri<count;++tri) {
                    Step step;step.source_triangle=(source-packet.model.triangles)/16+tri;step.kind=Step::Triangle;step.w0=r.byte(source+tri*16);step.w1=(w0>>16)&15U;
                    for(unsigned c=0;c<3;++c) {
                        const auto n=r.byte(source+tri*16+1+c);if(n>=32||!valid[n])throw std::runtime_error("local model triangle reference");
                        step.corners[c]=vertices[n];step.corners[c].s=std::int16_t(r.half(source+tri*16+4+c*4));step.corners[c].t=std::int16_t(r.half(source+tri*16+6+c*4));
                    }
                    packet.steps.push_back(step);
                }
            } else if(op==0xB6||op==0xB7||op==0xE7)packet.steps.push_back({Step::Command,w0,w1});
            else throw std::runtime_error("local model geometry opcode");
            if(packet.steps.size()>32768 || prepared_steps+packet.steps.size()>step_limit)
                throw std::runtime_error("local model packet budget");
        }
        packet.triangles=triangles;entry.valid=triangles!=0;
        return entry.valid?&packet:nullptr;
    }
};
LocalModelRT64Pass::LocalModelRT64Pass():impl_(std::make_unique<Impl>()) {}
LocalModelRT64Pass::~LocalModelRT64Pass()=default;
void LocalModelRT64Pass::prepare(std::span<const dkr_owned_draw_event> events,std::span<std::uint8_t> ram,std::uint32_t scene) noexcept {
    auto& p=*impl_;
    p.profile.begin(scene,unsigned(events.size()),p.kernel_builds,p.vertex_uploads,p.batches,p.texture_hits,p.texture_captures);
    detail::MicroScope prepare_cost{p.profile.prepare_us};
    if(p.texture_scene!=scene||p.texture_frame==UINT64_MAX){p.retained_textures.clear();p.retained_texture_bytes=0;p.texture_scene=scene;p.texture_frame=0;}
    ++p.texture_frame;p.suppressed.clear();
    p.observations.clear();p.images.clear();p.ram=ram;p.scene=scene;p.cursor=kTextureBegin;p.draws=0;
    p.cached=p.packet_steps=p.kernel_operations=0;
    p.submitted_triangles=0;p.racer_cached=p.racer_steps=p.racer_operations=0;p.racer_triangles.fill(0);
    std::size_t retained=0;for(const auto& entry:p.cache)retained+=entry.packet.steps.capacity();
    if(retained>131072)for(auto& entry:p.cache)std::vector<Step>().swap(entry.packet.steps);
    retained=0;for(const auto& entry:p.racer_cache)retained+=entry.packet.steps.capacity();
    if(retained>detail::PreparedSteps*2U)for(auto& entry:p.racer_cache)std::vector<Step>().swap(entry.packet.steps);
    const bool maximum=enhancements::maximum_detail_enabled();
    p.settings={enhancements::presentation_profile(),enhancements::scenery_retention_mode(),enhancements::view_distance_multiplier(),
        enhancements::keep_hub_scenery_requested(),enhancements::keep_track_scenery_requested(),enhancements::keep_minigame_scenery_requested(),
        enhancements::animated_scenery_distance_multiplier(),enhancements::billboard_effect_distance_multiplier(),enhancements::water_lava_detail_multiplier(),enhancements::extended_culling_requested()};
    p.enabled=enhancements::modern_presentation_enabled() && ram.size()>=kTextureEnd;
    if(!p.enabled)return;
    try {
        for(const auto& e:events)if(maximum && e.kind==DKR_OWNED_LOCAL_MODEL) {
            if(!dkr_owned_draw_event_valid(&e)||p.observations.size()==detail::Observations){p.enabled=false;return;}
            p.observations.push_back({e});
        }
        std::stable_sort(p.observations.begin(),p.observations.end(),[](const auto& a,const auto& b){return a.e.address<b.e.address;});
        // Preflight racers first. Optional scenery cannot consume their pose,
        // packet or texture preparation capacity. Poses matching across views
        // use one checked packet; only transforms/head angles remain per view.
        for(const auto& o:p.observations)try{(void)p.build(o.e,false);}
        catch(const std::exception& error){detail::note({scene,o.e.parameters[1],o.e.token,0,0,1,maximum,error.what()});}
        Reader r{ram};
        std::array<std::vector<std::uint32_t>,4> drawn;
        for(const auto& e:events)if(e.kind==DKR_OWNED_LOCAL_OBJECT&&dkr_owned_draw_event_valid(&e))drawn[e.token].push_back(e.parameters[0]);
        for(auto& view:drawn)std::sort(view.begin(),view.end());
        if(std::all_of(drawn.begin(),drawn.end(),[](const auto& view){return view.empty();}))return;
        const auto count=r.word(revision_addresses::ObjectCount),start=r.word(revision_addresses::ObjectListStart),list=r.word(revision_addresses::ObjectList);
        if(count>2048 || start>count)throw std::runtime_error("local scenery object count");
        r.offset(list,count*4,4);
        // The canonical frame tells us which actors were actually submitted
        // in each view. Supplemental ranges never draw those a second time.
        for(const auto& view:events) {
            if(view.kind!=DKR_OWNED_LOCAL_SCENERY || !dkr_owned_draw_event_valid(&view) ||
               !p.settings.live_enabled(int(view.parameters[1])))continue;
            std::array<float,3> camera;
            for(unsigned a=0;a<3;++a){camera[a]=std::bit_cast<float>(view.parameters[2+a]);if(!std::isfinite(camera[a]))throw std::runtime_error("local scenery camera");}
            unsigned added=0;
            for(unsigned i=start;i<count && added<64 && p.observations.size()<640;++i) {
                const auto object=r.word(list+i*4);r.offset(object,0x80,4);
                const unsigned flags=r.half(object+6),behaviour=r.half(object+0x48);
                if(flags&0xD080U || (flags&(0x200U<<(view.token&1U))) || behaviour==1 || behaviour==58)continue;
                if(behaviour!=0 && !enhancements::is_animated_scenery_behaviour(int(behaviour)) &&
                   !enhancements::is_billboard_or_effect_behaviour(int(behaviour)) && !enhancements::is_water_or_lava_behaviour(int(behaviour)))continue;
                if(std::binary_search(drawn[view.token].begin(),drawn[view.token].end(),object))continue;
                const auto header=r.word(object+0x40);r.offset(header,0x78,4);
                if(r.byte(header+0x53)!=0 || r.word(object+0x60))continue; // Attachments need their own owner; never leave detached parts.
                const auto index=r.byte(object+0x3A),models=r.byte(header+0x55);
                if(!models||index>=models||models>32)continue;
                const auto instance=r.word(r.word(object+0x68)+index*4);if(!instance)continue;
                const int multiplier=enhancements::object_view_distance_multiplier(int(behaviour),p.settings.view_distance,
                    p.settings.animated_distance,p.settings.billboard_distance,p.settings.water_distance);
                if(multiplier<=1 && !p.settings.enabled(int(view.parameters[1])) && !p.settings.extended)continue;
                if(!local_scenery::region_visible(std::int16_t(r.half(object+0x2E)),view,p.settings.retention))continue;
                float distance=0;
                for(unsigned a=0;a<3;++a){const float delta=r.real(object+12+a*4)-camera[a];distance+=delta*delta;}
                const auto alpha=local_scenery::fade_alpha(enhancements::effective_view_distance(p.settings.profile,std::int16_t(r.half(header+0x4E)),multiplier),distance);
                if(!alpha || (alpha<255 && !view.parameters[15]))continue;
                dkr_owned_draw_event e{};e.kind=DKR_OWNED_LOCAL_MODEL;e.address=view.address;e.token=view.token;
                e.parameters[1]=object;e.parameters[2]=instance;e.parameters[3]=r.half(object+0x18);e.parameters[4]=r.byte(object+0x3B);
                e.parameters[5]=view.parameters[15]?4U:0U;e.parameters[6]=view.parameters[15] && alpha==255;e.parameters[9]=std::bit_cast<std::uint32_t>(1.F);
                p.observations.push_back({e,false,false,true,alpha,distance});++added;
            }
        }
        std::stable_sort(p.observations.begin(),p.observations.end(),[](const auto& a,const auto& b){
            if(a.e.address!=b.e.address)return a.e.address<b.e.address;
            return a.squared_distance>b.squared_distance;
        });
    }catch(const std::exception& error){
        std::erase_if(p.observations,[](const auto& o){return o.extra;});
        detail::note({scene,0,0,0,0,1,maximum,error.what()});
    }catch(...){std::erase_if(p.observations,[](const auto& o){return o.extra;});}
}
bool LocalModelRT64Pass::replaces_triangle(std::uint32_t command)const noexcept {
    const auto& p=*impl_;if(!p.enabled)return false;
    const auto at=std::upper_bound(p.suppressed.begin(),p.suppressed.end(),command,[](auto value,const auto& row){return value<row.first;});
    return at!=p.suppressed.begin()&&command<std::prev(at)->second;
}
void LocalModelRT64Pass::draw_at(RT64::State& state,std::uint32_t command_address,IdentitySelector select) {
    auto& p=*impl_;if(!p.enabled||!select)return;
    const auto address=command_address+0x80000000U;
    auto found=std::lower_bound(p.observations.begin(),p.observations.end(),address,[](const auto& o,auto value){return o.e.address<value;});
    if(found==p.observations.end()||found->e.address!=address)return;
    detail::MicroScope submit_cost{p.profile.submit_us};
    for(;found!=p.observations.end()&&found->e.address==address;++found) {
        auto& o=*found;if(o.consumed)continue;o.consumed=true;
        auto& rsp=*state.rsp;
        if(state.rdp->colorCombinerStackSize>=RDP_EXTENDED_STACK_SIZE || state.rdp->primColorStackSize>=RDP_EXTENDED_STACK_SIZE || state.rdp->envColorStackSize>=RDP_EXTENDED_STACK_SIZE)continue;
        const Packet* packet=nullptr;
        const mods::vehicles::ModelProfile* driver_mask=nullptr;
        auto transform=hlslpp::float4x4::identity();
        std::uint32_t environment=0xFFFFFF00U,primitive=0xFFFFFF00U|o.alpha;
        try {
            // Resolve every optional resource before touching decoder state.
            if(o.extra) {
                const Reader r{p.ram};
                transform=hlslpp::mul(object_matrix(r,o.e.parameters[1]),rsp.modelMatrixStack[0]);
                if(float(transform[3].w)<=0)continue;
                const auto shading=r.word(o.e.parameters[1]+0x54);
                if(shading) {
                    r.offset(shading,8,4);
                    const auto intensity=std::uint32_t(std::clamp(r.real(shading)*255.F,0.F,255.F));
                    primitive=(intensity<<24)|(intensity<<16)|(intensity<<8)|o.alpha;
                    environment=(std::uint32_t(r.byte(shading+4))<<24)|(std::uint32_t(r.byte(shading+5))<<16)|
                        (std::uint32_t(r.byte(shading+6))<<8)|r.byte(shading+7);
                }
            }
            packet=p.build(o.e,o.extra);
            if(!packet){detail::note({p.scene,o.e.parameters[1],o.e.token,0,0,1,true,"invalid prepared model; original retained"});continue;}
            auto& triangles=o.extra?p.submitted_triangles:p.racer_triangles[o.e.token];
            const auto limit=o.extra?32768U:detail::ViewTriangles;
            if(packet->triangles>limit-triangles){detail::note({p.scene,o.e.parameters[1],o.e.token,0,0,1,true,"view triangle capacity; original retained"});continue;}
            if(const auto* cars=vehicles::active_render_pass();!o.extra&&cars)
                driver_mask=cars->detail_profile(o.e.parameters[1],o.e.parameters[2]);
            if(driver_mask)for(const auto& step:packet->steps)
                if(step.kind==Step::Triangle&&step.source_triangle>=driver_mask->hide.size())
                    throw std::runtime_error("high-detail driver mask exceeds its own model");
        }catch(const std::exception& error){detail::note({p.scene,o.e.parameters[1],o.e.token,0,0,1,true,error.what()});continue;}
        // All spans, uploads and references have been checked before the first
        // GPU draw. No callback, allocator or guest interpreter runs below.
        NativeDrawScope restore(state);
        if(o.extra) {
            rsp.modelMatrixStack[1]=transform;
            state.rdp->setPrimColor(0,0,primitive);
            state.rdp->setEnvColor(environment);
        }
        if(o.e.parameters[7])rsp.modelMatrixStack[2]=hlslpp::mul(head_matrix(packet->model,std::int16_t(o.e.parameters[8])),rsp.modelMatrixStack[1]);
        select(rsp,presentation::normalise_identity(p.scene^o.e.parameters[1]^presentation::mix_identity(o.e.parameters[2])^(o.e.parameters[7]<<28)^0x4D4F444CU^(o.e.token*0x9E3779B9U)));
        #include "detail_batch.inl"
        state.flush();o.replaced=true;++p.draws;
        if(!o.extra)p.suppress(o.e.address-0x80000000U,o.e.parameters[0]-0x80000000U);
        if(o.extra)p.submitted_triangles+=packet->triangles;else p.racer_triangles[o.e.token]+=packet->triangles;
        if(!o.extra)detail::note({p.scene,o.e.parameters[1],o.e.token,Reader{p.ram}.byte(o.e.parameters[1]+0x3a),0,1,true,"highest resident presentation mesh"});
        if(!o.extra&&p.logged++<16)std::fprintf(stderr,"[graphics][local-detail] highest-loaded driver instance=%08X object=%08X car-mask=%u triangles=%u; independent-driver-detail-v1\n",
            o.e.parameters[2],o.e.parameters[1],unsigned(driver_mask!=nullptr),packet->triangles);
    }
}
}
