#pragma once
#include "waterfall_scroll_policy.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include "reusable_flow_map.hpp"
#include "performance_switches.hpp"

namespace dkr::runtime::water {
using FlowCorners=std::array<std::array<float,2>,3>;
struct FlowMotion {FlowCorners velocity{};bool matched=false,held=false,image_changed=false;};

// Only reviewed static triangle addresses enter this renderer-owned history.
// UV phase is world-surface state: changing viewport/camera/terrain ordering
// must not pause it. RT64 already uploads and interpolates tcVelFloats; use
// that existing channel without changing any shader or dependency source.
template<template<class,class>class Table> class BasicFlowHistory {
    struct Sample {FlowCorners uv;std::array<float,2> period;FlowMotion motion;std::uint32_t image;};
    Table<std::uint32_t,Sample> previous_,current_;
    double time_=-1;
    std::uint32_t scene_=~0U,model_=0;
    std::uint64_t material_generation_=0;
public:
    void begin(std::uint32_t scene,std::uint32_t model,double time,bool allowed,std::uint64_t material_generation=0) {
        if(!allowed || !std::isfinite(time) || time<0 || scene!=scene_ || model!=model_ ||
           time<time_ || time-time_>0.25 || material_generation!=material_generation_) {
            previous_.clear();current_.clear();time_=-1;
        }
        scene_=scene;model_=model;material_generation_=material_generation;
        if(!allowed)return;
        if(time!=time_) {
            previous_.swap(current_);current_.clear();time_=time;
        }
    }
    FlowMotion observe(std::uint32_t source,FlowCorners uv,std::array<float,2> period,std::uint32_t image) {
        if(time_<0 || !valid_waterfall_period(period[0]) || !valid_waterfall_period(period[1]))return {};
        for(auto& corner:uv)for(float value:corner)if(!std::isfinite(value))return {};
        if(auto it=current_.find(source);it!=current_.end()) {
            // Repeated views of one immutable triangle share this sample.
            // Conflicting data at one source is not a safe interpolation key.
            return it->second.uv==uv && it->second.period==period && it->second.image==image ? it->second.motion : FlowMotion{};
        }
        if(current_.size()>=16384)return {};
        FlowMotion motion{};
        if(auto it=previous_.find(source);it!=previous_.end() && it->second.period==period) {
            motion.matched=true;motion.held=true;motion.image_changed=it->second.image!=image;
            for(unsigned corner=0;corner<3;++corner)for(unsigned axis=0;axis<2;++axis) {
                const float velocity=waterfall_uv_velocity(it->second.uv[corner][axis],uv[corner][axis],period[axis]);
                motion.velocity[corner][axis]=velocity;
                if(std::abs(velocity)>0.000001F)motion.held=false;
            }
        }
        current_.emplace(source,Sample{uv,period,motion,image});
        return motion;
    }
};
template<class K,class V>using LegacyFlowMap=std::unordered_map<K,V>;
class FlowHistory {
    BasicFlowHistory<LegacyFlowMap> legacy_;
    BasicFlowHistory<dkr::runtime::surgical::ReusableFlowMap> reusable_;
public:
    void begin(std::uint32_t scene,std::uint32_t model,double time,bool allowed,std::uint64_t generation=0) {
        if(surgical::flow_reuse())reusable_.begin(scene,model,time,allowed,generation);
        else legacy_.begin(scene,model,time,allowed,generation);
    }
    FlowMotion observe(std::uint32_t source,FlowCorners uv,std::array<float,2> period,std::uint32_t image) {
        return surgical::flow_reuse()?reusable_.observe(source,uv,period,image):legacy_.observe(source,uv,period,image);
    }
};
inline FlowHistory& flow_history() {static thread_local FlowHistory history;return history;}
}
