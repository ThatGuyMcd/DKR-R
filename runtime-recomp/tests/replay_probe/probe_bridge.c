#include "probe_bridge.h"
#include <fenv.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Intentionally no live native runtime dependency. Bounded nested C-only
   frames never copy jmp_buf and never jump over a native C++ adapter. The
   exclusive owned CPU thread remains the only allowed caller. */
typedef struct {
    uint8_t* ram;
    struct recomp_context* context;
    size_t bytes;
    uint64_t budget;
    int active;
    jmp_buf boundary;
    fenv_t environment;
    dkr_probe_result result;
    uint64_t watch_address;
    uint32_t watch_value;
    const char* last_file;
    unsigned last_line;
    unsigned inherited_depth;
    unsigned native_callbacks;
} ProbeFrame;
enum { DKR_PROBE_NATIVE_DEPTH = 4 };
static ProbeFrame probe_frames[DKR_PROBE_NATIVE_DEPTH];
static unsigned probe_frame;
#define probe probe_frames[probe_frame]
static dkr_probe_mod_service mod_service;
static void* mod_user;

int dkr_probe_bind_mod_service(dkr_probe_mod_service service,void* user) {
    if(probe_frame || probe.active || ((service==NULL)!=(user==NULL)))return 0;
    mod_service=service;mod_user=user;return 1;
}
int dkr_probe_mod_dispatch(const char* operation,uint8_t* ram,struct recomp_context* context,
    const uint64_t* args,unsigned count,const uint32_t* fields,unsigned event,uint64_t* result) {
    if(!mod_service)return 0;
    if(!probe.active || ram!=probe.ram || !operation || !context || !result || count>16 || (count && !args))
        dkr_probe_block("invalid-owned-mod-service");
    ++probe.native_callbacks;
    /* The callback catches all C++ exceptions and returns. It MUST NOT call
       unchecked guest functions or any trapping probe API itself. */
    const int handled=mod_service(mod_user,operation,ram,context,args,count,fields,event,result);
    --probe.native_callbacks;
    if(handled<0)dkr_probe_block("owned-mod-service-failure");
    if(handled>1)dkr_probe_block("invalid-owned-mod-service-result");
    return handled;
}

void dkr_probe_watch_word(uint64_t address) {
    if (probe.active) abort();
    probe.watch_address = address;
}
static void check_watch(void) {
    if (probe.watch_address) {
        uint32_t value;
        const uint64_t offset = probe.watch_address - UINT64_C(0xffffffff80000000);
        memcpy(&value, probe.ram + (size_t)offset, sizeof(value));
        if (value != probe.watch_value) {
            fprintf(stderr, "private watch 0x%llx: 0x%x -> 0x%x after %s:%u\n",
                    (unsigned long long)probe.watch_address, probe.watch_value, value,
                    probe.last_file ? probe.last_file : "native-boundary", probe.last_line);
            probe.watch_value = value;
        }
    }
}

void dkr_probe_block(const char* operation) {
    if (!probe.active) abort();
    probe.result.blocked = operation;
    dkr_probe_boss_diagnostic_failure(&probe.result, probe.context);
    longjmp(probe.boundary, 1);
}
int dkr_probe_diagnostic_read(uint8_t* ram, uint32_t address, unsigned width, uint32_t* value) {
    if (!probe.active || ram != probe.ram || !value || address < 0x80000000U ||
        (width != 1 && width != 2 && width != 4)) return 0;
    const uint64_t offset = ((uint64_t)address ^ (width == 1 ? 3U : width == 2 ? 2U : 0U)) - UINT64_C(0x80000000);
    if ((offset & (width - 1U)) || offset >= probe.bytes || width > probe.bytes - offset) return 0;
    *value = 0;
    memcpy(value, probe.ram + (size_t)offset, width);
    return 1;
}
void dkr_probe_checkpoint(void) {
    if (!probe.active) abort();
    if (++probe.result.operations > probe.budget) dkr_probe_block("operation-budget");
}
void dkr_probe_enter(const char* function) {
    dkr_probe_checkpoint();
    ++probe.result.guest_entries;
    if (probe.inherited_depth + probe.result.stack_depth >= 128) dkr_probe_block("guest-stack-budget");
    probe.result.stack[probe.result.stack_depth++] = function;
}
void dkr_probe_leave(void) {
    if (!probe.active || !probe.result.stack_depth) dkr_probe_block("unbalanced-guest-stack");
    --probe.result.stack_depth;
}
#if defined(_MSC_VER)
#define DKR_CHECKED_INLINE static __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define DKR_CHECKED_INLINE static inline __attribute__((always_inline))
#else
#define DKR_CHECKED_INLINE static inline
#endif
DKR_CHECKED_INLINE void* checked_memory_at(uint8_t* ram, uint64_t address, unsigned width, unsigned lane_xor,
                          const char* file, unsigned line) {
    uint64_t offset;
    if (!probe.active) abort();
    check_watch();
    if (probe.watch_address) { probe.last_file = file; probe.last_line = line; }
    dkr_probe_checkpoint();
    ++probe.result.memory_accesses;
    /* Preserve the checked retail header's sign-extended KSEG0 convention.
       Do not mask a corrupt pointer into apparently valid guest RAM. */
    if (ram != probe.ram || address < UINT64_C(0xffffffff80000000) ||
        (width != 1 && width != 2 && width != 4) ||
        lane_xor != (width == 1 ? 3U : width == 2 ? 2U : 0U)) {
        probe.result.bad_address = address;
        probe.result.source_file = file; probe.result.source_line = line;
        dkr_probe_block("invalid-guest-address");
    }
    offset = (address ^ lane_xor) - UINT64_C(0xffffffff80000000);
    if ((offset & (width - 1U)) || offset >= probe.bytes || width > probe.bytes - offset) {
        probe.result.bad_address = address;
        probe.result.source_file = file; probe.result.source_line = line;
        dkr_probe_block("out-of-range-guest-address");
    }
    /* Diagnostics are cold-path fields. A failure always leaves through
       longjmp and dkr_probe_run resets the result before the next invocation;
       successful loads must not store/clear these fields millions of times.
       Every bounds/lane/sign-extension check and operation counter remains. */
    return probe.ram + (size_t)offset;
}
void* dkr_probe_memory_at(uint8_t* ram,uint64_t address,unsigned width,unsigned lane_xor,
                          const char* file,unsigned line) {
    return checked_memory_at(ram,address,width,lane_xor,file,line);
}
void* dkr_probe_memory_1_at(uint8_t* ram,uint64_t address,const char* file,unsigned line) {
    return checked_memory_at(ram,address,1,3,file,line);
}
void* dkr_probe_memory_2_at(uint8_t* ram,uint64_t address,const char* file,unsigned line) {
    return checked_memory_at(ram,address,2,2,file,line);
}
void* dkr_probe_memory_4_at(uint8_t* ram,uint64_t address,const char* file,unsigned line) {
    return checked_memory_at(ram,address,4,0,file,line);
}
#undef DKR_CHECKED_INLINE
void* dkr_probe_memory(uint8_t* ram, uint64_t address, unsigned width, unsigned lane_xor) {
    return dkr_probe_memory_at(ram, address, width, lane_xor, NULL, 0);
}
void dkr_probe_audio_dma(uint8_t* ram, uint8_t* dmem, uint32_t dmem_address,
                         uint32_t dram_address, uint32_t inclusive_length, int write,
                         const char* file, unsigned line) {
    if (!probe.active) abort();
    check_watch();
    probe.last_file = file; probe.last_line = line;
    dkr_probe_checkpoint();
    ++probe.result.memory_accesses;
    probe.result.bad_address = UINT64_C(0xffffffff80000000) + dram_address;
    probe.result.source_file = file; probe.result.source_line = line;
    /* Inclusive lengths must not wrap at +1. No write, including to DMEM,
       occurs until the entire transfer is known to fit. Hardware masks live
       in the reviewed generated adapter, not in this checked boundary. */
    if (ram != probe.ram || !dmem || (write != 0 && write != 1) ||
        (dram_address & 7U) || dram_address > 0xFFFFF8U)
        dkr_probe_block("invalid-owned-audio-dma");
    if (dmem_address >= 4096 || inclusive_length >= 4096 - dmem_address ||
        dram_address >= probe.bytes || inclusive_length >= probe.bytes - dram_address ||
        ((dram_address + inclusive_length) | 3U) >= probe.bytes)
        dkr_probe_block("out-of-range-audio-rsp-dma");
    const unsigned bytes = inclusive_length + 1;
    for (unsigned i = 0; i < bytes; ++i) {
        const unsigned dmem_lane = (dmem_address + i) ^ 3U;
        const unsigned dram_lane = (dram_address + i) ^ 3U;
        if (write) ram[dram_lane] = dmem[dmem_lane];
        else dmem[dmem_lane] = ram[dram_lane];
    }
    probe.result.bad_address = 0;
    probe.result.source_file = NULL; probe.result.source_line = 0;
}
static dkr_probe_result run_frame(dkr_probe_entry entry, uint8_t* ram, size_t bytes,
                               struct recomp_context* context, uint64_t budget) {
    dkr_probe_result rejected = {0};
    if (probe.active || !entry || !ram || !bytes || !context || !budget) {
        rejected.blocked = "invalid-probe-invocation";
        return rejected;
    }
    memset(&probe.result, 0, sizeof(probe.result));
    probe.ram = ram; probe.bytes = bytes; probe.budget = budget; probe.context = context;
    probe.last_file = NULL; probe.last_line = 0;
    if (probe.watch_address) {
        const uint64_t offset = probe.watch_address - UINT64_C(0xffffffff80000000);
        if (probe.watch_address < UINT64_C(0xffffffff80000000) || (offset & 3) ||
            offset >= bytes || sizeof(uint32_t) > bytes - offset) {
            rejected.blocked = "invalid-private-watchpoint";
            return rejected;
        }
        memcpy(&probe.watch_value, ram + (size_t)offset, sizeof(probe.watch_value));
    }
    if (fegetenv(&probe.environment) != 0) {
        rejected.blocked = "floating-environment-unavailable";
        return rejected;
    }
    probe.active = 1;
    if (setjmp(probe.boundary) == 0) {
        entry(ram, context);
        check_watch();
        if (probe.result.stack_depth) dkr_probe_block("unbalanced-guest-return");
        probe.result.completed = 1;
    }
    probe.active = 0;
    if (fesetenv(&probe.environment) != 0) {
        probe.result.completed = 0;
        probe.result.blocked = "floating-environment-restore-failed";
    }
    return probe.result;
}
dkr_probe_result dkr_probe_run(dkr_probe_entry entry,uint8_t* ram,size_t bytes,
    struct recomp_context* context,uint64_t budget) {
    if(probe_frame || probe.active){dkr_probe_result rejected={0};rejected.blocked="invalid-probe-invocation";return rejected;}
    probe.inherited_depth=0;probe.native_callbacks=0;
    return run_frame(entry,ram,bytes,context,budget);
}
dkr_probe_result dkr_probe_run_native(dkr_probe_entry entry,uint8_t* ram,struct recomp_context* context) {
    dkr_probe_result rejected={0};
    if(!probe.active || !probe.native_callbacks || !mod_service || ram!=probe.ram || !entry || !context ||
       probe_frame+1>=DKR_PROBE_NATIVE_DEPTH || probe.result.operations>=probe.budget) {
        rejected.blocked="invalid-owned-native-invocation";return rejected;
    }
    ProbeFrame* parent=&probe;
    const uint64_t remaining=parent->budget-parent->result.operations;
    const size_t bytes=parent->bytes;
    ++probe_frame;
    probe.inherited_depth=parent->inherited_depth+parent->result.stack_depth;
    probe.native_callbacks=0;probe.watch_address=parent->watch_address;
    const dkr_probe_result result=run_frame(entry,ram,bytes,context,remaining);
    --probe_frame;
    // Charge the SAME root budget. Recursing through native callbacks cannot
    // obtain another budget or bypass the aggregate guest-stack bound.
    parent->result.operations+=result.operations;
    parent->result.memory_accesses+=result.memory_accesses;
    parent->result.guest_entries+=result.guest_entries;
    return result;
}
