#pragma once
#include "hle/rt64_state.h"
#include "hle/rt64_rdp.h"
#include "hle/rt64_rsp.h"
#include "revision_addresses.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace dkr::runtime::presentation {

// The font renderer has already CPU-clipped a partially visible left glyph
// before packing unsigned 12-bit rectangle coordinates. Recover ONLY complete,
// recognised title-name runs from the immutable task. Never change guest RAM,
// font metrics, timings, texture uploads, colours or unrelated menu scissors.
class TitleTextViewport {
    struct Symbols {
        std::uint32_t begin,modes,fonts,font_count,kerning,names,colours;
    };
    static constexpr std::array<Symbols,2> symbol_tables_{{
@TITLE_SYMBOL_TABLES@
    }};
    struct Glyph {
        const RT64::DisplayList* command=nullptr;
        int left=0,top=0,right=0,bottom=0,s=0,t=0,atlas=-1;
    };
    static constexpr unsigned capacity=32;
    std::array<Glyph,capacity> glyphs_{};
    unsigned count_=0;
    const Symbols* symbols_=nullptr;
    const std::uint8_t* ram_=nullptr;
    std::uint32_t font_=0;
    float cover_=1.F;
    bool enabled_=false;
    std::array<std::uint32_t,8> images_{};
    unsigned image_count_=0;

    static void skipped(unsigned reason,unsigned detail=0) {
        static thread_local std::uint32_t reported=0;
        const auto bit=1U<<reason;
        if((reported&bit)==0) {
            reported|=bit;
            std::fprintf(stderr,"[title][names] original path retained reason=%u detail=0x%08X\n",reason,detail);
        }
    }

    static bool valid(std::uint32_t address,std::uint32_t size) {
        const auto p=address&0x1FFFFFFFU;
        return p>0 && p<0x800000U && size<=0x800000U-p;
    }
    std::uint8_t u8(std::uint32_t address) const {return ram_[(address&0x7FFFFFU)^3U];}
    std::uint16_t u16(std::uint32_t address) const {
        std::uint16_t value;std::memcpy(&value,ram_+((address&0x7FFFFFU)^2U),2);return value;
    }
    std::uint32_t u32(std::uint32_t address) const {
        std::uint32_t value;std::memcpy(&value,ram_+(address&0x7FFFFCU),4);return value;
    }
    static int signed12(unsigned value) {return value&0x800U?int(value)-4096:int(value);}
    bool colour(std::uint32_t env,std::uint32_t prim) const {
        if(prim==0xFFFFFFFFU && env==0xFFFFFF00U)return true;
        if((prim&0xFFFFFF00U)!=0xFFFFFF00U)return false;
        for(unsigned i=0;i<4;++i) {
            const auto p=symbols_->colours+i*5;
            const auto expected=(std::uint32_t(u8(p))<<24)|(std::uint32_t(u8(p+1))<<16)|
                (std::uint32_t(u8(p+2))<<8)|u8(p+3);
            if(env==expected && (prim&255U)==u8(p+4))return true;
        }
        return false;
    }
    int atlas(const RT64::DisplayList& command) const {
        for(unsigned i=0;i<32;++i) {
            const auto header=u32(font_+0x80+i*4);
            if(!valid(header,0x20))continue;
            if((u32(header+0xC)&0x1FFFFFFFU)==(command.w1&0x1FFFFFFFU) &&
                u16(header+0xA)==command.p0(16,8))return int(i);
        }
        return -1;
    }
    bool match_name(std::uint32_t name) {
        if(!valid(name,capacity+1))return false;
        unsigned letters=0;
        int advance=0,base_x=0,base_y=0;
        const bool compact=u32(symbols_->kerning)!=0;
        std::array<Glyph,capacity> repaired{};
        for(unsigned i=0;i<=capacity;++i) {
            const auto ch=u8(name+i);
            if(ch==0) {
                if(!letters || letters!=count_ || advance>320)return false;
                // draw_text used middle/centre alignment. Translate the WHOLE
                // string's centre path, not individual glyph positions/widths.
                const int centre=base_x/4+(advance>>1);
                if(centre< -128 || centre>448 || base_y<128*4 || base_y>240*4)return false;
                const int shift=int(std::lround((centre-160.F)*(cover_-1.F)*4.F));
                for(unsigned j=0;j<letters;++j) {
                    repaired[j].left+=shift;repaired[j].right+=shift;
                }
                glyphs_=repaired;
                return true;
            }
            if(i==capacity || ch<33 || ch>=128 || letters>=count_)return false;
            const auto data=font_+0x100+(ch-32)*8;
            const auto& raw=glyphs_[letters];
            if(u8(data)==255 || raw.atlas!=u8(data))return false;
            const int width=u8(data+6)*4,height=u8(data+7)*4;
            const int s=u8(data+4)*32,t=u8(data+5)*32;
            if(!width || !height)return false;
            if(!letters) {
                base_x=raw.right-width-u8(data+2)*4;
                base_y=raw.bottom-height-u8(data+3)*4;
            }
            const int left=base_x+advance*4+u8(data+2)*4,top=base_y+u8(data+3)*4;
            const int right=left+width,bottom=top+height;
            // Reproduce retail's exact clipping and atlas offset as evidence
            // that this command is THIS character, before undoing that clip.
            const bool clipped=left<0 && right>0;
            if(raw.left!=(clipped?0:left) || raw.right!=right || raw.top!=top || raw.bottom!=bottom ||
                raw.s!=s+(clipped?-left*8:0) || raw.t!=t)return false;
            repaired[letters]=raw;
            repaired[letters].left=left;repaired[letters].s=s;
            int space=u16(font_+0x20)?u16(font_+0x20):u8(data+1);
            if(compact && space)--space;
            advance+=space;++letters;
        }
        return false;
    }
public:
    void begin(RT64::State& state,float aspect,bool modern_fit) {
        count_=image_count_=0;enabled_=false;ram_=state.RDRAM;
        if(!modern_fit || !std::isfinite(aspect) || aspect<=4.F/3.F+0.001F)return;
        if(u32(revision_addresses::CurrentMenuId)!=0 || u32(revision_addresses::TitleRevealTimer)!=0)return;
        const bool v80=revision_addresses::CurrentMenuId==revision_addresses::kUsV80.CurrentMenuId;
        symbols_=&symbol_tables_[v80?1:0];
        const auto fonts=u32(symbols_->fonts);
        if(u32(symbols_->font_count)<3 || u32(symbols_->font_count)>32 || !valid(fonts,3*0x400)) {
            skipped(1,u32(symbols_->font_count));return;
        }
        font_=fonts+2*0x400; // ASSET_FONTS_BIGFONT, decomp font.h/asset_enums.h.
        if(!u16(font_+0x22) || u16(font_+0x22)>64){skipped(2,u16(font_+0x22));return;}
        cover_=std::clamp(aspect/(4.F/3.F),1.F,8.F);enabled_=true;
    }
    void prepare(RT64::State& state,const RT64::DisplayList* start) {
        if(!enabled_ || start->p0(16,1) ||
            (state.rsp->fromSegmented(start->w1)&0x1FFFFFFFU)!=(symbols_->begin&0x1FFFFFFFU))return;
        count_=0;
        const auto offset=reinterpret_cast<const std::uint8_t*>(start)-ram_;
        if(offset<0 || std::uint64_t(offset)+8>0x800000U || (offset&7))return;
        std::array<Glyph,capacity> raw{};
        unsigned n=0;
        int current_atlas=-1;
        bool mode=false,ended=false;
        std::uint32_t env=0,prim=0;
        for(unsigned i=1;i<256;++i) {
            if(std::uint64_t(offset)+(i+1)*8>0x800000U)return;
            const auto& cmd=start[i];
            const auto opcode=cmd.w0>>24;
            if(opcode==0xB7 && cmd.w1==0x0205U) {ended=true;break;}
            if(opcode==0xE7)continue;
            if(opcode==0xFA) {prim=cmd.w1;continue;}
            if(opcode==0xFB) {env=cmd.w1;continue;}
            if(opcode==0x07) {
                if((cmd.w1&0x1FFFFFFFU)==(symbols_->modes&0x1FFFFFFFU) && cmd.p0(16,8)==2)mode=true;
                else {current_atlas=atlas(cmd);if(current_atlas<0){skipped(3,cmd.w1);return;}}
                continue;
            }
            if(opcode!=0xE4 || !mode || current_atlas<0 || n==capacity || !colour(env,prim) ||
                std::uint64_t(offset)+(i+3)*8>0x800000U){skipped(4,cmd.w0);return;}
            // Derived from DKR's own gbi.h, not F3DEX: B3 is ST, B2 is slope.
            if(start[i+1].w0!=@TITLE_RDPHALF1@ || start[i+2].w0!=@TITLE_RDPHALF2@ ||
                start[i+2].w1!=0x04000400U || cmd.p1(24,3)!=0){skipped(5,start[i+1].w0);return;}
            raw[n++]={&cmd,signed12(cmd.p1(12,12)),signed12(cmd.p1(0,12)),
                signed12(cmd.p0(12,12)),signed12(cmd.p0(0,12)),
                std::int16_t(start[i+1].p1(16,16)),std::int16_t(start[i+1].p1(0,16)),current_atlas};
            i+=2;
        }
        if(!ended || !n){skipped(6,n);return;}
        for(unsigned name=0;name<10;++name) {
            glyphs_=raw;count_=n;
            if(match_name(u32(symbols_->names+name*0x2C)))return;
        }
        count_=0;
        skipped(7,n);
    }
    bool draw(RT64::State& state,RT64::DisplayList** cursor) {
        if(!enabled_ || !count_ || state.rdp->colorImage.width!=320)return false;
        const Glyph* glyph=nullptr;
        for(unsigned i=0;i<count_;++i)if(glyphs_[i].command==*cursor){glyph=&glyphs_[i];break;}
        if(!glyph)return false;
        auto& rdp=*state.rdp;
        const auto image=rdp.colorImage.address;
        if(std::find(images_.begin(),images_.begin()+image_count_,image)==images_.begin()+image_count_) {
            if(image_count_==images_.size())return false;
            images_[image_count_++]=image;
        }
        state.flush();
        const auto at=rdp.scissorStackSize-1;
        const auto previous=rdp.scissorRectStack[at];
        const auto left_origin=rdp.extended.scissorLeftOriginStack[at];
        const auto right_origin=rdp.extended.scissorRightOriginStack[at];
        const auto aspect=rdp.extended.global.rectAspect;
        auto& clip=rdp.scissorRectStack[at];
        const int gutter=int(std::ceil(640.F*(cover_-1.F)));
        clip.ulx=-gutter;clip.lrx=1280+gutter;
        rdp.extended.scissorLeftOriginStack[at]=rdp.extended.scissorRightOriginStack[at]=G_EX_ORIGIN_NONE;
        state.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
        rdp.setRectAspect(G_EX_ASPECT_ADJUST);
        RT64::ExtendedAlignment alignment{};
        alignment.leftOrigin=alignment.rightOrigin=G_EX_ORIGIN_NONE;
        rdp.drawTexRect(glyph->left,glyph->top,glyph->right,glyph->bottom,0,
            std::int16_t(glyph->s),std::int16_t(glyph->t),1024,1024,false,alignment);
        state.flush();
        rdp.scissorRectStack[at]=previous;
        rdp.extended.scissorLeftOriginStack[at]=left_origin;
        rdp.extended.scissorRightOriginStack[at]=right_origin;
        state.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
        rdp.setRectAspect(aspect);
        *cursor+=2;
        static thread_local bool notice=false;
        if(!notice) {
            std::fprintf(stderr,"[title][names] full-width travel; unclipped signed glyphs; original proportions and colour trails\n");
            notice=true;
        }
        return true;
    }
    bool has_images() const {return image_count_!=0;}
    template<class Pair> void normalise(Pair& pair) const {
        if(pair.colorImage.width==320 && !pair.scissorRect.isNull() &&
            std::find(images_.begin(),images_.begin()+image_count_,pair.colorImage.address)!=images_.begin()+image_count_) {
            pair.scissorRect.ulx=0;pair.scissorRect.lrx=1280;
        }
    }
};
} // namespace dkr::runtime::presentation
