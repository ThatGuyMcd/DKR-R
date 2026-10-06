#pragma once
#include "legacy_checkpoint.hpp"
#include "legacy_character_menu.hpp"
#include "legacy_runtime_io.hpp"
#include "../custom_music_decode.hpp"
#include "../revision_addresses.hpp"
#include <map>

namespace dkr::mods::online {
struct MusicSong {
    dkr::runtime::custom_tracks::MusicInfo info;
    std::shared_ptr<const dkr::runtime::custom_music::DecodedMusic> pcm;
};
using MusicLibrary=std::map<unsigned,MusicSong>;
// One per guest owner. Immutable songs are decoded before readiness; replay
// never touches a file, decoder thread, offline binding or output device.
class MusicState {
public:
    MusicState(std::shared_ptr<const MusicLibrary> library,bool revision80);
    void load(CharacterMenuMemory&,unsigned level);
    void loaded(CharacterMenuMemory&,recomp_context&);
    void started(CharacterMenuMemory&);
    bool volume(CharacterMenuMemory&,recomp_context&);
    void tick(CharacterMenuMemory&);
    void mix_s16le(std::span<std::uint8_t>,unsigned rate);
    void render(float*,std::size_t frames,unsigned rate);
    Bytes checkpoint()const;
    MusicState stage_checkpoint(View)const;
private:
    const MusicSong* song()const;
    void restore_row(CharacterMenuMemory&);
    std::int32_t sample(unsigned channel,unsigned rate,std::size_t frame,std::size_t count)const;
    void advance(unsigned rate);
    std::shared_ptr<const MusicLibrary> library_;
    dkr::runtime::revision_addresses::AddressTable addresses_;
    unsigned level_=UINT32_MAX;
    std::uint32_t row_=0,gain_=65536,applied_=0,base_bpm_=0,tempo_=65536;
    std::array<std::uint8_t,3> row_bytes_{};
    std::uint64_t position_=0; // source frames, Q32, canonical across CPUs
    bool row_held_=false,playing_=false;
};
}
