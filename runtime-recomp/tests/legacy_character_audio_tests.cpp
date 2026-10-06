#include "legacy_character_audio.hpp"
#include <iostream>
#if defined(DKR_TEST_AUDIO_GUEST)
#include "legacy_character_menu.hpp"
#include "legacy_character_materialize.hpp"
#include "legacy_mod_stage.hpp"
#include "recomp.h"
extern "C" void alBnkfNew(std::uint8_t*,recomp_context*);
#endif
using namespace dkr::mods;
namespace {
unsigned checks=0;
void check(bool ok){++checks;if(!ok)throw Error("Selection audio assertion "+std::to_string(checks));}
template<class F>void rejects(F fn){bool caught=false;try{fn();}catch(const Error&){caught=true;}check(caught);}
void w16(Bytes& b,unsigned p,unsigned v){b.at(p)=v>>8;b.at(p+1)=v;}
void w32(Bytes& b,unsigned p,unsigned v){w16(b,p,v>>16);w16(b,p+2,v);}
struct Fixture {
 Bytes ctl=Bytes(176),samples=Bytes(32),table=Bytes(6400);
 Fixture() {
  w16(ctl,0,0x4231);w16(ctl,2,1);w32(ctl,4,8);
  w16(ctl,8,1);w32(ctl,20,24);w16(ctl,38,1);w32(ctl,40,48);
  w32(ctl,48,64);w32(ctl,52,80);w32(ctl,56,88);
  w32(ctl,88,0);w32(ctl,92,32);ctl[96]=1;w32(ctl,100,112);
  w32(ctl,112,0);w32(ctl,116,16);w32(ctl,120,1);
  for(unsigned i=0;i<samples.size();++i)samples[i]=i;
  for(auto id:{0x87,0x93,0x19e}){w16(table,10*id,1);table[10*id+2]=100;table[10*id+4]=95;table[10*id+8]=63;}
  for(unsigned id=0;id<640;++id)if(character_race_cue(id,0)>=0) {
   w16(table,10*id,1);table[10*id+2]=100;table[10*id+3]=17;table[10*id+4]=95;
   w16(table,10*id+6,15000);table[10*id+8]=63;
  }
 }
 CharacterAudio prepare()const{return prepare_character_audio(ctl,samples,table,0);}
};
#if defined(DKR_TEST_AUDIO_GUEST)
void native(const CharacterAudio& audio) {
 Bytes memory(8*MiB);CharacterMenuFields fields{};
 for(unsigned i=0;i<fields.size();++i)fields[i]=0x80001000+0x100*i;
 CharacterMenuMemory g(memory,fields);
 constexpr unsigned base=0x80060000,samples=0x18000000;
 g.bytes(base,audio.control);
 recomp_context c{};c.r4=std::int32_t(base);c.r5=samples;c.r29=std::int32_t(0x807e0000);c.r16=123;c.r31=456;
 alBnkfNew(memory.data(),&c);
 check(c.r29==gpr(std::int32_t(0x807e0000)) && c.r16==123 && c.r31==456);
 const auto bank=be32(audio.control,4),inst=be32(audio.control,bank+12);
 check(g.read(base+4)==base+bank && g.read(base+bank+12)==base+inst);
 check(g.read(base+bank+2,1)==1 && g.read(base+inst+3,1)==1);
 for(unsigned i=0;i<be16(audio.control,inst+14);++i) {
  const auto sound=be32(audio.control,inst+16+4*i),wave=be32(audio.control,sound+8);
  check(g.read(base+inst+16+4*i)==base+sound);
  for(unsigned field:{0U,4U,8U})check(g.read(base+sound+field)==base+be32(audio.control,sound+field));
  check(g.read(base+wave)==samples+be32(audio.control,wave));
  check(g.read(base+wave+9,1)==1 && g.read(base+sound+14,1)==1);
  if(be32(audio.control,wave+12))check(g.read(base+wave+12)==base+be32(audio.control,wave+12));
  if(!audio.control[wave+8])check(g.read(base+wave+16)==base+be32(audio.control,wave+16));
 }
}
#endif
}
int main(int argc,char** argv){try{
 Fixture f;const auto closed=f.prepare();validate_character_audio(closed);
 check(closed.cues[0]==closed.cues[1] && closed.cues[1]==closed.cues[2]);
 check(closed.samples.size()==48 && closed.control.size()<f.ctl.size());
 check(character_audio_identity(closed).size()==64);
 const auto race=prepare_character_race_audio(f.ctl,f.samples,f.table,0);validate_character_race_audio(race);
 for(const auto& cue:race.cues)check(cue.sound==1 && cue.min_volume==17 && cue.range==15000 && cue.pitch==95);
 for(unsigned base=0;base<10;++base) {
  check(character_race_cue(0x156+base,base)==16);
  check(character_race_cue(0x7b+base,base)==17);
  for(unsigned i=0;i<8;++i){check(character_race_cue(0x162+i*12+base,base)==int(i));check(character_race_cue(0x1c2+i*12+base,base)==int(i+8));}
 }
 for(unsigned id=0;id<65536;++id) {
  unsigned character=999,cue=999;const bool encoded=decode_character_race_sound(id,character,cue);
  check(encoded==(id>=CharacterSoundBegin && (id&31)<18));
  if(encoded)check(character_race_sound(character,cue)==id);
 }
 rejects([&]{character_race_sound(MaxEnabledCharacters,0);});
 unsigned outside=0,cue_outside=0;check(!decode_character_race_sound(0x10000,outside,cue_outside));
 auto bad_race=race;bad_race.cues[16].sound=1000;rejects([&]{validate_character_race_audio(bad_race);});
 bad_race=race;bad_race.samples.push_back(0);rejects([&]{validate_character_race_audio(bad_race);});
 bad_race=race;bad_race.cues[0].range=65536;rejects([&]{validate_character_race_audio(bad_race);});
 auto bad=closed;bad.control.push_back(0);rejects([&]{validate_character_audio(bad);});
 bad=closed;bad.samples.push_back(0);rejects([&]{validate_character_audio(bad);});
 bad=closed;bad.cues[0].sound=500;rejects([&]{validate_character_audio(bad);});
 for(unsigned length=0;length<124;++length){auto short_bank=f;short_bank.ctl.resize(length);rejects([&]{short_bank.prepare();});}
 auto broken=f;broken.ctl[80]=1;rejects([&]{broken.prepare();}); // self-linked voice
 broken=f;w32(broken.ctl,56,0xfffffff0);rejects([&]{broken.prepare();});
 broken=f;w32(broken.ctl,92,33);rejects([&]{broken.prepare();});
 broken=f;w32(broken.ctl,116,17);rejects([&]{broken.prepare();}); // out-of-sample loop
 broken=f;w32(broken.ctl,112,17);rejects([&]{broken.prepare();});
 broken=f;broken.ctl[97]=1;rejects([&]{broken.prepare();}); // relocated source
 broken=f;broken.ctl[96]=3;rejects([&]{broken.prepare();});
 broken=f;broken.table[10*0x87+2]=255;rejects([&]{broken.prepare();});
 // ADPCM dependency closure: one 9-byte block -> 16 decoded samples.
 Fixture adpcm;adpcm.ctl.resize(224);adpcm.ctl[96]=0;w32(adpcm.ctl,92,9);
 w32(adpcm.ctl,104,160);w32(adpcm.ctl,160,1);w32(adpcm.ctl,164,1);
 validate_character_audio(adpcm.prepare());++checks;
#if defined(DKR_TEST_AUDIO_GUEST)
 native(closed);native(adpcm.prepare());
 if(argc>2) {
  auto original=read_file(utf8_path(argv[1]),MaxImage);canonicalize_rom(original);
  for(int i=2;i<argc;++i)for(const auto& patch:read_patch_inputs(utf8_path(argv[i]))) {
   const auto target=decode_patch(original,patch.data);
   const auto digest=sha256(patch.data);
   const auto analysis=analyze(original,target,digest);
   for(const auto& root:analysis.character_roots) {
    const auto character=prepare_character(original,target,digest,root.base_character);native(character.audio);
    validate_character_race_audio(character.race_audio);
    // Use the same native B1 relocator for the entire race closure too.
    native({character.race_audio.control,character.race_audio.samples,{}});
   }
  }
 }
#endif
 w32(adpcm.ctl,116,17);rejects([&]{adpcm.prepare();});
 w32(adpcm.ctl,116,16);w32(adpcm.ctl,160,0);rejects([&]{adpcm.prepare();});
 std::cout<<checks<<" selection audio closure/safety checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
