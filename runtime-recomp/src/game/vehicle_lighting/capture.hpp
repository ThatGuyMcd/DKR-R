#pragma once
#include "lighting.hpp"

namespace dkr::runtime::vehicles::lighting {
template <class Read>
Sample capture(Read read, std::uint32_t object, std::uint32_t intensity_symbol, std::uint32_t inverse_view_symbol,
               bool fast) {
    Sample sample;
    const auto shading = read(object + 0x54, 4);
    if (!shading)
        return sample;
    const auto real = [&](std::uint32_t address) {
        const float value = std::bit_cast<float>(read(address, 4));
        if (!std::isfinite(value))
            throw std::runtime_error("Non-finite custom vehicle light sample.");
        return value;
    };
    sample.exposure = real(shading) * real(intensity_symbol);
    sample.ambient = real(shading + 0x28);
    sample.diffuse = real(shading + 0x2c);
    const auto component = [&](unsigned offset) { return float(std::int16_t(read(shading + offset, 2))) / 16384.F; };
    sample.direction = {component(0x1c), component(0x1e), component(0x20)};
    if (!fast) {
        Matrix inverse_view{};
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c)
                inverse_view[r][c] = real(inverse_view_symbol + (r * 4 + c) * 4);
        sample.direction = transform(sample.direction, inverse_view);
    }
    sample.environment = read(shading + 4, 4);
    sample.flags = 1U | (fast ? 2U : 0U);
    return sample;
}
inline std::array<std::uint32_t, 8> words(const Sample& sample) {
    return {std::bit_cast<std::uint32_t>(sample.exposure),
            std::bit_cast<std::uint32_t>(sample.ambient),
            std::bit_cast<std::uint32_t>(sample.diffuse),
            std::bit_cast<std::uint32_t>(sample.direction.x),
            std::bit_cast<std::uint32_t>(sample.direction.y),
            std::bit_cast<std::uint32_t>(sample.direction.z),
            sample.environment,
            sample.flags};
}
} // namespace dkr::runtime::vehicles::lighting
