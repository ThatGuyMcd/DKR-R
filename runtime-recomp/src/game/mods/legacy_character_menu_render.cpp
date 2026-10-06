#include "legacy_character_menu_render.hpp"
#include <algorithm>
#include <bit>

namespace dkr::mods {
namespace {
gpr ptr(std::uint32_t p){return static_cast<gpr>(static_cast<std::int32_t>(p));}
class Calls {
public:
    CharacterMenuMemory guest;
    std::span<std::uint8_t> memory;
    recomp_context saved;
    std::uint32_t stack;
    Calls(std::span<std::uint8_t> mem,const CharacterMenuFields& fields,const recomp_context& ctx)
        :guest(mem,fields),memory(mem),saved(ctx),stack(static_cast<std::uint32_t>(ctx.r29)) {
        if((stack&0xe0000000U)!=0x80000000U || (stack&0x1fffffffU)<0x400 || stack%8)throw Error("Invalid character drawing stack.");
        stack-=0x400;guest.read(stack);guest.read(stack+0x3fc);
    }
    std::uint32_t call(OriginalPiHandler fn,std::initializer_list<std::uint32_t> args) {
        if(!fn)throw Error("Native character menu callback is unavailable.");
        if(args.size()>8)throw Error("Character menu callback argument budget exceeded.");
        auto ctx=saved;ctx.r29=ptr(stack);unsigned i=0;
        ctx.f_odd=ctx.mips3_float_mode?&ctx.f1.u32l:&ctx.f0.u32h;
        for(auto v:args) {
            switch(i){case 0:ctx.r4=ptr(v);break;case 1:ctx.r5=ptr(v);break;case 2:ctx.r6=ptr(v);break;case 3:ctx.r7=ptr(v);break;
            default:guest.write(stack+i*4,v);break;}++i;
        }
        fn(memory.data(),&ctx);return static_cast<std::uint32_t>(ctx.r2);
    }
};
}
void CharacterMenuRenderer::draw_hint(std::span<std::uint8_t> memory,const CharacterMenuFields& fields,recomp_context& ctx,
    const CharacterMenuDrawCalls& f,const std::vector<AllocatedCharacter>& entries,const CharacterMenuView& view) {
    if(!view.visible)return;
    Calls call(memory,fields,ctx);auto& g=call.guest;
    // Text-only footer. No page, portrait grid, projection or stock text moves.
    // Scratch is on a separate reserved call frame and never retained.
    const auto scratch=call.stack+0x100;
    auto text=[&](std::string value,unsigned x,unsigned y,unsigned colour) {
        if(value.size()>255)throw Error("Character hint exceeds its scratch budget.");
        Bytes bytes;for(unsigned char c:value)bytes.push_back(c>=32&&c<127?c:'?');bytes.push_back(0);g.bytes(scratch,bytes);
        call.call(f.font,{1});call.call(f.background,{0,0,0,180});
        call.call(f.colour,{colour>>24,(colour>>16)&255,(colour>>8)&255,0,255});
        call.call(f.text,{g.address(CharacterMenuField::DisplayList),x,y,scratch,0});
    };
    constexpr unsigned colours[]{0xffdc42ff,0x69d8ffff,0xff8f96ff,0x8dff83ff};
    bool custom=false;
    for(unsigned p=0;p<4;++p)if(view.active[p]&&view.custom[p]) {
        custom=true;auto name=entries.at(*view.custom[p]).name;
        if(name.size()>11)name=name.substr(0,10)+".";
        text("P"+std::to_string(p+1)+" "+name+(view.ready[p]?" OK":""),8+76*p,220,colours[p]);
    }
    if(custom&&entries.size()>CustomStageSlots)text("L / R: CHANGE CUSTOM RACER",65,231,0xffe29aff);
    call.call(f.font,{2});call.call(f.background,{0,0,0,0});call.call(f.colour,{255,255,255,0,255});
    call.call(f.reset,{g.address(CharacterMenuField::DisplayList)});
}
void CharacterMenuRenderer::initialize(std::span<std::uint8_t> memory,const CharacterMenuFields& fields,recomp_context& ctx,
    const CharacterMenuDrawCalls& f,const std::vector<AllocatedCharacter>& entries) {
    if(scratch_ || !portraits_.empty())throw Error("Character menu resources already belong to an active menu.");
    Calls call(memory,fields,ctx);
    scratch_=call.call(f.allocate,{512,0x7f7f7fff});
    if(!scratch_)throw Error("Not enough guest memory for the character page.");
    call.guest.bytes(scratch_,Bytes(512));
    try {
        for(const auto& c:entries) {
            const auto tex=call.call(f.load_texture,{c.portrait});
            if(!tex)throw Error("The custom character portrait could not be loaded.");
            portraits_.push_back(tex);
            if(!call.guest.read(tex,1) || !call.guest.read(tex+1,1))throw Error("Invalid custom character portrait dimensions.");
        }
    }catch(const Error&){release(memory,ctx,f);throw;}
}
void CharacterMenuRenderer::release(std::span<std::uint8_t> memory,recomp_context& ctx,const CharacterMenuDrawCalls& f) {
    // Original guest reference counts and deferred-free behaviour stay intact.
    for(const auto texture:portraits_) {if(!f.free_texture)throw Error("Missing portrait release callback.");auto c=ctx;c.r4=ptr(texture);f.free_texture(memory.data(),&c);}
    portraits_.clear();
    if(scratch_){if(!f.release)throw Error("Missing character menu heap release.");auto c=ctx;c.r4=ptr(scratch_);f.release(memory.data(),&c);scratch_=0;}
}
void CharacterMenuRenderer::draw(std::span<std::uint8_t> memory,const CharacterMenuFields& fields,recomp_context& ctx,
    const CharacterMenuDrawCalls& f,const std::vector<AllocatedCharacter>& entries,const CharacterMenuView& view) {
    if(!view.visible)return;
    if(!scratch_ || portraits_.size()!=entries.size())throw Error("Character page has no live portrait resources.");
    Calls call(memory,fields,ctx);auto& g=call.guest;using F=CharacterMenuField;
    const auto dlist=g.address(F::DisplayList);
    auto gfx=[&](std::uint32_t w0,std::uint32_t w1) {
        const auto at=g.get(F::DisplayList);if(at%8 || at>0xffffffffU-8)throw Error("Invalid character display list.");
        g.read(at+4);g.write(at,w0);g.write(at+4,w1);g.set(F::DisplayList,at+8);
    };
    auto box=[&](unsigned x,unsigned y,unsigned w,unsigned h,std::uint32_t colour) {
        if(!w || !h || x+w>320 || y+h>240)throw Error("Character card escaped its authored viewport.");
        // Same authored display-list setup and environment rectangle mode as
        // font.c:render_dialogue_box. No global HUD/projection setting changes.
        gfx(0x06000000,g.address(F::DialogueBegin));
        gfx(0x07020010,(g.address(F::DialogueModes)+16)&0x1fffffffU);
        gfx(0xfb000000,colour);
        gfx(0xf6000000|((x+w-1)<<14)|((y+h-1)<<2),(x<<14)|(y<<2));
        gfx(0xe7000000,0);
    };
    auto text=[&](std::string value,unsigned x,unsigned y,std::uint32_t colour=0xffffffffU) {
        if(value.size()>255)throw Error("Character label exceeds native scratch space.");
        Bytes bytes;for(unsigned char c:value)bytes.push_back(c>=32 && c<127?c:'?');bytes.push_back(0);g.bytes(scratch_+128,bytes);
        call.call(f.font,{1});call.call(f.background,{0,0,0,0});
        call.call(f.colour,{colour>>24,(colour>>16)&255,(colour>>8)&255,0,colour&255});
        call.call(f.text,{dlist,x,y,scratch_+128,0});
    };
    constexpr std::uint32_t colours[]{0xffdc42ff,0x69d8ffff,0xff8f96ff,0x8dff83ff};
    if(view.custom_page) {
        box(12,45,296,158,0x102238ff);
        text("CUSTOM CHARACTERS",24,49,0xffdb65ff);
        text("P"+std::to_string(view.owner+1)+"  PAGE "+std::to_string(view.page+1)+"/"+std::to_string((entries.size()+7)/8),210,49,colours[view.owner]);
        for(unsigned cell=0;cell<8;++cell) {
            const auto index=std::size_t(view.page)*8+cell;if(index>=entries.size())break;
            const auto x=22+(cell%4)*70,y=64+(cell/4)*62;
            unsigned marked=4;for(unsigned p=0;p<4;++p)if(view.active[p] && view.custom[p]==index){marked=p;break;}
            box(x,y,66,58,marked<4?colours[marked]:0x526a82ff);box(x+2,y+2,62,54,0x182f47ff);
            const auto tex=portraits_[index],width=g.read(tex,1),height=g.read(tex+1,1);
            const float scale=34.0f/static_cast<float>(std::max(width,height));
            g.write(scratch_,tex);g.write(scratch_+4,0);g.write(scratch_+8,0);g.write(scratch_+12,0);
            const float px=static_cast<float>(x)+33.0f-float(width)*scale*0.5f;
            call.call(f.texture,{dlist,scratch_,std::bit_cast<std::uint32_t>(px),std::bit_cast<std::uint32_t>(float(y+3)),
                std::bit_cast<std::uint32_t>(scale),std::bit_cast<std::uint32_t>(scale),0xffffffff,0});
            auto label=entries[index].name;
            if(label.size()>12)label=label.substr(0,11)+".";
            text(label,x+3,y+38);
            std::string badges;for(unsigned p=0;p<4;++p)if(view.active[p] && view.custom[p]==index)badges+="P"+std::to_string(p+1)+(view.ready[p]?" OK ":" ");
            text(badges,x+3,y+47,marked<4?colours[marked]:0xffffffff);
        }
        if(view.custom[view.owner]) {
            auto name=entries[*view.custom[view.owner]].name;
            if(name.size()>52)name=name.substr(0,49)+"...";
            text(name,24,190,colours[view.owner]);
        }
    }
    box(12,220,296,17,0x102238ee);
    text(view.custom_page?"L: BACK/PAGE  R: NEXT  A: SELECT  B: BACK":"R: CUSTOM CHARACTERS",21,224,0xffe29aff);
    for(unsigned p=0;p<4;++p)if(view.active[p] && view.custom[p]) {
        auto label="P"+std::to_string(p+1)+" "+entries[*view.custom[p]].name+(view.ready[p]?" OK":"");
        if(label.size()>18)label=label.substr(0,17)+".";
        box(12+p*74,204,73,14,0x102238ee);text(label,14+p*74,207,colours[p]);
    }
    call.call(f.font,{2});call.call(f.background,{0,0,0,0});call.call(f.colour,{255,255,255,0,255});
    call.call(f.reset,{dlist});
}
}
