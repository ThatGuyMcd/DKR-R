// Actual final allocator bodies, synthetic RAM only. No ROM/game/RSP execution.
extern "C" {
#include "recomp.h"
void mempool_init_main(uint8_t*,recomp_context*);
void mempool_alloc(uint8_t*,recomp_context*);
void mempool_alloc_safe(uint8_t*,recomp_context*);
void mempool_free(uint8_t*,recomp_context*);
void mempool_free_addr(uint8_t*,recomp_context*);
void mempool_free_queue_clear(uint8_t*,recomp_context*);
}
#include "mods/legacy_memory_safety.h"
#include "mods/legacy_heap_policy.hpp"
#include "primary_heap.h"
#include "netplay/runtime_state.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
std::vector<uint8_t> memory(0x1000000U);
dkr_memory_view view{memory.data(),0x800000U,DKR_MEMORY_TEST_REVISION};
bool repaired=false; unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
recomp_context context(){recomp_context c{};c.r29=(int32_t)0x807ff000U;c.r31=0x12345678U;c.f_odd=&c.f0.u32h;return c;}
uint32_t call(void(*fn)(uint8_t*,recomp_context*),uint32_t a=0,uint32_t b=0){
 auto c=context();c.r4=(int32_t)a;c.r5=(int32_t)b;fn(memory.data(),&c);
 check((uint32_t)c.r29==0x807ff000U && c.r31==0x12345678U,"Allocator clobbered stack/return address");return (uint32_t)c.r2;
}
uint32_t alloc(unsigned n){return call(mempool_alloc,n,0x11223344U);}
void init(bool after){repaired=after;std::fill(memory.begin(),memory.end(),0);call(mempool_init_main);}
void pattern(uint32_t p,unsigned n,uint8_t value){check(p>=0x80000000U && p+n<=0x807e0000U,"Allocation escaped guarded heap");std::fill_n(memory.begin()+(p&0x1fffffffU),n,value);}
}
extern "C" void interrupts_disable(uint8_t*,recomp_context* c){c->r2=1;}
extern "C" void interrupts_enable(uint8_t*,recomp_context*){}
extern "C" void stack_pointer(uint8_t*,recomp_context*){throw std::runtime_error("Unexpected retail diagnostic");}
extern "C" void dump_memory_to_cpak(uint8_t*,recomp_context*){throw std::runtime_error("Unexpected retail diagnostic");}
extern "C" void stubbed_printf(uint8_t*,recomp_context*){}
extern "C" void do_break(uint32_t){throw std::runtime_error("Unexpected guest break");}
extern "C" void dkr_legacy_heap_capacity(uint8_t*,recomp_context* c){
 auto end=dkr::mods::legacy_heap_end((uint32_t)c->r15,0x800000U,false);
 c->r15=(int32_t)(repaired?dkr_primary_heap_end(end,0x800000U):end);
}
extern "C" uint32_t dkr_legacy_memory_service(uint8_t*,recomp_context*,unsigned op,unsigned rev,uint32_t a,uint32_t b){
 check(rev==DKR_MEMORY_TEST_REVISION,"Unexpected revision");uint32_t v=0;
 switch(op){
 case 0:for(unsigned i=0;i<32;i+=4)dkr_memory_write(view,dkr_memory_descriptor(view)+i,0,4);dkr_memory_write(view,dkr_memory_count_address(view),0,4);return 1600;
 case 1:v=dkr_memory_pool(view,0);if(v!=DKR_MEMORY_ERROR)v=dkr_memory_reserve_bytes(v);break;
 case 2:v=!a||dkr_memory_install(view,a,1600)?0:DKR_MEMORY_ERROR;break;
 case 3:v=dkr_memory_queue(view);break;
 case 4:v=dkr_memory_enqueue(view,a);break;
 case 5:v=dkr_memory_acquire(view,a);break;
 case 6:v=dkr_memory_release(view,a);break;
 case 7:v=dkr_memory_pool(view,a);break;
 case 8:v=dkr_memory_mutation(view,a,b)?0:DKR_MEMORY_ERROR;break;
 default:v=DKR_MEMORY_ERROR;break;
 }
 if(v==DKR_MEMORY_ERROR)throw std::runtime_error("Controlled memory stop op="+std::to_string(op));return v;
}
int main(){try{
 // Unknown bounds and inadequate maps are never expanded; existing custom
 // admission is preserved byte-for-byte, including its already larger bound.
 check(dkr_primary_heap_end(0x80400000U,0x7fffffU)==0x80400000U,"Short map admitted");
 for(uint32_t end:{0U,0x80300000U,0x80400010U,0x807e0000U,0x80800000U})check(dkr_primary_heap_end(end,0x1000000U)==end,"Unknown/already admitted pool changed");
 check(dkr_primary_heap_end(dkr::mods::legacy_heap_end(0x80400000U,0x800000U,true),0x800000U)==0x80800000U,"Existing custom admission changed");
 init(false);const auto before=dkr_memory_measure(view);check(before.valid,"Initial retail pool invalid");
 // A repeatable fragmented workload of 960 small blocks, then a held tail.
 // 43 nonadjacent 3 KiB holes leave >128 KiB free but no 6.8 KiB block.
 // It reproduces the failure class; this is not a reconstruction of user RAM.
 const unsigned count=960,small=3072,ordinary=256;
 const unsigned consumed=43*small+(count-43)*ordinary;
 check(before.largest_free>consumed+16,"Fixture footprint exceeds baseline");
 const unsigned held_tail=before.largest_free-consumed-16;
 for(bool after:{false,true}){
  init(after);auto initial=dkr_memory_measure(view);
  std::vector<uint32_t> blocks;std::vector<unsigned> sizes;
  for(unsigned i=0;i<count;++i){unsigned n=i<86 && i%2==0?small:ordinary;auto p=alloc(n);check(p!=0,"Workload allocation failed");blocks.push_back(p);sizes.push_back(n);pattern(p,n,uint8_t(i%251+1));}
  auto tail=alloc(held_tail);check(tail!=0,"Held tail failed");pattern(tail,held_tail,0xa5);
  for(unsigned i=0;i<86;i+=2)call(mempool_free,blocks[i]);
  const auto queued=dkr_memory_measure(view);check(queued.queue==43,"Delayed free queue changed");
  call(mempool_free_queue_clear);check(dkr_memory_measure(view).queue==43,"Resources freed before two ticks");
  call(mempool_free_queue_clear);auto fragmented=dkr_memory_measure(view);
  check(fragmented.valid && fragmented.queue==0,"Fragmented pool invalid");
  check(fragmented.free_bytes>128*1024,"Failure fixture lacks fragmented free bytes");
  // Snapshot/restore must retain the expansion, descriptors and delayed frees.
  dkr::runtime::netplay::RuntimeState state(memory.size());auto c=context();check(state.register_context(&c),"Context registration failed");
  std::vector<uint8_t> snapshot(state.snapshot_size());check(state.capture(memory.data(),123,snapshot),"Snapshot failed");
  for(unsigned request:{6800U,16800U}){
   auto p=alloc(request);check(bool(p)==after,"Before/after allocation outcome differs");
   if(p){pattern(p,request,0x77);call(mempool_free_addr,p);}
   else {bool stopped=false;try{call(mempool_alloc_safe,request);}catch(const std::runtime_error& e){stopped=std::string(e.what()).find("op=10")!=std::string::npos;}check(stopped,"Required allocation failure no longer stops safely");}
  }
  std::fill(memory.begin()+0x400000,memory.begin()+0x7e0000,0x55);uint32_t frame=0;
  check(state.restore(memory.data(),snapshot,frame) && frame==123,"Expanded state restore failed");
  check(std::equal(memory.begin(),memory.end(),snapshot.begin()+24),"Snapshot did not restore all canonical RAM");
  for(unsigned i=0;i<count;++i){if(i<86 && i%2==0)continue;auto start=memory.begin()+(blocks[i]&0x1fffffffU);check(std::all_of(start,start+sizes[i],[&](auto v){return v==uint8_t(i%251+1);}),"Live resource changed");call(mempool_free_addr,blocks[i]);}
  call(mempool_free_addr,tail);auto released=dkr_memory_measure(view);
  check(released.valid && released.free_bytes==initial.free_bytes && released.used_slots==initial.used_slots,"Resources leaked across unload");
  std::cout<<(after?"AFTER":"BEFORE")<<" revision="<<DKR_MEMORY_TEST_REVISION<<" free="<<fragmented.free_bytes<<" largest="<<fragmented.largest_free<<" slots="<<fragmented.used_slots<<'/'<<fragmented.max_slots<<" requests=6800,16800 "<<(after?"accepted":"rejected safely")<<'\n';
 }
 // Fill right up to the admitted boundary, not just small low-address blocks.
 init(true);std::fill(memory.begin()+0x7e0000,memory.begin()+0x7f0000,0xcd);
 const auto full=dkr_memory_measure(view);auto whole=alloc(full.largest_free);
 check(whole!=0,"Cannot allocate full admitted free tail");pattern(whole,full.largest_free,0x5a);
 check(!alloc(16),"Exhausted pool allocated outside its bound");
 check(std::all_of(memory.begin()+0x7e0000,memory.begin()+0x7f0000,[](auto v){return v==0xcd;}),"Full allocation overwrote guard");
 call(mempool_free_addr,whole);
 // Soak delayed releases and a checkpoint while resources are still queued.
 // The emulated call stack legitimately occupies the top of the guard. Keep
 // its 64 KiB window separate from the canary immediately above the heap.
 init(true);auto initial=dkr_memory_measure(view);std::fill(memory.begin()+0x7e0000,memory.begin()+0x7f0000,0xcd);
 for(unsigned scene=0;scene<5000;++scene){
  auto a=alloc(6800+(scene%4)*16),b=alloc(16800);check(a && b,"Soak allocation failed");pattern(a,6800,0xab);pattern(b,16800,0xbc);
  call(mempool_free,a);call(mempool_free,b);
  if(scene%100==0){auto copy=memory;call(mempool_free_queue_clear);memory=copy;view.ram=memory.data();}
  call(mempool_free_queue_clear);check(dkr_memory_measure(view).queue==2,"Deferred ownership lost");call(mempool_free_queue_clear);
  const auto m=dkr_memory_measure(view);check(m.valid && m.free_bytes==initial.free_bytes && m.used_slots==initial.used_slots,"Soak leaked resources");
 }
 check(std::all_of(memory.begin()+0x7e0000,memory.begin()+0x7f0000,[](auto v){return v==0xcd;}),"Upper guard was overwritten");
 std::cout<<checks<<" memory stability checks passed, revision "<<DKR_MEMORY_TEST_REVISION<<"; 5000 cycles; no game or ROM executed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
