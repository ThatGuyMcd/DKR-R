#pragma once
#include <stdint.h>
#include <stddef.h>

/* Authored, guest-owned allocator safety. Every persistent byte is in RDRAM,
   so restoring a rollback checkpoint also restores queue/reserve ownership.
   The retail two-tick deferred-free lifetime is never shortened. */
#define DKR_MEMORY_MAGIC 0x444b524dU
#define DKR_TRANSITION_STRIDE 7712U
#define DKR_TRANSITION_RESERVES 3U
#define DKR_MEMORY_ERROR UINT32_MAX
typedef struct {
    uint8_t* ram;
    size_t bytes;
    unsigned revision;
} dkr_memory_view;
static inline uint32_t dkr_memory_read(dkr_memory_view g, uint32_t p, unsigned n) {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; ++i)
        v = (v << 8) | g.ram[((p & 0x1fffffffU) + i) ^ 3U];
    return v;
}
static inline void dkr_memory_write(dkr_memory_view g, uint32_t p, uint32_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        g.ram[((p & 0x1fffffffU) + i) ^ 3U] = (uint8_t)(v >> (8U * (n - 1U - i)));
}
static inline int dkr_memory_range(dkr_memory_view g, uint32_t p, size_t n, unsigned align) {
    const size_t o = p & 0x1fffffffU;
    return (p & 0xe0000000U) == 0x80000000U && !(p & (align - 1U)) && o <= g.bytes && n <= g.bytes - o;
}
static inline uint32_t dkr_memory_pools(dkr_memory_view g) {
    return g.revision == 77 ? 0x80123580U : 0x80123b00U;
}
static inline uint32_t dkr_memory_descriptor(dkr_memory_view g) {
    return dkr_memory_pools(g) + 72U;
}
static inline uint32_t dkr_memory_count_address(dkr_memory_view g) {
    return dkr_memory_descriptor(g) + 2048U;
}
static inline uint32_t dkr_memory_capacity(dkr_memory_view g) {
    const uint32_t d = dkr_memory_descriptor(g);
    return dkr_memory_read(g, d, 4) == DKR_MEMORY_MAGIC ? dkr_memory_read(g, d + 8, 4) : 256U;
}
static inline uint32_t dkr_memory_queue(dkr_memory_view g) {
    const uint32_t d = dkr_memory_descriptor(g), cap = dkr_memory_capacity(g);
    const uint32_t p = cap == 256U ? d : dkr_memory_read(g, d + 4, 4);
    const uint32_t count = dkr_memory_read(g, dkr_memory_count_address(g), 4);
    if (cap < 256U || cap > 8192U || count > cap || !dkr_memory_range(g, p, (size_t)cap * 8U, 4))
        return DKR_MEMORY_ERROR;
    return p;
}
static inline uint32_t dkr_memory_reserve_bytes(unsigned capacity) {
    return capacity * 8U + DKR_TRANSITION_STRIDE * DKR_TRANSITION_RESERVES;
}
static inline int dkr_memory_install(dkr_memory_view g, uint32_t base, unsigned capacity) {
    if (capacity < 1600U || capacity > 8192U || !dkr_memory_range(g, base, dkr_memory_reserve_bytes(capacity), 16))
        return 0;
    const uint32_t d = dkr_memory_descriptor(g);
    dkr_memory_write(g, d + 4, base, 4);
    dkr_memory_write(g, d + 8, capacity, 4);
    dkr_memory_write(g, d + 12, base + capacity * 8U, 4);
    dkr_memory_write(g, d + 16, 0, 4);
    dkr_memory_write(g, d + 20, 0, 4); /* queue high-water mark */
    dkr_memory_write(g, d + 24, 0, 4); /* fallback count */
    dkr_memory_write(g, d, DKR_MEMORY_MAGIC, 4);
    return 1;
}
static inline uint32_t dkr_memory_enqueue(dkr_memory_view g, uint32_t address) {
    const uint32_t q = dkr_memory_queue(g), caddr = dkr_memory_count_address(g);
    if (q == DKR_MEMORY_ERROR)
        return DKR_MEMORY_ERROR;
    if (!address)
        return 0;
    if (!dkr_memory_range(g, address, 1, 1))
        return DKR_MEMORY_ERROR;
    const uint32_t n = dkr_memory_read(g, caddr, 4), timer = dkr_memory_read(g, caddr + 4, 4);
    if (!timer || timer > 255U)
        return DKR_MEMORY_ERROR;
    /* Repeated frees cannot create a later release of a reused allocation. */
    for (uint32_t i = 0; i < n; ++i)
        if (dkr_memory_read(g, q + i * 8U, 4) == address)
            return 0;
    if (n >= dkr_memory_capacity(g))
        return DKR_MEMORY_ERROR;
    dkr_memory_write(g, q + n * 8U, address, 4);
    dkr_memory_write(g, q + n * 8U + 4, timer, 1);
    dkr_memory_write(g, caddr, n + 1U, 4);
    const uint32_t d = dkr_memory_descriptor(g);
    if (q != d && n + 1U > dkr_memory_read(g, d + 20, 4))
        dkr_memory_write(g, d + 20, n + 1U, 4);
    return 0;
}
static inline uint32_t dkr_memory_acquire(dkr_memory_view g, uint32_t bytes) {
    const uint32_t d = dkr_memory_descriptor(g);
    if (dkr_memory_read(g, d, 4) != DKR_MEMORY_MAGIC || bytes > DKR_TRANSITION_STRIDE)
        return 0;
    const uint32_t base = dkr_memory_read(g, d + 12, 4), busy = dkr_memory_read(g, d + 16, 4);
    if (busy >= 8U || !dkr_memory_range(g, base, DKR_TRANSITION_STRIDE * 3U, 16))
        return DKR_MEMORY_ERROR;
    for (unsigned i = 0; i < 3; ++i)
        if (!(busy & (1U << i))) {
            dkr_memory_write(g, d + 16, busy | (1U << i), 4);
            return base + i * DKR_TRANSITION_STRIDE;
        }
    return 0;
}
/* Only mempool_free_addr calls this, AFTER the normal deferred queue expires. */
static inline uint32_t dkr_memory_release(dkr_memory_view g, uint32_t address) {
    const uint32_t d = dkr_memory_descriptor(g);
    if (dkr_memory_read(g, d, 4) != DKR_MEMORY_MAGIC)
        return 0;
    const uint32_t base = dkr_memory_read(g, d + 12, 4), busy = dkr_memory_read(g, d + 16, 4);
    if (busy >= 8U || !dkr_memory_range(g, base, DKR_TRANSITION_STRIDE * 3U, 16))
        return DKR_MEMORY_ERROR;
    if (address < base || address >= base + DKR_TRANSITION_STRIDE * 3U)
        return 0;
    if ((address - base) % DKR_TRANSITION_STRIDE)
        return DKR_MEMORY_ERROR;
    const unsigned bit = 1U << ((address - base) / DKR_TRANSITION_STRIDE);
    if (!(busy & bit))
        return DKR_MEMORY_ERROR;
    dkr_memory_write(g, d + 16, busy & ~bit, 4);
    return 1;
}
static inline uint32_t dkr_memory_pool(dkr_memory_view g, uint32_t pool) {
    if (pool >= 4U || pool > dkr_memory_read(g, dkr_memory_pools(g) + 64U, 4))
        return DKR_MEMORY_ERROR;
    const uint32_t p = dkr_memory_pools(g) + 16U * pool;
    const uint32_t max = dkr_memory_read(g, p, 4), used = dkr_memory_read(g, p + 4, 4);
    const uint32_t slots = dkr_memory_read(g, p + 8, 4), size = dkr_memory_read(g, p + 12, 4);
    if (!max || max > 8192U || !used || used >= max || size < (size_t)max * 20U || !dkr_memory_range(g, slots, size, 4))
        return DKR_MEMORY_ERROR;
    return max;
}
static inline int dkr_memory_slot(dkr_memory_view g, uint32_t pool, uint32_t index) {
    const uint32_t max = dkr_memory_pool(g, pool);
    if (max == DKR_MEMORY_ERROR || index >= max)
        return 0;
    const uint32_t p = dkr_memory_pools(g) + pool * 16U, base = dkr_memory_read(g, p + 8, 4), s = base + index * 20U;
    const uint32_t data = dkr_memory_read(g, s, 4), size = dkr_memory_read(g, s + 4, 4);
    const uint32_t prev = dkr_memory_read(g, s + 10, 2), next = dkr_memory_read(g, s + 12, 2);
    const uint32_t end = base + dkr_memory_read(g, p + 12, 4);
    if (data < base + max * 20U || data > end || size > end - data || (data & 15U) || (prev != 65535U && prev >= max) ||
        (next != 65535U && next >= max))
        return 0;
    if (prev != 65535U &&
        (dkr_memory_read(g, base + prev * 20U + 12, 2) != index ||
         dkr_memory_read(g, base + prev * 20U, 4) + dkr_memory_read(g, base + prev * 20U + 4, 4) != data))
        return 0;
    if (next != 65535U && (dkr_memory_read(g, base + next * 20U + 10, 2) != index ||
                           dkr_memory_read(g, base + next * 20U, 4) != data + size))
        return 0;
    return 1;
}
static inline int dkr_memory_mutation(dkr_memory_view g, uint32_t pool, uint32_t index) {
    if (!dkr_memory_slot(g, pool, index))
        return 0;
    const uint32_t p = dkr_memory_pools(g) + 16U * pool, base = dkr_memory_read(g, p + 8, 4);
    const uint32_t max = dkr_memory_read(g, p, 4), used = dkr_memory_read(g, p + 4, 4);
    if (dkr_memory_read(g, base + used * 20U + 14, 2) >= max)
        return 0;
    const uint32_t s = base + index * 20U;
    for (unsigned offset = 10; offset <= 12; offset += 2) {
        const uint32_t neighbour = dkr_memory_read(g, s + offset, 2);
        if (neighbour != 65535U && !dkr_memory_slot(g, pool, neighbour))
            return 0;
    }
    return 1;
}
/* Full bounded walk used ONLY on an allocation failure, not every frame. */
typedef struct {
    uint32_t free_bytes, largest_free, used_slots, max_slots, queue, high_water;
    int valid;
} dkr_memory_metrics;
static inline dkr_memory_metrics dkr_memory_measure(dkr_memory_view g) {
    dkr_memory_metrics m = {0};
    m.max_slots = dkr_memory_pool(g, 0);
    m.queue = dkr_memory_read(g, dkr_memory_count_address(g), 4);
    const uint32_t d = dkr_memory_descriptor(g);
    if (dkr_memory_read(g, d, 4) == DKR_MEMORY_MAGIC)
        m.high_water = dkr_memory_read(g, d + 20, 4);
    if (m.max_slots == DKR_MEMORY_ERROR)
        return m;
    const uint32_t p = dkr_memory_pools(g), base = dkr_memory_read(g, p + 8, 4);
    m.used_slots = dkr_memory_read(g, p + 4, 4);
    uint32_t index = 0;
    for (unsigned visited = 0; visited < m.max_slots; ++visited) {
        if (!dkr_memory_slot(g, 0, index))
            return m;
        const uint32_t s = base + 20U * index, bytes = dkr_memory_read(g, s + 4, 4);
        if (!dkr_memory_read(g, s + 8, 2)) {
            m.free_bytes += bytes;
            if (bytes > m.largest_free)
                m.largest_free = bytes;
        }
        index = dkr_memory_read(g, s + 12, 2);
        if (index == 65535U) {
            m.valid = 1;
            return m;
        }
    }
    return m;
}
