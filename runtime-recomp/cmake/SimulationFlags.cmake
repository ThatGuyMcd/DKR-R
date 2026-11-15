include_guard(GLOBAL)
# Signed integer overflow wraps in everything that computes simulation state.
# Recompiled N64 code and the RSP fallback do 32 bit arithmetic that C
# promotes to signed int; GCC at -O3 exploited one such overflow on riscv64
# and ppc64le and changed the audio. -fwrapv defines the wrap, so the result
# cannot depend on the optimiser. It does not replace the replay probe
# (docs/CROSS-ARCHITECTURE.md). MSVC has no such switch and is left alone.
if(MSVC)
    set(DKR_SIMULATION_WRAP_OPTIONS "")
else()
    set(DKR_SIMULATION_WRAP_OPTIONS -fwrapv)
endif()
