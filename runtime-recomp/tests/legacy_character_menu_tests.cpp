#include "legacy_character_stage.hpp"
#include "legacy_character_menu_render.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <cstring>
#include <cmath>
#include <limits>
#include <numbers>
#include "../src/game/character_select_animation_policy.hpp"
using namespace dkr::mods;
namespace {
unsigned checks=0;void check(bool ok,const char* why){++checks;if(!ok)throw Error(why);}
template<class F>void rejects(F fn){bool caught=false;try{fn();}catch(const Error&){caught=true;}check(caught,"Unsafe stage input accepted.");}
using F=CharacterMenuField;CharacterMenuFields fields;Bytes memory(1024*1024);
std::shared_ptr<CharacterNamespace> library() {
 auto a=std::make_shared<CharacterNamespace>();
 a->characters={{std::string(64,'a'),"Haunter",8,906,{304,305,306},310,40},
                {std::string(64,'b'),"Yooka",3,907,{311,312,313},314,170}};
 return a;
}
void initialize(CharacterMenuMemory& g,unsigned players) {
 std::fill(memory.begin(),memory.end(),0);
 for(unsigned p=0;p<4;++p){g.set(F::Active,p<players,p,1);g.set(F::NativeIndices,p,p,1);g.set(F::NativeIDs,p,p,1);}
 g.set(F::Players,players);g.set(F::SelectTable,0x80050000);
 constexpr unsigned voices[]{0,9,1,5,3,2,7,4,6,8};
 for(unsigned i=0;i<10;++i)g.write(0x80050000+14*i+12,voices[i],2);
}
void controls(CharacterMenuMemory& g,unsigned p,int x,int y=0,unsigned buttons=0) {
 for(unsigned i=0;i<4;++i){g.set(F::Buttons,0,i*4);g.set(F::StickX,0,i*2,2);g.set(F::StickY,0,i*2,2);}
 g.set(F::Buttons,buttons,p*4);g.set(F::StickX,x,p*2,2);g.set(F::StickY,y,p*2,2);
}
void selection() {
 CharacterMenuMemory g(memory,fields);auto a=library();CharacterMenuAdapter m(a);
 const auto initial_menu=m.checkpoint();
 check(m.stage_checkpoint(initial_menu).checkpoint()==initial_menu,"Initial character sidecar did not round-trip.");
 for(std::size_t cut=0;cut<initial_menu.size();++cut)rejects([&]{m.stage_checkpoint(View(initial_menu).first(cut));});
 initialize(g,4);g.set(F::NativeIndices,4,0,1);g.set(F::NativeIndices,7,1,1);
 controls(g,0,-1);check(m.input(g,false)==std::vector<unsigned>{0xec},"Left edge navigation lost ping.");
 check(m.custom(0)==0,"Physical left did not reach the additional left actor.");
 check(g.get(F::CurrentMusic,0,1)==8 && g.get(F::CurrentMusic,1,1)==20 && !g.get(F::CurrentMusic,2,2),"Custom selection lost native music fade.");
 controls(g,1,1);m.input(g,false);check(m.custom(1)==1,"Physical right did not reach additional right actor.");
 check(m.custom(0)==0,"P2 movement reset P1.");
 check(g.get(F::NativeIndices,0,1)<8 && g.get(F::NativeIndices,1,1)==4,"Logical IDs leaked into native fields.");
 controls(g,2,0,0,0x8000);const auto before=memory;m.input(g,false);
 check(before==memory,"Native ready/confirmation input was consumed.");
 controls(g,3,0,0,0x10);m.input(g,false);check(!m.custom(3),"Obsolete R-page is still active.");
 // Originals remain selectable despite custom cursor placeholders.
 g.set(F::NativeIndices,5,2,1);controls(g,2,-1);m.input(g,false);
 check(g.get(F::NativeIndices,2,1)==4 && !m.custom(2),"Yooka blocked original Conker.");
 check(g.get(F::CurrentMusic,0,1)==3,"Original Conker lost his music channel.");
 controls(g,0,1);m.input(g,false);check(!m.custom(0) && g.get(F::NativeIndices,0,1)==5,"Right did not leave the left custom actor / skip occupied Conker.");
 g.set(F::NativeIndices,4,0,1);controls(g,0,-1);m.input(g,false);
 for(unsigned p=0;p<4;++p)g.set(F::Status,2,p,1);g.set(F::Ready,4);
 controls(g,0,0,0,0x4000);const auto ready=memory;m.input(g,false);
 check(memory==ready,"Native cancel and audio path was replaced.");
 CharacterRoster roster(a);m.commit(g,roster,4);
 roster.commit(std::array<std::uint8_t,4>{8,3,2,3},4);
 check(roster.active(0)==a->characters[0].id && roster.active(1)==a->characters[1].id && roster.active(2).empty(),"Logical stage identity lost on race entry.");
 const auto menu_bytes=m.checkpoint(),roster_bytes=roster.checkpoint();
 auto saved_menu=m.stage_checkpoint(menu_bytes);auto saved_roster=roster.stage_checkpoint(roster_bytes);
 m.reset();roster.clear_active();m=std::move(saved_menu);roster=std::move(saved_roster);
 check(m.checkpoint()==menu_bytes && roster.checkpoint()==roster_bytes,"Character selection/roster restore lost logical identity.");
 auto corrupt=roster_bytes;corrupt.push_back(0);rejects([&]{roster.stage_checkpoint(corrupt);});
 rejects([&]{m.commit(g,roster,3);});
 for(unsigned unlock=0;unlock<4;++unlock)for(unsigned player=0;player<4;++player) {
  initialize(g,4);m.reset();m.set_unlocks(unlock&1,unlock&2);
  for(unsigned turn=0;turn<80;++turn) {
   const int x=turn%4==0?-1:turn%4==1?1:0;
   const int y=turn%4==2?-1:turn%4==3?1:0;
   controls(g,player,x,y);m.input(g,false);
   for(unsigned p=0;p<4;++p)check(g.get(F::NativeIndices,p,1)<8+unsigned(bool(unlock&1))+unsigned(bool(unlock&2)),"Directional input escaped native index bounds.");
  }
 }
 initialize(g,2);m.reset();g.set(F::NativeIndices,4,0,1);g.set(F::NativeIndices,4,1,1);
 controls(g,0,-1);m.input(g,true);controls(g,1,-1);m.input(g,true);
 check(m.custom(0)==0 && m.custom(1)==0,"DOUBLEVISION lost duplicate custom selection.");
 g.set(F::Active,0,0,1);controls(g,1,0);m.input(g,true);check(!m.custom(0),"Disconnected controller retained custom cursor.");
 g.set(F::Delay,1);controls(g,1,1);const auto delay=memory;m.input(g,true);check(memory==delay,"Launch transition modified by stage input.");
 auto bad=fields;bad[0]=0;rejects([&]{CharacterMenuMemory invalid(memory,bad);});
}
CharacterStage* active_stage=nullptr;unsigned spawned=0,freed=0,particles=0;
bool fail_spawn=false;
float music_phase=0.25f;
unsigned clock_reads=0,clock_overrides=0;
std::uint32_t camera_address=0x80098000;
void spawn(std::uint8_t*,recomp_context* c) {
 CharacterMenuMemory g(memory,fields);auto nested=*c;const auto entry=std::uint32_t(c->r4),sp=std::uint32_t(c->r29);
 nested.r29=std::int32_t(sp-0x68);nested.r4=g.read(entry,1)|((g.read(entry+1,1)&128)<<1);g.write(sp,entry);
 check(active_stage->redirect_spawn(memory,nested),"Reviewed spawn header was not redirected.");
 check(unsigned(nested.r4)>=300 && unsigned(nested.r4)<400,"Wrong isolated stage header.");
 if(fail_spawn){c->r2=0;return;}
 const auto obj=0x80090000+spawned++*0x1000;
 for(unsigned axis=0;axis<3;++axis)g.write(obj+0x0c+axis*4,
  std::bit_cast<unsigned>(float(static_cast<std::int16_t>(g.read(entry+2+axis*2,2)))));
 g.write(obj+0x40,obj+0x100);g.write(obj+0x10c,std::bit_cast<unsigned>(0.28f));g.write(obj+0x64,obj+0x200);
 g.write(obj+0x68,obj+0x300);g.write(obj+0x300,obj+0x400);g.write(obj+0x400,obj+0x500);
 g.write(obj+0x538,obj+0x600);g.write(obj+0x528,2,2);g.write(obj+0x548,2,2);
 g.write(obj+0x600,0,1);g.write(obj+0x60c,4,1);g.write(obj+0x544,obj+0x700);
 g.write(obj+0x704,10);g.write(obj+0x70c,20);
 c->r2=std::int32_t(obj);
}
void free_object(std::uint8_t*,recomp_context*){++freed;}
void fraction(std::uint8_t*,recomp_context* c){
 check(c->f_odd==(c->mips3_float_mode?&c->f1.u32l:&c->f0.u32h),"Stage clock aliases caller float registers.");
 *c->f_odd=0x12345678;++clock_reads;c->f0.fl=clock_reads%2?0.91f:0.03f;
}
void steady_fraction(std::uint8_t*,recomp_context* c){
 check(clock_reads==clock_overrides+1,"Animation override did not follow native clock bookkeeping.");
 ++clock_overrides;c->f0.fl=music_phase;
}
void camera(std::uint8_t*,recomp_context* c){
 check(c->f_odd==(c->mips3_float_mode?&c->f1.u32l:&c->f0.u32h),"Stage camera aliases caller float registers.");
 *c->f_odd=0xabcdef01;c->r2=std::int32_t(camera_address);
}
void emit(std::uint8_t*,recomp_context* c){check(c->r5==2,"Confirmation particles changed native logic rate.");++particles;}
void stage() {
 CharacterMenuMemory g(memory,fields);initialize(g,2);auto a=library();
 CharacterStage s;active_stage=&s;CharacterStageCalls calls{spawn,free_object,fraction,emit,steady_fraction,camera};
 g.write(camera_address+0xc,std::bit_cast<unsigned>(0.0f));
 g.write(camera_address+0x14,std::bit_cast<unsigned>(168.0f)); // Authored stage camera is on +Z.
 recomp_context c{};c.r29=std::int32_t(0x800f0000);const auto saved=c;
 s.initialize(memory,fields,c,calls,a->characters);
 const auto stage_start=s.checkpoint();
 check(s.stage_checkpoint(stage_start,a->characters.size()).checkpoint()==stage_start,"Live stage sidecar did not round-trip.");
 for(std::size_t cut=0;cut<stage_start.size();++cut)rejects([&]{s.stage_checkpoint(View(stage_start).first(cut),a->characters.size());});
 check(spawned==2 && !std::memcmp(&saved,&c,sizeof c),"Stage spawn corrupted intercepted registers.");
 CharacterMenuView v;v.active[1]=true;v.custom[1]=0;g.set(F::Status,1,1,1);
 c.r4=std::int32_t(0x80090000);c.r5=2;
 check(s.update(memory,fields,c,calls,v),"Additional actor fell through to fixed retail actor tables.");
 check(g.read(0x8009003b,1)==0 && g.read(0x80090600,1)==1 && g.read(0x8009060c,1)==4,"Custom sign ownership touched non-sign material.");
 check(g.read(0x80090018,2)==72 && particles==1,"Custom animation/music/confirmation semantics differ from native.");
 c.r4=std::int32_t(0x80091000);check(s.update(memory,fields,c,calls,v),"Idle actor lost update.");
 check(g.read(0x8009103b,1)==1 && g.read(0x80091018,2)==152,"Idle dance does not follow native music phase.");
 check(g.read(0x80090000,2)==0 && g.read(0x80091000,2)==0,"Custom actors still use the reversed half-turn offset.");
 // All custom actors read the same phase as stock, irrespective of audio-clock
 // sampling jitter. Exercise beat wraps and idle/selected poses at variable dt.
 v={};
 for(unsigned frame=0;frame<1200;++frame) {
  music_phase=dkr::runtime::enhancements::advance_character_select_phase(music_phase,frame%3+1,182);
  for(unsigned i=0;i<2;++i) {
   const auto object=0x80090000+i*0x1000;c.r4=std::int32_t(object);c.r5=frame%3+1;
   v.active[i]=frame%11<5;v.custom[i]=i;
   const auto saved_context=c;
   s.update(memory,fields,c,calls,v);
   check(!std::memcmp(&saved_context,&c,sizeof c),"Stage animation/facing corrupted intercepted registers.");
   const auto length=v.active[i]?144:304;
   const float stock=music_phase>0.5f?length-(music_phase-0.5f)*2*length:music_phase*2*length;
   check(std::abs(int(g.read(object+0x18,2))-int(stock))<=1,"Custom animation diverged from stock beat (including wrap).");
  }
 }
 // Confirm a shared frontal plane for either side and a moving camera,
 // without changing pitch, roll, uniform scale or world position.
 for(float camera_x:{-75.0f,0.0f,91.0f})for(float camera_z:{-200.0f,130.0f})for(unsigned i=0;i<2;++i) {
  const auto object=0x80090000+i*0x1000;c.r4=std::int32_t(object);
  g.write(camera_address+0xc,std::bit_cast<unsigned>(camera_x));g.write(camera_address+0x14,std::bit_cast<unsigned>(camera_z));
  g.write(camera_address,0xfd00,2);
  g.write(object+2,123,2);g.write(object+4,456,2);
  std::array<unsigned,4> transform;for(unsigned j=0;j<4;++j)transform[j]=g.read(object+8+4*j);
  s.update(memory,fields,c,calls,v);
  check(g.read(object,2)==0xfd00,"Selection actor toes inward or retains the reversed half-turn offset.");
  check(g.read(object+2,2)==123 && g.read(object+4,2)==456,"Facing correction tilted the character.");
  for(unsigned j=0;j<4;++j)check(g.read(object+8+4*j)==transform[j],"Facing correction changed scale/position.");
 }
 c.r4=std::int32_t(0x80090000);
 const auto previous_yaw=g.read(0x80090000,2);
 g.write(camera_address+0xc,g.read(0x8009000c));g.write(camera_address+0x14,g.read(0x80090014));
 s.update(memory,fields,c,calls,v);check(g.read(0x80090000,2)==previous_yaw,"Coincident camera generated an undefined yaw.");
 camera_address=0;rejects([&]{s.update(memory,fields,c,calls,v);});camera_address=0x80098000;
 c.r4=std::int32_t(0x80099000);const auto original_memory=memory;const auto original_reads=clock_reads;
 check(!s.update(memory,fields,c,calls,v),"Original actor was intercepted.");
 check(memory==original_memory && clock_reads==original_reads,"Stock actor touched custom animation/camera state.");
 const auto stage_end=s.checkpoint();
 s.release(memory,c,calls);s.release(memory,c,calls);check(freed==2,"Stage double-freed or leaked native objects.");
 const auto released=s.checkpoint();check(s.stage_checkpoint(released,a->characters.size()).checkpoint()==released,"Deferred-free actors lost ownership in sidecar.");
 s=s.stage_checkpoint(stage_end,a->characters.size());check(s.checkpoint()==stage_end,"Animation sign state did not restore.");
 // No allocator/free callback may run merely to restore native metadata.
 check(freed==2,"Stage restore freed guest objects.");s=s.stage_checkpoint(released,a->characters.size());
 c.r4=std::int32_t(0x80090000);check(s.update(memory,fields,c,calls,v),"Deferred-free actor fell into fixed native tables.");
 g.set(F::ObjectCount,511);rejects([&]{s.initialize(memory,fields,c,calls,a->characters);});
}
void browsing() {
 CharacterMenuMemory g(memory,fields);auto a=library();
 for(unsigned i=2;i<MaxEnabledCharacters;++i){auto c=a->characters[i%2];c.name="Racer "+std::to_string(i);c.id=sha256(View(reinterpret_cast<const std::uint8_t*>(c.name.data()),c.name.size()));a->characters.push_back(c);}
 CharacterMenuAdapter m(a);initialize(g,4);
 // Unoccupied visible actors are also reserved: previously this moved P1
 // from the lawn to the opposite lawn/shelf while pressing only R.
 initialize(g,1);g.set(F::NativeIndices,4,0,1);controls(g,0,-1);m.input(g,false);
 controls(g,0,0,0,0x10);m.input(g,false);
 check(m.custom(0)==4 && m.view(g).displayed[0]==4,"R jumped into another visible stage slot.");
 for(unsigned s=1;s<4;++s)check(m.view(g).displayed[s]==s,"Browsing changed an unoccupied visible actor.");
 controls(g,0,0,0,0x20);m.input(g,false);
 check(m.custom(0)==0 && m.view(g).displayed[0]==0,"L failed to return in the same physical slot.");
 initialize(g,2);m.reset();
 for(unsigned p=0;p<2;++p){g.set(F::NativeIndices,4,p,1);controls(g,p,-1);m.input(g,true);}
 controls(g,0,0,0,0x10);
 check(m.input(g,true)==std::vector<unsigned>{0x15c} && m.custom(0)==0 && m.custom(1)==0,
       "Shared DOUBLEVISION actor was replaced underneath another player.");
 initialize(g,4);m.reset();
 // Lower-left/right and both ends of the upper shelf are independent.
 constexpr unsigned stock[]{4,7,0,3};constexpr int direction[]{-1,1,-1,1};
 for(unsigned p=0;p<4;++p){g.set(F::NativeIndices,stock[p],p,1);controls(g,p,direction[p]);m.input(g,false);check(m.custom(p)==p,"Four custom stage anchors cannot be reached independently.");}
 g.set(F::Status,2,1,1); // P2's confirmed identity must never be paged away.
 for(unsigned n=0;n<2*MaxEnabledCharacters+5;++n) {
  controls(g,0,0,0,0x10);m.input(g,false);
  check(m.custom(1)==1 && m.custom(2)==2 && m.custom(3)==3,"L/R browsing changed another player's selection.");
  auto v=m.view(g);check(std::ranges::find(v.displayed,m.custom(1))!=v.displayed.end(),"Confirmed actor disappeared.");
  check(*m.custom(0)<MaxEnabledCharacters && *m.custom(0)!=1 && *m.custom(0)!=2 && *m.custom(0)!=3,"Browsing violated duplicate policy or bounds.");
  check(m.view(g).displayed[0]==m.custom(0),"A large roster changed the player's physical stage slot.");
  check((g.get(F::Buttons)&0x30)==0,"Consumed shoulder input reached the native menu.");
 }
 const auto prior=m.custom(0);controls(g,0,0,0,0x30);m.input(g,false);check(m.custom(0)==prior,"Simultaneous L/R moved the roster.");
 const auto large_menu=m.checkpoint();check(large_menu.size()<512 && m.stage_checkpoint(large_menu).checkpoint()==large_menu,"Large roster identity bloated or broke rollback checkpoints.");
 g.set(F::Status,2,0,1);controls(g,0,0,0,0x10);m.input(g,false);check(m.custom(0)==prior,"Confirmed custom racer was changed by L/R.");
}
void stage_browsing() {
 CharacterMenuMemory g(memory,fields);initialize(g,4);auto a=library();
 for(unsigned i=2;i<5;++i){auto c=a->characters[i%2];c.id=std::string(64,'a'+i);c.stage_header=310+i*4;a->characters.push_back(c);}
 spawned=freed=0;CharacterStage s;active_stage=&s;
 CharacterStageCalls calls{spawn,free_object,fraction,emit,steady_fraction,camera};
 recomp_context c{};c.r29=std::int32_t(0x800f0000);s.initialize(memory,fields,c,calls,a->characters);
 check(spawned==4,"Library loaded every enabled character instead of only four stage actors.");
 check(std::bit_cast<float>(g.read(0x80092010))==-10 && std::bit_cast<float>(g.read(0x80093010))==-10,"Extra actors are not on the upper shelf.");
 CharacterMenuView v;for(unsigned i=0;i<4;++i)v.displayed[i]=i;v.displayed[0]=4;
 g.write(camera_address,0xff00,2);c.r5=2;
 for(unsigned i=0;i<4;++i) {
  const auto object=0x80090000+i*0x1000;c.r4=std::int32_t(object);
  check(g.read(object,2)==0,"A custom actor spawned with the old reversed orientation.");
  check(s.update(memory,fields,c,calls,v) && g.read(object,2)==0xff00,"A shelf/lawn actor did not receive the 180-degree correction.");
 }
 fail_spawn=true;check(!s.synchronize(memory,c,calls,a->characters,v),"Failed replacement was not rejected.");fail_spawn=false;
 c.r4=std::int32_t(0x80090000);c.r5=2;g.write(camera_address,0xff00,2);
 check(s.update(memory,fields,c,calls,v) && g.read(0x80090000,2)==0xff00,"Failed replacement destroyed the old actor.");
 check(s.synchronize(memory,c,calls,a->characters,v) && spawned==5 && freed==1,"Replacement was not committed exactly once.");
 v.active[2]=true;v.custom[2]=4;c.r4=std::int32_t(0x80094000);s.update(memory,fields,c,calls,v);
 check(g.read(0x80094600,1)==2,"Paged actor uses its physical slot instead of the selected content identity.");
 c.r4=std::int32_t(0x80090000);check(s.update(memory,fields,c,calls,v),"Replaced actor fell into stock fixed tables before deferred destruction.");
 s.release(memory,c,calls);check(freed==5,"Four-slot stage leaked or double-freed a paged actor.");
}
unsigned hint_texts=0,hint_resets=0,hint_font=0,hint_alpha=0;
void hint_font_call(std::uint8_t*,recomp_context* c){hint_font=unsigned(c->r4);}
void hint_background(std::uint8_t*,recomp_context* c){hint_alpha=unsigned(c->r7);}
void hint_colour(std::uint8_t*,recomp_context*){}
void hint_text(std::uint8_t*,recomp_context* c) {
 CharacterMenuMemory g(memory,fields);
 check(c->f_odd==(c->mips3_float_mode?&c->f1.u32l:&c->f0.u32h),"Hint aliases intercepted float registers.");
 check(std::uint32_t(c->r4)==g.address(F::DisplayList) && c->r5<320 && c->r6<240,"Hint escaped the native menu viewport.");
 check(unsigned(c->r7)>unsigned(c->r29)+32 && g.read(unsigned(c->r29)+16)==0,"Hint scratch aliases native call arguments.");
 std::string value;for(unsigned n=0;n<256;++n){const auto ch=g.read(unsigned(c->r7)+n,1);if(!ch)break;value+=char(ch);}
 check(!value.empty() && value.size()<256,"Hint text is not bounded and terminated.");++hint_texts;
}
void hint_reset(std::uint8_t*,recomp_context*){++hint_resets;}
void hints() {
 CharacterMenuMemory g(memory,fields);initialize(g,4);auto a=library();
 for(unsigned i=2;i<5;++i){auto c=a->characters[i%2];c.name="Long custom character name";a->characters.push_back(c);}
 CharacterMenuRenderer renderer;CharacterMenuDrawCalls calls;
 calls.font=hint_font_call;calls.background=hint_background;calls.colour=hint_colour;calls.text=hint_text;calls.reset=hint_reset;
 CharacterMenuView v;v.visible=true;
 for(unsigned p=0;p<4;++p){v.active[p]=true;v.custom[p]=p;v.ready[p]=p%2;}
 recomp_context c{};c.r29=std::int32_t(0x800f0000);const auto saved=c;
 renderer.draw_hint(memory,fields,c,calls,a->characters,v);
 check(hint_texts==5 && hint_resets==1,"Four-player names or L/R hint missing.");
 check(hint_font==2 && hint_alpha==0 && !std::memcmp(&saved,&c,sizeof c),"Hint leaked native text state or intercepted registers.");
 v.visible=false;renderer.draw_hint(memory,fields,c,calls,a->characters,v);
 check(hint_texts==5 && hint_resets==1,"Hint rendered through a menu transition.");
}
}
int main(){try{for(unsigned i=0;i<fields.size();++i)fields[i]=0x80001000+i*0x100;selection();stage();browsing();stage_browsing();hints();std::cout<<checks<<" native-stage selection/lifetime checks passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
