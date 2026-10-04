#pragma once
#include <chrono>
#include <cstdint>
#include <memory>

// Vendor queue translation units are C++17. Keep this integration seam opaque;
// do not change a dependency's language standard or expose C++20 span there.
namespace dkr::runtime::netplay::experimental {class RenderSnapshot;class SubmissionPermit;}
namespace dkr::runtime::netplay::experimental::present_gate {
struct Owner;
struct Statistics { std::uint64_t tagged=0,accepted=0,rejected=0; };

// Private opt-in integration seam. The Patch Pipeline emits calls only when
// DKR_EXPERIMENTAL_RENDER_QUALIFICATION or DKR_EXPERIMENTAL_RACE_TEST is
// enabled. Unregistered stable renderers keep their original queue policy.
// Register after renderer setup, unregister AFTER renderer worker shutdown.
class Registration final {
public:
    explicit Registration(const void* state);
    ~Registration();
    Registration(const Registration&)=delete;
    Registration& operator=(const Registration&)=delete;
    Statistics statistics() const;
    bool completed(std::uint64_t workload,std::uint64_t present) const;
    void wait_completed(std::uint64_t workload,std::uint64_t present) const;
    const void* state() const;
private:
    friend class QueueHoldForTest;
    std::shared_ptr<Owner> owner_;
};
class Scope final {
public:
    Scope(const Registration& registration,std::shared_ptr<const RenderSnapshot> snapshot);
    ~Scope();
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
private:
    const void* previous_state_;
    std::shared_ptr<const RenderSnapshot> previous_snapshot_;
};
// Exact (slot address, present id, workload id) association. A renderer restart
// owns a fresh Registration; stale pointers alone are never an identity.
void tag(const void* state,const void* slot,std::uint64_t present,std::uint64_t workload);
struct WorkloadFrame {
    std::uint64_t epoch=0,generation=0;
    std::uint32_t frame=0,color_address=0;
    bool owned=false,visible=false;
};
// Producer-only exact target approval. This does NOT enable PresentEarly or
// use VI history. The actual queue admission still owns retirement safety.
std::uint32_t scoped_color_address(const void* state);
void tag_workload(const void* state,const void* slot,std::uint64_t workload,bool paused);
WorkloadFrame workload_frame(const void* slot,std::uint64_t workload);
bool same_visual_history(WorkloadFrame previous,WorkloadFrame current);
// Completion is AFTER the whole workload/present operation, not RT64's
// first-target notification. WSI resources keep RT64's original ownership.
void complete_workload(const void* slot,std::uint64_t workload);
void complete_present(const void* slot,std::uint64_t present,std::uint64_t workload);
struct Admission {
    Admission();
    ~Admission();
    Admission(Admission&&) noexcept;
    Admission& operator=(Admission&&) noexcept;
    Admission(const Admission&)=delete;
    Admission& operator=(const Admission&)=delete;
    bool allowed=true; // Unregistered stable renderer: original queue behavior.
    bool experimental=false;
    std::uint64_t epoch=0;
    std::unique_ptr<SubmissionPermit> permit;
};
Admission acquire(const void* slot,std::uint64_t present,std::uint64_t workload);

// Deterministic PRIVATE test interlock: park the worker before or after acquiring
// its generation permit, testing queued retirement and already-started draining. No
// launcher setting or production path enables this. Destructor always releases.
enum class HoldPoint { BeforeAdmission, AfterAdmission };
class QueueHoldForTest final {
public:
    explicit QueueHoldForTest(const Registration& registration,
                              HoldPoint point=HoldPoint::BeforeAdmission);
    ~QueueHoldForTest();
    bool wait(std::chrono::milliseconds timeout);
    QueueHoldForTest(const QueueHoldForTest&)=delete;
    QueueHoldForTest& operator=(const QueueHoldForTest&)=delete;
private:
    std::shared_ptr<Owner> owner_;
};
}
