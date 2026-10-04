#include "experimental_checkpoint_hash.hpp"
#define XXH_INLINE_ALL
#include "xxHash/xxhash.h"

namespace dkr::runtime::netplay::experimental {
std::uint64_t checkpoint_hash(std::span<const std::uint8_t> bytes) {
    // Already-pinned read-only dependency. XXH3 has a portable scalar path and
    // platform-selected vector path; no AVX-only requirement or runtime worker.
    // Checkpoints remain compared byte-for-byte in replay qualification.
    return XXH3_64bits(bytes.data(),bytes.size());
}
}
