#pragma once
#include "plume_render_interface_types.h"

namespace dkr::runtime::android_graphics {
// Describe the directions implemented by the checked Vulkan backend, not all
// operations representable by RenderTextureCopyLocation. In particular a
// PLACED_FOOTPRINT destination is a buffer, never a destination texture.
inline bool supported_texture_copy(const plume::RenderTextureCopyLocation& dst,
                                   const plume::RenderTextureCopyLocation& src) noexcept {
    using plume::RenderTextureCopyType;
    if (dst.type != RenderTextureCopyType::SUBRESOURCE || dst.texture == nullptr) return false;
    if (src.type == RenderTextureCopyType::SUBRESOURCE) return src.texture != nullptr;
    if (src.type == RenderTextureCopyType::PLACED_FOOTPRINT) return src.buffer != nullptr;
    return false;
}
}
