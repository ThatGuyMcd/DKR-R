    bool match_credit(std::uint32_t element,std::uint32_t env,std::uint32_t prim) {
        // Only the actual active ASCII credit rows, never arbitrary dialogue.
        const auto name=u32(element+0x14);
        if(!valid(name,capacity+1) || u8(element+0x13)!=0 || u8(element+0x12)!=4 ||
            u16(element+4)!=160 || u16(element+0x1E)!=0 ||
            font_!=font_base_+std::uint32_t(u8(element+0x11))*0x400U)return false;
        const auto expected_env=(std::uint32_t(u8(element+12))<<24)|
            (std::uint32_t(u8(element+13))<<16)|(std::uint32_t(u8(element+14))<<8)|u8(element+15);
        if(env!=expected_env || prim!=(0xFFFFFF00U|u8(element+16)))return false;
        const bool compact=u32(symbols_->kerning)!=0;
        int alignment_width=0;
        unsigned expected_count=0;
        for(unsigned i=0;i<=capacity;++i) {
            const auto ch=u8(name+i);
            if(!ch)break;
            if(i==capacity || ch<32 || ch>=128)return false;
            int space=0;
            if(ch==32)space=u16(font_+0x24);
            else {
                const auto data=font_+0x100+(ch-32)*8;
                if(u8(data)==255)return false;
                space=u16(font_+0x20)?u16(font_+0x20):u8(data+1);
                ++expected_count;
            }
            if(compact && space)--space;
            alignment_width+=space;
        }
        if(!expected_count || expected_count!=count_ || alignment_width>512)return false;
        std::array<Glyph,capacity> repaired{};
        int advance=0,base_x=0,base_y=0;
        unsigned letters=0;
        for(unsigned i=0;i<=capacity;++i) {
            const auto ch=u8(name+i);
            if(!ch) {
                const int centre=base_x/4+(alignment_width>>1);
                // Retail credits enter at 480, hold at 160 and leave at -160.
                if(centre< -161 || centre>481 || base_y<0 || base_y>288*4)return false;
                const int shift=int(std::lround((centre-160.F)*(cover_-1.F)*4.F));
                for(unsigned j=0;j<letters;++j) {
                    repaired[j].left+=shift;repaired[j].right+=shift;
                }
                glyphs_=repaired;return true;
            }
            if(ch==32) {
                // Retail advances spaces inside the loop, outside the glyph
                // kerning decrement. get_text_width still kerns those spaces.
                advance+=u16(font_+0x24);continue;
            }
            const auto data=font_+0x100+(ch-32)*8;
            const auto& raw=glyphs_[letters];
            if(raw.atlas!=u8(data))return false;
            const int width=u8(data+6)*4,height=u8(data+7)*4;
            const int s=u8(data+4)*32,t=u8(data+5)*32;
            if(!width || !height)return false;
            if(!letters) {
                base_x=raw.right-width-u8(data+2)*4-advance*4;
                base_y=raw.bottom-height-u8(data+3)*4;
            }
            const int left=base_x+advance*4+u8(data+2)*4,top=base_y+u8(data+3)*4;
            const int right=left+width,bottom=top+height;
            const bool clipped=left<0 && right>0;
            // Credits can exceed X=512 while entering from the right. Compare
            // the PACKED coordinate domain, then restore the full signed quad.
            if((raw.left&4095)!=((clipped?0:left)&4095) || (raw.right&4095)!=(right&4095) ||
                raw.top!=top || raw.bottom!=bottom || raw.s!=s+(clipped?-left*8:0) || raw.t!=t)return false;
            repaired[letters]=raw;
            repaired[letters].left=left;repaired[letters].right=right;repaired[letters].s=s;
            int space=u16(font_+0x20)?u16(font_+0x20):u8(data+1);
            if(compact && space)--space;
            advance+=space;++letters;
        }
        return false;
    }
