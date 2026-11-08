#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>
#define DKR_PROBE_HAS_FULL_SCENES 1
struct recomp_context {unsigned dummy;};
static int offline=1,canonical_presentation=1,owner=0;
static struct {unsigned scene_unload_phase;} state;
static jmp_buf rejected;
static void dkr_probe_checkpoint(void){}
static int dkr_probe_mod_dispatch(const char* n,uint8_t* ram,struct recomp_context* c,
    unsigned a,unsigned b,const uint32_t* f,unsigned event,uint64_t* result){
    (void)n;(void)ram;(void)c;(void)a;(void)b;(void)f;(void)event;
    if(owner){*result=73;return 1;}return 0;
}
static void dkr_probe_block(const char* n){(void)n;longjmp(rejected,1);}
