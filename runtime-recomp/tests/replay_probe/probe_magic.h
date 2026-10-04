#pragma once
#include "probe_bridge.h"
#ifdef __cplusplus
bool dkr_probe_magic_checks();
extern "C" {
#endif
enum {DKR_MAGIC_APPLY,DKR_MAGIC_COMPLETE,DKR_MAGIC_FRAME_COMPLETE};
void dkr_probe_bind_magic(void* owner);
int dkr_probe_magic_enabled(void);
int dkr_probe_magic_request(unsigned operation,uint32_t words[2]);
void dkr_probe_magic_addresses(uint32_t addresses[2]);
int dkr_probe_magic_service(const char* name,uint8_t* ram,struct recomp_context* context);
#ifdef __cplusplus
}
#endif
