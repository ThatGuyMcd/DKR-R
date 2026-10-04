#include "funcs.h"
#include "probe_save.h"

static unsigned save_slot, save_initials;
static int save_write;
void dkr_probe_save_arguments(unsigned slot,unsigned initials,int write) {
    save_slot=slot; save_initials=initials; save_write=write;
}
void dkr_probe_save_tick(uint8_t* ram,struct recomp_context* ctx) {
    uint8_t* rdram=ram; // MEM_* macros require the named checked guest base.
    dkr_probe_enter("private-retail-save-tick");
    if(save_slot>2 || save_initials>UINT16_MAX) dkr_probe_block("invalid-private-save-arguments");
    get_settings(rdram,ctx);
    const gpr settings=ctx->r2;
    if(save_write) {
        // Explicit unit policy, not a race/balloon/initials-menu simulation.
        // Use the RETAIL writer to encode course flags/progression/checksum.
        MEM_B(0x4B,settings)=0;
        MEM_W(0x50,settings)=save_initials;
        ctx->r4=save_slot; ctx->r5=settings;
        write_save_data(rdram,ctx);
        if(ctx->r2) dkr_probe_block("retail-save-write-refused");
    }
    ctx->r4=save_slot; ctx->r5=settings;
    read_save_file(rdram,ctx);
    if(save_write && (ctx->r2 || MEM_BU(0x4B,settings) || (uint32_t)MEM_W(0x50,settings)!=save_initials))
        dkr_probe_block("retail-save-roundtrip-mismatch");
    // Preserve the retail deferred allocator-free phase, rather than leaking
    // one heap block per EEPROM operation or manually clearing its metadata.
    mempool_free_queue_clear(rdram,ctx);
    dkr_probe_leave();
}
