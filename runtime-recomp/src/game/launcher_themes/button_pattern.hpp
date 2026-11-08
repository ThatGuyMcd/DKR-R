#pragma once
#include "pixels.hpp"
#include <cmath>

namespace dkr::launcher_themes {
// Twenty small cached tiles, including the Canadian national variant. Both
// renderer contexts upload these with their existing font atlas, once.
inline constexpr int ButtonTileWidth=128,ButtonTileHeight=64,ButtonPatternColumns=4;
// Repeated guard pixels let a partial edge tile merge with its neighbour.
// This keeps rounded corners intact at every scroll phase without a mask or
// per-frame polygon clipping. The complete cache is under 2 MiB of RGBA.
inline constexpr int ButtonAtlasTileWidth=ButtonTileWidth*3;
inline constexpr int ButtonPatternWidth=ButtonAtlasTileWidth*ButtonPatternColumns,ButtonPatternHeight=320;
inline int button_variant(Theme theme,Calendar calendar) {
    if(theme==Theme::National&&calendar==Calendar::Canada)return int(Theme::Count);
    return int(theme)>=0&&theme<Theme::Count?int(theme):0;
}
inline bool pattern_line(float x,float y,float ax,float ay,float bx,float by,float thickness=1.3F) {
    const float dx=bx-ax,dy=by-ay,den=dx*dx+dy*dy;
    const float t=den>0?std::clamp(((x-ax)*dx+(y-ay)*dy)/den,0.F,1.F):0;
    const float ex=x-ax-t*dx,ey=y-ay-t*dy;return ex*ex+ey*ey<thickness*thickness;
}
inline unsigned button_pixel(int variant,int px,int py) {
    const bool canada=variant==int(Theme::Count);
    const Theme theme=canada?Theme::National:Theme(variant);
    if(theme==Theme::Original)return ((px/32+py/32)&1)?0xB04316:0xF4911F;
    const auto palette=Palettes[int(theme)];
    float x=float(px%64)-32.F,y=float(py)-32.F;
    const float radius=std::sqrt(x*x+y*y),angle=std::atan2(y,x);
    bool ink=false,bright=false;
    switch(theme) {
    case Theme::Halloween:
        // Two complete webs per tile: radial spokes and curved strands.
        for(int ray=0;ray<8;++ray) {
            const float a=ray*.78539816F;
            ink|=pattern_line(x,y,0,0,32.F*std::cos(a),32.F*std::sin(a),.7F);
        }
        for(int ring=1;ring<4;++ring)ink|=std::abs(radius-(ring*9.F+1.2F*std::cos(angle*8)))<.8F;
        break;
    case Theme::Bonfire:case Theme::NewYear:
        for(int ray=0;ray<10;++ray) {
            const float a=ray*.62831853F;
            ink|=pattern_line(x,y,11.F*std::cos(a),11.F*std::sin(a),24.F*std::cos(a),24.F*std::sin(a));
        }
        bright=radius<3.F;
        if(theme==Theme::NewYear)ink|=(px%23<4&&py%19<8);
        break;
    case Theme::Christmas:
        // Wrapped gifts with contrasting ribbon and a bow above the cross.
        ink=std::abs(x)<4||std::abs(y)<3;
        bright=(std::abs((x-7)*(x-7)/49+(y+7)*(y+7)/16-1)<.3F||
            std::abs((x+7)*(x+7)/49+(y+7)*(y+7)/16-1)<.3F);
        break;
    case Theme::Winter:
        for(int ray=0;ray<6;++ray) {
            const float a=ray*1.04719755F,dx=std::cos(a),dy=std::sin(a);
            ink|=pattern_line(x,y,0,0,22*dx,22*dy);
            ink|=pattern_line(x,y,14*dx,14*dy,12*dx-6*dy,12*dy+6*dx);
            ink|=pattern_line(x,y,14*dx,14*dy,12*dx+6*dy,12*dy-6*dx);
        }break;
    case Theme::Spring:
        ink=std::abs(radius-(12.F+4.F*std::cos(angle*6)))<3.F;bright=radius<5;
        break;
    case Theme::Summer:
        ink=std::abs(y-13.F*std::sin(float(px)*.09817477F))<3.F;
        bright=(x*x+(y+17)*(y+17)<25);break;
    case Theme::Autumn:case Theme::Thanksgiving:
        ink=(std::abs(x)*.7F+std::abs(y)<22.F)&&radius>3;
        bright=pattern_line(x,y,-14,17,14,-17,1.F);
        if(theme==Theme::Thanksgiving)bright|=std::abs(x)<1.5F&&std::abs(y)<25;
        break;
    case Theme::Valentine:
        {const float a=(x-6)*(x-6)+(y+5)*(y+5),b=(x+6)*(x+6)+(y+5)*(y+5);
         ink=(y<=0&&(a<81||b<81))||(y>0&&y<18&&std::abs(x)<15-y*.8F);}
        break;
    case Theme::Patrick:
        ink=x*x+(y+10)*(y+10)<64||(x-9)*(x-9)+y*y<64||(x+9)*(x+9)+y*y<64;
        bright=pattern_line(x,y,0,0,2,21,2);break;
    case Theme::Easter:
        ink=x*x/(12.F*12.F)+(y+2)*(y+2)/(21.F*21.F)<1;
        bright=ink&&(std::abs(y-4.F*std::sin(x*.45F))<2.F||std::abs(y+11)<2.F);break;
    case Theme::Lunar:case Theme::EidFitr:
        ink=x*x/180.F+y*y/270.F<1.F&&x*x/120.F+y*y/200.F>.7F;
        bright=std::abs(y+18)<2&&std::abs(x)<9||std::abs(x)<1.5F&&std::abs(y)<16||
            pattern_line(x,y,0,18,0,26);break;
    case Theme::Diwali:
        ink=y>5&&y<18&&x*x+(y-3)*(y-3)<300;
        bright=std::abs(x)<(y+18)*.4F&&y<3&&y>-18;break;
    case Theme::EidAdha:case Theme::Hanukkah:
        ink=std::abs(radius-(16+6*std::cos(angle*(theme==Theme::Hanukkah?6:8))))<2;
        bright=radius<3;break;
    case Theme::National:
        ink=py<17&&std::abs(x)<24.F&&(py>2&&py<17-std::abs(x)*.3F);
        if(canada)bright=std::abs(y)<18&&std::abs(x)<(20-std::abs(y)*.7F)&&
            (std::abs(x)<4||std::abs(y+3)<5||std::abs(std::abs(x)-std::abs(y+5))<4);
        else bright=std::abs(radius-(10+4*std::cos(angle*5)))<2.F;
        break;
    default:break;
    }
    unsigned background=palette.dark;
    if(theme==Theme::Christmas)background=0xA91F38;
    if(theme==Theme::Spring)background=0x217866;
    if(theme==Theme::Patrick)background=0x12653B;
    return bright?0xFFF1C4:ink?palette.accent:background;
}
inline void fill_button_patterns(unsigned char* pixels,int atlas_width,int left,int top) {
    for(int v=0;v<=int(Theme::Count);++v)for(int y=0;y<ButtonTileHeight;++y)for(int x=0;x<ButtonAtlasTileWidth;++x) {
        const unsigned rgb=button_pixel(v,x%ButtonTileWidth,y);
        auto* out=pixels+(std::size_t(top+v/ButtonPatternColumns*ButtonTileHeight+y)*atlas_width+
            left+v%ButtonPatternColumns*ButtonAtlasTileWidth+x)*4;
        out[0]=static_cast<unsigned char>(rgb>>16);out[1]=static_cast<unsigned char>(rgb>>8);
        out[2]=static_cast<unsigned char>(rgb);out[3]=255;
    }
}
} // namespace dkr::launcher_themes
