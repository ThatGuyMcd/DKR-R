#include "legacy_character_audio.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace dkr::mods {
namespace {
void w16(Bytes& b,unsigned p,unsigned v){b.at(p)=v>>8;b.at(p+1)=v;}
void w32(Bytes& b,unsigned p,unsigned v){w16(b,p,v>>16);w16(b,p+2,v);}
template<class Audio,class Cues> Audio close_bank(View ctl,View samples,Cues cues) {
    if(ctl.size()>4*MiB || samples.size()>16*MiB || be16(ctl,0)!=0x4231 || be16(ctl,2)!=1)
        throw Error("Selection audio requires a bounded native B1 sound bank.");
    const auto bank=be32(ctl,4);slice(ctl,bank,16);
    if(be16(ctl,bank)!=1 || ctl[bank+2] || be32(ctl,bank+8))throw Error("Unsupported selection sound bank structure.");
    const auto inst=be32(ctl,bank+12);slice(ctl,inst,16);
    const auto count=be16(ctl,inst+14);
    if(!count || count>1023 || ctl[inst+3])throw Error("Invalid selection sound instrument.");
    slice(ctl,inst+16,count*4);
    std::set<unsigned> sounds;
    std::function<void(unsigned,std::set<unsigned>&)> visit=[&](unsigned id,std::set<unsigned>& chain) {
        if(!id)return;
        if(id>count || !chain.insert(id).second || chain.size()>64)throw Error("Invalid or cyclic selection sound sequence.");
        const auto sound=be32(ctl,inst+16+(id-1)*4);slice(ctl,sound,16);
        const auto key=be32(ctl,sound+4);slice(ctl,key,6);
        sounds.insert(id);
        if(sounds.size()>64)throw Error("Selection audio dependency count is too large.");
        visit(unsigned(ctl[key])+unsigned(ctl[key+2]&0xc0)*4,chain);
        chain.erase(id);
    };
    for(const auto& cue:cues){std::set<unsigned> chain;visit(cue.sound,chain);}
    if(sounds.empty())throw Error("Character has no selection audio cues.");
    Audio out;out.cues=cues;
    out.control.resize((40+4*sounds.size()+7)&~std::size_t(7),0);
    w16(out.control,0,0x4231);w16(out.control,2,1);w32(out.control,4,8);
    std::copy_n(ctl.begin()+bank,16,out.control.begin()+8);w32(out.control,20,24);
    std::copy_n(ctl.begin()+inst,16,out.control.begin()+24);w16(out.control,38,unsigned(sounds.size()));
    std::map<unsigned,unsigned> sound_ids;unsigned next=1;
    for(auto id:sounds)sound_ids[id]=next++;
    auto append=[&](View data) {
        const auto offset=unsigned(out.control.size());
        if(data.size()>256*1024-offset)throw Error("Selection sound metadata exceeds its budget.");
        out.control.insert(out.control.end(),data.begin(),data.end());out.control.resize((out.control.size()+7)&~std::size_t(7),0);
        return offset;
    };
    std::map<std::pair<unsigned,unsigned>,unsigned> copied;
    auto copy=[&](unsigned kind,unsigned offset,unsigned size) {
        const auto key=std::pair{kind,offset};
        if(auto i=copied.find(key);i!=copied.end())return i->second;
        if(!offset)throw Error("Selection sound has a null required dependency.");
        const auto target=append(slice(ctl,offset,size));copied[key]=target;return target;
    };
    std::map<std::pair<unsigned,unsigned>,unsigned> sample_offsets;
    for(const auto id:sounds) {
        const auto original=be32(ctl,inst+16+(id-1)*4);
        if(ctl[original+14])throw Error("Selection sound is already relocated.");
        const auto sound=copy(0,original,16);w32(out.control,40+4*(sound_ids[id]-1),sound);
        w32(out.control,sound,copy(1,be32(ctl,original),16));
        const auto key=copy(2,be32(ctl,original+4),6);w32(out.control,sound+4,key);
        const auto source_key=be32(ctl,original+4);
        const auto following=unsigned(ctl[source_key])+unsigned(ctl[source_key+2]&0xc0)*4;
        const auto mapped=following?sound_ids.at(following):0;
        out.control[key]=mapped&255;out.control[key+2]=(out.control[key+2]&0x3f)|((mapped>>2)&0xc0);
        const auto source_wave=be32(ctl,original+8);slice(ctl,source_wave,20);
        if(ctl[source_wave+9] || ctl[source_wave+8]>1)throw Error("Unsupported selection sample format.");
        const auto wave=copy(3,source_wave,20);w32(out.control,sound+8,wave);
        const auto source=be32(ctl,source_wave),length=be32(ctl,source_wave+4);
        if(!length || length>2*MiB)throw Error("Selection sample exceeds its length budget.");
        slice(samples,source,length);
        const auto sample_key=std::pair{source,length};
        if(!sample_offsets.contains(sample_key)) {
            if(length+32>4*MiB-out.samples.size())throw Error("Selection sample closure exceeds its budget.");
            sample_offsets[sample_key]=unsigned(out.samples.size());
            out.samples.insert(out.samples.end(),samples.begin()+source,samples.begin()+source+length);
            out.samples.resize((out.samples.size()+31)&~std::size_t(15),0); // DMA tail guard
        }
        w32(out.control,wave,sample_offsets.at(sample_key));
        const auto loop=be32(ctl,source_wave+12);
        if(loop) {
            slice(ctl,loop,12);
            if(be32(ctl,loop)>be32(ctl,loop+4))throw Error("Selection sample loop is reversed.");
            const auto decoded_samples=ctl[source_wave+8]==0?(length/9)*16:length/2;
            if(be32(ctl,loop+4)>decoded_samples)throw Error("Selection loop exceeds its decoded sample data.");
            w32(out.control,wave+12,copy(4+ctl[source_wave+8],loop,ctl[source_wave+8]==0?44:12));
        }
        if(ctl[source_wave+8]==0) {
            const auto book=be32(ctl,source_wave+16);
            const auto order=be32(ctl,book),predictors=be32(ctl,book+4);
            if(!order || order>8 || !predictors || predictors>16)throw Error("Invalid selection ADPCM predictor book.");
            w32(out.control,wave+16,copy(6,book,8+order*predictors*16));
        } else w32(out.control,wave+16,0);
    }
    for(auto& cue:out.cues) {
        if(cue.volume>127 || cue.pitch>255 || cue.priority>255)throw Error("Invalid selection sound parameters.");
        if(cue.sound)cue.sound=sound_ids.at(cue.sound);
    }
    return out;
}
}
CharacterAudio prepare_character_audio(View control,View samples,View table,unsigned base) {
    if(base>=10)throw Error("Invalid selection voice identity.");
    constexpr unsigned events[]{0x87,0x93,0x19e};
    std::array<CharacterCue,3> cues;
    for(unsigned i=0;i<3;++i) {
        const auto data=slice(table,10*(events[i]+base),10);
        cues[i]={be16(data,0),data[2],data[4],data[8]};
    }
    return close_bank<CharacterAudio>(control,samples,cues);
}
void validate_character_audio(const CharacterAudio& audio) {
    const auto checked=close_bank<CharacterAudio>(audio.control,audio.samples,audio.cues);
    if(checked.control!=audio.control || checked.samples!=audio.samples || checked.cues!=audio.cues)
        throw Error("Selection audio contains unowned, noncanonical or invalid dependencies.");
}
std::string character_audio_identity(const CharacterAudio& audio) {
    std::string identity=sha256(audio.control)+sha256(audio.samples);
    for(const auto& c:audio.cues)identity+=":"+std::to_string(c.sound)+","+std::to_string(c.volume)+","+std::to_string(c.pitch)+","+std::to_string(c.priority);
    return sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
}
int character_race_cue(unsigned sound,unsigned base) {
    if(base>=10)return -1;
    for(unsigned i=0;i<8;++i) {
        if(sound==0x162+12*i+base)return int(i);
        if(sound==0x1c2+12*i+base)return int(8+i);
    }
    return sound==0x156+base?16:sound==0x7b+base?17:-1;
}
unsigned character_race_sound(unsigned character,unsigned cue) {
    if(character>=MaxEnabledCharacters || cue>=18)throw Error("Invalid custom race sound identity.");
    return CharacterSoundBegin+character*CharacterSoundStride+cue;
}
bool decode_character_race_sound(unsigned sound,unsigned& character,unsigned& cue) {
    if(sound<CharacterSoundBegin || sound>0xffffU || (sound&31)>=18)return false;
    character=(sound-CharacterSoundBegin)/CharacterSoundStride;cue=sound&31;return true;
}
CharacterRaceAudio prepare_character_race_audio(View control,View samples,View table,unsigned base) {
    if(base>=10)throw Error("Invalid race voice identity.");
    std::array<CharacterRaceCue,18> cues;
    for(unsigned i=0;i<cues.size();++i) {
        const unsigned sound=(i<8?0x162+12*i:i<16?0x1c2+12*(i-8):i==16?0x156:0x7b)+base;
        const auto data=slice(table,10*sound,10);
        cues[i]={{be16(data,0),data[2],data[4],data[8]},data[3],be16(data,6)};
        if(cues[i].min_volume>127)throw Error("Invalid custom spatial sound volume.");
    }
    return close_bank<CharacterRaceAudio>(control,samples,cues);
}
void validate_character_race_audio(const CharacterRaceAudio& audio) {
    for(const auto& c:audio.cues)if(c.min_volume>127 || c.range>65535)throw Error("Invalid custom spatial sound parameters.");
    const auto checked=close_bank<CharacterRaceAudio>(audio.control,audio.samples,audio.cues);
    if(checked.control!=audio.control || checked.samples!=audio.samples || checked.cues!=audio.cues)
        throw Error("Race audio contains unowned, noncanonical or invalid dependencies.");
}
}
