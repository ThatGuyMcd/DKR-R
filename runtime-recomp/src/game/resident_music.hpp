#pragma once
#include "mods/online_mod_music.hpp"
#include <stop_token>

namespace dkr::mods::online::resident_music {
// Prepared recordings use an aliasing metadata pointer: the owning object
// pins the verified encoded bytes, NOT a library-wide collection of PCM.
struct Prepared {
    std::shared_ptr<const dkr::runtime::custom_music::DecodedMusic> metadata;
    std::string pcm_sha256;
};
Prepared prepare(Bytes encoded,const dkr::runtime::custom_tracks::MusicInfo&,std::stop_token);

// Only ordinary course loading or the confirmed owned scene continuation may
// open a decoder. Audio mixing and checkpoint replay never grant this scope.
class PreparationScope final {
public:
    explicit PreparationScope(bool enabled=true,bool audible=true) noexcept;
    ~PreparationScope();
    PreparationScope(const PreparationScope&)=delete;
    PreparationScope& operator=(const PreparationScope&)=delete;
private:
    bool enabled_,silent_;
};

// A preview can suppress music without granting permission to decode.
// Kept local to the course-prepare service so a failed guest continuation
// cannot leave thread-local suppression active after its native unwind.
class PreviewScope final {
public:
    explicit PreviewScope(bool enabled) noexcept;
    ~PreviewScope();
    PreviewScope(const PreviewScope&)=delete;
    PreviewScope& operator=(const PreviewScope&)=delete;
private:
    bool enabled_;
};

// Delegates the existing integer mixer/checkpoint ABI without changing any
// accepted MusicSong/MusicLibrary, RuntimeResources or ModWorld header layout.
class State {
public:
    State(std::shared_ptr<const MusicLibrary>,bool revision80);
    void load(CharacterMenuMemory&,unsigned level);
    void loaded(CharacterMenuMemory& g,recomp_context& c){native_.loaded(g,c);}
    void started(CharacterMenuMemory& g){native_.started(g);}
    bool volume(CharacterMenuMemory& g,recomp_context& c){return native_.volume(g,c);}
    void tick(CharacterMenuMemory& g){native_.tick(g);}
    void mix_s16le(std::span<std::uint8_t> b,unsigned r){native_.mix_s16le(b,r);}
    void render(float* b,std::size_t n,unsigned r){native_.render(b,n,r);}
    Bytes checkpoint()const{return native_.checkpoint();}
    State stage_checkpoint(View)const;
private:
    std::shared_ptr<const MusicLibrary> sources_;
    bool revision80_;
    std::shared_ptr<const MusicLibrary> active_;
    MusicState native_;
    std::shared_ptr<const MusicLibrary> activate(unsigned level,bool allow_decode)const;
};
}
