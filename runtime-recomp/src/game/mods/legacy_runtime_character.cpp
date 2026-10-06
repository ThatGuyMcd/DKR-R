#include "legacy_runtime_character.hpp"
#include "legacy_character_menu.hpp"

namespace dkr::mods {
void dispatch_character_ai_event(CharacterRoster& roster,CharacterAiState& state,std::span<std::uint8_t> memory,
    const recomp_context& ctx,unsigned event,std::span<const std::uint8_t> unlocked,OriginalPiHandler random) {
    CharacterMenuFields fields;fields.fill(0x80000000U);CharacterMenuMemory g(memory,fields);
    if(event==0){state={};roster.clear_ai();return;}
    if(event==3){state.spawn.reset();return;}
    if(event==2){
        state.spawn.reset();if(!state.racers)return;
        const auto entry=std::uint32_t(ctx.r22),index=std::uint32_t(ctx.r20);
        if(index>=state.racers)throw Error("AI primary spawn has an invalid racer index.");
        if(index>=state.humans && g.read(entry+0xe,2)==4)state.spawn={{entry,index}};
        return;
    }
    if(event!=1)throw Error("Unknown race AI lifecycle event.");
    const auto sp=std::uint32_t(ctx.r29),settings=std::uint32_t(ctx.r23);
    if(sp>0xffffffffU-0x144 || settings>0xffffffffU-0x117)throw Error("AI setup guest range overflow.");
    const auto humans=g.read(sp+0x144),racers=g.read(std::uint32_t(ctx.r6)),
        type=g.read(sp+0x68),mode=g.read(sp+0x138);
    if(!humans || humans>4 || racers<humans || racers>8)throw Error("AI setup topology is out of range.");
    roster.scene_humans(humans);state={};
    // Retail changes the local race type to hub (5) for time trials. Only
    // ordinary races and the three challenge types admit computer choices.
    if(mode==1 || !(type==0 || (type>=64 && type<=66)) || racers==humans)return;
    g.read(settings+0x117,1);
    std::array<std::uint8_t,8> ids{};
    for(unsigned i=0;i<racers;++i)ids[i]=g.read(settings+0x59+24*i,1);
    if(!random || (sp&7) || (sp&0x1fffffffU)<0x100)throw Error("Missing native AI RNG/scratch frame.");
    g.read(sp-0x100);g.read(sp-1,1);
    roster.assign_ai(ids,humans,racers,unlocked,[&](unsigned count){
        auto call=ctx;call.r29=static_cast<std::int32_t>(sp-0x100);
        call.f_odd=call.mips3_float_mode?&call.f1.u32l:&call.f0.u32h;
        call.r4=0;call.r5=count-1;random(memory.data(),&call);return unsigned(call.r2);
    });
    for(unsigned i=humans;i<racers;++i)g.write(settings+0x59+24*i,ids[i],1);
    state.humans=humans;state.racers=racers;
}
void dispatch_character_event(CharacterRoster& roster,std::span<std::uint8_t> guest,
    recomp_context& ctx,unsigned event,std::uint32_t roster_address,
    std::optional<std::pair<std::uint32_t,unsigned>> ai_spawn) {
    auto read=[&](std::uint32_t address,unsigned length) {
        const auto offset=address&0x1fffffffU;
        if((address&0xe0000000U)!=0x80000000U || guest.size()%4 || offset>guest.size() || length>guest.size()-offset)
            throw Error("Character hook received an invalid guest range.");
        std::uint32_t value=0;
        for(unsigned i=0;i<length;++i)value=(value<<8)|guest[(offset+i)^3];
        return value;
    };
    if(event==2)roster.clear_active();
    else if(event==1) {
        const auto count=static_cast<std::uint32_t>(ctx.r4);
        if(count>4 || roster_address>0xffffffffU-4)throw Error("Character commit has an invalid native player count/range.");
        std::array<std::uint8_t,4> ids{};
        for(unsigned i=0;i<count;++i)ids[i]=read(roster_address+i,1);
        roster.commit(ids,count);
    } else if(event==0) {
        const auto header=static_cast<std::uint32_t>(ctx.r4);
        if(header>=30)return;
        const auto stack=static_cast<std::uint32_t>(ctx.r29);
        if(stack>0xffffffffU-0x68)throw Error("Character stack range overflow.");
        const auto entry=read(stack+0x68,4);
        if(entry>0xffffffffU-16 || (read(entry+1,1)&0x7f)!=16)return;
        auto player=read(entry+0xe,2);
        if(player<4 && roster.ai(player))return; // An AI index is not a human controller.
        if(player>=4){
            // AI sentinel 4 is not racer #4. Only the checked primary race
            // spawn call can supply its actual participant index.
            if(player!=4 || !ai_spawn || ai_spawn->first!=entry)return;
            player=ai_spawn->second;
        }
        if(const auto mapped=roster.header(player,header))ctx.r4=*mapped;
    } else throw Error("Unknown character lifecycle event.");
}
}
