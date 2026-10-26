#pragma once

#include "presentation_policy.hpp"
#include <cstdint>

namespace dkr::runtime::presentation {

// Producer-thread state only. A finish flag, menu stage or render-rate tick is
// not evidence that the wooden frame has actually been drawn. Advance only
// when a task containing its first contracted draw is submitted. The next
// authored world frame is then the SECOND zoom-out frame, not the finish line.
struct PostraceFrameGate {
    bool contracted_frame_submitted = false;
    bool pending_contracted_frame = false;

    void reset() {
        *this = {};
    }
    void begin_frame() {
        pending_contracted_frame = false;
    }
    void observe_wood(int x1, int y1, int x2, int y2, int width, int height) {
        const bool valid =
            width > 0 && height > 0 && x1 >= 0 && y1 >= 0 && x2 > x1 && y2 > y1 && x2 <= width && y2 <= height;
        pending_contracted_frame |= valid && (x1 > 0 || y1 > 0 || x2 < width || y2 < height);
    }
    void submit_frame(bool presented) {
        contracted_frame_submitted |= presented && pending_contracted_frame;
        pending_contracted_frame = false;
    }
};

constexpr bool postrace_full_view_scope(bool modern, bool expanded, bool in_game, int postrace, int players, int trophy,
                                        int viewport_layout) {
    return modern && expanded && in_game && postrace != 0 && players == 1 && trophy == 0 && viewport_layout == 0;
}

// One key per logical camera, sampled at its first root matrix. Repeated
// CAMERA_FINISH_RACE stores and continuous look-at rotations are NOT cuts.
// Node identity catches nearby cuts that a distance threshold cannot detect.
struct FinishCameraShot {
    std::uint32_t observed_owner = 0;
    std::uint32_t observed_node = 0;
    std::uint32_t previous_owner = 0;
    std::uint32_t previous_node = 0;
    int previous_mode = -1;
    bool valid = false;

    void observe_node(std::uint32_t owner, std::uint32_t node) {
        observed_owner = owner;
        observed_node = node;
    }
    bool sample(int mode) {
        using namespace dkr::runtime::enhancements;
        const auto owner = mode == kCameraFinishRace ? observed_owner : 0U;
        const auto node = mode == kCameraFinishRace ? observed_node : 0U;
        const bool finish_boundary = is_finish_camera_mode(mode) || is_finish_camera_mode(previous_mode);
        const bool cut =
            valid && finish_boundary && (mode != previous_mode || owner != previous_owner || node != previous_node);
        previous_mode = mode;
        previous_owner = owner;
        previous_node = node;
        valid = true;
        return cut;
    }
};

// Implemented by the project-owned viewport producer; never mutate RDRAM.
void postrace_presentation_begin_frame();
void postrace_presentation_submit_frame(bool presented);

} // namespace dkr::runtime::presentation
