#pragma once
#include "art_sdl.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstring>

namespace dkr::launcher_themes {
inline int corner_art_rect=-1;
inline void fill_corner_atlas(ImFontAtlas* atlas,unsigned char* pixels,int width,int index) {
    corner_art_rect=-1;auto* rect=atlas->GetCustomRectByIndex(index);
    if(!rect||!rect->IsPacked()||!pixels)return;
    int w=0,h=0,channels=0;
    auto* image=stbi_load_from_memory(ArtPng,int(sizeof(ArtPng)),&w,&h,&channels,4);
    if(image&&w==ArtWidth&&h==ArtHeight) {
        for(int y=0;y<h;++y)std::memcpy(pixels+(std::size_t(rect->Y+y)*width+rect->X)*4,
            image+std::size_t(y)*w*4,std::size_t(w)*4);
        corner_art_rect=index;
    }
    if(image)stbi_image_free(image);
}
inline void atlas_corners(ImDrawList* draw,ImVec2 at,ImVec2 extent,Theme theme,Calendar calendar) {
    auto* atlas=ImGui::GetIO().Fonts;
    auto* rect=corner_art_rect>=0?atlas->GetCustomRectByIndex(corner_art_rect):nullptr;
    if(!rect||!rect->IsPacked()||!atlas->TexID||extent.x<180||extent.y<180)return;
    const auto [first,second]=icons_for(theme,calendar);
    const float size=std::clamp(extent.y*.065F,52.F,88.F),inset=10.F;
    const auto image=[&](Icon icon,ImVec2 pos) {
        if(int(icon)<0||icon>=Icon::Count)return;
        const float x=float(rect->X+int(icon)%ArtColumns*ArtTile),y=float(rect->Y+int(icon)/ArtColumns*ArtTile);
        draw->AddImage(atlas->TexID,pos,{pos.x+size,pos.y+size},{x/atlas->TexWidth,y/atlas->TexHeight},
            {(x+ArtTile)/atlas->TexWidth,(y+ArtTile)/atlas->TexHeight});
    };
    draw->PushClipRect(at,{at.x+extent.x,at.y+extent.y},true);
    image(first,{at.x+inset,at.y+inset});
    image(second,{at.x+extent.x-size-inset,at.y+extent.y-size-inset});draw->PopClipRect();
}
} // namespace dkr::launcher_themes
