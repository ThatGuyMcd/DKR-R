#include "experimental_presentation.hpp"
#include "experimental_checkpoint_hash.hpp"
#include "experimental_performance.hpp"
#include <cstring>
#include <array>
#include <new>

namespace dkr::runtime::netplay::experimental {
struct SubmissionDomain {
    std::mutex mutex;
    std::uint64_t epoch=0,generation=0;
    std::size_t in_flight=0,consumers=0;
    bool alive=true;
    // Two queued immutable images + latest pending + publication candidate,
    // one decoder, INCLUDING cached capacity: hard cap 80 MiB, not a FIFO.
    std::mutex buffers_mutex;
    std::array<RenderSnapshot::Storage,4> images;
    std::vector<std::uint8_t> decoder;
    PresentationMailbox::BufferStatistics buffers;
    RenderSnapshot::Storage acquire_image(std::span<const dkr_owned_draw_event> events) {
        std::scoped_lock lock(buffers_mutex);
        for(auto& image:images)if(!image.ram.empty()) {
            const bool allocate=events.size()>image.events.capacity();
            // Reserve once to the admission limit, never grow a per-tick
            // vector. A failure leaves this free slot's RAM available; no
            // allocation/lease count is claimed before all copying succeeds.
            if(allocate)image.events.reserve(DKR_OWNED_MAX_DRAW_EVENTS);
            if(events.empty())image.events.clear();
            else image.events.assign(events.begin(),events.end());
            if(allocate)++buffers.metadata_allocations;
            else if(!events.empty())++buffers.metadata_reuses;
            ++buffers.image_reuses;return std::move(image);
        }
        if(buffers.image_allocations==images.size())throw std::bad_alloc();
        RenderSnapshot::Storage image;
        image.ram.resize(PresentationMailbox::kImageBytes);
        if(!events.empty())image.events.reserve(DKR_OWNED_MAX_DRAW_EVENTS);
        if(!events.empty())image.events.assign(events.begin(),events.end());
        ++buffers.image_allocations;
        if(!events.empty())++buffers.metadata_allocations;
        return image;
    }
    void release_image(RenderSnapshot::Storage image) noexcept {
        std::scoped_lock lock(buffers_mutex);
        for(auto& destination:images)if(destination.ram.empty()){destination=std::move(image);return;}
        std::terminate(); // A duplicated release is an ownership error, never spill memory.
    }
    std::vector<std::uint8_t> acquire_decoder() {
        std::scoped_lock lock(buffers_mutex);
        if(!decoder.empty()){++buffers.decode_reuses;return std::move(decoder);}
        if(buffers.decode_allocations==1)throw std::bad_alloc();
        std::vector<std::uint8_t> image(PresentationMailbox::kImageBytes);
        ++buffers.decode_allocations;return image;
    }
    void release_decoder(std::vector<std::uint8_t> image) noexcept {
        std::scoped_lock lock(buffers_mutex);decoder=std::move(image);
    }
};
namespace {
bool valid(RenderDescriptor d,std::span<const std::uint8_t> ram) {
    if(ram.size()!=PresentationMailbox::kImageBytes || d.frame==UINT32_MAX || d.black>1 ||
       d.display_start<0x80000000U || (d.display_start&7) || (d.display_end&7) ||
       d.display_end<d.display_start || d.display_end-d.display_start<16 ||
       d.display_end>0x80800000U ||
       d.framebuffer<0x80000000U || d.framebuffer>0x80800000U-320U*240U*2U || (d.framebuffer&7) ||
       d.depthbuffer<0x80000000U || d.depthbuffer>0x80800000U-320U*240U*2U || (d.depthbuffer&7))return false;
    std::uint32_t words[4];
    std::memcpy(words,ram.data()+d.display_end-0x80000000U-16,sizeof(words));
    return words[0]==0xE9000000U && !words[1] && words[2]==0xB8000000U && !words[3];
}
}
RenderSnapshot::RenderSnapshot(std::uint64_t epoch,std::uint64_t generation,RenderDescriptor descriptor,
                             std::span<const std::uint8_t> ram,std::span<const dkr_owned_draw_event> events,
                             std::shared_ptr<SubmissionDomain> domain,std::shared_ptr<const LocalSceneryScene> scenery)
    :epoch_(epoch),generation_(generation),descriptor_(descriptor),local_scenery_(std::move(scenery)),
     domain_(std::move(domain)),storage_(domain_->acquire_image(events)) {
    std::memcpy(storage_.ram.data(),ram.data(),ram.size());
}
std::uint64_t RenderSnapshot::hash() const {
    std::call_once(hash_once_,[this]{hash_=checkpoint_hash(storage_.ram);});
    return hash_;
}
RenderSnapshot::~RenderSnapshot() {
    if(consumed_){std::scoped_lock lock(domain_->mutex);--domain_->consumers;}
    domain_->release_image(std::move(storage_));
}
std::unique_ptr<SubmissionPermit> SubmissionPermit::acquire(std::shared_ptr<const RenderSnapshot> snapshot) {
    if(!snapshot)return {};
    // Allocate BEFORE claiming. Allocation failure cannot strand a drain count.
    std::unique_ptr<SubmissionPermit> permit;
    try {permit.reset(new SubmissionPermit(snapshot));}catch(...){return {};}
    auto& domain=*snapshot->domain_;
    std::scoped_lock lock(domain.mutex);
    if(!domain.alive || domain.epoch!=snapshot->epoch() || domain.generation!=snapshot->generation()) {
        // An unclaimed candidate must not run the counted-release destructor.
        permit->snapshot_.reset();return {};
    }
    ++domain.in_flight;return permit;
}
SubmissionPermit::~SubmissionPermit() {
    if(!snapshot_)return;
    auto& domain=*snapshot_->domain_;
    std::scoped_lock lock(domain.mutex);--domain.in_flight;
}
PresentationMailbox::PresentationMailbox():domain_(std::make_shared<SubmissionDomain>()) {}
PresentationMailbox::~PresentationMailbox() {
    std::scoped_lock lock(domain_->mutex);domain_->alive=false;
}
DecodeWorkspace::DecodeWorkspace(std::shared_ptr<const RenderSnapshot> snapshot)
    :snapshot_(std::move(snapshot)),ram_(snapshot_->domain_->acquire_decoder()) {
    const auto bytes=snapshot_->bytes();std::memcpy(ram_.data(),bytes.data(),bytes.size());
}
std::unique_ptr<DecodeWorkspace> DecodeWorkspace::create(std::shared_ptr<const RenderSnapshot> snapshot) {
    performance::Scope timing(performance::Stage::Decode);
    if(!snapshot || snapshot->decoder_claimed_.exchange(true,std::memory_order_acq_rel))return {};
    try {return std::unique_ptr<DecodeWorkspace>(new DecodeWorkspace(snapshot));}
    catch(...) {snapshot->decoder_claimed_.store(false,std::memory_order_release);return {};}
}
DecodeWorkspace::~DecodeWorkspace() {
    // Release writable payload BEFORE another caller can claim this lease.
    snapshot_->domain_->release_decoder(std::move(ram_));
    snapshot_->decoder_claimed_.store(false,std::memory_order_release);
}
bool PresentationMailbox::begin_epoch(std::uint64_t epoch) {
    std::scoped_lock lock(mutex_);
    if(!epoch || epoch<=epoch_ || generation_==UINT64_MAX || !active_.expired())return false;
    {std::scoped_lock domain_lock(domain_->mutex);if(domain_->consumers)return false;}
    pending_.reset();last_frame_.reset();epoch_=epoch;++generation_;
    {std::scoped_lock domain_lock(domain_->mutex);domain_->epoch=epoch_;domain_->generation=generation_;}
    return true;
}
bool PresentationMailbox::publish(std::uint64_t epoch,RenderDescriptor descriptor,std::span<const std::uint8_t> ram,
                                 std::span<const dkr_owned_draw_event> events,std::shared_ptr<const LocalSceneryScene> scenery) {
    performance::Scope timing(performance::Stage::Publish);
    std::scoped_lock producer_lock(producer_mutex_);
    if(events.size()>DKR_OWNED_MAX_DRAW_EVENTS)return false;
    for(const auto& e:events)if(!dkr_owned_draw_event_valid(&e))return false;
    std::uint64_t generation;
    {
        std::scoped_lock lock(mutex_);
        if(!epoch_ || epoch!=epoch_ || (last_frame_ && descriptor.frame<=*last_frame_) || !valid(descriptor,ram))return false;
        generation=generation_;
    }
    // Allocate/validate before replacing the previous pending frame. Failure
    // leaves that frame intact; at most four payload images can exist here.
    try {
        auto candidate=std::shared_ptr<const RenderSnapshot>(new RenderSnapshot(epoch,generation,descriptor,ram,events,domain_,std::move(scenery)));
        std::scoped_lock lock(mutex_);
        // Never enqueue a stale candidate if correction/scene retirement won
        // the race while its immutable image was being copied.
        if(epoch!=epoch_ || generation!=generation_ ||
           (last_frame_ && descriptor.frame<=*last_frame_))return false;
        pending_=std::move(candidate);last_frame_=descriptor.frame;return true;
    }catch(...){return false;}
}
std::shared_ptr<const RenderSnapshot> PresentationMailbox::take() {
    std::scoped_lock lock(mutex_);
    if(!active_.expired() || !pending_)return {};
    auto result=std::move(pending_);active_=result;
    // take() is the sole producer of this flag, before exposing the lease.
    result->consumed_=true;
    {std::scoped_lock domain_lock(domain_->mutex);++domain_->consumers;}
    return result;
}
bool PresentationMailbox::finish_decode(const std::shared_ptr<const RenderSnapshot>& snapshot) {
    std::scoped_lock lock(mutex_);
    if(!snapshot)return false;
    const auto active=active_.lock();
    if(!active || active!=snapshot || active->decoder_claimed_.load(std::memory_order_acquire))return false;
    active_.reset();return true;
}
bool PresentationMailbox::invalidate_from(std::uint64_t epoch,std::uint32_t first) {
    std::scoped_lock lock(mutex_);
    if(!epoch_ || epoch!=epoch_ || generation_==UINT64_MAX || first==UINT32_MAX ||
       !last_frame_ || first>*last_frame_)return false;
    retire_locked(first);return true;
}
bool PresentationMailbox::retire_for_restore(std::uint64_t epoch,std::uint32_t first) {
    std::scoped_lock lock(mutex_);
    if(!epoch_ || epoch!=epoch_ || generation_==UINT64_MAX || first==UINT32_MAX)return false;
    retire_locked(first);return true;
}
void PresentationMailbox::retire_locked(std::uint32_t first) {
    // Even a previously valid older lease is conservatively retired. A real
    // renderer must recheck this generation before presentation and retain
    // its last completed image while a replacement is being produced.
    ++generation_;pending_.reset();
    {std::scoped_lock domain_lock(domain_->mutex);domain_->generation=generation_;}
    if(last_frame_ && first<=*last_frame_)
        last_frame_=first ? std::optional<std::uint32_t>(first-1):std::nullopt;
}
bool PresentationMailbox::is_current(const std::shared_ptr<const RenderSnapshot>& snapshot) const {
    std::scoped_lock lock(mutex_);
    const auto active=active_.lock();
    return snapshot && active==snapshot && snapshot->epoch()==epoch_ && snapshot->generation()==generation_;
}
bool PresentationMailbox::quiescent() const {
    std::scoped_lock lock(mutex_);
    std::scoped_lock domain_lock(domain_->mutex);
    return active_.expired() && !pending_ && !domain_->consumers;
}
bool PresentationMailbox::submissions_drained() const {
    std::scoped_lock lock(domain_->mutex);return domain_->in_flight==0;
}
PresentationMailbox::BufferStatistics PresentationMailbox::buffer_statistics() const {
    std::scoped_lock lock(domain_->buffers_mutex);return domain_->buffers;
}
}
