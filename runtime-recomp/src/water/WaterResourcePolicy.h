#pragma once
#include <algorithm>
#include <cstdint>

namespace RT64::water_policy {
constexpr uint64_t sceneBudget=256ULL*1024*1024;
constexpr uint32_t maxScenes=16;
constexpr uint32_t invalidScene=UINT32_MAX;
// Payload accounting matches the allocated colour, depth pyramid, mask,
// optional original-colour atlas and isolated depth. No resolution reduction.
inline uint64_t sceneBytes(uint32_t width,uint32_t height,uint32_t samples,
    bool classic,bool selfDepth,uint32_t depthWidth,uint32_t depthHeight,uint32_t depthSamples) {
    if(!width || !height || width>8192 || height>8192 ||
        !samples || samples>8 || (selfDepth && (!depthWidth || !depthHeight ||
        depthWidth>8192 || depthHeight>8192 || !depthSamples || depthSamples>8))) return UINT64_MAX;
    uint64_t bytes=uint64_t(width)*height*(8+4*samples+(classic ? 8*samples : 0));
    for(uint32_t mip=0;mip<6;mip++) {
        bytes+=uint64_t(width)*height*4;
        if(std::min(width,height)<2)break;
        width=std::max(width/2,1u);height=std::max(height/2,1u);
    }
    if(selfDepth)bytes+=uint64_t(depthWidth)*depthHeight*4*depthSamples;
    return bytes;
}
inline bool fits(uint64_t active,uint64_t requested,uint32_t count) {
    return count<maxScenes && active<=sceneBudget && requested<=sceneBudget-active;
}
// Shared optical capture + mask are counted once per framebuffer, not once
// per legacy Z-write mode. Vertical water samples the authored texture directly
// in the retail-compatible shader: no framebuffer/sample atlas is allocated.
inline uint64_t scratchBytes(uint32_t width,uint32_t height,uint32_t samples,
    bool classic,bool shared,bool selfDepth,uint32_t depthWidth,uint32_t depthHeight,
    uint32_t depthSamples,uint32_t originalWidth=0,uint32_t originalHeight=0) {
    const auto checked=sceneBytes(width,height,samples,false,selfDepth,depthWidth,depthHeight,depthSamples);
    if(checked==UINT64_MAX)return checked;
    if(classic)return 12;
    return shared ? (selfDepth ? uint64_t(depthWidth)*depthHeight*4*depthSamples : 0) : checked;
}
inline uint32_t bucketExtent(uint32_t extent) {
    return extent && extent<=8192 ? std::min(8192u,((extent+63)/64)*64) : 0;
}
inline uint32_t retainedScenes(uint32_t pooled,uint32_t used) {return std::min(pooled,used);}
// Allocation/field failures revert only the current workload, then retry after
// a bounded cooldown. Budget rejection itself is cheap and needs no cooldown.
struct Recovery {
    uint32_t remaining=0;
    bool failed=false;
    void beginFrame() {failed=false;if(remaining) --remaining;}
    bool ready() const {return !failed && !remaining;}
    void fail() {failed=true;remaining=120;}
};
}
