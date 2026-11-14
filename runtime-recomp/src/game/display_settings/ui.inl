bool DrawDisplayChoices(float width, bool aspect_field) {
    using namespace dkr::display;
    const auto w = output_width.load(), h = output_height.load();
    if (aspect_field) {
        std::vector<SettingsChoice> aspects;
        for (const auto& r : Ratios)
            aspects.emplace_back(std::string(dkr::localisation::source(r.name)));
        bool changed = false;
        int aspect = int(preference.aspect);
        if (SettingsDropdown("display-aspect", "Aspect ratio", &aspect, aspects, width, false,
                             "Choose the game image's proportions. Borders preserve the selected shape.")) {
            preference.aspect = Aspect(aspect);
            if (preference.height > 0)
                preference.height = int(resolve(preference, w, h).render.height);
            publish();
            changed = true;
        }
        return changed;
    }
    bool changed = false;
    const auto list = choices(preference, w, h);
    std::vector<SettingsChoice> resolutions{
        {std::string(dkr::localisation::source("Automatic - match window"))},
        {std::string(dkr::localisation::source("Automatic - sharp integer scaling"))}};
    int selected = preference.height == 0 ? 0 : preference.height == -1 ? 1 : -1;
    for (const auto& item : list) {
        if (preference.height == item.height)
            selected = int(resolutions.size());
        resolutions.emplace_back(pixels(item.size));
    }
    if (selected < 0) {
        selected = int(resolutions.size());
        resolutions.push_back({pixels(resolve(preference, w, h).render), {}, true});
    }
    if (SettingsDropdown(
            "display-resolution", "Resolution", &selected, resolutions, width, false,
            "Render above your display resolution for a cleaner image. Higher values use more GPU power.")) {
        preference.height = selected == 0 ? 0 : selected == 1 ? -1 : list.at(std::size_t(selected - 2)).height;
        preference.legacy_downsample = 1;
        publish();
        changed = true;
    }
    const auto actual = resolve(preference, w, h);
    SettingsHelp(dkr::localisation::format("display.render_output",
                                           {{"render", pixels(actual.render)}, {"output", pixels(actual.output)}}),
                 width);
    return changed;
}
