#pragma once
#include <array>
#include <cmath>
#include <optional>
namespace dkr::mods {
using CharacterLabelWorld=std::array<float,3>;
using CharacterLabelMatrix=std::array<float,16>;
struct CharacterLabelPoint {float x,y;};
inline std::optional<CharacterLabelPoint> project_character_label(CharacterLabelWorld world,
    const CharacterLabelMatrix& view,const CharacterLabelMatrix& projection) {
    std::array<float,4> point{world[0],world[1],world[2],1},camera{},clip{};
    for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)camera[c]+=point[r]*view[r*4+c];
    for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)clip[c]+=camera[r]*projection[r*4+c];
    if(!std::isfinite(clip[3]) || clip[3]<=0.0001f)return std::nullopt;
    CharacterLabelPoint result{160+160*clip[0]/clip[3],120-120*clip[1]/clip[3]};
    // A model's head/feet can leave the screen while its stage actor remains
    // visible. Let the label layout clamp those extents instead of hiding it.
    if(!std::isfinite(result.x) || !std::isfinite(result.y))return std::nullopt;
    return result;
}
}
