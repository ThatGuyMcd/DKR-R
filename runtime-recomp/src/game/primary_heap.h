#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
#define DKR_HEAP_CONSTEXPR constexpr
#else
#define DKR_HEAP_CONSTEXPR
#endif

// The all-player model residency policy applies to stock sessions too. Admit
// deterministic headroom in RAM already mapped by both runtime owners. Keep
// custom admissions, unknown pools and smaller maps. No free queue changes.
// The upper 128 KiB remain outside this expansion (including private stacks).
static inline DKR_HEAP_CONSTEXPR uint32_t dkr_primary_heap_end(
    uint32_t admitted_end, size_t mapped_bytes) {
    return admitted_end == 0x80400000U && mapped_bytes >= 0x800000U
        ? 0x807E0000U : admitted_end;
}
#undef DKR_HEAP_CONSTEXPR
