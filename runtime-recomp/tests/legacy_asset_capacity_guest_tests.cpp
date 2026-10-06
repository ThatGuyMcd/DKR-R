// Headless execution of the regenerated retail allocation/texture/sprite
// functions. Asset I/O and allocations are bounded synthetic fixtures; no
// game window, private save, generated source edit or visual claim.
#include "legacy_character_menu.hpp"
#include "legacy_asset_capacity.hpp"
#include "recomp.h"
#include <iostream>
#include <map>
using namespace dkr::mods;
extern "C" void tex_init_textures(std::uint8_t*,recomp_context*);
extern "C" void load_texture(std::uint8_t*,recomp_context*);
extern "C" void tex_load_sprite(std::uint8_t*,recomp_context*);
extern "C" void allocate_object_model_pools(std::uint8_t*,recomp_context*);
namespace {
Bytes memory(8*MiB);
CharacterMenuFields fields=[]{CharacterMenuFields f;f.fill(0x80000000U);return f;}();
CharacterMenuMemory g(memory,fields);
constexpr unsigned Stack=0x807f0000,TableBase=0x80200000;
#if DKR_TEST_REVISION==77
constexpr unsigned TextureCache=0x80126328,TextureCount=0x80126330,SpriteCache=0x8012634c,SpriteCount=0x80126358;
#else
constexpr unsigned TextureCache=0x801268c8,TextureCount=0x801268d0,SpriteCache=0x801268ec,SpriteCount=0x801268f8;
#endif
bool augmented=false;unsigned cursor=0x80300000,checks=0,loads=0,guards=0;
std::map<unsigned,unsigned> allocations;
void check(bool ok,const char* text){++checks;if(!ok)throw Error(text);}
recomp_context context(){recomp_context c{};c.r29=std::int32_t(Stack);return c;}
unsigned allocate(unsigned size) {
 const auto begin=cursor;cursor+=((size+15)&~15U)+16;
 check(cursor<0x807e0000,"Synthetic allocator exhausted");
 allocations[begin]=size;g.bytes(begin,Bytes(size));g.write(begin+size,0xabcdef98);
 return begin;
}
void reset(bool custom) {
 std::fill(memory.begin(),memory.end(),0);augmented=custom;cursor=0x80300000;allocations.clear();loads=guards=0;
}
void canaries(){for(const auto& [address,size]:allocations)check(g.read(address+size)==0xabcdef98,"Cache overwrote adjacent allocation");}
}
extern "C" std::uint32_t dkr_legacy_asset_cache_capacity(std::uint8_t*,recomp_context*,unsigned kind) {
 return legacy_asset_cache_capacities(augmented,512,512,128,96).at(kind);
}
extern "C" void dkr_legacy_asset_cache_guard(std::uint8_t* ram,recomp_context* c,unsigned kind,std::uint32_t index) {
 ++guards;if(index>=dkr_legacy_asset_cache_capacity(ram,c,kind))throw Error("Checked cache exhaustion");
}
extern "C" void mempool_alloc_safe(std::uint8_t*,recomp_context* c){c->r2=std::int32_t(allocate(unsigned(c->r4)));}
extern "C" void mempool_alloc(std::uint8_t* ram,recomp_context* c){mempool_alloc_safe(ram,c);}
extern "C" void asset_table_load(std::uint8_t*,recomp_context* c){
 const unsigned section=unsigned(c->r4),address=TableBase+section*0x3000;
 const unsigned count=section==5 || section==3?512:section==13?128:96;
 for(unsigned i=0;i<=count;++i)g.write(address+4*i,64*i);
 g.write(address+4*(count+1),0xffffffffU);c->r2=std::int32_t(address);
}
extern "C" void asset_load(std::uint8_t*,recomp_context* c){
 ++loads;const unsigned address=unsigned(c->r5),size=unsigned(c->r7);
 g.bytes(address,Bytes(size));
 if(c->r4==2 || c->r4==4) {
  check(size>=32,"Synthetic texture is too short");
  g.write(address+0x12,0x100,2);g.write(address+0x16,64,2); // One RGBA frame, no palette.
 }
}
extern "C" void align16(std::uint8_t*,recomp_context* c){c->r2=(c->r4+15)&~15ULL;}
extern "C" void material_init(std::uint8_t*,recomp_context*){}
extern "C" void sprite_init_frame(std::uint8_t*,recomp_context*){}
extern "C" void drm_vehicle_traction(std::uint8_t*,recomp_context*){}
extern "C" void tex_free(std::uint8_t*,recomp_context*){throw Error("Unexpected synthetic texture failure");}
extern "C" void mempool_free(std::uint8_t*,recomp_context*){throw Error("Unexpected synthetic sprite failure");}
extern "C" void gzip_inflate(std::uint8_t*,recomp_context*){throw Error("Unexpected compressed fixture");}
extern "C" void byteswap32(std::uint8_t*,recomp_context*){throw Error("Unexpected compressed header");}
extern "C" void do_break(std::uint32_t){throw Error("Native arithmetic break");}
int main(){try{
 check(legacy_asset_cache_capacities(false,10000,10000,10000,10000)==std::array<std::uint32_t,4>{700,100,70,100},"Stock capacities changed");
 check(legacy_asset_cache_capacities(true,32767,32767,32767,32767)==std::array<std::uint32_t,4>{65534,32768,32768,32768},"Maximum admitted metadata extent wrong");
 for(bool custom:{false,true}) {
  reset(custom);auto c=context();tex_init_textures(memory.data(),&c);
  check(unsigned(c.r29)==Stack,"Texture init changed caller stack");
  check(allocations.at(g.read(TextureCache))==(custom?1024U:700U)*8,"Texture allocation operand ignored");
  check(allocations.at(g.read(SpriteCache))==(custom?129U:100U)*8,"Sprite allocation operand ignored");
  const auto before=allocations.size();c=context();allocate_object_model_pools(memory.data(),&c);
  auto it=allocations.begin();std::advance(it,before);
  check(it->second==(custom?97U:70U)*8,"Model cache allocation operand ignored");++it;
  check(it->second==100U*4,"Free-model allocation operand ignored");canaries();
  const auto textures=custom?1024U:700U;
  for(unsigned i=0;i<textures;++i) {
   c=context();c.r4=i<512?i:0x8000U+i-512;load_texture(memory.data(),&c);
   check(c.r2!=0 && unsigned(c.r29)==Stack,"Texture failed at a valid expanded cache slot");
   const auto expected=i<512?i:0x8000U+i-512;
   if(g.read(g.read(TextureCache)+8*i)!=expected || g.read(TextureCount)!=i+1)
    throw Error("Texture count/identity mismatch at "+std::to_string(i)+" count="+std::to_string(g.read(TextureCount))+" id="+std::to_string(g.read(g.read(TextureCache)+8*i)));
   ++checks;
  }
  check(g.read(TextureCount)==textures && guards==textures,"Texture pre-store guard was not reached for every slot");
  canaries();const auto before_loads=loads;c=context();c.r4=0;load_texture(memory.data(),&c);
  check(c.r2!=0 && g.read(TextureCount)==textures && loads==before_loads,"Texture hit reloaded or appended a record");
  if(!custom) {
   bool rejected=false;c=context();c.r4=0x8000U+188;
   try{load_texture(memory.data(),&c);}catch(const Error&){rejected=true;}
   check(rejected,"Stock overflow was not stopped before its write");canaries();
  }
  const auto sprites=custom?128U:99U;
  for(unsigned i=0;i<sprites;++i) {
   c=context();c.r4=i;c.r5=0;tex_load_sprite(memory.data(),&c);
   check(c.r2!=0 && unsigned(c.r29)==Stack,"Sprite failed at a valid expanded slot");
  }
  check(g.read(SpriteCount)==sprites,"Sprite capacity comparison mismatch");canaries();
 }
 std::cout<<checks<<" regenerated asset-cache checks passed (v"<<DKR_TEST_REVISION<<"); includes 1024 simultaneous textures.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
