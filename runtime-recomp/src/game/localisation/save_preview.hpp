#pragma once
#include "service.hpp"
#include "dkr_save_codec.hpp"

namespace dkr::localisation {
// Format typed, already-read data on the UI thread. The worker's result stays
// independent of the locale, so a pending read cannot restore old-language copy.
inline std::string save_preview(const runtime::saves::codec::SaveImage& image) {
    std::string result;
    for (std::size_t index = 0; index < image.slots.size(); ++index) {
        if (index) result += " | ";
        const auto& slot = image.slots[index];
        const auto number = std::to_string(index + 1);
        const auto name = slot.name.empty() ? text("save.empty") : std::string_view(slot.name);
        result += plural("save.slot", slot.balloons[0], {{"slot", number}, {"name", name}});
    }
    return result;
}
} // namespace dkr::localisation
