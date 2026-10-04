#include "funcs.h"
#include "probe_audio.h"

static unsigned requested_samples;
static int requested_sound;
void dkr_probe_audio_arguments(unsigned samples, int play_sound) {
    requested_samples=samples; requested_sound=play_sound!=0;
}
void dkr_probe_audio_tick(uint8_t* rdram, recomp_context* ctx) {
#if DKR_PROBE_HAS_AUDIO
    dkr_probe_enter("private-owned-retail-audio-cpu-and-dsp");
    dkr_probe_boss_trace("audio-begin",rdram,ctx);
    if(!dkr_probe_audio_enabled() || !requested_samples || requested_samples>2048 || requested_samples%16)
        dkr_probe_block("invalid-private-audio-frame");
    // Actual completed-ROM DMA messages and buffer ageing, not fake AI/SP
    // acknowledgements. Foreign queues/blocked threads still trap.
    __clearAudioDMA(rdram,ctx);
    if(requested_sound) {
        ctx->r4=0x22; ctx->r5=0; // retail SOUND_SELECT, deterministic test policy.
        sound_play(rdram,ctx);
    }
    ctx->r4=(gpr)(int32_t)DKR_PROBE_AUDIO_COMMANDS;
    ctx->r5=(gpr)(int32_t)DKR_PROBE_AUDIO_LENGTH;
    ctx->r6=DKR_PROBE_AUDIO_OUTPUT-0x80000000U;
    ctx->r7=requested_samples;
    alAudioFrame(rdram,ctx);
    const int count=MEM_W(0,(gpr)(int32_t)DKR_PROBE_AUDIO_LENGTH);
    const uint32_t end=(uint32_t)ctx->r2;
    if(count<=0 || count>0xA000/8 || end!=DKR_PROBE_AUDIO_COMMANDS+(unsigned)count*8)
        dkr_probe_block("invalid-retail-audio-command-list");
    dkr_probe_audio_rsp(rdram,DKR_PROBE_AUDIO_COMMANDS-0x80000000U,(unsigned)count*8);
    dkr_probe_boss_trace("audio-end",rdram,ctx);
    dkr_probe_leave();
#else
    (void)rdram; (void)ctx;
    dkr_probe_block("private-audio-payload-not-generated");
#endif
}
