#pragma once
#include "../atomic_snapshot.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dkr::display {
// Persist these identities, never a dropdown row index.
enum class Aspect { Window, Original, Standard, ClassicWide, Wide, Deck, Ultra64, Ultra43, SuperWide };
struct Ratio { int numerator,denominator;const char* name; };
inline constexpr std::array<Ratio,9> Ratios{{{0,0,"Match window"},{4,3,"Original 4:3"},
    {5,4,"Standard 5:4"},{3,2,"Widescreen 3:2"},{16,9,"Widescreen 16:9"},{16,10,"Widescreen 16:10"},
    {64,27,"Ultrawide 2560:1080"},{43,18,"Ultrawide 3440:1440"},{32,9,"Super ultrawide 32:9"}}};
struct Settings {
    Aspect aspect=Aspect::Window;
    int height=0; // Zero matches the content area; -1 preserves old integer scaling.
    int legacy_downsample=1;
    bool operator==(const Settings&)const=default;
};
inline Settings preference;
inline bool loaded=false;
inline dkr::runtime::AtomicSnapshot<const Settings> published{std::make_shared<const Settings>()};
inline std::atomic<unsigned> output_width{1280},output_height{720};
inline constexpr unsigned DimensionLimit=8192;
inline std::atomic<unsigned> device_limit{DimensionLimit};
inline Settings normalise(Settings s) {
    if(unsigned(s.aspect)>=Ratios.size())s.aspect=Aspect::Window;
    if(s.height < -1 || s.height>4320 || (s.height>0&&s.height<240))s.height=0;
    s.legacy_downsample=std::clamp(s.legacy_downsample,1,8);return s;
}
inline void publish(){preference=normalise(preference);published.store(std::make_shared<const Settings>(preference));}
inline bool load(std::string_view key,int value) {
    if(key=="display_aspect")preference.aspect=Aspect(value);
    else if(key=="display_height")preference.height=value;
    else if(key=="display_legacy_downsample")preference.legacy_downsample=value;
    else return false;
    loaded=true;publish();return true;
}
template<class Stream>void save(Stream& out) {
    out<<"display_aspect="<<int(preference.aspect)<<'\n'<<"display_height="<<preference.height<<'\n'
       <<"display_legacy_downsample="<<preference.legacy_downsample<<'\n';
}
inline void migrate(int resolution,int aspect,int downsample) {
    if(loaded)return;
    preference={aspect==0?Aspect::Original:Aspect::Window,resolution==2?-1:(resolution==1?480:240)*std::clamp(downsample,1,8),std::clamp(downsample,1,8)};
    loaded=true;publish();
}
inline bool legacy_matches(int resolution,int aspect,int downsample) {
    const int old_height=resolution==2?-1:(resolution==1?480:240)*std::clamp(downsample,1,8);
    return preference.aspect==(aspect==0?Aspect::Original:Aspect::Window)&&
        preference.height==old_height&&preference.legacy_downsample==std::clamp(downsample,1,8);
}
inline double ratio(const Settings& s,unsigned width,unsigned height) {
    const auto r=Ratios[unsigned(normalise(s).aspect)];
    return r.numerator?double(r.numerator)/r.denominator:width&&height?double(width)/height:4./3.;
}
inline float view_aspect(unsigned width,unsigned height) {return float(ratio(*published.load(),width,height));}
struct Size { unsigned width=0,height=0;bool operator==(const Size&)const=default; };
struct Resolved { Size render,output;double aspect=4./3.,scale=1;int downsample=1;bool integer=false; };
inline Resolved resolve(Settings s,unsigned width,unsigned height) {
    s=normalise(s);width=std::max(1U,width);height=std::max(1U,height);
    const auto a=ratio(s,width,height);
    const unsigned fitted=std::max(1U,unsigned(std::min(double(height),double(width)/a)));
    unsigned h=s.height>0?unsigned(s.height):s.height==-1?std::max(1U,(height+239)/240)*240:fitted;
    // Bound allocations by the admitted render target size, independently of
    // the monitor's modes. A fixed 4K choice remains 4K on a 1080p output.
    const auto limit=std::clamp(device_limit.load(),240U,DimensionLimit);
    h=std::max(1U,std::min(h,unsigned(std::min(double(limit),limit/a))));
    const unsigned w=std::max(1U,unsigned(std::lround(h*a)));
    return {{w,h},{width,height},a,double(h)/240.,s.legacy_downsample,s.height==-1};
}
inline std::string pixels(Size s){return std::to_string(s.width)+" x "+std::to_string(s.height);}
struct Choice { int height;Size size; };
inline std::vector<Choice> choices(Settings s,unsigned w,unsigned h) {
    std::vector<int> heights{240,360,480,540,600,640,720,768,800,900,960,1024,1080,1200,1280,1440,1600,1620,1800,1920,2048,2160,2400,2880,4320};
    if(s.height>0)heights.push_back(s.height);
    std::sort(heights.begin(),heights.end());heights.erase(std::unique(heights.begin(),heights.end()),heights.end());
    std::vector<Choice> result;
    for(int height:heights) {
        auto fixed=s;fixed.height=height;const auto size=resolve(fixed,w,h).render;
        if(size.height==unsigned(height)&&size.width<=DimensionLimit)result.push_back({height,size});
    }
    return result;
}
}
