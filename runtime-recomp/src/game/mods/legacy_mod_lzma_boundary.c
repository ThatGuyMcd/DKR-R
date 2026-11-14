#include <lzma.h>
#include <stdint.h>
// Upstream's xdelta wrapper requests UINT64_MAX. Constrain that request at
// our build boundary without modifying the dependency's source.
lzma_ret dkr_mods_lzma_stream_decoder(lzma_stream* stream, uint64_t limit, uint32_t flags) {
    const uint64_t maximum = UINT64_C(67108864);
    return lzma_stream_decoder(stream, limit < maximum ? limit : maximum, flags);
}
