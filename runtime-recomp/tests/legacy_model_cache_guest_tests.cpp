// Fault-inject actual recompiled model loaders. Never edit generated code.
#include "legacy_character_menu.hpp"
#include "recomp.h"
#include "legacy_heap_policy.hpp"
#include "legacy_asset_capacity.hpp"
#include "legacy_model_safety.hpp"
#include <iostream>
using namespace dkr::mods;
extern "C" void object_model_init(std::uint8_t*,recomp_context*);
extern "C" void model_instance_init(std::uint8_t*,recomp_context*);
extern "C" void mempool_init_main(std::uint8_t*,recomp_context*);
extern "C" void init_object_shading(std::uint8_t*,recomp_context*);
namespace {
Bytes memory(8*MiB);
CharacterMenuFields fields=[]{CharacterMenuFields f;f.fill(0x80000000U);return f;}();
CharacterMenuMemory g(memory,fields);
constexpr unsigned Cache=0x80300000,FreeList=0x80301000,Table=0x80302000,Model=0x80400000,Stack=0x807f0000;
#if DKR_TEST_REVISION==77
constexpr unsigned ModelTable=0x8011d620,CachePtr=0x8011d624,FreePtr=0x8011d628,
 Count=0x8011d62c,Ids=0x8011d630,FreeCount=0x8011d634;
#else
constexpr unsigned ModelTable=0x8011dba0,CachePtr=0x8011dba4,FreePtr=0x8011dba8,
 Count=0x8011dbac,Ids=0x8011dbb0,FreeCount=0x8011dbb4;
#endif
enum Failure {None,Allocation,Texture,Normals,Animation,Instance};
Failure fail=None;unsigned allocations=0,cleanups=0,checks=0;
bool custom_session=false;unsigned heap_base=0,heap_bytes=0,heap_slots=0;
void check(bool b,const char* message){++checks;if(!b)throw Error(message);}
recomp_context context(){recomp_context c{};c.r29=std::int32_t(Stack);return c;}
void reset(bool recycled) {
 std::fill(memory.begin(),memory.end(),0);allocations=cleanups=0;
 g.write(ModelTable,Table);g.write(CachePtr,Cache);g.write(FreePtr,FreeList);
 g.write(Count,recycled?1:0);g.write(Ids,512);g.write(FreeCount,recycled?1:0);
 // A never-published cell can contain zero/zero, exactly the false sky-ID
 // match seen in the dump. Recycled cells use retail's -1 tombstone instead.
 g.write(Cache,recycled?0xffffffff:0);g.write(Cache+4,recycled?0xffffffff:0);g.write(FreeList,0);
 for(unsigned i=0;i<513;++i)g.write(Table+4*i,16*i);
}
}
extern "C" void gzip_size_uncompressed(std::uint8_t*,recomp_context* c){c->r2=256;}
extern "C" void mempool_alloc(std::uint8_t*,recomp_context* c){
 ++allocations;c->r2=(fail==Allocation&&allocations==1)||(fail==Instance&&allocations==2)?0:std::int32_t(Model+(allocations-1)*0x1000);
}
extern "C" void asset_load(std::uint8_t*,recomp_context*){}
extern "C" void gzip_inflate(std::uint8_t*,recomp_context*){
 g.bytes(Model,Bytes(256));g.write(Model,0x80);g.write(Model+0x22,1,2);
}
extern "C" void load_texture(std::uint8_t*,recomp_context* c){c->r2=fail==Texture?0:std::int32_t(0x80500000);}
extern "C" void model_init_normals(std::uint8_t*,recomp_context* c){c->r2=fail==Normals?1:0;}
extern "C" void model_anim_init(std::uint8_t*,recomp_context* c){c->r2=fail==Animation?1:0;}
extern "C" void free_model_data(std::uint8_t*,recomp_context*){++cleanups;}
extern "C" void stubbed_printf(std::uint8_t*,recomp_context*){}
extern "C" int dkr_legacy_model_safety(std::uint8_t*,recomp_context*,unsigned operation,std::uint32_t address){
 const auto r=check_model_operation(memory,operation,address);
 if(r.failure)throw Error(r.failure);return r.no_shading?1:0;
}
unsigned shaded=0;
extern "C" void set_shading_properties(std::uint8_t*,recomp_context* c){
 ++shaded;g.write(unsigned(c->r4),0x3f800000); // Native call/output remain intact.
}
extern "C" void dkr_legacy_heap_capacity(std::uint8_t*,recomp_context* c){c->r15=std::int32_t(legacy_heap_end(unsigned(c->r15),memory.size(),custom_session));}
extern "C" std::uint32_t dkr_legacy_asset_cache_capacity(std::uint8_t*,recomp_context*,unsigned kind){return legacy_asset_cache_capacities(custom_session,512,512,100,512).at(kind);}
extern "C" void dkr_legacy_asset_cache_guard(std::uint8_t* ram,recomp_context* c,unsigned kind,std::uint32_t index){if(index>=dkr_legacy_asset_cache_capacity(ram,c,kind))throw Error("Model cache guard");}
extern "C" void mempool_init(std::uint8_t*,recomp_context* c){heap_base=unsigned(c->r4);heap_bytes=unsigned(c->r5);heap_slots=unsigned(c->r6);}
extern "C" void mempool_free_timer(std::uint8_t*,recomp_context*){}
extern "C" void do_break(std::uint32_t){throw Error("Native arithmetic break");}
int main(){try{
 for(bool recycled:{false,true})for(auto f:{Allocation,Texture,Normals,Animation,Instance}){
  reset(recycled);fail=f;auto c=context();c.r4=42;object_model_init(memory.data(),&c);
  check(c.r2==0,"Failed model unexpectedly loaded");
  check(g.read(Count)==(recycled?1U:0U),"Failed load left a phantom model-cache entry");
  check(g.read(FreeCount)==(recycled?1U:0U),"Failed load consumed a recyclable cache slot");
  check(unsigned(c.r29)==Stack,"Native model loader changed caller stack");
  // The crash queried sky model zero after a previous failed load. Retry it
  // successfully and verify it is loaded, not read from an empty cache cell.
  fail=None;allocations=0;c=context();c.r4=0;object_model_init(memory.data(),&c);
  check(c.r2!=0,"Subsequent sky model did not recover");
  check(g.read(Count)==1 && g.read(FreeCount)==0,"Recovered cache accounting is wrong");
  check(g.read(Cache)==0 && g.read(Cache+4)==Model,"Recovered sky cache is invalid");
  allocations=0;c=context();c.r4=0;object_model_init(memory.data(),&c);
  check(c.r2!=0 && allocations==1,"Successful cache hit reloaded the model");
 }
 auto c=context();c.r4=0;model_instance_init(memory.data(),&c);
 check(c.r2==0 && unsigned(c.r29)==Stack,"NULL model did not fail safely");
 custom_session=true;reset(false);fail=None;
 for(unsigned id=0;id<100;++id){c=context();c.r4=id;object_model_init(memory.data(),&c);
  check(c.r2!=0 && g.read(Count)==id+1,"Expanded model cache stopped at the retail 70-model limit");}
 check(g.read(Cache+99*8)==99,"Expanded model identity was corrupted");
 for(bool custom:{false,true}){
  custom_session=custom;c=context();mempool_init_main(memory.data(),&c);
  check(heap_base+heap_bytes==(custom?0x80800000U:0x80400000U),"Wrong native main-pool extent");
  check(heap_slots==1600 && unsigned(c.r29)==Stack,"Heap hook changed allocator ABI");
 }
 check(legacy_heap_end(0x80400000U,0x400000U,true)==0x80400000U,"Heap exceeds available RDRAM");
 check(legacy_heap_end(0x80300000U,memory.size(),true)==0x80300000U,"Unknown heap bound modified");
 // Recreate the supplied dump's sparse slot shape and exact bad model value.
 constexpr unsigned Object=0x80200000,Header=0x801f7200,Slots=Object+0x80,Shade=Object+0x98,InstancePtr=0x802e9fb0;
 auto object=[&](unsigned count=5){
  std::fill(memory.begin(),memory.end(),0);shaded=0;
  g.write(Object+0x40,Header);g.write(Object+0x68,Slots);g.write(Header+0x55,count,1);
  g.write(Slots+16,InstancePtr);g.write(InstancePtr,Model);g.write(Model+0x24,1,2);
  g.write(Model+4,Model+0x100);g.write(Model+0x40,Model+0x200);
 };
 auto shade=[&]{auto c=context();c.r4=std::int32_t(Object);c.r5=std::int32_t(Shade);c.r16=123;
  init_object_shading(memory.data(),&c);check(unsigned(c.r29)==Stack && c.r16==123,"Shading changed callee-saved ABI");return unsigned(c.r2);};
 auto reject=[&](const char* expected){bool caught=false;try{shade();}catch(const Error& e){caught=std::string(e.what())==expected;}
  check(caught && !shaded,"Invalid model reached retail shading dereference");};
 object();check(shade()==52 && shaded==1 && g.read(Object+0x54)==Shade,"Valid sparse fifth model lost its shading");
 object();g.write(Model+0x40,0);check(shade()==4 && !shaded && g.read(Object+0x54)==0,"Unlit model lost retail size/NULL semantics");
 object();g.write(Slots+16,0);g.write(Shade,0x00100010);check(shade()==4 && !shaded,"Empty slots scanned into following behaviour data");
 object(0);check(shade()==4 && !shaded,"Empty model array was scanned");
 object();g.write(InstancePtr,0x00100010);reject("model pointer");
 object();g.write(Slots+16,0x007fffff);reject("model instance pointer");
 object();g.write(InstancePtr,0x807ffffc);reject("model pointer");
 object();g.write(Model+0x40,0x807ffffe);reject("model normals array");
 object();g.write(Model+4,0x00100010);reject("model vertex array");
 object();g.write(Header+0x55,128,1);reject("model slot array");
 object();g.write(Object+0x68,0x807ffffc);reject("model slot array");
 object();g.write(Header+0x53,1,1);g.write(Object+0x68,0);check(shade()==12 && !shaded,"Billboard path was modified");
 object();g.write(Header+0x53,2,1);g.write(Object+0x68,0);check(shade()==4 && !shaded,"Misc texture path was modified");
 // Check ALL loaded slots at spawn, not only the one shading happens to use.
 object();g.write(Slots,InstancePtr);g.write(Slots+16,0x00100010);
 check(check_object_models(memory,Object,false).failure!=nullptr,"Spawn published a damaged later LOD slot");
 object();const auto valid=memory;auto saved=context();
 check(dkr_legacy_model_safety(memory.data(),&saved,1,Object)==0 && memory==valid,"Validation mutated valid model allocations");
 std::cout<<checks<<" native model-cache failure checks passed (v"<<DKR_TEST_REVISION<<").\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
