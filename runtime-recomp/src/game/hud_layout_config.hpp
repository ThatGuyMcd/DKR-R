#pragma once
#include "hud_group_layout.hpp"
#include <json/json.hpp>
#include <string>

namespace dkr::runtime::hud::groups {
inline std::string entry_key(Aspect a, Scenario s, Target t, Widget w) {
    return std::string(kAspectKeys[static_cast<std::size_t>(a)]) + "/" +
           std::string(kScenarioKeys[static_cast<std::size_t>(s)]) + "/" +
           std::string(kTargetKeys[static_cast<std::size_t>(t)]) + "/" +
           std::string(kWidgetKeys[static_cast<std::size_t>(w)]);
}
template <class F> void each_entry(F&& f) {
    for (std::size_t a = 0; a < kAspects; ++a)
        for (std::size_t s = 0; s < kScenarios; ++s)
            for (std::size_t t = 0; t < kTargets; ++t)
                for (std::size_t w = 0; w < kWidgets; ++w)
                    f(static_cast<Aspect>(a), static_cast<Scenario>(s), static_cast<Target>(t), static_cast<Widget>(w));
}
inline nlohmann::json encode(const Layout& l) {
    nlohmann::json j{{"version", 2},
                     {"mode", l.mode == LayoutMode::Custom          ? "custom"
                              : l.mode == LayoutMode::FitToViewport ? "fill"
                              : l.mode == LayoutMode::SafeArea      ? "safe-area"
                                                                    : "original"},
                     {"scale", l.scale},
                     {"customFill", l.custom_fill},
                     {"placements", nlohmann::json::object()}};
    each_entry([&](Aspect a, Scenario s, Target t, Widget w) {
        const auto& p = l.entries[index(a, s, t, w)];
        if (p == Placement{})
            return;
        j["placements"][entry_key(a, s, t, w)] = {
            {"x", p.x}, {"y", p.y}, {"scale", p.scale}, {"anchor", p.anchor}, {"enabled", p.enabled}};
    });
    return j;
}
inline std::optional<Layout> decode(const nlohmann::json& j) {
    try {
        if (j.at("version").get<int>() != 2 || !j.at("placements").is_object() ||
            j.at("placements").size() > kAspects * kScenarios * kTargets * kWidgets)
            return {};
        Layout l;
        const auto mode = j.at("mode").get<std::string>();
        if (mode == "custom")
            l.mode = LayoutMode::Custom;
        else if (mode == "fill")
            l.mode = LayoutMode::FitToViewport;
        else if (mode == "safe-area")
            l.mode = LayoutMode::SafeArea;
        else if (mode != "original")
            return {};
        l.scale = j.at("scale").get<float>();
        l.custom_fill = j.value("customFill", true);
        each_entry([&](Aspect a, Scenario s, Target t, Widget w) {
            const auto found = j["placements"].find(entry_key(a, s, t, w));
            if (found == j["placements"].end())
                return;
            auto& p = l.entries[index(a, s, t, w)];
            p.x = found->at("x").get<float>();
            p.y = found->at("y").get<float>();
            p.scale = found->at("scale").get<float>();
            p.anchor = found->at("anchor").get<int>();
            p.enabled = found->at("enabled").get<bool>();
        });
        return valid(l) ? std::optional<Layout>(l) : std::nullopt;
    } catch (...) {
        return {};
    }
}
} // namespace dkr::runtime::hud::groups
