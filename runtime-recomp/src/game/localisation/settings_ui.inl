// The control is outside launch-only disabling so it works in both surfaces.
// Switching copy leaves IDs, buffers, selected mods and services in place.
void DrawLanguageSetting(float width) {
    using namespace dkr::localisation;
    std::vector<SettingsChoice> options{{std::string(text("common.automatic"))}};
    int selected = 0;
    const auto choices = languages();
    for (std::size_t i = 0; i < choices.size(); ++i) {
        options.emplace_back(std::string(choices[i].name));
        if (preference() == choices[i].code) selected = int(i + 1);
    }
    PaddockGap(14.F);
    const auto caption = text("common.language");
    if (SettingsDropdown("launcher-language", caption, &selected, options, width)) {
        if (select(selected == 0 ? "auto" : choices[std::size_t(selected - 1)].code)) SaveSettings();
    }
    PaddockGap(6.F);
    PaddockText(PaddockReading(13.F, false, 1.5F), PaddockRgb(0xABC0CC), text("common.language_help"), width);
}
