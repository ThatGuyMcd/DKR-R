#pragma once
// Authored presentation sidecar. The Patch Pipeline includes this only in
// isolated car/RT64 source copies; no checked-out dependency is modified.
#include "common/rt64_hlslpp.h"
#include "hle/rt64_workload.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace dkr::runtime::vehicles::wheel_interpolation {
// Keep the shared renderer header compatible with RT64's C++17 target.
inline constexpr double Tau=6.283185307179586476925286766559;
inline constexpr double Turn=4294967296.0;
inline constexpr std::size_t MaxSamples=8192;
struct Sample {
    hlslpp::float4x4 base=hlslpp::float4x4::identity();
    std::array<float,3> pivot{};
    std::uint64_t phase=0;
    std::uint32_t transform=0,identity=0,scene=0,epoch=0,object=0;
    unsigned axle=0;
    float yaw=0,steering=0;
    bool front=false,allowed=false;
};
struct Capture {
    // Control-block identity prevents resource-address reuse without pinning
    // an entire retired mod catalogue just to retain a few wheel matrices.
    std::weak_ptr<const void> owner;
    std::uint64_t token=0;
    std::vector<Sample> samples;
};
struct Slot {
    const RT64::Workload* workload=nullptr;
    std::uint64_t serial=0;
    std::shared_ptr<Capture> pending;
    std::shared_ptr<const Capture> sealed;
};
struct Registry {
    std::mutex mutex;
    // RT64 has four workload slots. Bound this sidecar even across renderer
    // recreation; an evicted record simply uses the existing snap/fallback.
    std::array<Slot,16> slots;
    std::uint64_t serial=0;
};
inline Registry registry;
inline std::atomic<unsigned> diagnostic_count{0};
inline bool finite(const hlslpp::float4x4& matrix) {
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
        if(!std::isfinite(float(matrix[r][c])))return false;
    return true;
}
inline hlslpp::float4x4 translation(float x,float y,float z) {
    auto matrix=hlslpp::float4x4::identity();matrix[3]=hlslpp::float4(x,y,z,1);return matrix;
}
inline hlslpp::float4x4 rotation(unsigned axle,float angle) {
    auto matrix=hlslpp::float4x4::identity();const float c=std::cos(angle),s=std::sin(angle);
    if(axle==0){matrix[1]=hlslpp::float4(0,c,s,0);matrix[2]=hlslpp::float4(0,-s,c,0);}
    else if(axle==1){matrix[0]=hlslpp::float4(c,0,-s,0);matrix[2]=hlslpp::float4(s,0,c,0);}
    else {matrix[0]=hlslpp::float4(c,s,0,0);matrix[1]=hlslpp::float4(-s,c,0,0);}
    return matrix;
}
inline float phase_angle(std::uint64_t phase) {
    return float(double(std::uint32_t(phase))*(Tau/Turn));
}
inline std::int64_t phase_travel(std::uint64_t previous,std::uint64_t current) {
    const auto bits=current-previous;std::int64_t value;
    std::memcpy(&value,&bits,sizeof(value));return value;
}
inline hlslpp::float4x4 local(const Sample& sample,float spin,float steering) {
    // Static vertices already contain the authored yaw. Undo it before the
    // pivot-local rotation, then restore it. No camera/projection decomposition.
    auto matrix=hlslpp::mul(rotation(1,-sample.yaw),translation(-sample.pivot[0],-sample.pivot[1],-sample.pivot[2]));
    matrix=hlslpp::mul(matrix,rotation(sample.axle,spin));
    if(sample.front)matrix=hlslpp::mul(matrix,rotation(1,steering));
    matrix=hlslpp::mul(matrix,translation(sample.pivot[0],sample.pivot[1],sample.pivot[2]));
    return hlslpp::mul(matrix,rotation(1,sample.yaw));
}
inline Slot& slot_for(const RT64::Workload& workload) {
    for(auto& slot:registry.slots)if(slot.workload==&workload)return slot;
    auto& slot=*std::min_element(registry.slots.begin(),registry.slots.end(),
        [](const Slot& a,const Slot& b){return a.serial<b.serial;});
    slot={};slot.workload=&workload;return slot;
}
inline Capture& writable(Slot& slot,std::uint64_t token,std::shared_ptr<const void> owner) {
    slot.serial=++registry.serial;
    if(!slot.pending||slot.pending->token!=token||slot.pending->owner.owner_before(owner)||owner.owner_before(slot.pending->owner)) {
        slot.pending=std::make_shared<Capture>();slot.pending->token=token;slot.pending->owner=std::move(owner);
        slot.pending->samples.reserve(128);
    }
    return *slot.pending;
}
inline void begin(const RT64::Workload& workload,std::uint64_t token,std::shared_ptr<const void> owner) {
    std::lock_guard lock(registry.mutex);
    // A task may append to an existing workload. Preserve earlier transforms,
    // but discard anything belonging to a recycled/reset draw-data range.
    const auto first=workload.drawData.worldTransforms.size();
    for(auto& slot:registry.slots)if(slot.workload==&workload) {
        if(!slot.pending)return;
        if(slot.pending->token!=token||slot.pending->owner.owner_before(owner)||owner.owner_before(slot.pending->owner)) {slot.pending.reset();return;}
        auto& samples=slot.pending->samples;
        auto at=std::lower_bound(samples.begin(),samples.end(),first,
            [](const Sample& sample,std::size_t index){return sample.transform<index;});
        samples.erase(at,samples.end());return;
    }
}
inline void record(const RT64::Workload& workload,std::uint64_t token,
        std::shared_ptr<const void> owner,Sample sample) {
    if(sample.transform>=workload.drawData.worldTransforms.size()||sample.axle>2||!finite(sample.base))return;
    std::lock_guard lock(registry.mutex);auto& capture=writable(slot_for(workload),token,std::move(owner));
    if(!capture.samples.empty()&&capture.samples.back().transform>=sample.transform)return;
    if(capture.samples.size()<MaxSamples)capture.samples.push_back(std::move(sample));
}
inline void seal(const RT64::Workload& workload,std::uint64_t token) {
    std::lock_guard lock(registry.mutex);
    for(auto& slot:registry.slots)if(slot.workload==&workload) {
        slot.sealed=slot.pending&&slot.pending->token==token?std::move(slot.pending):nullptr;
        // Only the decoder had a mutable reference. Published samples are now
        // immutable and remain alive until every render-frame reader is done.
        slot.pending.reset();return;
    }
}
inline std::shared_ptr<const Capture> capture(const RT64::Workload& workload) {
    std::lock_guard lock(registry.mutex);
    for(const auto& slot:registry.slots)if(slot.workload==&workload&&slot.sealed&&slot.sealed->token==workload.workloadId)
        return slot.sealed;
    return {};
}
inline const Sample* find(const Capture& capture,std::size_t index) {
    const auto at=std::lower_bound(capture.samples.begin(),capture.samples.end(),index,
        [](const Sample& sample,std::size_t value){return sample.transform<value;});
    return at!=capture.samples.end()&&at->transform==index?&*at:nullptr;
}
inline hlslpp::float4x4 at_weight(const Sample& previous,const Sample& current,float weight,
        const hlslpp::float4x4& previous_exact,const hlslpp::float4x4& current_exact) {
    if(weight<=0)return previous_exact;if(weight>=1)return current_exact;
    // Unsigned modular storage makes checkpoint addition defined. Recover the
    // signed accumulated travel, NOT the shortest arc between wrapped angles.
    const auto travel=phase_travel(previous.phase,current.phase);
    const double phase=double(std::uint32_t(previous.phase))+double(travel)*double(weight);
    const float spin=float(std::remainder(phase,Turn)*(Tau/Turn));
    const float steering=previous.steering+float(std::remainder(double(current.steering-previous.steering),Tau))*weight;
    auto base=current.base;
    for(unsigned row=0;row<4;++row)base[row]=previous.base[row]+(current.base[row]-previous.base[row])*weight;
    return hlslpp::mul(local(current,spin,steering),base);
}
inline bool interpolate(const std::shared_ptr<const Capture>& previous,const std::shared_ptr<const Capture>& current,
        std::size_t previous_index,std::size_t current_index,float previous_weight,float current_weight,
        const hlslpp::float4x4& previous_exact,const hlslpp::float4x4& current_exact,
        hlslpp::float4x4& previous_matrix,hlslpp::float4x4& current_matrix) {
    if(!previous||!current||previous->owner.owner_before(current->owner)||current->owner.owner_before(previous->owner))return false;
    const auto* a=find(*previous,previous_index);const auto* b=find(*current,current_index);
    if(!a||!b||!a->allowed||!b->allowed||a->identity!=b->identity||a->scene!=b->scene||a->epoch!=b->epoch||
       a->object!=b->object||a->axle!=b->axle||a->front!=b->front||a->pivot!=b->pivot||a->yaw!=b->yaw||
       !std::isfinite(previous_weight)||!std::isfinite(current_weight))return false;
    const auto pm=at_weight(*a,*b,previous_weight,previous_exact,current_exact);
    const auto cm=at_weight(*a,*b,current_weight,previous_exact,current_exact);
    if(!finite(pm)||!finite(cm))return false;
    previous_matrix=pm;current_matrix=cm;
    if(current_weight>0&&current_weight<1&&diagnostic_count.fetch_add(1,std::memory_order_relaxed)<4)
        std::fprintf(stderr,"[custom-vehicles][wheel-interpolation] arc-phase64 weight=%.4f travel-turns=%.5f; static mesh, no extra render pass\n",
            current_weight,double(phase_travel(a->phase,b->phase))/Turn);
    return true;
}
} // namespace dkr::runtime::vehicles::wheel_interpolation
