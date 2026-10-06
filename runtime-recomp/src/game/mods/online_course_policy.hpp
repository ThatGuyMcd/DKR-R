#pragma once
#include "../custom_tracks.hpp"
#include "legacy_mod_format.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace dkr::mods::online {
// A frozen course set uses one boot-sized display-list contract for its whole
// lifetime. Menu previews do not own a safe allocator boundary at which to
// enlarge their lists. Size all viewports once, never below retail, and keep
// the original constructor/allocator responsible for the actual allocation.
inline std::array<std::uint32_t,4> course_display_budgets(
    const dkr::runtime::custom_tracks::PreparedTracks* courses) {
    std::array<std::uint32_t,4> out{4500,7000,11000,11000};
    if(!courses)return out;
    if(courses->courses.size()!=courses->model_arenas.size() ||
       courses->courses.size()!=courses->model_batches.size())
        throw Error("Frozen custom course measurements are incomplete.");
    std::int32_t most=0;
    for(std::size_t i=0;i<courses->courses.size();++i) {
        if(courses->courses[i].level_id<65 || courses->courses[i].level_id>127 ||
           courses->model_arenas[i]<-1 || courses->model_batches[i]<-1 ||
           courses->model_arenas[i]>0x1ff000)
            throw Error("A custom course exceeds its safe online level/arena budget.");
        most=std::max(most,courses->model_batches[i]);
    }
    for(unsigned players=0;players<4;++players) {
        const auto wanted=std::uint64_t(out[players])+std::uint64_t(most)*10*(players+1);
        if(wanted>0x20000)throw Error("A custom course exceeds its safe online display-list budget.");
        out[players]=static_cast<std::uint32_t>(wanted);
    }
    return out;
}
inline std::uint32_t course_heap(const dkr::runtime::custom_tracks::PreparedTracks* courses,
    unsigned level) {
    if(!courses)return 0;
    for(std::size_t i=0;i<courses->courses.size();++i)if(courses->courses[i].level_id==level) {
        const auto arena=courses->model_arenas.at(i);
        if(arena<=dkr::runtime::custom_tracks::kRetailTrackHeap)return 0;
        const auto wanted=(std::uint64_t(arena)+0x1000+15)&~std::uint64_t(15);
        if(wanted>0x200000)throw Error("A custom course exceeds its safe online collision heap.");
        return static_cast<std::uint32_t>(wanted);
    }
    return 0;
}
inline bool admitted_course_heap(const dkr::runtime::custom_tracks::PreparedTracks* courses,
    std::uint32_t heap) {
    if(!heap)return true;
    if(!courses)return false;
    return std::ranges::any_of(courses->courses,[&](const auto& course){
        return course_heap(courses,course.level_id)==heap;
    });
}
}
