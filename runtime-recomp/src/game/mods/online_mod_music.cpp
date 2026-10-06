#include "online_mod_music.hpp"
#include "../custom_music_sequence.hpp"
#include <algorithm>

namespace dkr::mods::online {
MusicState::MusicState(std::shared_ptr<const MusicLibrary> library,bool rev80)
    :library_(std::move(library)),addresses_(rev80?dkr::runtime::revision_addresses::kUsV80:dkr::runtime::revision_addresses::kUsV77){}
const MusicSong* MusicState::song()const {
    if(!library_)return nullptr;
    const auto found=library_->find(level_);return found==library_->end()?nullptr:&found->second;
}
void MusicState::restore_row(CharacterMenuMemory& guest) {
    if(!row_held_)return;
    for(unsigned i=0;i<3;++i)guest.write(row_+i,row_bytes_[i],1);
    row_held_=false;row_=0;row_bytes_={};
}
void MusicState::load(CharacterMenuMemory& guest,unsigned level) {
    restore_row(guest);level_=library_&&library_->contains(level)?level:UINT32_MAX;
    position_=0;gain_=65536;applied_=0;base_bpm_=0;tempo_=65536;playing_=false;
}
void MusicState::loaded(CharacterMenuMemory& guest,recomp_context& ctx) {
    restore_row(guest);const auto* s=song();
    if(!s || s->info.kind!=dkr::runtime::custom_tracks::MusicKind::Sequence ||
       std::uint32_t(ctx.r16)!=guest.read(addresses_.MusicPlayer) ||
       !std::uint32_t(ctx.r16) || !std::uint32_t(ctx.r17) ||
       guest.read(std::uint32_t(ctx.r19),1)!=s->info.carrier)return;
    unsigned capacity=0;const auto table=guest.read(addresses_.SequenceTable),lengths=guest.read(addresses_.SequenceLengths);
    if(table && lengths) {
        const auto count=guest.read(table+2,2);if(count>256)throw Error("Custom music sequence count is invalid.");
        for(unsigned i=0;i<count;++i)capacity=std::max(capacity,guest.read(lengths+4*i));
    }
    if(!capacity)capacity=dkr::runtime::custom_music::kRetailSequenceCapacity;
    if(capacity>0x10000)throw Error("Custom music buffer contract is invalid.");
    std::array<bool,128> programs{};const std::array<bool,128>* known=nullptr;
    const auto bank=guest.read(std::uint32_t(ctx.r16)+0x20);
    if(bank) {const auto count=guest.read(bank,2);if(count>128)throw Error("Custom music instrument bank is invalid.");
        for(unsigned i=0;i<count;++i)programs[i]=guest.read(bank+0x0c+4*i)!=0;known=&programs;}
    if(!s->info.sequence)throw Error("Frozen music has no sequence bytes.");
    const auto check=dkr::runtime::custom_music::validate_sequence(*s->info.sequence,capacity,known);
    if(!check.ok())throw Error("Custom music sequence cannot play safely: "+check.message);
    guest.bytes(std::uint32_t(ctx.r17),*s->info.sequence);
    if(s->info.sequence->size()%2)guest.write(std::uint32_t(ctx.r17)+unsigned(s->info.sequence->size()),0,1);
    const auto sound=guest.read(addresses_.SequenceSoundTable);
    if(sound) {row_=sound+unsigned(s->info.carrier)*3;
        for(unsigned i=0;i<3;++i)row_bytes_[i]=std::uint8_t(guest.read(row_+i,1));row_held_=true;
        guest.write(row_,s->info.volume,1);guest.write(row_+1,s->info.tempo_bpm,1);guest.write(row_+2,s->info.reverb,1);}
}
void MusicState::started(CharacterMenuMemory& guest){restore_row(guest);}
bool MusicState::volume(CharacterMenuMemory& guest,recomp_context& ctx) {
    const auto* s=song();if(!s || !s->pcm || !std::uint32_t(ctx.r4) ||
       std::uint32_t(ctx.r4)!=guest.read(addresses_.MusicPlayer))return false;
    if(guest.read(addresses_.CurrentSequence,1)!=s->info.carrier &&
       guest.read(addresses_.MusicNextSequence,1)!=s->info.carrier)return false;
    const auto requested=std::int16_t(ctx.r5);const auto base=guest.read(addresses_.MusicBaseVolume,1);
    gain_=requested>0&&base?unsigned(std::min<std::uint64_t>(65536,std::uint64_t(requested)*256/base)):0;
    ctx.r5=0;return true;
}
void MusicState::tick(CharacterMenuMemory& guest) {
    const auto* s=song();bool playing=false;
    if(s && s->pcm) {const auto player=guest.read(addresses_.MusicPlayer);
        playing=player && guest.read(player+0x2c)==1 && guest.read(addresses_.CurrentSequence,1)==s->info.carrier;}
    if(playing && !playing_){position_=0;base_bpm_=0;}
    playing_=playing;tempo_=65536;
    if(playing && s->info.final_lap_speedup) {const auto bpm=std::int16_t(guest.read(addresses_.MusicTempo,2));
        if(bpm>0){if(!base_bpm_)base_bpm_=bpm;tempo_=unsigned(std::clamp<std::uint64_t>(std::uint64_t(bpm)*65536/base_bpm_,32768,131072));}}
}
std::int32_t MusicState::sample(unsigned channel,unsigned,std::size_t frame,std::size_t count)const {
    const auto* s=song();if(!s || !s->pcm || (!playing_ && !applied_))return 0;
    const auto& pcm=*s->pcm;const auto end=s->info.loop_end?s->info.loop_end:pcm.frames;
    const auto start=s->info.loop_start,index=position_>>32,next=index+1<end?index+1:start;
    const auto a=pcm.samples.at(std::size_t(index)*2+channel),b=pcm.samples.at(std::size_t(next)*2+channel);
    const auto interpolated=std::int64_t(a)+((std::int64_t(b-a)*std::int64_t(position_&UINT32_MAX))>>32);
    const auto target=playing_?std::uint64_t(gain_)*s->info.volume/100:0;
    const auto ramp=std::int64_t(applied_)+(std::int64_t(target)-applied_)*std::int64_t(frame+1)/std::int64_t(count);
    return std::int32_t(interpolated*ramp/65536);
}
void MusicState::advance(unsigned rate) {
    const auto* s=song();if(!s || !s->pcm)return;
    position_+=(std::uint64_t(s->pcm->sample_rate)<<16)*tempo_/rate;
    const auto end=(s->info.loop_end?s->info.loop_end:s->pcm->frames)<<32,start=s->info.loop_start<<32;
    if(position_>=end)position_=start+(position_-end)%(end-start);
}
void MusicState::mix_s16le(std::span<std::uint8_t> pcm,unsigned rate) {
    if(pcm.size()%4 || rate<8000 || rate>192000)throw Error("Custom music output contract is invalid.");
    const auto frames=pcm.size()/4;if(!song() || !song()->pcm || !frames)return;
    for(std::size_t f=0;f<frames;++f) {for(unsigned c=0;c<2;++c) {
        const auto at=f*4+c*2;const auto original=std::int16_t(unsigned(pcm[at])|unsigned(pcm[at+1])<<8);
        const auto mixed=std::clamp<int>(original+sample(c,rate,f,frames),-32768,32767);
        pcm[at]=std::uint8_t(mixed);pcm[at+1]=std::uint8_t(unsigned(mixed)>>8);
    }advance(rate);}applied_=playing_?unsigned(std::uint64_t(gain_)*song()->info.volume/100):0;
}
void MusicState::render(float* output,std::size_t frames,unsigned rate) {
    if(!output || !frames || rate<8000 || rate>192000 || !song() || !song()->pcm)return;
    for(std::size_t f=0;f<frames;++f){for(unsigned c=0;c<2;++c)output[f*2+c]+=float(sample(c,rate,f,frames));advance(rate);}
    applied_=playing_?unsigned(std::uint64_t(gain_)*song()->info.volume/100):0;
}
Bytes MusicState::checkpoint()const {
    CheckpointWriter out;out.u32(1);out.u32(level_);out.u32(row_);out.flag(row_held_);
    for(auto b:row_bytes_)out.u32(b);out.u64(position_);out.u32(gain_);out.u32(applied_);out.u32(base_bpm_);out.u32(tempo_);out.flag(playing_);
    return std::move(out).finish();
}
MusicState MusicState::stage_checkpoint(View bytes)const {
    MusicState next=*this;CheckpointReader in(bytes);if(in.u32()!=1)throw Error("Unsupported custom music checkpoint.");
    next.level_=in.u32();if(next.level_!=UINT32_MAX && (!library_ || !library_->contains(next.level_)))throw Error("Unadmitted custom music level.");
    next.row_=in.u32();next.row_held_=in.flag();for(auto& b:next.row_bytes_)b=std::uint8_t(in.bounded(255));
    next.position_=in.u64();next.gain_=in.bounded(65536);next.applied_=in.bounded(131072);next.base_bpm_=in.bounded(32767);
    next.tempo_=in.bounded(131072);next.playing_=in.flag();in.end();
    const auto* s=next.song();
    if(next.tempo_<32768 || (next.row_held_ && (!s || s->info.kind!=dkr::runtime::custom_tracks::MusicKind::Sequence || next.row_<0x80000000U || next.row_>0x807ffffdU)) ||
       (!next.row_held_ && (next.row_ || next.row_bytes_!=std::array<std::uint8_t,3>{})) ||
       ((!s || !s->pcm) && (next.position_ || next.playing_ || next.applied_ || next.base_bpm_)) ||
       (s && s->pcm && next.position_>=((s->info.loop_end?s->info.loop_end:s->pcm->frames)<<32)))throw Error("Invalid custom music checkpoint state.");
    return next;
}
}
