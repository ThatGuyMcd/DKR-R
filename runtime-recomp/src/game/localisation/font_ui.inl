#include <algorithm>
#include <type_traits>
#include <utility>
#include <vector>

// Initial language group uses Latin/Cyrillic only. Merge once while creating
// each existing atlas. Language changes never mutate an atlas or its texture.
void MergeLocalisationFont(ImFontAtlas* atlas, ImFont* destination, float size) {
    static_assert(std::is_same_v<ImWchar, std::uint16_t>);
    const auto ranges = dkr::localisation::fallback_ranges();
    const auto bytes = dkr::localisation::fallback_font();
    ImFontConfig config{};
    config.FontDataOwnedByAtlas = false;
    config.MergeMode = true;
    config.DstFont = destination;
    config.PixelSnapH = true;
    config.OversampleH = 1;
    config.OversampleV = 1;
    atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(bytes.data()), int(bytes.size()), size, &config,
                                ranges.data());
}
void MergeLocalisationFonts(ImFontAtlas* atlas) {
    static const auto without_digits = [] {
        std::vector<ImWchar> ranges;
        const auto all = dkr::localisation::fallback_ranges();
        for (std::size_t i = 0; all[i]; i += 2) {
            if (all[i] < '0') {
                ranges.push_back(all[i]);
                ranges.push_back(std::min<ImWchar>(all[i + 1], '0' - 1));
            }
            if (all[i + 1] > '9') {
                ranges.push_back(std::max<ImWchar>(all[i], '9' + 1));
                ranges.push_back(all[i + 1]);
            }
        }
        ranges.push_back(0);
        return ranges;
    }();
    std::vector<std::pair<ImFont*, float>> destinations;
    for (auto& config : atlas->ConfigData)
        if (!config.MergeMode && config.DstFont) {
            // Let each original face supply its supported accents/Cyrillic.
            // Preserve bases that deliberately exclude digits for Jumpman.
            bool digits = config.GlyphRanges == nullptr;
            if (config.GlyphRanges)
                for (auto* range = config.GlyphRanges; *range; range += 2)
                    if (range[0] <= '0' && range[1] >= '0')
                        digits = true;
            config.GlyphRanges = digits ? dkr::localisation::fallback_ranges().data() : without_digits.data();
            destinations.emplace_back(config.DstFont, config.SizePixels);
        }
    for (const auto& [font, size] : destinations)
        MergeLocalisationFont(atlas, font, size);
}
