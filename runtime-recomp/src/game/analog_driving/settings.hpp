#pragma once
#include "input.hpp"
#include <ostream>
#include <string>

namespace dkr::runtime::driving {
struct Channel {int release=3,full=100,curve=100;bool inverted=false;};
struct Profile {bool enabled=false;Channel accelerator{},brake{};};
// UI and device polling share the existing platform/UI owner; atomic fields
// keep the persisted calibration safe for asynchronous input sampling.
struct AtomicProfile {
    std::atomic<int> enabled{0};
    std::array<std::atomic<int>,8> values{{3,100,100,0,3,100,100,0}};
};
inline std::array<AtomicProfile,4> profiles;
inline Profile profile(unsigned p) {
    p=std::min(p,3U);auto& a=profiles[p];Profile v;v.enabled=a.enabled.load()!=0;
    v.accelerator={a.values[0].load(),a.values[1].load(),a.values[2].load(),a.values[3].load()!=0};
    v.brake={a.values[4].load(),a.values[5].load(),a.values[6].load(),a.values[7].load()!=0};return v;
}
inline void configure(unsigned p,const Profile& v) {
    if(p>=4)return;auto& a=profiles[p];a.enabled=v.enabled;
    unsigned i=0;for(const auto& c:{v.accelerator,v.brake}) {
        a.values[i++]=std::clamp(c.release,0,20);
        a.values[i++]=std::clamp(c.full,50,100);
        a.values[i++]=std::clamp(c.curve,50,250);a.values[i++]=c.inverted;
    }
}
inline float calibrate(float raw,const Channel& c) {
    if(!std::isfinite(raw))return 0;
    raw=std::clamp(raw,0.F,1.F);if(c.inverted)raw=1-raw;
    const float low=float(c.release)*.01F,high=float(c.full)*.01F;
    return std::pow(std::clamp((raw-low)/std::max(high-low,.01F),0.F,1.F),float(c.curve)*.01F);
}
inline bool load(const std::string& key,int value) {
    for(unsigned p=0;p<4;++p) {
        const std::string prefix="player"+std::to_string(p+1)+"_analogue_";
        if(!key.starts_with(prefix))continue;
        const auto field=key.substr(prefix.size());auto v=profile(p);
        if(field=="enabled")v.enabled=value!=0;
        else {
            const bool brake=field.starts_with("brake_");
            if(!brake&&!field.starts_with("accelerator_"))return false;
            auto& c=brake?v.brake:v.accelerator;const auto part=field.substr(brake?6:12);
            if(part=="release")c.release=value;else if(part=="full")c.full=value;
            else if(part=="curve")c.curve=value;else if(part=="inverted")c.inverted=value!=0;
            else return false;
        }
        configure(p,v);return true;
    }return false;
}
inline void save(std::ostream& out) {
    for(unsigned p=0;p<4;++p) {
        const auto v=profile(p);const auto prefix="player"+std::to_string(p+1)+"_analogue_";
        out<<prefix<<"enabled="<<v.enabled<<'\n';unsigned i=0;
        for(const auto& c:{v.accelerator,v.brake}) {
            const auto key=prefix+(i++?"brake_":"accelerator_");
            out<<key<<"release="<<c.release<<'\n'<<key<<"full="<<c.full<<'\n'
                <<key<<"curve="<<c.curve<<'\n'<<key<<"inverted="<<c.inverted<<'\n';
        }
    }
}
inline std::array<std::atomic<std::uint32_t>,4> preview;
inline bool held(float value,bool prior) {return value>(prior?.005F:.02F);}
}
