#include "camera_scene.hpp"
#include "camera_obstruction_math.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dkr::runtime::camera {
namespace {
struct Reader {
    std::span<const std::uint8_t> ram;
    std::uint32_t offset(std::uint32_t address,std::size_t count,unsigned alignment)const {
        if(address<0x80000000U||address>=0x80800000U||(address&(alignment-1)))throw mods::Error("Invalid retained camera-scene address.");
        auto p=address-0x80000000U;
        if(p>ram.size()||count>ram.size()-p||count>0x800000U-p)throw mods::Error("Retained camera-scene span exceeds RDRAM.");
        return p;
    }
    std::uint32_t word(std::uint32_t a)const{std::uint32_t v;auto p=offset(a,4,4);std::memcpy(&v,ram.data()+p,4);return v;}
    unsigned half(std::uint32_t a)const{std::uint16_t v;auto p=offset(a,2,2);std::memcpy(&v,ram.data()+(p^2U),2);return v;}
    unsigned byte(std::uint32_t a)const{return ram[offset(a,1,1)^3U];}
    std::array<float,3> vertex(std::uint32_t a)const {
        return {float(std::int16_t(half(a))),float(std::int16_t(half(a+2))),float(std::int16_t(half(a+4)))};
    }
};
dkr_cam_vec vec(const float* p){return dkr_cam_v(p[0],p[1],p[2]);}
bool finite(const float* p){return std::all_of(p,p+3,[](float x){return std::isfinite(x)&&std::abs(x)<=1.e7F;});}
}
std::shared_ptr<const CollisionScene> CollisionScene::capture(std::span<const std::uint8_t> snapshot,
        std::uint32_t model,std::uint64_t generation) {
    if(snapshot.size()<0x800000U||snapshot.size()%4)throw mods::Error("Camera scene requires an immutable complete retail image.");
    Reader r{snapshot};r.offset(model,0x40,4);
    const auto count=r.half(model+0x1A),textures=r.half(model+0x18),segments=r.word(model+4),table=r.word(model);
    if(!count||count>512||textures>256)throw mods::Error("Camera-scene model exceeds its static capacity.");
    r.offset(segments,count*0x44,4);if(textures)r.offset(table,textures*8,4);
    auto result=std::shared_ptr<CollisionScene>(new CollisionScene);result->generation_=generation;
    for(unsigned segment=0;segment<count;++segment) {
        const auto s=segments+segment*0x44;
        const auto vertices=r.word(s),faces=r.word(s+4),batches=r.word(s+12);
        const auto vc=r.half(s+0x1C),fc=r.half(s+0x1E),bc=r.half(s+0x20);
        if(vc>32767||fc>32767||bc>4096)throw mods::Error("Camera-scene segment exceeds its bounds.");
        r.offset(vertices,vc*10,2);r.offset(faces,fc*16,4);r.offset(batches,(bc+1)*12,4);
        for(unsigned batch=0;batch<bc;++batch) {
            const auto b=batches+batch*12,texture=r.byte(b),base=r.half(b+2),first=r.half(b+4),end=r.half(b+16);
            if(first>end||end>fc||base>vc)throw mods::Error("Invalid camera-scene batch range.");
            if(texture!=255&&texture>=textures)throw mods::Error("Invalid camera-scene surface.");
            const auto surface=texture==255?255:r.byte(table+texture*8+7);
            if(!dkr_cam_solid_batch(r.word(b+8),int(surface)))continue;
            Chunk chunk{{1.e9F,1.e9F,1.e9F},{-1.e9F,-1.e9F,-1.e9F},std::uint32_t(result->triangles_.size()),0};
            for(unsigned face=first;face<end;++face) {
                const auto f=faces+face*16;if(r.byte(f)&0x80U)continue;
                if(result->triangles_.size()>=131072)throw mods::Error("Camera-scene memory budget exceeded.");
                Triangle triangle;std::array<float,3>* corner[]{&triangle.a,&triangle.b,&triangle.c};
                for(unsigned i=0;i<3;++i) {
                    const auto index=base+r.byte(f+i+1);if(index>=vc)throw mods::Error("Invalid camera-scene triangle vertex.");
                    *corner[i]=r.vertex(vertices+index*10);
                    for(unsigned axis=0;axis<3;++axis) {
                        chunk.lo[axis]=std::min(chunk.lo[axis],(*corner[i])[axis]);
                        chunk.hi[axis]=std::max(chunk.hi[axis],(*corner[i])[axis]);
                    }
                }
                result->triangles_.push_back(triangle);++chunk.count;
            }
            if(chunk.count) {
                if(result->chunks_.size()>=16384)throw mods::Error("Camera-scene batch budget exceeded.");
                result->chunks_.push_back(chunk);
            }
        }
    }
    return result;
}
std::size_t CollisionScene::bytes()const{return triangles_.size()*sizeof(Triangle)+chunks_.size()*sizeof(Chunk);}
DkrViewHit CollisionScene::query(const DkrViewQuery& q,unsigned budget)const {
    DkrViewHit out{};out.api=1;out.size=sizeof(out);out.status=DKR_VIEW_QUERY_UNSUPPORTED;
    if(q.api!=1||q.size!=sizeof(q)||q.include_dynamic||!finite(q.origin)||!finite(q.target)||
       !std::isfinite(q.radius)||q.radius<0||q.radius>128||q.reserved[0]||q.reserved[1])return out;
    out.status=DKR_VIEW_QUERY_NONE;out.fraction=1;
    std::copy_n(q.target,3,out.position);
    std::array<float,3> lo,hi;
    for(unsigned i=0;i<3;++i){lo[i]=std::min(q.origin[i],q.target[i])-q.radius;hi[i]=std::max(q.origin[i],q.target[i])+q.radius;}
    double best=2;const Triangle* selected=nullptr;
    for(const auto& chunk:chunks_) {
        bool overlap=true;for(unsigned i=0;i<3;++i)overlap&=lo[i]<=chunk.hi[i]&&hi[i]>=chunk.lo[i];
        if(!overlap)continue;
        for(unsigned i=0;i<chunk.count;++i) {
            if(!budget--) {out.status=DKR_VIEW_QUERY_BUDGET;out.fraction=1;return out;}
            const auto& triangle=triangles_[chunk.first+i];
            auto hit=dkr_cam_triangle_hit(vec(q.origin),vec(q.target),vec(triangle.a.data()),vec(triangle.b.data()),vec(triangle.c.data()),q.radius);
            if(hit<best){best=hit;selected=&triangle;}
        }
    }
    if(selected&&best<=1) {
        out.status=DKR_VIEW_QUERY_HIT;out.fraction=float(best);
        for(unsigned i=0;i<3;++i)out.position[i]=q.origin[i]+(q.target[i]-q.origin[i])*out.fraction;
        auto normal=dkr_cam_cross(dkr_cam_sub(vec(selected->b.data()),vec(selected->a.data())),dkr_cam_sub(vec(selected->c.data()),vec(selected->a.data())));
        const auto length=std::sqrt(dkr_cam_dot(normal,normal));
        if(length>1.e-8) {
            normal=dkr_cam_scale(normal,1/length);
            if(dkr_cam_dot(normal,dkr_cam_sub(vec(q.origin),vec(selected->a.data())))<0)normal=dkr_cam_scale(normal,-1);
            out.normal[0]=float(normal.x);out.normal[1]=float(normal.y);out.normal[2]=float(normal.z);
        }
    }
    return out;
}
}
