#include "../src/game/android_texture_copy_policy.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace plume;
    using dkr::runtime::android_graphics::supported_texture_copy;
    // Opaque, never-dereferenced identity tokens: the policy must test presence,
    // never access either kind of GPU object while validating its copy direction.
    const int token = 0;
    const auto* texture = reinterpret_cast<const RenderTexture*>(&token);
    const auto* buffer = reinterpret_cast<const RenderBuffer*>(&token);
    const auto image = RenderTextureCopyLocation::Subresource(texture);
    const auto footprint = RenderTextureCopyLocation::PlacedFootprint(buffer,
        RenderFormat::R8G8B8A8_UNORM, 16, 16, 1, 64);
    if (supported_texture_copy(footprint, image)) throw std::runtime_error("1050022 crash direction accepted");
    if (!supported_texture_copy(image, footprint) || !supported_texture_copy(image, image))
        throw std::runtime_error("Existing upload/image copy rejected");
    unsigned checks = 0;
    for (int destination = 0; destination < 4; ++destination)
        for (int source = 0; source < 4; ++source)
            for (unsigned pointers = 0; pointers < 16; ++pointers) {
                RenderTextureCopyLocation dst{}, src{};
                dst.type = static_cast<RenderTextureCopyType>(destination);
                src.type = static_cast<RenderTextureCopyType>(source);
                dst.texture = pointers & 1 ? texture : nullptr;
                dst.buffer = pointers & 2 ? buffer : nullptr;
                src.texture = pointers & 4 ? texture : nullptr;
                src.buffer = pointers & 8 ? buffer : nullptr;
                const bool expected = destination == 1 && (pointers & 1) &&
                    ((source == 1 && (pointers & 4)) || (source == 2 && (pointers & 8)));
                if (supported_texture_copy(dst, src) != expected)
                    throw std::runtime_error("copy type/pointer contract mismatch");
                ++checks;
            }
    std::cout << "PASS: " << checks << " copy type/pointer combinations; texture-to-buffer rejected, existing paths retained\n";
}
