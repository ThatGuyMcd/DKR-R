#include "renderer_snapshot.hpp"

#include <cstdio>
#include <stdexcept>

int main() {
    std::uint8_t live_core = 0;
    std::uint8_t live_state = 0;
    std::uint8_t submission = 0;
    std::uint8_t* core = &live_core;
    std::uint8_t* state = &live_state;

    {
        dkr::runtime::RendererSnapshotScope scope(core, state, &submission);
        if (core != &submission || state != &submission) {
            std::fputs("snapshot scope did not expose submission memory\n", stderr);
            return 1;
        }
    }
    if (core != &live_core || state != &live_state) {
        std::fputs("snapshot scope did not restore live memory\n", stderr);
        return 1;
    }

    try {
        dkr::runtime::RendererSnapshotScope scope(core, state, &submission);
        throw std::runtime_error("test unwind");
    } catch (const std::runtime_error&) {
    }
    if (core != &live_core || state != &live_state) {
        std::fputs("snapshot scope failed to restore during unwind\n", stderr);
        return 1;
    }

    std::puts("[test][renderer-snapshot] PASS");
    {
        dkr::runtime::RendererSnapshotScope scope(core,state,&submission);
        scope.restore();scope.restore();
        if(core!=&live_core||state!=&live_state)return 1;
        // Early release is idempotent; destruction must not clobber a later
        // binding after all submission CPU readers have completed.
        core=&submission;
    }
    if(core!=&submission||state!=&live_state)return 1;
    return 0;
}
