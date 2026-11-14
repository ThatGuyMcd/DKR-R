#include "finish_presentation_policy.hpp"
#include <array>
#include <cassert>
#include <cstdio>

using namespace dkr::runtime::presentation;

int main() {
    static_assert(postrace_full_view_scope(true, true, true, 1, 1, 0, 0));
    static_assert(postrace_full_view_scope(true, true, true, 2, 1, 0, 0));
    static_assert(!postrace_full_view_scope(false, true, true, 1, 1, 0, 0));
    static_assert(!postrace_full_view_scope(true, false, true, 1, 1, 0, 0));
    static_assert(!postrace_full_view_scope(true, true, false, 1, 1, 0, 0));
    static_assert(!postrace_full_view_scope(true, true, true, 0, 1, 0, 0));
    static_assert(!postrace_full_view_scope(true, true, true, 1, 2, 0, 0));
    static_assert(!postrace_full_view_scope(true, true, true, 1, 3, 0, 0));
    static_assert(!postrace_full_view_scope(true, true, true, 1, 4, 0, 0));
    static_assert(!postrace_full_view_scope(true, true, true, 1, 1, 1, 0));
    // Protect existing split layouts even during active-player compaction.
    static_assert(!postrace_full_view_scope(true, true, true, 1, 1, 0, 1));
    static_assert(!postrace_full_view_scope(true, true, true, 1, 1, 0, 2));
    static_assert(!postrace_full_view_scope(true, true, true, 1, 1, 0, 3));

    // Render FPS must not change authored-frame timing. A finish/asset wait
    // of arbitrary length and a full-size frame draw must never activate 4:3.
    for (int rate : {30, 60, 120, 144, 180, 240, 500}) {
        PostraceFrameGate gate;
        for (int frame = 0; frame < 180; ++frame) {
            gate.begin_frame();
            if (frame > 150)
                gate.observe_wood(0, 0, 320, 240, 320, 240);
            for (int render = 0; render < rate; ++render)
                assert(!gate.contracted_frame_submitted);
            gate.submit_frame(true);
        }
        gate.begin_frame();
        gate.observe_wood(2, 0, 318, 237, 320, 240);
        assert(!gate.contracted_frame_submitted); // first zoom: still full
        gate.submit_frame(true);
        gate.begin_frame();
        assert(gate.contracted_frame_submitted); // second zoom: framed
        // Pause, fade-out and the end of the zoom do not re-expand the world.
        gate.submit_frame(true);
        gate.begin_frame();
        assert(gate.contracted_frame_submitted);
        gate.reset(); // retry/new results/scene/Adventure reward sequence
        assert(!gate.contracted_frame_submitted);
        gate.begin_frame();
        gate.observe_wood(80, 24, 240, 144, 320, 240); // skipped to terminal
        gate.submit_frame(false);                      // rollback resimulation: not presented
        assert(!gate.contracted_frame_submitted);
        gate.begin_frame();
        gate.observe_wood(2, 1, 318, 237, 320, 240);
        gate.begin_frame(); // abandoned display list
        gate.submit_frame(true);
        assert(!gate.contracted_frame_submitted);
        gate.observe_wood(0, 0, 0, 0, 320, 240); // invalid rectangle
        gate.submit_frame(true);
        assert(!gate.contracted_frame_submitted);
    }
    for (int players : {1, 2, 3, 4}) {
        std::array<FinishCameraShot, 8> shots{};
        for (int i = 0; i < players; ++i)
            assert(!shots[i].sample(0));
        for (int i = 0; i < players; ++i) {
            shots[i].observe_node(100 + i, 200 + i);
            assert(shots[i].sample(7)); // this player's finish cut once
            for (int tick = 0; tick < 1000; ++tick) {
                shots[i].observe_node(100 + i, 200 + i);
                assert(!shots[i].sample(7));
                assert(!shots[i].sample(7)); // perspective + world roots
                for (int j = i + 1; j < players; ++j)
                    assert(!shots[j].sample(0)); // unfinished players unaffected
            }
            shots[i].observe_node(100 + i, 900 + i); // even a nearby node
            assert(shots[i].sample(7));
            assert(!shots[i].sample(7));
            shots[i].observe_node(500 + i, 900 + i); // changes tracked racer
            assert(shots[i].sample(7));
            assert(shots[i].sample(5)); // no-node challenge fallback
            for (int tick = 0; tick < 1000; ++tick)
                assert(!shots[i].sample(5));
            assert(shots[i].sample(0));
            assert(!shots[i].sample(0));
            shots[i] = {}; // scene reset
            assert(!shots[i].sample(7));
        }
        // Distinct cutscene logical slots cannot churn the gameplay slot.
        assert(!shots[4].sample(5));
        assert(!shots[0].sample(7));
    }
    std::puts("[test][finish-presentation] PASS: submission-gated zoom timing and independent finish shots.");
}
