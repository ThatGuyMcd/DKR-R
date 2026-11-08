// The final native entry's exact dispatch prefix, with no guest tick executed.
extern "C" {
#include "recomp.h"
int dkr_memory_owned_heap_entry(const char*,uint8_t*,recomp_context*);
}
#include <iostream>
#include <stdexcept>
#include <string>
namespace {bool delegated=false;unsigned checks=0,checkpoints=0,dispatches=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("Owned heap entry assertion "+std::to_string(checks));}}
extern "C" void dkr_probe_checkpoint(){++checkpoints;}
extern "C" int dkr_probe_mod_dispatch(const char*,uint8_t*,recomp_context* ctx,
    const uint64_t*,unsigned,const uint32_t*,unsigned,uint64_t* result){
 ++dispatches;if(!delegated)return 0;ctx->r15=(int32_t)0x80800000U;*result=17;return 1;
}
int main(){try{
 uint8_t sentinel=0;recomp_context ctx{};
 for(uint32_t end:{0x80400000U,0x80400010U,0x807e0000U,0x80800000U}){
  ctx.r15=(int32_t)end;check(dkr_memory_owned_heap_entry("dkr_legacy_heap_capacity",&sentinel,&ctx)==0);
  check((uint32_t)ctx.r15==(end==0x80400000U?0x807e0000U:end));
 }
 ctx.r15=(int32_t)0x80400000U;
 check(!dkr_memory_owned_heap_entry("unhandled-operation",&sentinel,&ctx));check((uint32_t)ctx.r15==0x80400000U);
 delegated=true;check(dkr_memory_owned_heap_entry("dkr_legacy_heap_capacity",&sentinel,&ctx)==17);
 check((uint32_t)ctx.r15==0x80800000U);check(checkpoints==6 && dispatches==6 && sentinel==0);
 std::cout<<checks<<" owned heap entry checks passed; no mod service, delegated custom admission and unknown operations covered.\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
