#include "mods/online_mod_music.hpp"
#include <iostream>
#include <algorithm>
using namespace dkr::mods;
namespace {unsigned checks=0;void check(bool b){if(!b)throw Error("Music check "+std::to_string(checks));++checks;}}
int main() {
    try {
        for(bool rev80:{false,true}) {
            const auto& a=rev80?dkr::runtime::revision_addresses::kUsV80:dkr::runtime::revision_addresses::kUsV77;
            auto library=std::make_shared<online::MusicLibrary>();online::MusicSong song;
            auto pcm=std::make_shared<dkr::runtime::custom_music::DecodedMusic>();pcm->sample_rate=32000;pcm->frames=4;
            pcm->samples={1000,-1000,2000,-2000,3000,-3000,4000,-4000};song.pcm=pcm;
            song.info.carrier=1;song.info.volume=100;song.info.loop_start=1;song.info.loop_end=4;
            library->emplace(65,song);online::MusicState host(library,rev80),client(library,rev80);
            CharacterMenuFields fields;fields.fill(0x80000000U);Bytes ram(8*MiB);CharacterMenuMemory guest(ram,fields);
            guest.write(a.MusicPlayer,0x80400000);guest.write(0x8040002c,1);guest.write(a.CurrentSequence,1,1);
            guest.write(a.MusicBaseVolume,127,1);guest.write(a.MusicTempo,120,2);
            host.load(guest,65);client.load(guest,65);recomp_context ctx{};ctx.r4=0x80400000;ctx.r5=127*256;
            check(host.volume(guest,ctx)&&ctx.r5==0);ctx.r5=127*256;check(client.volume(guest,ctx));
            host.tick(guest);client.tick(guest);check(host.checkpoint()==client.checkpoint());
            const auto before=host.checkpoint();Bytes first(32),second(32);host.mix_s16le(first,32000);client.mix_s16le(second,32000);
            check(first==second && first!=Bytes(32));check(host.checkpoint()==client.checkpoint());
            host=host.stage_checkpoint(before);Bytes replay(32);host.mix_s16le(replay,32000);check(first==replay);
            guest.write(a.MusicTempo,134,2);host.tick(guest);const auto sped=host.checkpoint();
            first.assign(256,0);host.mix_s16le(first,32000);host=host.stage_checkpoint(sped);second.assign(256,0);host.mix_s16le(second,32000);
            check(first==second);const auto stable=host.checkpoint();
            for(std::size_t n=0;n<stable.size();++n) {bool rejected=false;try{host.stage_checkpoint(View(stable).first(n));}catch(const Error&){rejected=true;}check(rejected);}
            auto forged=stable;forged[7]=66;bool rejected=false;try{host.stage_checkpoint(forged);}catch(const Error&){rejected=true;}check(rejected);
            check(host.checkpoint()==stable);host.load(guest,1);first.assign(32,0);host.mix_s16le(first,32000);check(first==Bytes(32));
            ctx.r5=1000;check(!host.volume(guest,ctx)&&ctx.r5==1000);
        }
        std::cout<<checks<<" deterministic custom music/replay/bounds checks passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
