#pragma once
#include "polish_symbols.hpp"
#include "enhanced_water_registry.hpp"
#include "hle/rt64_rsp.h"
#include "revision_addresses.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dkr::runtime::presentation {
// Extend only the lower half of the actual intro skydome, in decoder-local
// vertices. Its equator/horizon, upper sky, logos and world projection stay put.
class IntroSky {
    std::uint32_t begin_=0,end_=0;
    float lower_scale_=1,bottom_=0,radius_=0;
public:
    void begin(const RT64::State& state,bool enabled) {
        begin_=end_=0;
        water::enhanced::Memory m(std::span<const std::uint8_t>(state.RDRAM,0x800000));
        if(!enabled || m.u32(revision_addresses::CurrentMenuId)!=1)return;
        const auto& symbols=polish_symbols[revision_addresses::CurrentMenuId==revision_addresses::kUsV80.CurrentMenuId?1:0];
        const auto obj=m.u32(symbols[6]);
        if(!obj || !m.valid(obj,0x6C))return;
        const unsigned index=m.u8(obj+0x3A);
        const auto instances=m.u32(obj+0x68);
        if(index>=8 || !instances || !m.valid(instances,(index+1)*4))return;
        const auto instance=m.u32(instances+index*4);
        if(!instance || !m.valid(instance,0x24))return;
        const auto model=m.u32(instance);
        if(!model || !m.valid(model,0x80))return;
        const auto count=m.u16(model+0x24);
        auto verts=m.u32(obj+0x44);
        if(!verts)verts=m.u32(model+4);
        if(!count || count>4096 || !verts || !m.valid(verts,std::uint64_t(count)*10))return;
        bottom_=32767;radius_=0;
        for(unsigned i=0;i<count;++i) {
            const auto p=verts+i*10;
            const float x=m.s16(p),z=m.s16(p+4);
            bottom_=std::min(bottom_,float(m.s16(p+2)));
            radius_=std::max(radius_,std::sqrt(x*x+z*z));
        }
        if(radius_<1)return;
        const float fov=m.f32(revision_addresses::CurrentCameraFov);
        if(!std::isfinite(fov) || fov<20 || fov>140)return;
        // tan(FOV/2) describes the exposed vertical cone. The original inset
        // hid the bottom ring; use a conservative bounded guard beyond it.
        lower_scale_=std::clamp(2.F*std::tan(fov*0.00872664626F)/std::tan(30.F*0.01745329252F),2.F,8.F);
        begin_=verts&0x1FFFFFFFU;end_=begin_+count*10;
    }
    void vertex(std::uint32_t address,RT64::RSP::Vertex& v) const {
        address&=0x1FFFFFFFU;
        if(!begin_ || address<begin_ || address>=end_ || (address-begin_)%10)return;
        float y=v.y;
        if(y<0)y*=lower_scale_;
        else if(bottom_>=0 && y<=bottom_+1.F)y=bottom_-radius_*lower_scale_;
        else return;
        v.y=std::int16_t(std::clamp(std::lround(y),-32768L,32767L));
        static thread_local bool notice=false;
        if(!notice){std::fprintf(stderr,"[intro][sky] FOV-aware lower-dome coverage; equator and world projection preserved\n");notice=true;}
    }
};
}
