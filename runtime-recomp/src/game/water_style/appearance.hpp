#pragma once

#include "enhanced_water_settings.hpp"
#include <ostream>

namespace dkr::runtime::water::style {
using enhanced::Kind;
using enhanced::Profile;
enum class Style : std::uint8_t { Dkr, Current };
// Low eight identity bits remain the debug view. This exact float-representable
// flag adds no material fields or structured-buffer stride.
inline constexpr std::uint32_t kDkrStyleFlag = 1U << 8U;
inline std::atomic<int> selected{int(Style::Dkr)};
inline constexpr const char* SettingKey = "enhanced_water_style";

inline bool load_setting(const std::string& key, int value) {
    if (key != SettingKey)
        return false;
    selected.store(std::clamp(value, 0, 1));
    return true;
}
inline void save_setting(std::ostream& output) {
    output << SettingKey << '=' << selected.load() << '\n';
}
inline void reset() {
    selected.store(int(Style::Dkr));
}

constexpr Profile styled_stock_profile(std::int32_t map, Style style) {
    auto p = enhanced::stock_profile(map);
    if (style != Style::Dkr)
        return p;
    // Body colour only: normals, reflection lighting, depth, absorption,
    // visibility and foam retain their original course profile values.
    // These are scene-linear colours, before the existing diffuse lighting
    // and tone compression. Values above one keep the turquoise visibly
    // bright in the final image without brightening reflected scenery.
    switch (map) {
    case 30:
    case 19:
    case 31:
        break;
    case 28:
        p.deep = {0.03F, 0.20F, 0.36F};
        p.shallow = {0.24F, 0.75F, 0.95F};
        break;
    case 8:
    case 10:
    case 4:
    case 14:
    case 40:
    case 53:
    case 61:
        p.deep = {0.010F, 0.85F, 1.0F};
        p.shallow = {0.12F, 2.1F, 2.05F};
        break;
    default:
        p.deep = {0.012F, 0.72F, 0.90F};
        p.shallow = {0.10F, 1.8F, 1.95F};
        break;
    }
    return p;
}

template <class Material> void apply(Material& material, Kind kind, bool authored_profile) {
    if (selected.load(std::memory_order_relaxed) != int(Style::Dkr) || kind == Kind::None || kind == Kind::Lava ||
        kind == Kind::Waterfall || kind == Kind::Fountain)
        return;
    if (!authored_profile) {
        const auto profile = styled_stock_profile(std::int32_t(material.identity.z), Style::Dkr);
        const bool tint = !enhanced::use_track_colours.load(std::memory_order_relaxed);
        const auto scale = [&](const std::atomic<int>& setting) {
            return tint ? float(setting.load(std::memory_order_relaxed)) * 0.01F : 1.0F;
        };
        material.deepColor.x = profile.deep.x * scale(enhanced::tint_red);
        material.deepColor.y = profile.deep.y * scale(enhanced::tint_green);
        material.deepColor.z = profile.deep.z * scale(enhanced::tint_blue);
        material.shallowColor.x = profile.shallow.x * scale(enhanced::tint_red);
        material.shallowColor.y = profile.shallow.y * scale(enhanced::tint_green);
        material.shallowColor.z = profile.shallow.z * scale(enhanced::tint_blue);
    }
    material.identity.y = float(std::uint32_t(material.identity.y) | kDkrStyleFlag);
}
} // namespace dkr::runtime::water::style
