// A host-only submission boundary fixture, not a replacement SDK or RSP.
#include "sp_fixture.hpp"
#include "diagnostics.hpp"
#include <stdexcept>
unsigned fixture_worker=0,fixture_inline=0,fixture_gfx=0,fixture_other=0,fixture_calls=0,fixture_presented=0;
bool fixture_throw=false;
extern "C" void osSpTaskStartGo_recomp(std::uint8_t* rdram,recomp_context* ctx) {
    ++fixture_calls;const auto* task=TO_PTR(OSTask,ctx->r4);
    const bool bootstrap=dkr::lobby_stability::bootstrap_match.load()!=0;
    const bool allowed=!bootstrap||dkr::lobby_stability::bootstrap_audio_worker_allowed();
    if(task->t.type==M_AUDTASK){if(allowed)++fixture_worker;else ++fixture_inline;}
    else if(task->t.type==M_GFXTASK){++fixture_gfx;if(bootstrap&&allowed)++fixture_presented;}
    else ++fixture_other;
    if(fixture_throw)throw std::runtime_error("Synthetic SDK submission failure");
}
