// Execute the unchanged native result, RNG/gating, and spatial sound paths
// with project-owned Patch Pipeline callbacks on both reviewed ROM revisions.
#include "legacy_character_presentation.hpp"
#include <algorithm>
#include <iostream>
using namespace dkr::mods;
extern "C" {
void postrace_load(std::uint8_t*,recomp_context*);
void results_render(std::uint8_t*,recomp_context*);
void func_80098774(std::uint8_t*,recomp_context*);
void play_random_character_voice(std::uint8_t*,recomp_context*);
void racer_play_sound(std::uint8_t*,recomp_context*);
void sound_play_direct(std::uint8_t*,recomp_context*);
void hud_eggs_portrait(std::uint8_t*,recomp_context*);
void hud_lives_render(std::uint8_t*,recomp_context*);
void hud_treasure(std::uint8_t*,recomp_context*);
void hud_main_hub(std::uint8_t*,recomp_context*);
void hud_element_render(std::uint8_t*,recomp_context*);
void menu_cinematic_loop(std::uint8_t*,recomp_context*);
}
namespace {
Bytes memory(8*MiB);CharacterMenuFields fields=[] {CharacterMenuFields f;f.fill(0x80000000);return f;}();
CharacterMenuMemory g(memory,fields);unsigned checks=0,next=0x80400000,random_value=0,random_calls=0,points=0,plays=0;
unsigned last_sound=0,last_priority=0,last_bank=0,last_handle=0,echoes=0,lookup_calls=0,random_begin=0,random_end=0;
std::vector<unsigned> drawn;
bool trial=false,hud_mode=false,fail_hud_asset=false;unsigned hud_failures=0;struct StopDrawing {};
constexpr unsigned Settings=0x80300000,Object=0x80310000,Racer=0x80320000,Stack=0x807f0000;
#if DKR_TEST_REVISION==77
constexpr unsigned PostRace=0x80126c54,ResultObjects=0x800e0a24,Results=0x800e0bec,Order=0x800e0cec,
 Portraits=0x800e0af0,Players=0x800df4bc,RankCount=0x800e0fe4,IconPositions=0x800e14bc,RankIDs=0x80126430,
 ResultIDs=0x80126428,Trophy=0x800e1048,DisplayList=0x801263a0,HudMat=0x801263a8,MenuText=0x800df4a0,SpatialTable=0x80119c40;
constexpr unsigned CurrentHud=2148691164,HudIds=2148691184,HudCache=2148691188,HudStale=2148691160,
 HudColour=2148411444,HudPal=2148691157,HudPlayers=2148691255,HudSprites=2148691328,HudSpriteCount=2148692352;
constexpr unsigned CinematicList=0x80126804;
#else
constexpr unsigned PostRace=0x80127230,ResultObjects=0x800e0fa4,Results=0x800e116c,Order=0x800e126c,
 Portraits=0x800e1070,Players=0x800dfa3c,RankCount=0x800e1564,IconPositions=0x800e1a3c,RankIDs=0x801269d0,
 ResultIDs=0x801269c8,Trophy=0x800e15c8,DisplayList=0x8012693c,HudMat=0x80126944,MenuText=0x800dfa20,SpatialTable=0x8011a1c0;
constexpr unsigned CurrentHud=2148692636,HudIds=2148692656,HudCache=2148692660,HudStale=2148692632,
 HudColour=2148412852,HudPal=2148692629,HudPlayers=2148692727,HudSprites=2148692800,HudSpriteCount=2148693824;
constexpr unsigned CinematicList=0x80126da4;
#endif
void check(bool b,const char* why){++checks;if(!b)throw Error(why);}
gpr ptr(unsigned v){return std::int32_t(v);}
recomp_context context(){recomp_context c{};c.r29=ptr(Stack);return c;}
std::shared_ptr<CharacterNamespace> assets=std::make_shared<CharacterNamespace>();
std::unique_ptr<CharacterRoster> roster;CharacterPresentation presentation;
void allocate(std::uint8_t*,recomp_context* c){c->r2=ptr(next);next+=(unsigned(c->r4)+15)&~15U;}
void texture(std::uint8_t*,recomp_context* c){c->r2=ptr(0x80380000+unsigned(c->r4)*32);g.write(unsigned(c->r2),0x28280000);}
void relocate(std::uint8_t*,recomp_context* c){g.write(unsigned(c->r4)+4,unsigned(c->r4)+8);}
void bank_play(std::uint8_t*,recomp_context* c){++plays;last_bank=unsigned(c->r4);last_sound=unsigned(c->r5);last_priority=unsigned(c->r6);
 last_handle=unsigned(c->r7);if(last_handle)g.write(last_handle,0x80370000);}
void parameter(std::uint8_t*,recomp_context*){}
void point(std::uint8_t*,recomp_context* c){++points;last_sound=unsigned(c->r4);last_handle=g.read(unsigned(c->r29)+44);
 check(g.read(unsigned(c->r29)+16)==4,"Native voice lost its one-time-trigger flag");
 if(last_handle)g.write(last_handle,0x80360000);}
CharacterPresentationCalls calls{allocate,texture,relocate,bank_play,parameter,point};
unsigned expected_portrait(unsigned racer){const auto cell=presentation.portrait(*roster,assets->characters,racer);return cell?g.read(cell):0x80350050;}
}
extern "C" std::uint32_t dkr_legacy_character_portrait_lookup(std::uint8_t*,recomp_context*,std::uint32_t base) {
 ++lookup_calls;check(base>=Settings && (base-Settings)%24==0,"Portrait hook passed the wrong racer register");
 return presentation.portrait(*roster,assets->characters,(base-Settings)/24);
}
extern "C" unsigned dkr_legacy_character_race_sound(std::uint8_t*,recomp_context*,std::uint32_t racer,unsigned id) {
 check(racer==Racer,"Native voice hook lost its racer pointer");
 return presentation.sound(*roster,assets->characters,g.read(racer,2),g.read(racer+3,1),id);
}
extern "C" int dkr_legacy_character_play_sound(std::uint8_t*,recomp_context* c,unsigned kind) {
 return presentation.play(memory,*c,calls,assets->characters,kind);
}
extern "C" int dkr_legacy_character_menu(std::uint8_t*,recomp_context*,unsigned event,const std::uint32_t*) {
 check(event==8,"Unexpected selection-sound event");return 0;
}
extern "C" void dkr_legacy_character_hud_bind(std::uint8_t*,recomp_context* c,unsigned hud,unsigned racer) {
 presentation.bind_hud(unsigned(c->r29),hud,*roster,assets->characters,racer<4?racer:g.read(racer,2));
}
extern "C" void dkr_legacy_character_hud_unbind(std::uint8_t*,recomp_context* c){presentation.unbind_hud(unsigned(c->r29));}
extern "C" unsigned dkr_legacy_character_hud_lookup(std::uint8_t*,recomp_context*,unsigned stack,unsigned hud) {
 return presentation.hud_lookup(stack,hud,g.read(hud+6,2));
}
extern "C" unsigned dkr_legacy_character_cinematic_portrait(std::uint8_t*,recomp_context*,unsigned id) {
 return presentation.cinematic_portrait(id);
}
extern "C" void input_pressed(std::uint8_t*,recomp_context* c){c->r2=0;}
extern "C" void func_800214C4(std::uint8_t*,recomp_context* c){c->r2=0;}
extern "C" void get_settings(std::uint8_t*,recomp_context* c){c->r2=ptr(Settings);}
extern "C" void is_time_trial_enabled(std::uint8_t*,recomp_context* c){c->r2=trial;}
extern "C" void is_in_two_player_adventure(std::uint8_t*,recomp_context* c){c->r2=hud_mode;}
extern "C" void menu_racer_portraits(std::uint8_t*,recomp_context*){for(unsigned i=0;i<10;++i)g.write(Portraits+i*4,0x80350000+16*i);}
extern "C" void texrect_draw(std::uint8_t*,recomp_context* c){drawn.push_back(hud_mode?g.read(unsigned(c->r5)):unsigned(c->r5));if(!hud_mode && drawn.size()==4)throw StopDrawing{};}
extern "C" void texrect_draw_scaled(std::uint8_t*,recomp_context* c){drawn.push_back(g.read(unsigned(c->r5)));}
extern "C" void cam_get_viewport_layout(std::uint8_t*,recomp_context* c){c->r2=0;}
extern "C" void load_texture(std::uint8_t* m,recomp_context* c){if(fail_hud_asset)c->r2=0;else texture(m,c);}
extern "C" void object_model_init(std::uint8_t*,recomp_context* c){c->r2=0;}
extern "C" void spawn_object(std::uint8_t*,recomp_context* c){c->r2=0;}
extern "C" void tex_load_sprite(std::uint8_t*,recomp_context* c){c->r2=fail_hud_asset?0:ptr(0x80214000);}
extern "C" void dkr_hud_asset_load_failed(std::uint8_t*,recomp_context*){++hud_failures;}
extern "C" void render_ortho_triangle_image(std::uint8_t*,recomp_context* c){drawn.push_back(unsigned(c->r7));}
extern "C" void cam_get_active_camera(std::uint8_t*,recomp_context* c){c->r2=ptr(0x80218000);}
extern "C" void rand_range(std::uint8_t*,recomp_context* c){++random_calls;c->r2=random_value;}
extern "C" void sound_count(std::uint8_t*,recomp_context* c){c->r2=1023;}
extern "C" void sndp_play(std::uint8_t*,recomp_context* c){++plays;last_sound=unsigned(c->r5);last_handle=unsigned(c->r6);}
extern "C" void sndp_play_with_priority(std::uint8_t* m,recomp_context* c){bank_play(m,c);}
extern "C" void sndp_set_param(std::uint8_t*,recomp_context*){}
extern "C" void audspat_point_create(std::uint8_t* m,recomp_context* c){point(m,c);}
extern "C" void audspat_point_stop(std::uint8_t*,recomp_context*){}
extern "C" void audspat_calculate_echo(std::uint8_t*,recomp_context*){++echoes;}
extern "C" void dkr_netplay_presentation_random_begin(std::uint8_t*,recomp_context*){++random_begin;}
extern "C" void dkr_netplay_presentation_random_end(std::uint8_t*,recomp_context*){++random_end;}
#define NOOP(name) extern "C" void name(std::uint8_t*,recomp_context*){}
NOOP(menu_asset_load) NOOP(menu_imagegroup_load) NOOP(cam_set_sprite_anim_mode)
NOOP(clear_dialogue_box_open_flag) NOOP(dialogue_clear) NOOP(draw_text) NOOP(menu_element_render)
NOOP(mtx_ortho) NOOP(open_dialogue_box) NOOP(render_dialogue_text) NOOP(rendermode_reset)
NOOP(set_current_dialogue_background_colour) NOOP(set_current_dialogue_box_coords)
NOOP(set_current_text_background_colour) NOOP(set_current_text_colour) NOOP(set_dialogue_font)
NOOP(set_text_background_colour) NOOP(set_text_colour) NOOP(set_text_font) NOOP(sprite_opaque)
NOOP(stubbed_printf)
NOOP(hud_balloons) NOOP(hud_speedometre) NOOP(rdp_init)
NOOP(hud_draw_model) NOOP(mtx_cam_push) NOOP(mtx_pop) NOOP(render_object)
NOOP(dkr_hud_element_begin) NOOP(dkr_hud_element_end)
NOOP(cinematic_free) NOOP(load_level_for_menu) NOOP(music_change_off)
extern "C" void do_break(std::uint32_t){throw Error("Native fixture reached an arithmetic break");}
int main(){try {
 for(unsigned i=0;i<2;++i){AllocatedCharacter a;a.id=std::string(64,'a'+i);a.base_character=3;a.portrait=906+i;a.race_audio.control=Bytes(32);
  for(unsigned c=0;c<18;++c)a.race_audio.cues[c]={{c+1,100,100,63},10,15000};assets->characters.push_back(a);}
 roster=std::make_unique<CharacterRoster>(assets);roster->request(0,assets->characters[0].id);roster->request(1,assets->characters[1].id);
 roster->commit(std::array<std::uint8_t,4>{3,3,3,3},4);auto c=context();
 presentation.initialize(memory,c,calls,assets->characters,std::array<std::uint32_t,2>{0x18000000,0x19000000});
 for(unsigned i=0;i<2;++i)presentation.prepare(memory,c,calls,assets->characters,i,0x18000000+i*0x1000000,true);
 for(unsigned i=0;i<8;++i){g.write(Settings+0x59+24*i,5,1);g.write(Settings+0x5a+24*i,i,1);}
 for(unsigned selected=0;selected<4;++selected)for(bool time_trial:{false,true}) {
  trial=time_trial;g.write(Settings+0x114,selected,1);g.write(PostRace,10);g.write(ResultObjects,0xffff,2);
  c=context();postrace_load(memory.data(),&c);
  check(g.read(Results+20)==expected_portrait(selected),"Post-race primary portrait belongs to the donor");
  if(!trial)for(unsigned i=0;i<8;++i)check(g.read(Order+(7-i)*32+20)==expected_portrait(i),"Finishing order lost a logical portrait");
 }
 g.write(Players,4);g.write(DisplayList,0x803a0000);g.write(HudMat,0x803b0000);g.write(MenuText,0x80340000);
 g.write(0x80000300,1);c=context();c.f_odd=&c.f0.u32h;
 try{results_render(memory.data(),&c);}catch(const StopDrawing&){}
 check(drawn.size()==4,"Results screen did not draw all human portraits");
 for(unsigned i=0;i<4;++i)check(drawn[i]==expected_portrait(i),"Multiplayer results used the donor portrait");
 g.write(RankCount,4);g.write(IconPositions+24,0x80390000);g.write(IconPositions+28,0x80390100);
 for(unsigned i=0;i<24;++i){g.write(0x80390000+i*2,40+i*5,2);g.write(0x80390100+i*2,60+i*3,2);}
 constexpr unsigned ranks[]{3,1,0,2},results[]{1,2,3,0};
 for(unsigned i=0;i<4;++i){g.write(RankIDs+i,ranks[i],1);g.write(ResultIDs+i,results[i],1);}
 for(unsigned rankings=0;rankings<2;++rankings){c=context();c.r4=rankings;func_80098774(memory.data(),&c);
  for(unsigned i=0;i<4;++i)check(g.read(Trophy+(2+3*i)*32+20)==expected_portrait(rankings?ranks[i]:results[i]),"Trophy rankings lost per-racer identity");}
 check(lookup_calls>=40,"Not all native portrait hooks executed");
 // Trophy cinematic keeps logical custom identity in its transient list,
 // even when two custom racers share a donor with a stock racer.
 drawn.clear();g.write(CinematicList,0x803d0000);
 for(unsigned i=0;i<4;++i)g.write(0x803d0000+i,presentation.cinematic_id(*roster,assets->characters,i,5),1);
 g.write(0x803d0004,255,1);c=context();
 try{menu_cinematic_loop(memory.data(),&c);}catch(const StopDrawing&){}
 check(drawn.size()==4,"Cinematic omitted a portrait");
 for(unsigned i=0;i<4;++i)check(drawn[i]==expected_portrait(i),"Cinematic used the donor portrait");
 // Native voice selection still owns the RNG and stores the unencoded last
 // sound ID for its repeat suppression. Test every voice variant and side.
 g.write(SpatialTable,0x803c0000); // Stock control racers still use the native sound table.
 g.write(Object+0x64,Racer);g.write(Racer+3,3,1);
 for(unsigned player=0;player<4;++player)for(unsigned negative=0;negative<2;++negative)for(unsigned variant=0;variant<8;++variant) {
  g.write(Racer,player,2);g.write(Racer+0x24,0);g.write(Racer+0x28,0,2);g.write(Racer+0x108,0);
  random_value=variant;c=context();c.r4=ptr(Object);c.r5=negative?0x1c2:0x162;c.r6=8;c.r7=0;const auto before=points;
  play_random_character_voice(memory.data(),&c);const auto native=(negative?0x1c2:0x162)+3+12*variant;
  check(points==before+1,"Native custom voice failed to create one point");
  check(last_sound==(player<2?character_race_sound(player,negative*8+variant):0),"Voice mapped to wrong private cue/bank");
  check(g.read(Racer+0x28,2)==native,"Encoded voice corrupted native lastSoundID repeat suppression");
  check(last_handle==Racer+0x24,"Native AudioPoint handle ownership changed");
  if(player<2){c=context();c.r4=last_sound;c.r5=ptr(0x80330000);sound_play_direct(memory.data(),&c);
   check(last_sound==negative*8+variant+1 && last_priority==0,"Spatial playback did not reach the custom bank");}
 }
 check(random_begin==random_end && random_calls==64,"Voice wrapper changed native RNG boundaries");
 for(unsigned player=0;player<2;++player){g.write(Racer,player,2);g.write(Racer+0x108,0);c=context();c.r4=ptr(Object);c.r5=0x159;
  racer_play_sound(memory.data(),&c);check(last_sound==17 && last_priority==63,"Horn did not reach private character bank");}
 check(echoes==2,"Custom horn skipped native spatial echo calculation");
 // Exit-control and already-playing masks must continue to suppress new audio.
 const auto before=points;g.write(Racer+0x108,1);c=context();c.r4=ptr(Object);c.r5=0x162;c.r6=8;play_random_character_voice(memory.data(),&c);
 check(points==before,"Exit-controlled racer emitted a custom voice");
 g.write(Racer+0x108,0);g.write(Racer+0x24,1);c=context();c.r4=ptr(Object);c.r5=0x162;c.r6=8;play_random_character_voice(memory.data(),&c);
 check(points==before,"Custom voice bypassed the existing active-sound mask");
 // Execute all four HUD callers and the actual texture renderer, including
 // its queued/unit-scale, tinted, arbitrary-scale and PAL branches.
 hud_mode=true;constexpr unsigned Hud=0x80200000,Cache=0x80210000,Ids=0x80211000,StockTexture=0x80213000;
 g.write(CurrentHud,Hud);g.write(HudIds,Ids);g.write(HudCache,Cache);g.write(HudStale,0x80212000);
 g.write(Ids+59*2,0xc000|126,2);g.write(Cache+59*4,StockTexture);g.write(StockTexture,0x28280000);
 g.write(HudPlayers,1,1);g.write(Settings+0x71,3,1);
 for(auto offset:{0x640U,0x720U}){g.write(Hud+offset+8,0x3f800000);g.write(Hud+offset+12,0x42c80000);g.write(Hud+offset+16,0x42480000);}
 for(unsigned player=0;player<4;++player)for(unsigned variant=0;variant<4;++variant) {
  g.write(Racer,player,2);g.write(Racer+3,3,1);
  for(auto fn:{hud_eggs_portrait,hud_lives_render,hud_treasure,hud_main_hub}) {
   g.write(HudColour,variant==0?0xfffffffe:0xffffffff);g.write(HudPal,variant==3,1);
   g.write(0x80000300,variant==3?0:1);g.write(Hud+0x648,variant==2?0x3f000000:0x3f800000);
   drawn.clear();g.write(HudSpriteCount,0);c=context();c.f_odd=&c.f0.u32h;c.r4=ptr(fn==hud_main_hub?Object:Racer);c.r5=1;
   fn(memory.data(),&c);
   const auto owner=fn==hud_main_hub?1:player;const auto cell=presentation.portrait(*roster,assets->characters,owner);
   const auto expected=cell?g.read(g.read(cell)):StockTexture;
   if(variant==0)check(g.read(HudSprites)==expected,"Queued HUD portrait used donor texture");
   else {
    if(drawn.empty() || drawn.front()!=expected)std::cerr<<"HUD case player="<<player<<" variant="<<variant<<" function="<<(fn==hud_eggs_portrait?"eggs":fn==hud_lives_render?"lives":fn==hud_treasure?"treasure":"hub")<<" expected="<<std::hex<<expected<<" actual="<<(drawn.empty()?0:drawn.front())<<std::dec<<'\n';
    check(!drawn.empty() && drawn.front()==expected,"Native HUD portrait used donor texture");
   }
   check(g.read(Cache+59*4)==StockTexture,"Custom HUD replaced the stock donor cache");
  }
 }
 // Non-portrait HUD assets must keep their identity and coordinates after
 // custom portrait draws, including a failed load followed by a successful retry.
 for(unsigned element:{0U,12U,13U})for(bool failed_first:{false,true})for(bool scaled:{false,true}) {
  const unsigned hud=Hud+element*32,asset=element==0?1:element;
  g.write(hud+6,asset,2);g.write(hud+8,scaled?0x3fc00000:0x3f800000);
  g.write(hud+12,0x42480000);g.write(hud+16,0x42700000);g.write(hud+24,0,2);
  g.write(Ids+asset*2,(element==0?0xc000:0x8000)|126,2);g.write(Cache+asset*4,0);
  g.write(HudColour,0xffffffff);g.write(HudPal,0,1);g.write(0x80000300,1);
  auto draw=[&] {c=context();c.f_odd=&c.f0.u32h;c.r4=ptr(DisplayList);c.r5=ptr(HudMat);c.r6=ptr(HudMat+4);c.r7=ptr(hud);
   hud_element_render(memory.data(),&c);check(unsigned(c.r29)==Stack,"HUD draw changed caller stack");};
  const auto failures=hud_failures;drawn.clear();fail_hud_asset=failed_first;
  if(failed_first){draw();check(drawn.empty() && hud_failures==failures+1,"Failed HUD asset was not diagnosed");}
  fail_hud_asset=false;draw();
  check(drawn.size()==1,"Ready/Go/position failed to draw after custom portrait or allocation retry");
  check(g.read(hud+12)==0x42480000 && g.read(hud+16)==0x42700000,"HUD draw leaked a coordinate adjustment");
  check(g.read(Cache+asset*4)!=0,"HUD asset retry was not cached");
 }
 // Exercise the high u16 token range through the actual native voice entry,
 // spatial wrapper and direct playback, not only a synthetic encoder.
 for(unsigned i=2;i<MaxEnabledCharacters;++i) {
  auto a=assets->characters[1];const auto label=std::to_string(i);
  a.id=sha256(View(reinterpret_cast<const std::uint8_t*>(label.data()),label.size()));assets->characters.push_back(std::move(a));
 }
 roster=std::make_unique<CharacterRoster>(assets);roster->request(0,assets->characters.back().id);
 roster->commit(std::array<std::uint8_t,1>{3},1);
 std::vector<std::uint32_t> samples(assets->characters.size(),0x18000000);
 presentation=CharacterPresentation{};c=context();presentation.initialize(memory,c,calls,assets->characters,samples);
 presentation.prepare(memory,c,calls,assets->characters,MaxEnabledCharacters-1,samples.back(),true);
 g.write(Racer,0,2);g.write(Racer+0x24,0);g.write(Racer+0x28,0,2);g.write(Racer+0x108,0);random_value=7;
 c=context();c.r4=ptr(Object);c.r5=0x1c2;c.r6=8;c.r7=0;play_random_character_voice(memory.data(),&c);
 const auto token=character_race_sound(MaxEnabledCharacters-1,15);
 check(last_sound==token && token>0x8000,"High library ordinal was truncated by native spatial voice dispatch");
 g.write(0x80330000,token,2);c=context();c.r4=g.read(0x80330000,2);c.r5=ptr(0x80330004);sound_play_direct(memory.data(),&c);
 check(last_sound==16,"Queued high u16 sound token did not reach its private voice bank");
 check(presentation.cinematic_id(*roster,assets->characters,0,3)==64,"Cinematic token leaked a large library ordinal into a byte");
 check(presentation.cinematic_portrait(64)==presentation.portrait(*roster,assets->characters,0),"Large library lost its cinematic portrait");
 const auto saved=presentation.checkpoint();check(presentation.stage_checkpoint(saved,assets->characters.size()).checkpoint()==saved,"Sparse guest presentation lost rollback ownership");
 std::cout<<checks<<" native portrait/voice/HUD pipeline checks passed (v"<<DKR_TEST_REVISION<<").\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
