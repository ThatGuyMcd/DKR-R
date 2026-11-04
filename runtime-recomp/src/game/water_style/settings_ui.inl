// Shared by the launcher and overlay's existing water settings card.
namespace water_style=dkr::runtime::water::style;
int water_style_value=water_style::selected.load();
if(SettingsDropdown("water-style", "Water style", &water_style_value,
    {"DKR colours", "Current HD"}, inner)) {
    water_style::selected.store(water_style_value);SaveSettings();
}
PaddockGap(8.0F);
SettingsHelp("Water colour and highlights. Your reflection method and quality stay the same.",inner);
PaddockGap(16.0F);
