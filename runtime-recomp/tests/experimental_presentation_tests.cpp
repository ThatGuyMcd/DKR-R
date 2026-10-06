#include "netplay/experimental_presentation.hpp"
#include "netplay/experimental_checkpoint_hash.hpp"
#include "netplay/experimental_present_gate.hpp"
#include "netplay/experimental_performance.hpp"
#include "renderer_snapshot.hpp"
#include <cassert>
#include <cstring>
#include <thread>
#include <atomic>
#include <iostream>
using namespace dkr::runtime::netplay::experimental;
int main() {
    // Compile with the ownership suite; execution is left to qualification.
    // Profiling admission cannot turn stable queue work into an owned frame.
    namespace perf=dkr::runtime::netplay::experimental::performance;
    perf::set_enabled(false);
    const auto samples=perf::counters[unsigned(perf::Stage::Metadata)].count.load();
    perf::record(perf::Stage::Metadata,1234);
    assert(perf::counters[unsigned(perf::Stage::Metadata)].count.load()==samples);
    perf::set_enabled(true);
    perf::record(perf::Stage::Metadata,1234);
    assert(perf::counters[unsigned(perf::Stage::Metadata)].count.load()==samples+1);
    assert(perf::counters[unsigned(perf::Stage::Metadata)].maximum.load()>=1234);
    assert(perf::counters[unsigned(perf::Stage::Metadata)].histogram[0].load()>0);
    assert(!perf::observed_present);
    {
        perf::PresentScope first(true,1);
        assert(perf::observed_present);
        perf::presented();
        {
            perf::PresentScope stable(false,1);
            perf::presented();
            assert(!perf::last_present.time_since_epoch().count());
        }
        assert(perf::observed_present);
    }
    assert(!perf::observed_present);
    perf::set_enabled(false);
    std::vector<std::uint8_t> ram(PresentationMailbox::kImageBytes);
    const std::uint32_t end[4]={0xE9000000U,0,0xB8000000U,0};
    std::memcpy(ram.data()+0x1010,end,sizeof(end));
    RenderDescriptor descriptor{0,0x80001000U,0x80001020U,0x80100000U,0x80200000U};
    PresentationMailbox box,foreign;
    assert(!box.publish(1,descriptor,ram) && !box.take() && box.quiescent());
    assert(!box.begin_epoch(0) && box.begin_epoch(3) && !box.begin_epoch(3));
    assert(box.publish(3,descriptor,ram) && !box.quiescent());
    auto lease=box.take();assert(lease && box.is_current(lease) && !foreign.is_current(lease));
    const auto expected=checkpoint_hash(ram);
    // Diagnostic hashing is lazy but still thread-safe and byte-identical.
    // First access may occur on concurrent consumers, after publication.
    std::thread first_hash([&]{assert(lease->hash()==expected);});
    assert(lease->hash()==expected);first_hash.join();
    assert(!box.take() && !box.begin_epoch(4));
    descriptor.frame=1;ram[0x3000]=51;assert(box.publish(3,descriptor,ram));
    descriptor.frame=2;ram[0x3000]=52;assert(box.publish(3,descriptor,ram));
    assert(lease->hash()==expected && checkpoint_hash(lease->bytes())==expected);
    assert(!box.publish(2,descriptor,ram) && !box.publish(3,descriptor,ram));
    // Full validation BEFORE pending replacement; malformed descriptors and
    // command terminators cannot make a valid queued frame disappear.
    auto corrupt=descriptor;corrupt.frame=3;
    for(unsigned fault=0;fault<10;++fault) {
        auto bad=corrupt;
        if(fault==0)bad.frame=UINT32_MAX;
        if(fault==1)bad.display_start=0;
        if(fault==2)bad.display_start+=1;
        if(fault==3)bad.display_end=bad.display_start+8;
        if(fault==4)bad.display_end=0x80800008U;
        if(fault==5)bad.framebuffer=0;
        if(fault==6)bad.framebuffer=0x807FFFF8U;
        if(fault==7)bad.depthbuffer=0x80800000U;
        if(fault==8)bad.depthbuffer+=1;
        if(fault==9)bad.black=2;
        assert(!box.publish(3,bad,ram));
    }
    assert(!box.publish(3,corrupt,std::span(ram).first(ram.size()-1)));
    ram[0x1010]^=1;assert(!box.publish(3,corrupt,ram));ram[0x1010]^=1;
    assert(!box.invalidate_from(2,0) && !box.invalidate_from(3,3) && box.is_current(lease));
    assert(box.invalidate_from(3,1) && !box.is_current(lease));
    descriptor.frame=1;ram[0x3000]=11;assert(box.publish(3,descriptor,ram));
    // Delayed consumer can read while CPU buffers change. It cannot observe
    // rewritten guest data or acquire another lease before releasing this one.
    std::atomic<bool> ready=false,done=false;
    std::thread reader([&] {
        ready.store(true);
        while(!done.load())assert(checkpoint_hash(lease->bytes())==expected);
        assert(checkpoint_hash(lease->bytes())==expected);
    });
    while(!ready.load())std::this_thread::yield();
    for(unsigned f=2;f<20;++f) {
        descriptor.frame=f;ram[0x3000]=std::uint8_t(f);assert(box.publish(3,descriptor,ram));
        assert(!box.take());
    }
    done.store(true);reader.join();
    lease.reset();lease=box.take();assert(lease && lease->descriptor().frame==19 && box.is_current(lease));
    assert(lease->bytes()[0x3000]==19);
    assert(box.invalidate_from(3,0) && !box.is_current(lease));
    descriptor.frame=0;assert(box.publish(3,descriptor,ram));
    lease.reset();lease=box.take();assert(lease && lease->descriptor().frame==0);
    lease.reset();assert(box.quiescent() && box.begin_epoch(4));
    assert(!box.publish(3,descriptor,ram) && box.publish(4,descriptor,ram));
    lease=box.take();assert(lease && !foreign.is_current(lease));
    // Lease payload remains alive even after its mailbox is destroyed.
    std::shared_ptr<const RenderSnapshot> survivor;
    {PresentationMailbox transient;assert(transient.begin_epoch(1) && transient.publish(1,descriptor,ram));survivor=transient.take();}
    assert(survivor && checkpoint_hash(survivor->bytes())==survivor->hash());
    assert(!SubmissionPermit::acquire(survivor)); // Destroyed mailbox cannot submit.
    PresentationMailbox rewind;
    assert(!rewind.retire_for_restore(1,0) && rewind.begin_epoch(1));
    assert(rewind.retire_for_restore(1,0)); // Correction before first publication.
    descriptor.frame=4;assert(rewind.publish(1,descriptor,ram));
    auto held=rewind.take();assert(held && rewind.is_current(held));
    assert(!rewind.retire_for_restore(2,5) && !rewind.retire_for_restore(1,UINT32_MAX));
    assert(rewind.is_current(held));
    assert(rewind.retire_for_restore(1,5) && !rewind.is_current(held));
    assert(!rewind.publish(1,descriptor,ram)); // Does not invent newer history.
    descriptor.frame=5;assert(rewind.publish(1,descriptor,ram) && !rewind.take());
    assert(checkpoint_hash(held->bytes())==held->hash());held.reset();
    held=rewind.take();assert(held && rewind.is_current(held));
    assert(!DecodeWorkspace::create({}));
    auto workspace=DecodeWorkspace::create(held);
    assert(workspace && workspace->bytes().size()==PresentationMailbox::kImageBytes);
    assert(checkpoint_hash(workspace->bytes())==held->hash());
    assert(!DecodeWorkspace::create(held)); // Never duplicate one decoder's mutable image.
    auto* live_core=ram.data();auto* live_state=ram.data()+4;
    auto* core=live_core;auto* state=live_state;
    try {
        dkr::runtime::RendererSnapshotScope scope(core,state,workspace->bytes().data());
        assert(core==workspace->bytes().data() && state==core);
        core[0x3000]^=0xFF; // Real scope permits renderer readback/scratch writes.
        throw 1;
    }catch(int){}
    assert(core==live_core && state==live_state);
    assert(checkpoint_hash(held->bytes())==held->hash());
    assert(checkpoint_hash(workspace->bytes())!=held->hash());
    assert(rewind.retire_for_restore(1,0) && !rewind.is_current(workspace->snapshot()));
    held.reset();
    assert(!rewind.quiescent() && !rewind.begin_epoch(2));
    assert(checkpoint_hash(workspace->snapshot()->bytes())==workspace->snapshot()->hash());
    workspace.reset();
    assert(rewind.quiescent() && rewind.begin_epoch(2));
    // Concurrent callers cannot allocate two writable workspaces for one lease.
    descriptor.frame=0;assert(rewind.publish(2,descriptor,ram));held=rewind.take();
    std::unique_ptr<DecodeWorkspace> first,second;
    std::thread a([&]{first=DecodeWorkspace::create(held);});
    std::thread b([&]{second=DecodeWorkspace::create(held);});a.join();b.join();
    assert(bool(first)!=bool(second));
    held.reset();assert(!rewind.quiescent());first.reset();second.reset();assert(rewind.quiescent());
    // Real queue seam: enqueue identity is insufficient; retirement between
    // enqueue and consumption MUST reject without retaining old RAM images.
    int owner=0,slot=0;
    assert(present_gate::acquire(&slot,1,1).allowed); // Unregistered stable path.
    assert(rewind.begin_epoch(3)&&rewind.publish(3,descriptor,ram));held=rewind.take();
    {
        present_gate::Registration registration(&owner);
        assert(!present_gate::scoped_color_address(&owner));
        assert(!present_gate::workload_frame(&slot,1).owned);
        {present_gate::Scope scoped(registration,held);
         assert(present_gate::scoped_color_address(&owner)==0x00100000U);
         assert(!present_gate::scoped_color_address(&slot));
         present_gate::tag_workload(&owner,&slot,1,false);}
        auto temporal=present_gate::workload_frame(&slot,1);
        assert(temporal.owned && temporal.visible && temporal.epoch==held->epoch() &&
               temporal.generation==held->generation() && temporal.color_address==0x00100000U);
        assert(!present_gate::workload_frame(&slot,2).owned);
        auto next_temporal=temporal;++next_temporal.frame;
        assert(present_gate::same_visual_history(temporal,next_temporal));
        assert(!present_gate::same_visual_history(temporal,temporal));
        ++next_temporal.generation;assert(!present_gate::same_visual_history(temporal,next_temporal));
        next_temporal=temporal;++next_temporal.epoch;++next_temporal.frame;
        assert(!present_gate::same_visual_history(temporal,next_temporal));
        assert(present_gate::same_visual_history({},{})); // Stable matching unchanged.
        present_gate::tag_workload(&owner,&slot,2,true);
        assert(present_gate::workload_frame(&slot,2).generation==temporal.generation);
        present_gate::tag_workload(&owner,&slot,3,false);
        assert(!present_gate::workload_frame(&slot,3).owned);
        assert(!registration.completed(3,1));
        present_gate::complete_workload(&slot,2);assert(!registration.completed(3,1));
        present_gate::complete_workload(&slot,3);assert(!registration.completed(3,1));
        {present_gate::Scope scoped(registration,held);present_gate::tag(&owner,&slot,1,1);}
        present_gate::complete_present(&slot,1,2);assert(!registration.completed(3,1));
        present_gate::complete_present(&slot,1,1);assert(registration.completed(3,1));
        registration.wait_completed(3,1);
        assert(!present_gate::acquire(&slot,2,1).allowed); // Slot/id mismatch is not stable.
        auto started=present_gate::acquire(&slot,1,1);
        assert(started.allowed&&started.permit&&!rewind.submissions_drained());
        assert(rewind.retire_for_restore(3,0));
        assert(!present_gate::acquire(&slot,1,1).allowed&&!rewind.submissions_drained());
        started.permit.reset();assert(rewind.submissions_drained());
        held.reset();assert(rewind.quiescent()); // Registry slots use WEAK ownership.
        assert(!present_gate::acquire(&slot,1,1).allowed); // Expired experimental token.
        present_gate::tag(&owner,&slot,2,2); // Empty producer scope returns to stable behavior.
        auto original=present_gate::acquire(&slot,2,2);assert(original.allowed&&!original.permit);
        assert(rewind.publish(3,descriptor,ram));held=rewind.take();
        {present_gate::Scope scoped(registration,held);present_gate::tag(&owner,&slot,3,3);}
        auto hold=std::make_unique<present_gate::QueueHoldForTest>(registration);
        present_gate::Admission queued;
        std::thread worker([&]{queued=present_gate::acquire(&slot,3,3);});
        assert(hold->wait(std::chrono::seconds(2)));
        assert(rewind.retire_for_restore(3,0));hold.reset();worker.join();
        assert(!queued.allowed&&!queued.permit&&rewind.submissions_drained());
        held.reset();assert(rewind.quiescent());
        assert(rewind.publish(3,descriptor,ram));held=rewind.take();
        {present_gate::Scope scoped(registration,held);present_gate::tag(&owner,&slot,4,4);}
        hold=std::make_unique<present_gate::QueueHoldForTest>(registration,present_gate::HoldPoint::AfterAdmission);
        std::thread started_worker([&]{queued=present_gate::acquire(&slot,4,4);});
        assert(hold->wait(std::chrono::seconds(2))&&!rewind.submissions_drained());
        assert(rewind.retire_for_restore(3,0)&&!rewind.submissions_drained());
        assert(!present_gate::acquire(&slot,4,4).allowed); // New claims are retired.
        hold.reset();started_worker.join();
        assert(queued.allowed&&queued.permit&&!rewind.submissions_drained());
        queued.permit.reset();assert(rewind.submissions_drained());
        const auto stats=registration.statistics();assert(stats.tagged==3&&stats.accepted==2&&stats.rejected==5);
        held.reset();assert(rewind.quiescent());
    }
    assert(present_gate::acquire(&slot,3,3).allowed); // Drained registration retired.
    // Steady-state allocation is capped INCLUDING cached images. Exercise
    // active+pending+replacement and writable decode reuse across retirement.
    PresentationMailbox pooled;assert(pooled.begin_epoch(1));
    descriptor.frame=0;assert(pooled.publish(1,descriptor,ram));
    auto pooled_lease=pooled.take();auto pooled_decode=DecodeWorkspace::create(pooled_lease);
    const auto pooled_original=pooled_lease->bytes()[0x3000];
    for(unsigned frame=1;frame<100;++frame) {
        descriptor.frame=frame;ram[0x3000]=std::uint8_t(frame);
        assert(pooled.publish(1,descriptor,ram));
        assert(pooled_lease->bytes()[0x3000]==pooled_original);
    }
    auto allocations=pooled.buffer_statistics();
    assert(allocations.image_allocations==3 && allocations.decode_allocations==1 && allocations.image_reuses>=97);
    pooled_decode.reset();pooled_lease.reset();pooled_lease=pooled.take();
    pooled_decode=DecodeWorkspace::create(pooled_lease);
    assert(pooled_decode && pooled_decode->bytes()[0x3000]==99 && pooled.buffer_statistics().decode_reuses==1);
    assert(pooled.retire_for_restore(1,0));
    pooled_decode.reset();pooled_lease.reset();assert(pooled.begin_epoch(2));
    descriptor.frame=0;assert(pooled.publish(2,descriptor,ram));pooled_lease=pooled.take();
    assert(pooled_lease->generation()>1 && pooled.buffer_statistics().image_allocations==3);
    assert(!pooled.finish_decode({}));
    auto decoding=DecodeWorkspace::create(pooled_lease);assert(decoding&&!pooled.finish_decode(pooled_lease));
    decoding.reset();assert(pooled.finish_decode(pooled_lease));
    assert(!pooled.quiescent()&&!pooled.begin_epoch(3)); // Retained GPU lease still owns its epoch.
    descriptor.frame=1;assert(pooled.publish(2,descriptor,ram));
    auto queued_second=pooled.take();assert(queued_second&&pooled.finish_decode(queued_second));
    descriptor.frame=2;assert(pooled.publish(2,descriptor,ram));
    descriptor.frame=3;assert(pooled.publish(2,descriptor,ram)); // Two queues + pending + candidate.
    assert(pooled.buffer_statistics().image_allocations==4);
    assert(pooled.retire_for_restore(2,0));
    assert(!pooled.quiescent()&&!pooled.begin_epoch(3));
    pooled_lease.reset();queued_second.reset();assert(pooled.quiescent()&&pooled.begin_epoch(3));
    // Draw observations share the image's ownership and bounded reuse pool.
    // Empty frames clear old events without invalidating a retained reader;
    // queued GPU owners must never observe another frame's metadata.
    {
        PresentationMailbox metadata;assert(metadata.begin_epoch(1));
        dkr_owned_draw_event event{};event.kind=DKR_OWNED_MATRIX;
        event.address=0x80004000U;event.token=0x12345678U;
        assert(dkr_owned_draw_event_valid(&event));
        auto projection=event;
        projection.parameters[4]=60;projection.parameters[5]=0x42A00000;projection.parameters[6]=3;
        assert(dkr_owned_draw_event_valid(&projection));
        projection.parameters[5]=0x7FC00000;assert(!dkr_owned_draw_event_valid(&projection));
        projection.parameters[5]=0x42A00000;projection.parameters[6]=4;
        assert(!dkr_owned_draw_event_valid(&projection));
        projection.parameters[6]=3;projection.parameters[4]=0;
        assert(!dkr_owned_draw_event_valid(&projection));
        projection=event;projection.parameters[7]=1;
        assert(!dkr_owned_draw_event_valid(&projection));
        std::vector<dkr_owned_draw_event> events(1,event);
        descriptor.frame=0;assert(metadata.publish(1,descriptor,ram,events));
        auto first=metadata.take();assert(first&&metadata.finish_decode(first));
        for(unsigned frame=1;frame<100;++frame) {
            descriptor.frame=frame;
            const unsigned count=frame%4==0 ? 0 : frame%4==1 ? 1 :
                frame%4==2 ? 128 : DKR_OWNED_MAX_DRAW_EVENTS;
            event.token=frame;events.assign(count,event);
            assert(metadata.publish(1,descriptor,ram,events));
            assert(first->draw_events().size()==1&&
                   first->draw_events().front().token==0x12345678U);
        }
        auto stats=metadata.buffer_statistics();
        assert(stats.image_allocations==3&&stats.metadata_allocations==3&&
               stats.metadata_reuses>60);
        auto second=metadata.take();assert(second&&metadata.finish_decode(second));
        assert(second->draw_events().size()==DKR_OWNED_MAX_DRAW_EVENTS&&
               second->draw_events().back().token==99);
        descriptor.frame=100;event.token=100;events.assign(128,event);
        assert(metadata.publish(1,descriptor,ram,events));
        auto third=metadata.take();assert(third&&metadata.finish_decode(third));
        descriptor.frame=101;event.token=101;events.assign(1,event);
        assert(metadata.publish(1,descriptor,ram,events));
        assert(metadata.buffer_statistics().image_allocations==4);
        // All four images are owned. Reject a fifth without discarding the
        // pending frame, changing its metadata or claiming more capacity.
        descriptor.frame=102;event.token=102;events.assign(1,event);
        assert(!metadata.publish(1,descriptor,ram,events));
        auto pending=metadata.take();assert(pending&&pending->descriptor().frame==101&&
            pending->draw_events().front().token==101&&metadata.finish_decode(pending));
        assert(first->draw_events().front().token==0x12345678U&&
               second->draw_events().back().token==99&&third->draw_events().back().token==100);
        first.reset();assert(metadata.publish(1,descriptor,ram,events));
        assert(metadata.buffer_statistics().image_allocations==4);
        assert(metadata.retire_for_restore(1,0));
        assert(!metadata.begin_epoch(2));
        second.reset();third.reset();pending.reset();
        assert(metadata.quiescent()&&metadata.begin_epoch(2));
        descriptor.frame=0;assert(metadata.publish(2,descriptor,ram));
        auto empty=metadata.take();assert(empty&&empty->draw_events().empty());
        assert(metadata.finish_decode(empty));empty.reset();
        // Admission is unchanged, including the metadata-size limit.
        events.resize(DKR_OWNED_MAX_DRAW_EVENTS+1,event);descriptor.frame=1;
        assert(!metadata.publish(2,descriptor,ram,events)&&metadata.quiescent());
        stats=metadata.buffer_statistics();
        assert(stats.metadata_allocations==4&&stats.image_allocations==4);
        static_assert(PresentationMailbox::kPeakDrawEventBytes==
            4U*DKR_OWNED_MAX_DRAW_EVENTS*sizeof(dkr_owned_draw_event));
    }
    std::cout<<"Immutable frame handover: rewind, replacement, delayed reader, drain and invalid descriptors passed; no RT64/VI admission.\n";
}
