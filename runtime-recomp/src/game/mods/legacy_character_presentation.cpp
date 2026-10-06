#include "legacy_character_presentation.hpp"
#include <bit>
#include <algorithm>

namespace dkr::mods {
namespace {
gpr ptr(std::uint32_t p){return static_cast<gpr>(static_cast<std::int32_t>(p));}
class Calls {
    CharacterMenuFields fields_=[]{CharacterMenuFields f;f.fill(0x80000000U);return f;}();
public:
    CharacterMenuMemory guest;
    std::span<std::uint8_t> memory;
    recomp_context saved;
    std::uint32_t stack;
    Calls(std::span<std::uint8_t> m,const recomp_context& c):guest(m,fields_),memory(m),saved(c),stack(std::uint32_t(c.r29)) {
        if((stack&0xe0000000U)!=0x80000000U || (stack&0x1fffffffU)<0x400 || stack%8)
            throw Error("Invalid custom presentation stack.");
        stack-=0x400;guest.read(stack);guest.read(stack+0x3fc);
    }
    std::uint32_t call(OriginalPiHandler fn,std::initializer_list<std::uint32_t> args) {
        if(!fn || args.size()>12)throw Error("Missing or invalid native presentation callback.");
        auto ctx=saved;ctx.r29=ptr(stack);unsigned i=0;
        ctx.f_odd=ctx.mips3_float_mode?&ctx.f1.u32l:&ctx.f0.u32h;
        for(auto v:args) {
            switch(i){case 0:ctx.r4=ptr(v);break;case 1:ctx.r5=ptr(v);break;case 2:ctx.r6=ptr(v);break;case 3:ctx.r7=ptr(v);break;
            default:guest.write(stack+i*4,v);break;}++i;
        }
        fn(memory.data(),&ctx);return std::uint32_t(ctx.r2);
    }
};
std::size_t selected(const CharacterRoster& roster,const std::vector<AllocatedCharacter>& entries,unsigned racer) {
    if(racer>=8)return entries.size();
    const auto& id=roster.active(racer);
    if(id.empty())return entries.size();
    for(std::size_t i=0;i<entries.size();++i)if(entries[i].id==id)return i;
    throw Error("Committed custom character has no presentation resources.");
}
}
void CharacterPresentation::initialize(std::span<std::uint8_t> memory,const recomp_context& ctx,
    const CharacterPresentationCalls& f,const std::vector<AllocatedCharacter>& entries,
    std::span<const std::uint32_t> samples) {
    if(ready_)return;
    if(!banks_.empty() || !portrait_cells_.empty() || entries.empty() || entries.size()!=samples.size() || entries.size()>MaxEnabledCharacters)
        throw Error("Invalid custom presentation resource ownership.");
    portrait_cells_.resize(entries.size());banks_.resize(entries.size());selection_banks_.resize(entries.size());
    ready_=true;
}
void CharacterPresentation::prepare(std::span<std::uint8_t> memory,const recomp_context& ctx,
    const CharacterPresentationCalls& f,const std::vector<AllocatedCharacter>& entries,
    unsigned i,std::uint32_t sample_address,bool audio) {
    if(!ready_ || entries.size()!=banks_.size() || i>=entries.size())throw Error("Unadmitted custom presentation resource.");
    const auto& c=entries[i];
    if(portrait_cells_[i] && (!audio || c.race_audio.control.empty() || banks_[i]))return;
    Calls call(memory,ctx);auto& g=call.guest;
    if(!portrait_cells_[i] && !c.portrait_identity.empty())for(unsigned j=0;j<entries.size();++j)
        if(portrait_cells_[j] && entries[j].portrait_identity==c.portrait_identity) {portrait_cells_[i]=portrait_cells_[j];break;}
    if(!portrait_cells_[i]) {
        const auto texture=call.call(f.texture_load,{c.portrait});
        if(!texture || !g.read(texture,1) || !g.read(texture+1,1))throw Error("Custom result portrait failed to load.");
        const auto draw=call.call(f.allocate,{24,0x7f7f7fff});
        if(!draw)throw Error("No guest memory for a custom result portrait.");
        g.bytes(draw,Bytes(24));g.write(draw,texture);g.write(draw+16,draw);
        portrait_cells_[i]=draw+16;
    }
    if(audio && !c.race_audio.control.empty() && !banks_[i]) {
        if(!c.race_audio_identity.empty())for(unsigned j=0;j<entries.size();++j)
            if(banks_[j] && entries[j].race_audio_identity==c.race_audio_identity) {banks_[i]=banks_[j];break;}
        if(banks_[i])return;
        if(!sample_address)throw Error("Custom race sample mount is missing.");
        const auto control=call.call(f.allocate,{unsigned(c.race_audio.control.size()),0x7f7f7fff});
        if(!control)throw Error("No guest memory for custom race audio.");
        g.bytes(control,c.race_audio.control);
        call.call(f.bank_relocate,{control,sample_address});banks_[i]=g.read(control+4);
    }
}
std::uint32_t CharacterPresentation::selection_bank(std::span<std::uint8_t> memory,const recomp_context& ctx,
    const CharacterPresentationCalls& f,const std::vector<AllocatedCharacter>& entries,unsigned i,std::uint32_t samples) {
    if(!ready_ || entries.size()!=selection_banks_.size() || i>=entries.size() || !samples)
        throw Error("Unadmitted custom selection voice.");
    if(selection_banks_[i])return selection_banks_[i];
    const auto& c=entries[i];
    if(!c.selection_audio_identity.empty())for(unsigned j=0;j<entries.size();++j)
        if(selection_banks_[j] && entries[j].selection_audio_identity==c.selection_audio_identity)
            return selection_banks_[i]=selection_banks_[j];
    Calls call(memory,ctx);auto& g=call.guest;
    const auto base=call.call(f.allocate,{unsigned(c.audio.control.size()),0x7f7f7fff});
    if(!base)throw Error("Not enough guest memory for the selected custom voice.");
    g.bytes(base,c.audio.control);call.call(f.bank_relocate,{base,samples});
    return selection_banks_[i]=g.read(base+4);
}
std::uint32_t CharacterPresentation::portrait(const CharacterRoster& roster,
    const std::vector<AllocatedCharacter>& entries,unsigned racer)const {
    if(!ready_)return 0;
    const auto i=selected(roster,entries,racer);
    return i<entries.size()?portrait_cells_.at(i):0;
}
unsigned CharacterPresentation::sound(const CharacterRoster& roster,const std::vector<AllocatedCharacter>& entries,
    unsigned racer,unsigned native_character,unsigned native_sound)const {
    if(!ready_)return native_sound;
    const auto i=selected(roster,entries,racer);
    if(i==entries.size() || !banks_.at(i) || native_character!=entries[i].base_character)return native_sound;
    const auto cue=character_race_cue(native_sound,native_character);
    return cue<0?native_sound:character_race_sound(unsigned(i),unsigned(cue));
}
void CharacterPresentation::bind_hud(std::uint32_t stack,std::uint32_t hud,const CharacterRoster& roster,
    const std::vector<AllocatedCharacter>& entries,unsigned racer) {
    unbind_hud(stack);
    if(!ready_)return;
    const auto i=selected(roster,entries,racer);if(i==entries.size() || !portrait_cells_.at(i))return;
    if(hud_bindings_.size()>=16)throw Error("Custom portrait draw nesting exceeds its budget.");
    hud_bindings_.push_back({stack,hud,portrait_cells_.at(i)-16,56+entries[i].base_character});
}
void CharacterPresentation::unbind_hud(std::uint32_t stack) {
    std::erase_if(hud_bindings_,[&](const auto& b){return b.stack==stack;});
}
std::uint32_t CharacterPresentation::hud_lookup(std::uint32_t stack,std::uint32_t hud,unsigned sprite)const {
    for(const auto& b:hud_bindings_)if(b.stack==stack && b.hud==hud && b.sprite==sprite)return b.cell;
    return 0;
}
unsigned CharacterPresentation::cinematic_id(const CharacterRoster& roster,const std::vector<AllocatedCharacter>& entries,
    unsigned racer,unsigned native_id) {
    if(!ready_)return native_id;
    const auto i=selected(roster,entries,racer);
    // This byte belongs exclusively to the transient trophy portrait list;
    // never put it in Settings, racer behaviour, saves or ghost recordings.
    if(i==entries.size())return native_id;
    if(!portrait_cells_.at(i))throw Error("Cinematic portrait was not prepared by its racer owner.");
    // The retail list stores a byte: tokens name participants, not library
    // ordinals. Entry 1500 is as representable as entry zero.
    cinematic_cells_.at(racer)=portrait_cells_[i];return 64U+racer;
}
std::uint32_t CharacterPresentation::cinematic_portrait(unsigned id)const {
    if(id<64 || id>=72)return 0;
    if(!ready_ || !cinematic_cells_[id-64])throw Error("Unowned cinematic portrait identity.");
    return cinematic_cells_[id-64];
}
bool CharacterPresentation::play(std::span<std::uint8_t> memory,const recomp_context& ctx,
    const CharacterPresentationCalls& f,const std::vector<AllocatedCharacter>& entries,unsigned kind)const {
    unsigned i=0,c=0;
    if(!decode_character_race_sound(unsigned(ctx.r4),i,c))return false;
    if(!ready_ || kind>2 || i>=entries.size() || !banks_.at(i))throw Error("Unowned custom race sound.");
    const auto& cue=entries[i].race_audio.cues[c];Calls call(memory,ctx);auto& g=call.guest;
    if(kind==0) {
        const auto sp=std::uint32_t(ctx.r29),handle=g.read(sp+20),flags=g.read(sp+16);
        if(!cue.sound){if(handle)g.write(handle,0);return true;}
        // Keep AudioPoint** ownership, flags, native attenuation, and deferred
        // playback. Only its private sound identity and parameters differ.
        call.call(f.spatial_point,{unsigned(ctx.r4),unsigned(ctx.r5),unsigned(ctx.r6),unsigned(ctx.r7),
            flags,cue.min_volume,cue.volume,cue.range,0,cue.pitch,cue.priority,handle});
    } else {
        auto handle=std::uint32_t(ctx.r5);
        // Encoded calls normally supply native handles. Retain a session-owned
        // fallback cell, not temporary stack memory, for the null-handle API.
        if(!handle)handle=portrait_cells_.at(i)+4;
        if(!cue.sound){g.write(handle,0);return true;}
        call.call(f.bank_play,{banks_.at(i),cue.sound,kind==1?0U:cue.priority,handle});
        if(kind==2)if(const auto voice=g.read(handle)) {
            call.call(f.parameter,{voice,8,cue.volume*256});
            call.call(f.parameter,{voice,16,std::bit_cast<std::uint32_t>(cue.pitch/100.0f)});
        }
    }
    return true;
}
}
