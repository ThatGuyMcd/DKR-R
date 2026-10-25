// Owned adapter around an unchanged SDK entrypoint. The included source is
// never copied, patched or written by this layer; its other exports are kept.
#include "diagnostics.hpp"
#include <stdexcept>
#include "sdk_task_source.hpp"
#define osSpTaskStartGo_recomp dkr_stability_original_osSpTaskStartGo_recomp
#include DKR_STABILITY_ORIGINAL_SP
#undef osSpTaskStartGo_recomp

extern "C" void osSpTaskStartGo_recomp(uint8_t* rdram,recomp_context* ctx) {
    bool audio=false;
    if(dkr::lobby_stability::bootstrap_match.load()) {
        if(!rdram||!ctx)throw std::runtime_error("Invalid bootstrap task submission.");
        const auto address=static_cast<std::uint32_t>(ctx->r4);
        const auto physical=address&0x1FFFFFFFU;
        if((address&3U)||physical>0x800000U-sizeof(OSTask))throw std::runtime_error("Invalid bootstrap task address.");
        const OSTask* task=TO_PTR(OSTask,ctx->r4);
        audio=task->t.type==M_AUDTASK;
        if(audio)dkr::lobby_stability::bootstrap_event("audio-queued-to-SP-worker",0,address);
    }
    dkr::lobby_stability::BootstrapAudioSubmission submission(audio);
    dkr_stability_original_osSpTaskStartGo_recomp(rdram,ctx);
}
