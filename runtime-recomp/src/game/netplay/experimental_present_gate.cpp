#include "experimental_present_gate.hpp"
#include "experimental_presentation.hpp"
#include <array>
#include <algorithm>
#include <condition_variable>
#include <stdexcept>

namespace dkr::runtime::netplay::experimental::present_gate {
struct Owner {
    struct Record {
        const void* slot=nullptr;
        std::uint64_t present=0,workload=0;
        // Slot metadata must NOT retain four RAM images or strand mailbox
        // leases. Only the producer workspace and an active permit own RAM.
        std::weak_ptr<const RenderSnapshot> snapshot;
        bool experimental=false;
    };
    const void* state=nullptr;
    std::array<Record,4> records{}; // RT64 PRESENT_QUEUE_SIZE, bounded per owner.
    struct WorkloadRecord {const void* slot=nullptr;std::uint64_t id=0;WorkloadFrame frame;};
    std::array<WorkloadRecord,4> workloads{}; // RT64 WORKLOAD_QUEUE_SIZE.
    mutable std::mutex mutex;
    std::condition_variable condition;
    Statistics stats;
    std::uint64_t completed_workload=0,completed_present=0;
    bool holding=false,worker_waiting=false,alive=true;
    HoldPoint hold_point=HoldPoint::BeforeAdmission;
};
namespace {
struct Registry {
    std::mutex mutex;
    std::array<std::shared_ptr<Owner>,8> owners{};
};
Registry& registry() {static Registry result;return result;}
thread_local const void* scoped_state=nullptr;
thread_local std::shared_ptr<const RenderSnapshot> scoped_snapshot;
void hold_for_test(const std::shared_ptr<Owner>& target,HoldPoint point) {
    std::unique_lock lock(target->mutex);
    if(target->holding && target->hold_point==point) {
        target->worker_waiting=true;target->condition.notify_all();
        target->condition.wait(lock,[&]{return !target->holding||!target->alive;});
        target->worker_waiting=false;
    }
}
}
Registration::Registration(const void* state) {
    if(!state)throw std::invalid_argument("Missing experimental renderer identity");
    auto candidate=std::make_shared<Owner>();candidate->state=state;
    auto& all=registry();std::scoped_lock lock(all.mutex);
    for(const auto& owner:all.owners)if(owner&&owner->state==state)
        throw std::logic_error("Experimental renderer already registered");
    for(auto& owner:all.owners)if(!owner){owner=owner_=std::move(candidate);return;}
    throw std::length_error("Experimental renderer registry exhausted");
}
Registration::~Registration() {
    // Caller has already stopped/joined the workers. Release test holds even
    // on failed qualification, and retire EVERY slot before address reuse.
    {
        std::scoped_lock lock(owner_->mutex);owner_->holding=false;owner_->alive=false;
        for(auto& record:owner_->records)record={};
        for(auto& record:owner_->workloads)record={};
    }
    owner_->condition.notify_all();
    auto& all=registry();std::scoped_lock lock(all.mutex);
    for(auto& owner:all.owners)if(owner==owner_){owner.reset();break;}
}
Statistics Registration::statistics() const {std::scoped_lock lock(owner_->mutex);return owner_->stats;}
bool Registration::completed(std::uint64_t workload,std::uint64_t present) const {
    std::scoped_lock lock(owner_->mutex);
    return owner_->completed_workload>=workload && owner_->completed_present>=present;
}
void Registration::wait_completed(std::uint64_t workload,std::uint64_t present) const {
    std::unique_lock lock(owner_->mutex);
    owner_->condition.wait(lock,[&]{return !owner_->alive ||
        (owner_->completed_workload>=workload && owner_->completed_present>=present);});
}
const void* Registration::state() const {return owner_->state;}
Scope::Scope(const Registration& registration,std::shared_ptr<const RenderSnapshot> snapshot)
    :previous_state_(scoped_state),previous_snapshot_(scoped_snapshot) {
    if(!snapshot)throw std::invalid_argument("Missing experimental present snapshot");
    scoped_state=registration.state();scoped_snapshot=std::move(snapshot);
}
Scope::~Scope() {scoped_state=previous_state_;scoped_snapshot=std::move(previous_snapshot_);}
Admission::Admission()=default;
Admission::~Admission()=default;
Admission::Admission(Admission&&) noexcept=default;
Admission& Admission::operator=(Admission&&) noexcept=default;
std::uint32_t scoped_color_address(const void* state) {
    if(state!=scoped_state||!scoped_snapshot||scoped_snapshot->descriptor().black)return 0;
    return scoped_snapshot->descriptor().framebuffer & 0x007fffffU;
}
void tag_workload(const void* state,const void* slot,std::uint64_t id,bool paused) {
    auto& all=registry();std::scoped_lock registry_lock(all.mutex);
    for(const auto& owner:all.owners)if(owner&&owner->state==state) {
        std::scoped_lock lock(owner->mutex);
        Owner::WorkloadRecord* record=nullptr;
        for(auto& value:owner->workloads)if(value.slot==slot){record=&value;break;}
        if(!record)for(auto& value:owner->workloads)if(!value.slot){record=&value;break;}
        if(!slot||!id||!record||!owner->alive)throw std::logic_error("Invalid owned workload slot");
        WorkloadFrame frame;
        if(state==scoped_state&&scoped_snapshot) {
            const auto& d=scoped_snapshot->descriptor();
            frame={scoped_snapshot->epoch(),scoped_snapshot->generation(),d.frame,
                d.framebuffer&0x007fffffU,true,!d.black};
        } else if(paused)frame=record->frame; // Repeat identity, never a new logical frame.
        *record={slot,id,frame};return;
    }
}
WorkloadFrame workload_frame(const void* slot,std::uint64_t id) {
    auto& all=registry();std::scoped_lock registry_lock(all.mutex);
    for(const auto& owner:all.owners)if(owner) {
        std::scoped_lock lock(owner->mutex);
        for(const auto& record:owner->workloads)if(record.slot==slot)
            return owner->alive&&record.id==id?record.frame:WorkloadFrame{};
    }
    return {};
}
bool same_visual_history(WorkloadFrame previous,WorkloadFrame current) {
    // Gaps from latest-only publication are fine; corrected generations and
    // scene epochs are not. A repeat cannot become a new animation endpoint.
    return !current.owned || (previous.owned && previous.visible && current.visible &&
        previous.epoch==current.epoch && previous.generation==current.generation &&
        previous.frame<current.frame);
}
void complete_workload(const void* slot,std::uint64_t workload) {
    auto& all=registry();std::scoped_lock registry_lock(all.mutex);
    for(const auto& owner:all.owners)if(owner) {
        std::scoped_lock lock(owner->mutex);
        for(const auto& record:owner->workloads)if(record.slot==slot && record.id==workload && owner->alive) {
            owner->completed_workload=(std::max)(owner->completed_workload,workload);
            owner->condition.notify_all();return;
        }
    }
}
void complete_present(const void* slot,std::uint64_t present,std::uint64_t workload) {
    auto& all=registry();std::scoped_lock registry_lock(all.mutex);
    for(const auto& owner:all.owners)if(owner) {
        std::scoped_lock lock(owner->mutex);
        for(const auto& record:owner->records)if(record.slot==slot && record.present==present &&
                record.workload==workload && owner->alive) {
            owner->completed_present=(std::max)(owner->completed_present,present);
            owner->condition.notify_all();return;
        }
    }
}
void tag(const void* state,const void* slot,std::uint64_t present,std::uint64_t workload) {
    std::shared_ptr<Owner> target;
    {
        auto& all=registry();std::scoped_lock lock(all.mutex);
        for(const auto& owner:all.owners)if(owner&&owner->state==state){target=owner;break;}
    }
    if(!target)return;
    std::scoped_lock lock(target->mutex);
    Owner::Record* record=nullptr;
    for(auto& value:target->records)if(value.slot==slot){record=&value;break;}
    if(!record)for(auto& value:target->records)if(!value.slot){record=&value;break;}
    if(!slot||!present||!record||!target->alive)throw std::logic_error("Invalid experimental present slot");
    const auto snapshot=state==scoped_state?scoped_snapshot:nullptr;
    *record={slot,present,workload,snapshot,bool(snapshot)};
    if(record->experimental)++target->stats.tagged;
}
Admission acquire(const void* slot,std::uint64_t present,std::uint64_t workload) {
    std::shared_ptr<Owner> target;
    std::shared_ptr<const RenderSnapshot> snapshot;
    bool mismatch=false;
    {
        auto& all=registry();std::scoped_lock lock(all.mutex);
        for(const auto& owner:all.owners)if(owner) {
            std::scoped_lock owner_lock(owner->mutex);
            for(const auto& record:owner->records)if(record.slot==slot) {
                target=owner;snapshot=record.snapshot.lock();
                mismatch=record.present!=present||record.workload!=workload||!owner->alive||
                         (record.experimental&&!snapshot);
                break;
            }
            if(target)break;
        }
    }
    // Empty scope is explicitly original behavior, not a stale experimental
    // token left over from a previous reuse of the four queue slots.
    if(!target||(!snapshot&&!mismatch))return {};
    hold_for_test(target,HoldPoint::BeforeAdmission);
    {std::scoped_lock lock(target->mutex);mismatch=mismatch||!target->alive;}
    Admission result;result.allowed=false;result.experimental=true;
    if(snapshot)result.epoch=snapshot->epoch();
    if(!mismatch)result.permit=SubmissionPermit::acquire(std::move(snapshot));
    result.allowed=bool(result.permit);
    if(result.allowed)hold_for_test(target,HoldPoint::AfterAdmission);
    {std::scoped_lock lock(target->mutex);
     if(result.allowed)++target->stats.accepted;else ++target->stats.rejected;}
    return result;
}
QueueHoldForTest::QueueHoldForTest(const Registration& registration,HoldPoint point):owner_(registration.owner_) {
    std::scoped_lock lock(owner_->mutex);
    if(owner_->holding||owner_->worker_waiting)throw std::logic_error("Nested experimental queue hold");
    owner_->holding=true;owner_->hold_point=point;
}
QueueHoldForTest::~QueueHoldForTest() {
    {std::scoped_lock lock(owner_->mutex);owner_->holding=false;}
    owner_->condition.notify_all();
}
bool QueueHoldForTest::wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(owner_->mutex);
    return owner_->condition.wait_for(lock,timeout,[&]{return owner_->worker_waiting;});
}
}
