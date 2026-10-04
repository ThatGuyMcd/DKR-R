#include "rollback_state_store.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using dkr::runtime::netplay::RollbackStateStore;
    constexpr std::size_t state_bytes = RollbackStateStore::kPageBytes * 3U + 17U;
    RollbackStateStore store(state_bytes, 6U, 3U);
    std::vector<std::uint8_t> state(state_bytes, 0U);
    for (std::uint32_t frame = 0U; frame < 12U; ++frame) {
        state[(frame * 997U) % state.size()] = static_cast<std::uint8_t>(frame + 1U);
        assert(store.save(frame, state, 0x1000U + frame));
        std::vector<std::uint8_t> loaded(state_bytes);
        std::uint64_t checksum = 0U;
        assert(store.load(frame, loaded, &checksum));
        assert(loaded == state);
        assert(checksum == 0x1000U + frame);
    }
    assert(!store.contains(0U));
    assert(store.contains(11U));
    std::vector<std::uint8_t> branch(state_bytes);
    assert(store.load(8U, branch));
    branch[RollbackStateStore::kPageBytes + 9U] ^= 0x5AU;
    store.discard_after(8U);
    assert(store.save(9U, branch, 9U));
    std::vector<std::uint8_t> loaded(state_bytes);
    assert(store.load(9U, loaded));
    assert(loaded == branch);
    // Three mostly-identical states must consume substantially less than
    // three full copies; this protects handheld memory use.
    assert(store.memory_bytes() < state_bytes * 3U);

    // Exercise DKR's actual rollback footprint. Checkpoint-group eviction must
    // keep a usable rollback window without allocating/promoting a full state
    // on every frame once the ring reaches capacity.
    constexpr std::size_t runtime_state_bytes = 0x01000000U + 24U +
                                                32U * 536U;
    RollbackStateStore runtime_store(runtime_state_bytes, 18U, 4U);
    std::vector<std::uint8_t> runtime_state(runtime_state_bytes, 0U);
    for (std::uint32_t frame = 0U; frame < 32U; ++frame) {
        for (std::size_t page = 0U; page < 32U; ++page) {
            const std::size_t offset =
                ((static_cast<std::size_t>(frame) * 131U + page * 977U) %
                 runtime_state.size());
            runtime_state[offset] ^= static_cast<std::uint8_t>(frame + page);
        }
        assert(runtime_store.save(frame, runtime_state,
                                  0xD1DD0000ULL + frame));
    }
    std::vector<std::uint8_t> runtime_loaded(runtime_state_bytes);
    std::uint64_t runtime_checksum = 0U;
    assert(runtime_store.load(31U, runtime_loaded, &runtime_checksum));
    assert(runtime_loaded == runtime_state);
    assert(runtime_checksum == 0xD1DD001FULL);
    assert(runtime_store.memory_bytes() < runtime_state_bytes * 8U);
    // Experimental contiguous checkpoints/reuse must be byte-identical to
    // the existing store, including non-page-sized tails and repeated rewinds.
    RollbackStateStore reference(state_bytes,18,4,0);
    RollbackStateStore optimized(state_bytes,18,4,2,true);
    std::vector<std::uint8_t> comparison(state_bytes),reference_bytes(state_bytes),optimized_bytes(state_bytes);
    for(unsigned frame=0;frame<120;++frame) {
        comparison[(frame*977)%state_bytes]^=std::uint8_t(frame+1);
        comparison.back()=std::uint8_t(frame);
        assert(reference.save(frame,comparison,frame)&&optimized.save(frame,comparison,frame));
        reference.retire_before(frame>6?frame-6:0);optimized.retire_before(frame>6?frame-6:0);
        for(unsigned at=frame>6?frame-6:0;at<=frame;++at) {
            std::uint64_t a=UINT64_MAX,b=UINT64_MAX;
            assert(reference.load(at,reference_bytes,&a)==optimized.load(at,optimized_bytes,&b));
            assert(a==b && reference_bytes==optimized_bytes);
        }
        if(frame>6&&frame%10==7) {
            reference.discard_after(frame-2);optimized.discard_after(frame-2);
            assert(reference.save(frame,comparison,frame)&&optimized.save(frame,comparison,frame));
        }
    }
    const auto cached=optimized.allocated_bytes();
    optimized.trim_spares(0);assert(optimized.allocated_bytes()<=cached);
    assert(optimized.load(119,optimized_bytes)&&optimized_bytes==comparison);

    // Sparse/empty deltas must keep their comparison base correct, including
    // reverting a page, a non-page-sized tail, and branching after a rewind.
    RollbackStateStore sparse_reference(state_bytes,18,4,0);
    RollbackStateStore sparse_optimized(state_bytes,18,4,2,true);
    std::fill(comparison.begin(),comparison.end(),0);
    for(unsigned frame=0;frame<24;++frame) {
        if(frame%5==0) {
            comparison[RollbackStateStore::kPageBytes+7]^=0x37;
            comparison.back()^=0x51;
        }
        if(frame==13) {
            assert(sparse_reference.load(11,comparison));
            sparse_reference.discard_after(11);sparse_optimized.discard_after(11);
            comparison[0]=0xAA;comparison.back()=0x77;
        }
        assert(sparse_reference.save(frame,comparison,frame));
        assert(sparse_optimized.save(frame,comparison,frame));
        assert(sparse_reference.load(frame,reference_bytes));
        assert(sparse_optimized.load(frame,optimized_bytes));
        assert(reference_bytes==comparison&&optimized_bytes==comparison);
    }
    return 0;
}
