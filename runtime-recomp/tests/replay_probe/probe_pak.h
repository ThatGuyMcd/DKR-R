#pragma once
#include "probe_bridge.h"
#ifdef __cplusplus
#include <span>
extern "C" {
#endif
enum dkr_probe_pak_operation { DKR_PAK_STATUS,DKR_PAK_FREE,DKR_PAK_COUNT,DKR_PAK_FIND,
    DKR_PAK_STATE,DKR_PAK_READ,DKR_PAK_ALLOCATE,DKR_PAK_ERASE,DKR_PAK_WRITE,DKR_PAK_FORMAT };
void dkr_probe_bind_paks(void* paks);
int dkr_probe_paks_enabled(void);
unsigned dkr_probe_pak_mask(void);
int dkr_probe_pak_request(unsigned operation,unsigned port,uint32_t arguments[4],uint8_t* bytes,unsigned count);
// Called in C only. All C++ ownership helpers return before a probe trap.
int dkr_probe_pak_service(const char* name,uint8_t* ram,struct recomp_context* context,int* native_result);
void dkr_probe_retail_pak_arguments(unsigned frame,unsigned value);
void dkr_probe_retail_pak_tick(uint8_t* ram,struct recomp_context* context);
#ifdef __cplusplus
}
bool dkr_probe_pak_import_check();
bool dkr_probe_retail_pak_check(std::span<const uint8_t> fixture);
#endif
