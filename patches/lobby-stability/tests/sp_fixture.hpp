#pragma once
#include <cstdint>
struct recomp_context {std::uint64_t r4=0;};
struct OSTask {struct {std::uint32_t type;std::uint8_t other[60];}t;};
inline constexpr unsigned M_AUDTASK=2,M_GFXTASK=1;
#define TO_PTR(type,address) reinterpret_cast<type*>(rdram+(static_cast<std::uint32_t>(address)&0x1FFFFFFFU))
extern unsigned fixture_worker,fixture_inline,fixture_gfx,fixture_other,fixture_calls,fixture_presented;
extern bool fixture_throw;
extern "C" void osSpTaskStartGo_recomp(std::uint8_t*,recomp_context*);
