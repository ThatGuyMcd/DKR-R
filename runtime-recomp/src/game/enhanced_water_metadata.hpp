#pragma once
#include "enhanced_water_policy.hpp"
#include <json/json.hpp>
#include <stdexcept>
#include <string>

namespace dkr::runtime::water::enhanced {
inline bool parse_metadata(const nlohmann::json& json, Metadata& out, std::string& error) {
    Metadata staged;
    try {
        if (!json.is_object() || !json.contains("version") || !json["version"].is_number_integer() ||
            json["version"] != 1)
            throw std::runtime_error("enhancedWater requires version 1");
        if (json.contains("profile")) {
            const auto& p = json["profile"];
            if (!p.is_object())
                throw std::runtime_error("water profile must be an object");
            Profile profile;
            auto vector = [&](const char* key, Vec3& v) {
                if (!p.contains(key))
                    return;
                const auto& j = p[key];
                if (!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number())
                    throw std::runtime_error(std::string(key) + " requires three finite numbers");
                v = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
            };
            auto number = [&](const char* key, float& v) {
                if (!p.contains(key))
                    return;
                if (!p[key].is_number())
                    throw std::runtime_error(std::string(key) + " must be numeric");
                v = p[key].get<float>();
            };
            vector("deep", profile.deep);
            vector("shallow", profile.shallow);
            vector("sky", profile.sky);
            vector("sun", profile.sun);
            number("absorption", profile.absorption);
            number("roughness", profile.roughness);
            number("visibility", profile.visibility);
            number("ripple", profile.ripple);
            number("foam", profile.foam);
            if (!valid_profile(profile))
                throw std::runtime_error("water profile values exceed their safe ranges");
            staged.profile = profile;
        }
        if (json.contains("materials")) {
            const auto& list = json["materials"];
            if (!list.is_array() || list.size() > 256)
                throw std::runtime_error("water materials require at most 256 entries");
            std::array<bool, 256> assigned{};
            for (const auto& entry : list) {
                if (!entry.is_object() || !entry.contains("texture") || !entry["texture"].is_number_integer() ||
                    !entry.contains("kind") || !entry["kind"].is_string())
                    throw std::runtime_error("water material requires texture ordinal and kind");
                const auto ordinal = entry["texture"].get<std::int64_t>();
                if (ordinal < 0 || ordinal >= 256 || assigned[std::size_t(ordinal)])
                    throw std::runtime_error("water texture ordinal is out of range or repeated");
                const auto kind = entry["kind"].get<std::string>();
                Hint hint;
                if (kind == "auto")
                    hint = Hint::Automatic;
                else if (kind == "exclude")
                    hint = Hint::Exclude;
                else if (kind == "calm")
                    hint = Hint::Calm;
                else if (kind == "waves")
                    hint = Hint::Waves;
                else if (kind == "waterfall")
                    hint = Hint::Waterfall;
                else if (kind == "fountain")
                    hint = Hint::Fountain;
                else if (kind == "lava")
                    hint = Hint::Lava;
                else
                    throw std::runtime_error("unknown water material kind");
                assigned[std::size_t(ordinal)] = true;
                staged.materials[std::size_t(ordinal)] = hint;
            }
        }
        out = staged;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
} // namespace dkr::runtime::water::enhanced
