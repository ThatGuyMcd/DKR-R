#include "online_presentation/frame_policy.hpp"
#include "netplay/experimental_presentation.hpp"
#include "netplay/experimental_present_gate.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
using namespace dkr::runtime::netplay::experimental;
int main() {
    int state = 0, slot = 0;
    present_gate::Registration registration(&state);
    std::vector<std::uint8_t> ram(PresentationMailbox::kImageBytes);
    const std::uint32_t end[4]{0xE9000000U, 0, 0xB8000000U, 0};
    std::memcpy(ram.data() + 0x1010, end, sizeof(end));
    RenderDescriptor descriptor{0, 0x80001000U, 0x80001020U, 0x80100000U, 0x80200000U};
    unsigned checks = 0;
    for (unsigned race : {0U, 3U, 64U, 65U, 66U, 6U})
        for (unsigned shared : {0U, 1U}) {
            PresentationMailbox box;
            assert(box.begin_epoch(91));
            std::array<dkr_owned_draw_event, 3> events{};
            events[0].kind = DKR_OWNED_ONLINE_SHARED;
            events[0].address = 0x80001000;
            for (unsigned i = 0; i < 2; ++i) {
                auto& e = events[i + 1];
                e.kind = DKR_OWNED_ONLINE_VIEW;
                e.address = 0x80001008 + i * 8;
                e.token = i;
                e.parameters[0] = 1;
                e.parameters[2] = race;
                e.parameters[3] = shared;
                e.parameters[4] = 2;
            }
            assert(box.publish(91, descriptor, ram, events));
            auto lease = box.take();
            assert(lease);
            {
                present_gate::Scope scope(registration, lease);
                present_gate::tag(&state, &slot, 10, 20);
            }
            const bool expected = race != 6 && !shared;
            assert(present_gate::requires_gpu_picture(&slot, 10, 20) == expected);
            ++checks;
            assert(!present_gate::requires_gpu_picture(&slot, 11, 20));
            ++checks;
            assert(!present_gate::requires_gpu_picture(&slot, 10, 21));
            ++checks;
            {
                auto admitted = present_gate::acquire(&slot, 10, 20);
                assert(admitted.allowed);
            }
            assert(box.retire_for_restore(91, 0));
            {
                auto denied = present_gate::acquire(&slot, 10, 20);
                assert(!denied.allowed);
            }
            // Classification never authorizes a retired frame or pins its lease.
            assert(box.finish_decode(lease));
            lease.reset();
            assert(box.quiescent());
            assert(!present_gate::requires_gpu_picture(&slot, 10, 20));
            ++checks;
        }
    {
        PresentationMailbox box;
        assert(!box.retire_for_correction(91, 0) && box.begin_epoch(91));
        assert(box.retire_for_correction(91, 0));
        int older_slot = 0, newer_slot = 0;
        descriptor.frame = 10;
        assert(box.publish(91, descriptor, ram));
        auto older = box.take();
        assert(older);
        {
            present_gate::Scope scope(registration, older);
            present_gate::tag(&state, &older_slot, 100, 200);
        }
        assert(box.finish_decode(older));
        descriptor.frame = 11;
        assert(box.publish(91, descriptor, ram));
        auto newer = box.take();
        assert(newer);
        {
            present_gate::Scope scope(registration, newer);
            present_gate::tag(&state, &newer_slot, 101, 201);
        }
        assert(box.finish_decode(newer));
        descriptor.frame = 12;
        assert(box.publish(91, descriptor, ram));
        // Delay the actual queue consumer across a CPU rewind. Unchanged past
        // frames survive; the changed frame and its pending future do not.
        bool allowed = false;
        {
            auto hold = std::make_unique<present_gate::QueueHoldForTest>(registration,
                                                                         present_gate::HoldPoint::BeforeAdmission);
            std::thread worker([&] {
                auto admission = present_gate::acquire(&older_slot, 100, 200);
                allowed = admission.allowed;
            });
            assert(hold->wait(std::chrono::seconds(2)));
            assert(box.retire_for_correction(91, 11));
            hold.reset();
            worker.join();
        }
        assert(allowed && !box.take());
        {
            auto rejected = present_gate::acquire(&newer_slot, 101, 201);
            assert(!rejected.allowed);
        }
        for (unsigned frame = 12; frame < 42; ++frame) {
            descriptor.frame = frame;
            assert(box.publish(91, descriptor, ram));
            auto image = box.take();
            assert(image);
            assert(box.finish_decode(image) && box.retire_for_correction(91, frame));
            assert(!SubmissionPermit::acquire(image));
            auto unaffected = present_gate::acquire(&older_slot, 100, 200);
            assert(unaffected.allowed);
            auto wrong_slot = present_gate::acquire(&older_slot, 101, 200);
            assert(!wrong_slot.allowed);
        }
        assert(box.buffer_statistics().image_allocations <= 4);
        assert(box.retire_for_correction(91, 10));
        {
            auto denied = present_gate::acquire(&older_slot, 100, 200);
            assert(!denied.allowed);
        }
        assert(box.retire_for_correction(91, 50));
        {
            auto denied = present_gate::acquire(&older_slot, 100, 200);
            assert(!denied.allowed);
        } // Never resurrect.
        newer.reset();
        older.reset();
        assert(box.quiescent() && box.begin_epoch(92));
        descriptor.frame = 2;
        assert(box.publish(92, descriptor, ram));
        assert(box.retire_for_correction(92, 3));
        auto pending = box.take();
        assert(pending && box.is_current(pending));
        auto admitted = SubmissionPermit::acquire(pending);
        assert(admitted);
        assert(box.retire_for_restore(92, 50) && !box.is_current(pending) && !box.submissions_drained());
        assert(!SubmissionPermit::acquire(pending)); // Resource retirement is unconditional.
        admitted.reset();
        assert(box.submissions_drained() && box.finish_decode(pending));
        pending.reset();
        assert(box.quiescent() && box.begin_epoch(93));
    }

    std::cout
        << checks
        << " immutable queue-picture checks passed; menus and shared scenes retain native fallback, stale admissions remain rejected.\n";
}
