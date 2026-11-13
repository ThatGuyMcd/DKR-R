#pragma once
#include "imgui.h"
#include <array>
#include <utility>

namespace dkr::library_ui {
inline constexpr ImU32 NavigationGold=IM_COL32(255,209,107,255);
inline constexpr ImU32 NavigationMuted=IM_COL32(138,166,181,255);
inline void navigation_glow(ImDrawList* draw,const ImVec2* points,int count,bool active,bool closed=true) {
    if(!active)return;
    for(const auto [width,alpha]:{std::pair{9.F,12},std::pair{6.F,22},std::pair{3.F,40}})
        draw->AddPolyline(points,count,IM_COL32(255,209,107,alpha),closed?ImDrawFlags_Closed:0,width);
}
inline void draw_navigation_icons(ImDrawList* draw,ImVec2 left,ImVec2 right,bool dpad,ImFont* font,float font_size) {
    for(int i=0;i<2;++i) {
        const ImVec2 a{left.x-23.5F+i*25.F,left.y-10.F},b{a.x+22.F,a.y+20.F};
        const std::array<ImVec2,4> outline{{a,{b.x,a.y},b,{a.x,b.y}}};
        navigation_glow(draw,outline.data(),int(outline.size()),!dpad);
        draw->AddRectFilled(a,b,IM_COL32(12,39,53,255),4.F);
        draw->AddRect(a,b,dpad?NavigationMuted:NavigationGold,4.F,0,1.5F);
        const char* text=i?"RB":"LB";
        const auto extent=font->CalcTextSizeA(font_size,100.F,0,text);
        draw->AddText(font,font_size,{(a.x+b.x-extent.x)*.5F,(a.y+b.y-extent.y)*.5F},
            dpad?NavigationMuted:NavigationGold,text);
    }
    constexpr float outer=14.F,inner=5.F;
    const std::array<ImVec2,12> cross{{
        {right.x-inner,right.y-outer},{right.x+inner,right.y-outer},
        {right.x+inner,right.y-inner},{right.x+outer,right.y-inner},
        {right.x+outer,right.y+inner},{right.x+inner,right.y+inner},
        {right.x+inner,right.y+outer},{right.x-inner,right.y+outer},
        {right.x-inner,right.y+inner},{right.x-outer,right.y+inner},
        {right.x-outer,right.y-inner},{right.x-inner,right.y-inner}}};
    navigation_glow(draw,cross.data(),int(cross.size()),dpad);
    // Two rectangles avoid triangulating the concave cross as a convex path.
    draw->AddRectFilled({right.x-inner,right.y-outer},{right.x+inner,right.y+outer},IM_COL32(12,39,53,255),2.F);
    draw->AddRectFilled({right.x-outer,right.y-inner},{right.x+outer,right.y+inner},IM_COL32(12,39,53,255),2.F);
    draw->AddPolyline(cross.data(),int(cross.size()),dpad?NavigationGold:NavigationMuted,ImDrawFlags_Closed,1.5F);
    draw->AddCircleFilled(right,2.F,dpad?NavigationGold:NavigationMuted,8);
}
}
