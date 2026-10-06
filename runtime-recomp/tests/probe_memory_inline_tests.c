#include "replay_probe/probe_memory_inline.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct recomp_context { uint64_t unused; };
static uint64_t tested_address;
static unsigned tested_width,tested_lane;
static int use_inline,wrong_ram;
static uint8_t alternate_ram[32];
static dkr_probe_memory_guard* parent_guard;
void dkr_probe_boss_diagnostic_failure(const dkr_probe_result* result,struct recomp_context* context) {
    (void)result;(void)context;
}
static void memory_entry(uint8_t* ram,struct recomp_context* context) {
    (void)context;
    dkr_probe_enter("checked-memory-test");
    uint8_t* selected=wrong_ram ? alternate_ram:ram;
    void* pointer=use_inline ? dkr_probe_memory_inline_at(selected,tested_address,tested_width,tested_lane,"site",73):
        dkr_probe_memory_at(selected,tested_address,tested_width,tested_lane,"site",73);
    assert(pointer==ram+(size_t)((tested_address^tested_lane)-UINT64_C(0xffffffff80000000)));
    dkr_probe_leave();
}
static void nested_entry(uint8_t* ram,struct recomp_context* context) {
    (void)context;dkr_probe_enter("nested");
    assert(dkr_probe_memory_guard_current && dkr_probe_memory_guard_current!=parent_guard);
    (void)dkr_probe_memory_inline_at(ram,UINT64_C(0xffffffff80000000),4,0,"nested",9);
    dkr_probe_leave();
}
static int service(void* user,const char* operation,uint8_t* ram,struct recomp_context* context,
    const uint64_t* args,unsigned count,const uint32_t* fields,unsigned event,uint64_t* value) {
    (void)user;(void)operation;(void)args;(void)count;(void)fields;(void)event;
    const dkr_probe_result result=dkr_probe_run_native(nested_entry,ram,context);
    assert(dkr_probe_memory_guard_current==parent_guard);
    *value=0;return result.completed ? 1:-1;
}
static void root_entry(uint8_t* ram,struct recomp_context* context) {
    dkr_probe_enter("root");parent_guard=dkr_probe_memory_guard_current;
    (void)dkr_probe_memory_inline_at(ram,UINT64_C(0xffffffff80000000),4,0,"root",4);
    uint64_t value=0;
    assert(dkr_probe_mod_dispatch("test",ram,context,NULL,0,NULL,0,&value)==1);
    (void)dkr_probe_memory_inline_at(ram,UINT64_C(0xffffffff80000004),4,0,"root",8);
    dkr_probe_leave();
}
static int equal_text(const char* a,const char* b) {return (!a && !b) || (a && b && !strcmp(a,b));}
int main(void) {
    uint8_t ram[32]={0};struct recomp_context context={0};
    unsigned checks=0;
    const struct {uint64_t address;unsigned width,lane;int wrong;uint64_t budget;int watch;} cases[]={
        {UINT64_C(0xffffffff80000000),1,3,0,10,0},
        {UINT64_C(0xffffffff80000002),2,2,0,10,0},
        {UINT64_C(0xffffffff8000001c),4,0,0,10,0},
        {UINT64_C(0xffffffff80000000),4,0,0,10,1},
        {UINT64_C(0x80000000),4,0,0,10,0},
        {UINT64_C(0xffffffff80000000),4,0,1,10,0},
        {UINT64_C(0xffffffff80000000),3,0,0,10,0},
        {UINT64_C(0xffffffff80000000),1,0,0,10,0},
        {UINT64_C(0xffffffff80000001),2,2,0,10,0},
        {UINT64_C(0xffffffff80000002),4,0,0,10,0},
        {UINT64_C(0xffffffff80000020),4,0,0,10,0},
        {UINT64_MAX,4,0,0,10,0},
        {UINT64_C(0xffffffff80000000),4,0,0,1,0},
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        tested_address=cases[i].address;tested_width=cases[i].width;tested_lane=cases[i].lane;wrong_ram=cases[i].wrong;
        dkr_probe_watch_word(cases[i].watch ? UINT64_C(0xffffffff80000000):0);
        use_inline=0;
        const dkr_probe_result cold=dkr_probe_run(memory_entry,ram,sizeof(ram),&context,cases[i].budget);
        assert(!dkr_probe_memory_guard_current);use_inline=1;
        const dkr_probe_result fast=dkr_probe_run(memory_entry,ram,sizeof(ram),&context,cases[i].budget);
        assert(!dkr_probe_memory_guard_current);
        assert(cold.completed==fast.completed && equal_text(cold.blocked,fast.blocked) &&
            cold.operations==fast.operations && cold.memory_accesses==fast.memory_accesses &&
            cold.bad_address==fast.bad_address && cold.source_line==fast.source_line &&
            equal_text(cold.source_file,fast.source_file) && cold.stack_depth==fast.stack_depth);
        assert(i<4 ? fast.completed:!fast.completed);++checks;
    }
    dkr_probe_watch_word(0);
    assert(dkr_probe_bind_mod_service(service,&context));
    dkr_probe_result result=dkr_probe_run(root_entry,ram,sizeof(ram),&context,5);
    assert(result.completed && result.operations==5 && result.memory_accesses==3 && !dkr_probe_memory_guard_current);
    result=dkr_probe_run(root_entry,ram,sizeof(ram),&context,4);
    assert(!result.completed && equal_text(result.blocked,"operation-budget") && result.operations==5 &&
        result.memory_accesses==2 && !dkr_probe_memory_guard_current);
    assert(dkr_probe_bind_mod_service(NULL,NULL));
    printf("Checked inline memory: %u cold/fast parity cases and nested aggregate budget passed.\n",checks);
    return 0;
}
