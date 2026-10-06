// Executes retail race construction up to its primary spawn. This is a
// headless ABI check, not a gameplay or visual qualification.
#include "legacy_runtime_character.hpp"
#include "legacy_character_menu.hpp"
#include <algorithm>
#include <iostream>
using namespace dkr::mods;
extern "C" void track_setup_racers(std::uint8_t*,recomp_context*);
extern "C" void switch_error(const char*,std::uint32_t,std::uint32_t){throw Error("Unexpected native race-setup switch.");}
namespace {
Bytes memory(8*MiB);CharacterMenuFields fields=[] {CharacterMenuFields f;f.fill(0x80000000);return f;}();
CharacterMenuMemory g(memory,fields);CharacterAiState ai;std::unique_ptr<CharacterRoster> roster;
unsigned checks=0,mode=0,type=0,draws=0;bool time_trial=false,stopped=false,null_spawn=false,null_guard=false;
constexpr unsigned Settings=0x80300000,Header=0x80310000,Entry=0x80320000,Stack=0x807f0000;
constexpr std::array<std::uint8_t,10> Stock{0,9,1,5,3,2,7,4,6,8};
#if DKR_TEST_REVISION==77
constexpr unsigned ObjectCount=0x8011ae5c,TrialEnabled=0x8011aef4;
#else
constexpr unsigned ObjectCount=0x8011b3dc,TrialEnabled=0x8011b474;
#endif
struct Stop {};
void check(bool ok,const char* why){++checks;if(!ok)throw Error(why);}
gpr ptr(unsigned value){return std::int32_t(value);}
}
extern "C" void rand_range(std::uint8_t*,recomp_context* c){
 ++draws;check(c->r4==0 && c->r5<26 && unsigned(c->r29)==Stack-0x150-0x100,"Actual native AI RNG ABI mismatch.");c->r2=c->r5;
}
extern "C" void dkr_legacy_character_ai_event(std::uint8_t*,recomp_context* c,unsigned event,OriginalPiHandler random){
 dispatch_character_ai_event(*roster,ai,memory,*c,event,Stock,random);
 if(event==2){
  stopped=true;check(unsigned(c->r22)==Entry,"Actual native AI entry register changed.");
  const bool admitted=mode!=1 && ((type==0 && !time_trial) || (type>=64 && type<=66));
  if(admitted){
   const unsigned index=type==0?7:0;check(unsigned(c->r20)==index,"AI uses spawn order instead of participant identity.");
   check(ai.racers==(type==0?8:4) && draws==(type==0?7:3),"Native topology was not finalized before AI selection.");
   if(index==7)check(ai.spawn && ai.spawn->second==7 && g.read(Entry+14,2)==4,"Primary AI identity scope missing.");
   else check(!ai.spawn && g.read(Entry+14,2)==0,"Human spawn was tagged as AI.");
  }else check(!ai.racers && !ai.spawn && !draws,"Excluded native scene received custom AI.");
  if(!null_spawn)throw Stop{};
 }
}
extern "C" void level_header(std::uint8_t*,recomp_context* c){c->r2=ptr(Header);}
extern "C" void get_settings(std::uint8_t*,recomp_context* c){c->r2=ptr(Settings);}
extern "C" void get_game_mode(std::uint8_t*,recomp_context* c){c->r2=mode;}
extern "C" void get_misc_asset(std::uint8_t*,recomp_context* c){c->r2=ptr(Header+0x200);}
extern "C" void is_time_trial_enabled(std::uint8_t*,recomp_context* c){c->r2=time_trial;}
extern "C" void race_is_adventure_2P(std::uint8_t*,recomp_context* c){c->r2=0;}
extern "C" void level_properties_get(std::uint8_t*,recomp_context* c){c->r2=1;}
extern "C" void get_multiplayer_racer_count(std::uint8_t*,recomp_context* c){c->r2=8;}
extern "C" void get_player_selected_vehicle(std::uint8_t*,recomp_context* c){c->r2=0;}
extern "C" void mempool_alloc_safe(std::uint8_t*,recomp_context* c){check(c->r4==16,"Native AI entry allocation changed.");c->r2=ptr(Entry);}
extern "C" void cam_get_cameras(std::uint8_t*,recomp_context* c){c->r2=ptr(Header+0x400);}
#define NOOP(name) extern "C" void name(std::uint8_t*,recomp_context*){}
NOOP(set_taj_status) NOOP(model_anim_offset) NOOP(set_scene_viewport_num)
#define UNREACHED(name) extern "C" void name(std::uint8_t*,recomp_context*){throw Error("Past primary spawn boundary: " #name);}
extern "C" void spawn_object(std::uint8_t*,recomp_context* c){check(null_spawn,"Unexpected spawn fixture");c->r2=0;}
extern "C" void dkr_legacy_racer_spawn_failed(std::uint8_t*,recomp_context* c){check(c->r2==0,"Wrong null-spawn guard ABI");null_guard=true;throw Stop{};}
UNREACHED(free_object) UNREACHED(get_filtered_cheats) UNREACHED(get_save_file_index)
UNREACHED(is_in_adventure_two) UNREACHED(is_in_tracks_mode) UNREACHED(level_id) UNREACHED(level_music_start)
UNREACHED(mempool_free_timer) UNREACHED(mempool_free) UNREACHED(racer_sound_init) UNREACHED(racer_special_init)
UNREACHED(racetype_demo) UNREACHED(rumble_init) UNREACHED(safe_mark_write_save_file)
UNREACHED(set_next_taj_challenge_menu) UNREACHED(set_taj_voice_line) UNREACHED(timetrial_free_staff_ghost)
UNREACHED(timetrial_init_player_ghost) UNREACHED(timetrial_init_staff_ghost) UNREACHED(timetrial_reset_player_ghost)
UNREACHED(timetrial_valid_player_ghost) UNREACHED(update_player_racer)
int main(){try{
 auto assets=std::make_shared<CharacterNamespace>();
 for(unsigned i=0;i<16;++i){AllocatedCharacter a;a.id=std::string(64,'a'+i);a.base_character=i%10;assets->characters.push_back(a);}
 roster=std::make_unique<CharacterRoster>(assets);roster->commit(std::array<std::uint8_t,1>{0},1);
 for(bool failure:{false,true})for(auto m:{0U,1U})for(auto t:{0U,5U,8U,64U,65U,66U})for(bool trial:{false,true}){
  null_spawn=failure;null_guard=false;
  std::fill(memory.begin(),memory.end(),0);mode=m;type=t;time_trial=trial;draws=0;stopped=false;
  g.write(Header+0x4c,type,1);g.write(Header+0x4d,0,1);g.write(Settings+4,Header+0x300);
  g.write(ObjectCount,0);g.write(TrialEnabled,trial,1);
  for(unsigned i=0;i<8;++i){g.write(Settings+0x59+24*i,i,1);g.write(Settings+0x5a+24*i,(i+1)%8,1);}
  recomp_context c{};c.r4=0;c.r5=0;c.r6=0;c.r29=ptr(Stack);c.f_odd=&c.f0.u32h;
  try{track_setup_racers(memory.data(),&c);}catch(const Stop&){}
  check(stopped,"Retail setup never reached checked primary spawn.");
  check(null_guard==failure,"Failed native spawn reached an unguarded guest write");
  check(g.read(Settings+0x59,1)==0,"Native human character was overwritten by AI selection.");
 }
 std::cout<<checks<<" retail race AI ABI checks passed (v"<<DKR_TEST_REVISION<<").\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
