#include "recomp.h"
#include "probe_magic.h"
#include <string.h>
int dkr_probe_magic_service(const char* name,uint8_t* rdram,recomp_context* ctx) {
    if(!dkr_probe_magic_enabled())return 0;
    if(!strcmp(name,"dkr_apply_launch_magic_codes")) {
        uint32_t addresses[2],words[2];dkr_probe_magic_addresses(addresses);
        // Validate BOTH words before owner mutation. All memory accesses occur
        // in C; no checked guest memory exception crosses the policy's C++.
        for(unsigned i=0;i<2;++i)dkr_probe_memory(rdram,(int32_t)addresses[i],4,0);
        words[0]=MEM_W(0,(gpr)(int32_t)addresses[0]);words[1]=MEM_W(0,(gpr)(int32_t)addresses[1]);
        if(!dkr_probe_magic_request(DKR_MAGIC_APPLY,words))dkr_probe_block("unowned-magic-code-apply-frame");
        MEM_W(0,(gpr)(int32_t)addresses[0])=words[0];MEM_W(0,(gpr)(int32_t)addresses[1])=words[1];return 1;
    }
    const unsigned operation=!strcmp(name,"dkr_magic_code_credits_started")?DKR_MAGIC_COMPLETE:
        !strcmp(name,"dkr_magic_code_balloon_awarded")?DKR_MAGIC_COMPLETE:
        !strcmp(name,"dkr_magic_codes_frame_complete")?DKR_MAGIC_FRAME_COMPLETE:UINT32_MAX;
    if(operation==UINT32_MAX)return 0;
    uint32_t words[2]={!strcmp(name,"dkr_magic_code_credits_started")?1U<<10:1U<<26,0};
    if(!dkr_probe_magic_request(operation,words))dkr_probe_block("unowned-magic-code-action-frame");
    (void)ctx;return 1;
}
