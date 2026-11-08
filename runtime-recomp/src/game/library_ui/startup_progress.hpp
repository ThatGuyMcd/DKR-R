#pragma once
#include <algorithm>
#include <cstddef>

namespace dkr::library_ui {
// One measured batch at a time. A zero total is explicitly indeterminate;
// elapsed time never fabricates a completion percentage.
struct StartupProgress {
    std::size_t completed=0,total=0;
    float fraction()const {
        return total?float(std::min(completed,total))/float(total):0.F;
    }
    bool measured()const{return total!=0;}
};
struct StartupLayout {
    float logo=0,panel=0,top=0,caption=0,gap=0,scale=1;
};
inline StartupLayout startup_layout(float width,float height,float caption_height) {
    width=std::max(1.F,width);height=std::max(1.F,height);
    const float scale=std::clamp(std::min(width/1008.F,height/640.F),.65F,2.5F);
    const float gap=20.F*scale;
    const float panel=std::max(1.F,std::min(390.F*scale,width-32.F));
    const float caption=std::max(0.F,caption_height);
    const float logo=std::max(1.F,std::min({320.F*scale,width-40.F,height-caption-gap-32.F}));
    return {logo,panel,std::max(8.F,(height-logo-caption-gap)*.5F),caption,gap,scale};
}
inline bool inline_preparation(bool automatic,bool busy,bool failed) {
    return automatic&&busy&&!failed;
}
}
