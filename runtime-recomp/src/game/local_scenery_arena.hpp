#pragma once

#include <cstdint>

namespace dkr::runtime::local_scenery {
// Private owned-decoder storage only. Guest DMA/command admission is still
// restricted to the original first 8 MiB; these offsets are not guest assets.
inline constexpr std::uint32_t kTextureBegin=0x00800000U;
inline constexpr std::uint32_t kTextureEnd=0x00C00000U;
inline constexpr std::uint32_t kVertexScratch=0x00C00000U;
}
