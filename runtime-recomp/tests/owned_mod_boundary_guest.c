#include "replay_probe/probe_bridge.h"

/* Real C entry points: intentionally no C++ frame is inside a longjmp region. */
void owned_mod_boundary_success(uint8_t* ram,struct recomp_context* context) {
    (void)context;dkr_probe_enter("owned-mod-success");
    *(uint32_t*)dkr_probe_memory(ram,UINT64_C(0xffffffff80000020),4,0)=0x12345678;
    dkr_probe_leave();
}
void owned_mod_boundary_fault(uint8_t* ram,struct recomp_context* context) {
    (void)context;dkr_probe_enter("owned-mod-fault");
    dkr_probe_memory(ram,UINT64_C(0xffffffff80800000),4,0);
    dkr_probe_leave();
}
void owned_mod_boundary_budget(uint8_t* ram,struct recomp_context* context) {
    (void)ram;(void)context;dkr_probe_enter("owned-mod-budget");
    for(unsigned i=0;i<1000;++i)dkr_probe_checkpoint();
    dkr_probe_leave();
}
void owned_mod_boundary_dispatch(uint8_t* ram,struct recomp_context* context) {
    dkr_probe_enter("owned-mod-dispatch");uint64_t value=0;
    if(!dkr_probe_mod_dispatch("reviewed-test-service",ram,context,0,0,0,0,&value))
        dkr_probe_block("missing-test-service");
    dkr_probe_leave();
}
