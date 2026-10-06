#include "legacy_runtime_character.hpp"
#include "legacy_character_menu.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
using namespace dkr::mods;
namespace {
unsigned checks=0,draws=0;void check(bool ok,const char* why){++checks;if(!ok)throw Error(why);}
template<class F>void rejects(F fn){bool caught=false;try{fn();}catch(const Error&){caught=true;}check(caught,"Invalid AI input accepted.");}
Bytes memory(8*MiB);CharacterMenuFields fields=[] {CharacterMenuFields f;f.fill(0x80000000);return f;}();
CharacterMenuMemory g(memory,fields);
constexpr unsigned Settings=0x80300000,Stack=0x807f0000,Count=0x80310000,Entry=0x80320000;
constexpr std::array<std::uint8_t,10> Stock{0,9,1,5,3,2,7,4,6,8};
void random_last(std::uint8_t*,recomp_context* c){
 check(unsigned(c->r29)==Stack-0x100 && c->r4==0 && c->r5<26,"AI RNG damaged scratch ABI.");
 check(c->f_odd==(c->mips3_float_mode?&c->f1.u32l:&c->f0.u32h),"AI RNG aliases intercepted float state.");
 *c->f_odd=0x13579bdf;++draws;c->r2=c->r5;
}
std::shared_ptr<CharacterNamespace> library(){
 auto a=std::make_shared<CharacterNamespace>();
 for(unsigned i=0;i<16;++i){AllocatedCharacter c;c.id=std::string(64,'a'+i);c.name="AI "+std::to_string(i);
  c.base_character=i%10;c.headers={304+3*i,305+3*i,306+3*i};a->characters.push_back(c);}
 return a;
}
void pool_checks(){
 auto a=library();CharacterRoster roster(a);std::array<std::uint8_t,8> ids{0,9,1,5,3,2,7,4};
 roster.request(0,a->characters[0].id);roster.commit(ids,1);
 for(unsigned humans=1;humans<=4;++humans)for(unsigned racers=humans;racers<=8;++racers)for(unsigned seed=0;seed<64;++seed){
  unsigned turn=seed;ids={0,9,1,5,3,2,7,4};roster.scene_humans(humans);
  const auto original=ids;
  roster.assign_ai(ids,humans,racers,Stock,[&](unsigned n){turn=turn*1664525U+1013904223U;return turn%n;});
  std::set<std::string> used{a->characters[0].id};
  for(unsigned h=1;h<humans;++h)used.insert("stock"+std::to_string(ids[h]));
  for(unsigned h=0;h<humans;++h)check(ids[h]==original[h],"AI changed human behaviour.");
  for(unsigned i=humans;i<racers;++i){
   check(ids[i]<10,"Custom identity escaped native behaviour table.");
   auto id=roster.active(i);if(id.empty())id="stock"+std::to_string(ids[i]);
   check(used.insert(id).second,"AI duplicates a human/AI logical character.");
  }
 }
 ids={0,9,1,5,3,2,7,4};roster.scene_humans(1);
 const auto before=ids;rejects([&]{roster.assign_ai(ids,1,8,Stock,[](unsigned n){return n;});});
 check(ids==before && roster.active(1).empty(),"Failed AI draw partially committed roster.");
 rejects([&]{roster.assign_ai(ids,0,8,Stock,[](unsigned){return 0;});});
 rejects([&]{roster.assign_ai(ids,1,9,Stock,[](unsigned){return 0;});});
 std::set<unsigned> seen;
 // Every member, including originals whose donor also has a mod, is eligible.
 for(unsigned chosen=0;chosen<25;++chosen){ids=before;roster.scene_humans(1);
  roster.assign_ai(ids,1,2,Stock,[&](unsigned n){check(n==25,"Human custom excluded its stock donor from pool.");return chosen;});
  const auto& id=roster.active(1);
  if(id.empty())seen.insert(ids[1]);else seen.insert(10+unsigned(id[0]-'a'));
 }
 check(seen.size()==25 && seen.contains(0) && seen.contains(25),"Full stock/custom pool was not reachable.");
}
void bridge_checks(){
 auto a=library();CharacterRoster roster(a);std::array<std::uint8_t,4> native{0,9,1,5};
 roster.request(0,a->characters[0].id);roster.commit(native,1);CharacterAiState ai;
 recomp_context c{};c.r29=std::int32_t(Stack);c.r23=std::int32_t(Settings);c.r6=std::int32_t(Count);c.f_odd=&c.f0.u32h;
 for(unsigned mode:{0U,1U})for(unsigned type:{0U,5U,6U,7U,8U,64U,65U,66U}){
  dispatch_character_ai_event(roster,ai,memory,c,0,Stock,random_last);
  g.write(Stack+0x144,1);g.write(Stack+0x68,type);g.write(Stack+0x138,mode);g.write(Count,8);
  for(unsigned i=0;i<8;++i)g.write(Settings+0x59+24*i,i,1);
  const auto before=memory;const auto saved=c;const auto prior_draws=draws;
  dispatch_character_ai_event(roster,ai,memory,c,1,Stock,random_last);
  check(!std::memcmp(&c,&saved,sizeof c),"AI bridge changed intercepted CPU context.");
  const bool admitted=mode!=1 && (type==0 || (type>=64 && type<=66));
  check(draws-prior_draws==(admitted?7:0),"Excluded scene consumed AI RNG.");
  if(!admitted){check(before==memory && !ai.racers && roster.active(1).empty(),"Boss/hub/menu/TT admitted custom AI.");continue;}
  check(g.read(Settings+0x59,1)==0 && roster.active(0)==a->characters[0].id,"AI bridge changed human identity.");
  c.r20=7;c.r22=std::int32_t(Entry);g.write(Entry+14,4,2);g.write(Entry+1,16,1);
  dispatch_character_ai_event(roster,ai,memory,c,2,Stock,random_last);
  check(ai.spawn && ai.spawn->second==7,"Primary spawn used AI sentinel instead of participant index.");
  const unsigned donor=g.read(Settings+0x59+24*7,1);constexpr unsigned headers[]{2,3,4,5,6,7,8,9,1,0};
  c.r4=headers[donor];g.write(Stack+0x68,Entry);
  auto unscoped=c;dispatch_character_event(roster,memory,unscoped,0,0);
  check(unscoped.r4==c.r4,"Unscoped ghost/menu spawn inherited AI identity.");
  // Low-numbered AI participants must not masquerade as human controllers
  // in later arbitrary/ghost spawns either.
  g.write(Entry+14,1,2);auto false_human=c;
  constexpr unsigned native_headers[]{2,3,4,5,6,7,8,9,1,0};
  false_human.r4=native_headers[g.read(Settings+0x59+24,1)];const auto native_root=false_human.r4;
  dispatch_character_event(roster,memory,false_human,0,0);
  check(false_human.r4==native_root,"Low-numbered AI inherited human spawn routing.");
  g.write(Entry+14,4,2);
  const auto mapped=roster.header(7,unsigned(c.r4));
  auto scoped=c;dispatch_character_event(roster,memory,scoped,0,0,ai.spawn);
  check(mapped && scoped.r4==*mapped,"AI private vehicle root was not routed.");
  auto wrong=c;dispatch_character_event(roster,memory,wrong,0,0,std::pair<unsigned,unsigned>{Entry+16,7});
  check(wrong.r4==c.r4,"Different spawn entry inherited AI scope.");
  dispatch_character_ai_event(roster,ai,memory,c,3,Stock,random_last);check(!ai.spawn,"AI spawn scope escaped continuation.");
  dispatch_character_ai_event(roster,ai,memory,c,0,Stock,random_last);check(roster.active(7).empty(),"AI identity survived next scene.");
 }
 check(character_presentation_slot(0xffff,7)==7 && character_presentation_slot(4,7)==8 &&
       character_presentation_slot(0xffff,255)==8 && character_presentation_slot(2,7)==2,"AI presentation sentinel mapping is unsafe.");
}
}
int main(){try{pool_checks();bridge_checks();std::cout<<checks<<" scoped AI pool/spawn checks passed.\n";return 0;}
 catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
