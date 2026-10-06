#ifndef DKR_PROBE_MEMORY_INLINE_H
#define DKR_PROBE_MEMORY_INLINE_H
#include "probe_bridge.h"
#if defined(_MSC_VER)
#define DKR_MEMORY_INLINE static __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define DKR_MEMORY_INLINE static inline __attribute__((always_inline))
#else
#define DKR_MEMORY_INLINE static inline
#endif
// A checked fast path, not unchecked guest memory. The cold bridge remains
// the single fault/watchpoint implementation, including exact source sites.
// Native recursion selects a separate guard and charges the parent's budget.
DKR_MEMORY_INLINE void* dkr_probe_memory_inline_at(uint8_t* ram,uint64_t address,
    unsigned width,unsigned lane,const char* file,unsigned line) {
    dkr_probe_memory_guard* guard=dkr_probe_memory_guard_current;
    const uint64_t offset=(address^lane)-UINT64_C(0xffffffff80000000);
    if(guard && !guard->watching && ram==guard->ram &&
       address>=UINT64_C(0xffffffff80000000) &&
       (width==1 || width==2 || width==4) &&
       lane==(width==1 ? 3U : width==2 ? 2U : 0U) &&
       !(offset&(width-1U)) && offset<guard->bytes && width<=guard->bytes-offset &&
       *guard->operations<guard->budget) {
        ++*guard->operations;++*guard->accesses;
        return guard->ram+(size_t)offset;
    }
    return dkr_probe_memory_at(ram,address,width,lane,file,line);
}
#undef DKR_MEMORY_INLINE
#endif
