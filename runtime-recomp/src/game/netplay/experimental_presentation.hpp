#pragma once
#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>
#include "experimental_draw_events.h"
#include "local_scenery.hpp"

namespace dkr::runtime::netplay::experimental {
struct SubmissionDomain;
class SubmissionPermit;

struct RenderDescriptor {
    std::uint32_t frame=0, display_start=0, display_end=0, framebuffer=0, depthbuffer=0;
    std::uint32_t black=0;
};

// Owned producer -> consumer handover, NOT RT64/VI integration or admission.
// The producer must call publish at a completed, exclusively owned CPU frame.
// Readers get immutable bytes, never a pointer to rewindable simulation RAM.
class RenderSnapshot final {
public:
    ~RenderSnapshot();
    std::uint64_t epoch() const { return epoch_; }
    std::uint64_t generation() const { return generation_; }
    // Diagnostic immutable-image proof only, NOT the network/checkpoint hash.
    // Most production frames never request it. Concurrent readers compute it
    // exactly once against the retained immutable bytes, not a decode copy.
    std::uint64_t hash() const;
    const RenderDescriptor& descriptor() const { return descriptor_; }
    std::span<const std::uint8_t> bytes() const { return storage_.ram; }
    std::span<const dkr_owned_draw_event> draw_events() const { return storage_.events; }
    const std::shared_ptr<const LocalSceneryScene>& local_scenery() const { return local_scenery_; }
private:
    friend class PresentationMailbox;
    friend class DecodeWorkspace;
    friend class SubmissionPermit;
    friend struct SubmissionDomain;
    struct Storage {
        std::vector<std::uint8_t> ram;
        std::vector<dkr_owned_draw_event> events;
    };
    RenderSnapshot(std::uint64_t epoch,std::uint64_t generation,RenderDescriptor descriptor,
                   std::span<const std::uint8_t> ram,std::span<const dkr_owned_draw_event> events,
                   std::shared_ptr<SubmissionDomain> domain,std::shared_ptr<const LocalSceneryScene> scenery);
    std::uint64_t epoch_,generation_;
    mutable std::uint64_t hash_=0;
    mutable std::once_flag hash_once_;
    RenderDescriptor descriptor_;
    std::shared_ptr<const LocalSceneryScene> local_scenery_;
    // Domain precedes storage so acquisition can safely use its lifetime.
    // RAM and metadata are acquired/released together, never separate owners.
    std::shared_ptr<SubmissionDomain> domain_;
    Storage storage_;
    mutable bool consumed_=false; // Set by take(), counts retained queue leases.
    mutable std::atomic<bool> decoder_claimed_{false}; // Lifetime only; never simulation state.
};

// Taken at the ACTUAL queue-consumption boundary, not when a frame is queued.
// Retirement rejects queued-but-unclaimed work. A previously acquired permit
// may finish its already-started GPU/WSI operation; retirement is drained only
// after that permit releases. Never hold a mailbox/CPU-owner mutex over WSI.
class SubmissionPermit final {
public:
    static std::unique_ptr<SubmissionPermit> acquire(std::shared_ptr<const RenderSnapshot> snapshot);
    ~SubmissionPermit();
    SubmissionPermit(const SubmissionPermit&)=delete;
    SubmissionPermit& operator=(const SubmissionPermit&)=delete;
private:
    explicit SubmissionPermit(std::shared_ptr<const RenderSnapshot> snapshot):snapshot_(std::move(snapshot)) {}
    std::shared_ptr<const RenderSnapshot> snapshot_;
};

// RT64/F3DDKR decoding can write query/readback/scratch data. Never const-cast
// an immutable lease or expose rewindable CPU RAM to it. Exactly one mutable
// workspace per snapshot; retain this owner through ALL asynchronous readers.
// This storage boundary alone does NOT drain GPU work or authorize VI present.
class DecodeWorkspace final {
public:
    static std::unique_ptr<DecodeWorkspace> create(std::shared_ptr<const RenderSnapshot> snapshot);
    ~DecodeWorkspace();
    DecodeWorkspace(const DecodeWorkspace&) = delete;
    DecodeWorkspace& operator=(const DecodeWorkspace&) = delete;
    std::span<std::uint8_t> bytes() { return ram_; }
    const std::shared_ptr<const RenderSnapshot>& snapshot() const { return snapshot_; }
private:
    explicit DecodeWorkspace(std::shared_ptr<const RenderSnapshot> snapshot);
    std::shared_ptr<const RenderSnapshot> snapshot_;
    std::vector<std::uint8_t> ram_;
};

class PresentationMailbox final {
public:
    struct BufferStatistics {
        std::size_t image_allocations=0,decode_allocations=0,image_reuses=0,decode_reuses=0;
        std::size_t metadata_allocations=0,metadata_reuses=0;
    };
    PresentationMailbox();
    ~PresentationMailbox();
    static constexpr std::size_t kImageBytes=16U*1024U*1024U;
    // At most two queued leases + one latest pending + replacement candidate.
    // Bounds payload allocation, not allocator overhead or whole-process RSS.
    static constexpr std::size_t kPeakPayloadBytes=4*kImageBytes;
    // Draw observations belong to the same four immutable owners. Their
    // bounded pool is additional to RAM, never another queued-frame FIFO.
    static constexpr std::size_t kPeakDrawEventBytes=4*DKR_OWNED_MAX_DRAW_EVENTS*sizeof(dkr_owned_draw_event);
    // Includes the one active worker's mutable workspace, not GPU resources,
    // metadata or separately retained immutable local-scene assets (<=4 MiB
    // per cache generation). Never a total-process memory ceiling.
    static constexpr std::size_t kPeakDecodePayloadBytes=kPeakPayloadBytes+kImageBytes;
    bool begin_epoch(std::uint64_t epoch);
    bool publish(std::uint64_t epoch,RenderDescriptor descriptor,std::span<const std::uint8_t> ram,
                 std::span<const dkr_owned_draw_event> events={},std::shared_ptr<const LocalSceneryScene> scenery={});
    std::shared_ptr<const RenderSnapshot> take();
    // The renderer has finished ALL CPU readers of its writable workspace.
    // GPU queue ownership may retain the immutable lease; begin_epoch and
    // quiescent still wait for those owners, not merely this decoding slot.
    bool finish_decode(const std::shared_ptr<const RenderSnapshot>& snapshot);
    // Call before restoring a corrected frame. Invalidates old visual work;
    // does not mutate/release a snapshot which a consumer still owns.
    bool invalidate_from(std::uint64_t epoch,std::uint32_t first_changed_frame);
    // Driver rewind hook, including a correction before the first published
    // image or after a slow renderer's last publication. Retires any in-flight
    // candidate/lease without claiming that the GPU has finished consuming it.
    bool retire_for_restore(std::uint64_t epoch,std::uint32_t first_changed_frame);
    bool is_current(const std::shared_ptr<const RenderSnapshot>& snapshot) const;
    // Nonblocking status for an asynchronous restore/preparation owner. An
    // adapter requiring fully retired presentation MUST await this, keeping
    // UI/network serviceable, before declaring retirement complete.
    bool submissions_drained() const;
    bool quiescent() const;
    BufferStatistics buffer_statistics() const;
private:
    void retire_locked(std::uint32_t first_changed_frame);
    // Serialize producers without holding the small consumer-state lock while
    // copying/hashing RAM. A correction may retire a candidate during that work.
    std::mutex producer_mutex_;
    mutable std::mutex mutex_;
    std::uint64_t epoch_=0,generation_=0;
    std::optional<std::uint32_t> last_frame_;
    std::shared_ptr<const RenderSnapshot> pending_;
    std::weak_ptr<const RenderSnapshot> active_;
    std::shared_ptr<SubmissionDomain> domain_;
};
}
