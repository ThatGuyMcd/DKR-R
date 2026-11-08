#pragma once
#include "policy.hpp"
#include "revision_addresses.hpp"
#include <span>
#include <cstring>
namespace dkr::feedback {
// Read-only, bounded access to an already retained image. Struct offsets are
// the reviewed Racer layout in structs.h / sdk2_properties.inl for both ROMs.
inline void race_details(std::span<const std::uint8_t> ram,runtime::rom::Revision revision,unsigned player,Sample& s) {
    const auto valid=[&](std::uint32_t p,unsigned n){return p>=0x80000400U&&p<=0x80800000U-n&&ram.size()>=0x800000U;};
    const auto word=[&](std::uint32_t p){std::uint32_t v=0;if(valid(p,4)&&p%4==0)std::memcpy(&v,ram.data()+p-0x80000000U,4);return v;};
    const auto byte=[&](std::uint32_t p){return valid(p,1)?ram[((p-0x80000000U)^3U)]:0;};
    const auto& table=revision==runtime::rom::Revision::UsV77?runtime::revision_addresses::kUsV77:runtime::revision_addresses::kUsV80;
    const auto count=word(table.NumberOfRacers),list=word(table.RacersByPort);
    if(count>8||!valid(list,count*4))return;
    s.countdown=int(word(table.RaceStartTimer));if(s.countdown<0||s.countdown>300)s.countdown=0;
    for(unsigned i=0;i<count;++i) {
        const auto object=word(list+i*4);if(!valid(object,0x68))continue;
        const auto racer=word(object+0x64);if(!valid(racer,0x224))continue;
        std::int16_t port=0;std::memcpy(&port,ram.data()+((racer-0x80000000U)^2U),2);if(port!=int(player))continue;
        s.bananas=unsigned(byte(racer+0x185));s.ammunition=unsigned(byte(racer+0x173));
        s.finished=byte(racer+0x1D8)!=0;s.surface=unsigned(byte(racer+0x1DC));
        s.water=std::int8_t(byte(racer+0x1E5))>0;s.grounded=byte(racer+0x1E2)>0||s.water;
        s.impact=s.impact||std::int8_t(byte(racer+0x1DB))>0;return;
    }
}
}
