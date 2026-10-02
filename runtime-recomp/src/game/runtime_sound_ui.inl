// Included by runtime_ui.cpp inside its UI namespace, after
// runtime_online_ui.inl. Presentation only: volumes, the EQ and the 3-4 player
// music option go through the same audio settings as before.
//
// SOUND in the look of tools/launcher-html (pages/sound.js, styles/sound.css):
// Master leads and the island mix sits under it; every volume has a mute that
// keeps its level; the EQ starts from presets and remembers a hand-made
// curve. A on a slider (Enter on a keyboard) is the row's quick action: mute on
// a volume, back to 0 dB on a band. Accurate keeps only Master, as before.

// ------------------------------------------------------------ palette

constexpr unsigned kSndText = 0xFFF6DA;   // --text
constexpr unsigned kSndMuted = 0xB0C9CC;  // --muted
constexpr unsigned kSndFocus = 0xFFD11F;
constexpr unsigned kSndMuteRed = 0xFFA396;

struct SoundPreset {
    const char* id;
    const char* label;
    std::array<int, 3> eq;  // bass, mid, treble in dB
    const char* about;
};

constexpr std::array<SoundPreset, 4> kSoundPresets{{
    {"flat", "Flat", {0, 0, 0}, "The original game sound."},
    {"bass", "Bass boost", {5, 1, 0},
     "Fuller engines and music. Best on headphones and bigger speakers."},
    {"clear", "Clear", {-2, 2, 4},
     "Crisper effects that cut through the music. Good on small speakers."},
    {"warm", "Warm", {2, 0, -4}, "Softer highs for long sessions and late nights."},
}};
constexpr const char* kSoundCustomAbout =
    "Your own curve. Try any preset; pick Custom to get it back.";

struct SoundPageState {
    ImGuiContext* context = nullptr;
    int last_frame = -100;
    // The last Reset mix, so it can be undone until the mix is touched again
    // or the page is left.
    std::optional<std::array<float, 4>> undo_levels;
    std::array<bool, 4> undo_muted{};
};
SoundPageState g_sound_page;

std::array<int, 3> SoundCurve() {
    return {static_cast<int>(std::lround(dkr::runtime::platform::bass_gain())),
            static_cast<int>(std::lround(dkr::runtime::platform::mid_gain())),
            static_cast<int>(std::lround(dkr::runtime::platform::treble_gain()))};
}

void SetSoundCurve(const std::array<int, 3>& eq) {
    dkr::runtime::platform::set_bass_gain(static_cast<float>(eq[0]));
    dkr::runtime::platform::set_mid_gain(static_cast<float>(eq[1]));
    dkr::runtime::platform::set_treble_gain(static_cast<float>(eq[2]));
}

const SoundPreset* MatchSoundPreset(const std::array<int, 3>& eq) {
    for (const SoundPreset& preset : kSoundPresets) {
        if (preset.eq == eq) return &preset;
    }
    return nullptr;
}

bool SoundMixIsOriginal() {
    for (std::size_t channel = 1; channel < kSoundChannelCount; ++channel) {
        if (std::lround(SoundLevel(channel) * 100.0F) != 100 || g_sound_muted[channel]) {
            return false;
        }
    }
    return true;
}

// Racing has no lower case or minus sign: numbers stay in the race font and
// units in the reading face (.snd-value small).
void DrawSoundValue(ImDrawList* draw, float right, float mid_y, std::string_view number,
                    std::string_view unit, float number_px) {
    const PaddockType digits = PaddockSign(number_px, 1.0F, 0.0F);
    const PaddockType unit_type = PaddockReading(13.0F, true, 1.0F);
    const float unit_width = PaddockMeasure(unit_type, unit);
    const float number_width = PaddockMeasure(digits, number);
    const float left = std::round(right - unit_width - 2.0F - number_width);
    // The digits' line box is centred on the row; the unit shares its baseline.
    const float top = std::round(mid_y - digits.line * 0.5F);
    const float baseline = top + (digits.line - digits.content) * 0.5F + digits.ascent;
    PaddockDrawRun(draw, digits, {left, top}, PaddockCol(kSndText), number.data(),
                   number.data() + number.size());
    PaddockDrawRun(draw, unit_type,
                   {left + number_width + 2.0F,
                    baseline - (unit_type.line - unit_type.content) * 0.5F - unit_type.ascent},
                   PaddockCol(kSndMuted), unit.data(), unit.data() + unit.size());
}

// The speaker shows the level, like the system tray: two waves, one, none, or
// crossed out (pages/sound.js speakerIcon, a 24 px view box).
void DrawSoundSpeaker(ImDrawList* draw, ImVec2 at, ImU32 colour, int waves, bool crossed) {
    const auto p = [&](float x, float y) { return ImVec2{at.x + x, at.y + y}; };
    draw->AddRectFilled(p(3.5F, 9.2F), p(6.7F, 14.8F), colour);
    draw->AddQuadFilled(p(6.7F, 9.2F), p(11.5F, 5.0F), p(11.5F, 19.0F), p(6.7F, 14.8F), colour);
    constexpr float kSpread = 0.775F;  // asin(2.8 / 4): both arcs span +-44.4 degrees
    if (waves >= 1 && !crossed) {
        draw->PathArcTo(p(12.143F, 12.0F), 4.0F, -kSpread, kSpread, 12);
        draw->PathStroke(colour, 0, 2.0F);
    }
    if (waves >= 2 && !crossed) {
        draw->PathArcTo(p(12.087F, 12.0F), 8.0F, -kSpread, kSpread, 16);
        draw->PathStroke(colour, 0, 2.0F);
    }
    if (crossed) {
        draw->AddLine(p(15.5F, 9.5F), p(20.5F, 14.5F), colour, 2.0F);
        draw->AddLine(p(20.5F, 9.5F), p(15.5F, 14.5F), colour, 2.0F);
        for (const ImVec2 cap : {p(15.5F, 9.5F), p(20.5F, 14.5F), p(20.5F, 9.5F), p(15.5F, 14.5F)}) {
            draw->AddCircleFilled(cap, 1.0F, colour, 8);
        }
    }
}

// The mute button leading a volume row. Pointer only: the D-pad reaches mute
// through A on the slider, so a row is one stop.
bool DrawSoundMuteButton(const char* id, ImVec2 at, bool muted, float level) {
    ImGui::SetCursorScreenPos(at);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const PaddockPress press = PaddockBeginPress(id, {44.0F, 44.0F});
    ImGui::PopItemFlag();
    if (press.hovered) ImGui::SetTooltip("%s", muted ? "Unmute" : "Mute");
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 fill = muted
        ? PaddockMix(PaddockRgb(0xFF6E5A, 36U), PaddockRgb(0xFF6E5A, 56U), press.hover)
        : PaddockMix(PaddockRgb(0xFFFFFF, 0U), PaddockRgb(0xFFFFFF, 20U), press.hover);
    PaddockFill(draw, press.min, press.max, PaddockRound(10.0F), PaddockApply(fill));
    const ImU32 ink = muted ? PaddockCol(kSndMuteRed)
                            : PaddockApply(PaddockMix(PaddockRgb(0xA9C8D1),
                                                      PaddockRgb(0xFFFFFF), press.hover));
    const int percent = static_cast<int>(std::lround(level * 100.0F));
    DrawSoundSpeaker(draw, {press.min.x + 10.0F, press.min.y + 10.0F}, ink,
                     percent == 0 ? 0 : percent < 50 ? 1 : 2, muted);
    PaddockEndPress(press, 10.0F, 0.9F);
    return press.pressed;
}

// A row's own surface: hover tint, and the focus ring of its slider.
void DrawSoundRowSurface(ImVec2 a, ImVec2 b, bool master, bool hovered, bool focused) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const PaddockRadii radii = PaddockRound(12.0F);
    if (focused) {
        PaddockFill(draw, a, b, radii, PaddockCol(0xFFFFFF, 15U));
        draw->AddRect({a.x - 1.5F, a.y - 1.5F}, {b.x + 1.5F, b.y + 1.5F},
                      PaddockCol(kSndFocus), 13.5F, 0, 3.0F);
    } else if (master) {
        PaddockFill(draw, a, b, radii, PaddockCol(0x000000, hovered ? 36U : 51U));
    } else if (hovered) {
        PaddockFill(draw, a, b, radii, PaddockCol(0xFFFFFF, 9U));
    }
}

struct SoundRowLayout {
    float label_x = 0.0F;
    float label_width = 0.0F;
    float slider_x = 0.0F;
    float slider_width = 0.0F;
    float slider_y = 0.0F;   // top of the 44 px slider
    float value_right = 0.0F;
    float label_mid = 0.0F;  // vertical centre of the label and value
    float height = 0.0F;
};

// Mute | label | slider | value on one line (.snd-row); a narrow panel puts
// the slider on its own line under the other three.
SoundRowLayout LayoutSoundRow(ImVec2 at, float width, bool narrow, bool band, bool master) {
    SoundRowLayout row;
    const float pad_left = band ? 12.0F : master ? 8.0F : 4.0F;
    const float pad_right = master ? 16.0F : 12.0F;
    const float lead = band ? 0.0F : 44.0F + 14.0F;
    row.label_x = at.x + pad_left + lead;
    row.value_right = at.x + width - pad_right;
    if (narrow) {
        const float top = band ? 8.0F : 4.0F;
        row.label_mid = at.y + top + 22.0F;
        row.slider_x = at.x + pad_left;
        row.slider_y = at.y + top + 44.0F;
        row.slider_width = width - pad_left - pad_right;
        row.label_width = row.value_right - 72.0F - row.label_x;
        row.height = top + 44.0F + 44.0F + 6.0F;
        return row;
    }
    row.height = master ? 62.0F : 52.0F;
    row.label_mid = at.y + row.height * 0.5F;
    // minmax(6.5em, 9.5em) and minmax(4em, 6em) at 16 px: the most there is room for.
    row.label_width = band ? 96.0F : 152.0F;
    row.slider_x = row.label_x + row.label_width + 14.0F;
    row.slider_width = std::max(row.value_right - 72.0F - 14.0F - row.slider_x, 40.0F);
    row.slider_y = at.y + (row.height - 44.0F) * 0.5F;
    return row;
}

void DrawSoundRowLabel(const SoundRowLayout& row, std::string_view label, bool master,
                       float alpha) {
    const PaddockType type = master ? PaddockReading(19.0F, true, 1.2F)
                                    : PaddockReading(16.0F, true, 1.2F);
    const std::string shown = PaddockEllipsize(type, label, row.label_width);
    PaddockDrawRun(ImGui::GetWindowDrawList(), type,
                   {row.label_x, std::round(row.label_mid - type.line * 0.5F)},
                   PaddockCol(kSndText, static_cast<unsigned>(255.0F * alpha)),
                   shown.data(), shown.data() + shown.size());
}

// One volume: Master (the biggest thing on the page) or a mix channel.
// Returns true when the mix changed.
bool DrawSoundVolumeRow(std::size_t channel, const char* label, ImVec2 at, float width,
                        bool narrow, bool silenced) {
    const bool master = channel == 0U;
    const SoundRowLayout row = LayoutSoundRow(at, width, narrow, false, master);
    const ImVec2 row_max{at.x + width, at.y + row.height};
    const bool muted = g_sound_muted[channel];
    const float level = SoundLevel(channel);
    ImGui::PushID(label);
    const bool row_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                             ImGui::IsMouseHoveringRect(at, row_max);
    const ImGuiID slider_id = ImGui::GetID("##level");
    const bool slider_focused = ImGui::GetFocusID() == slider_id && ImGui::GetIO().NavVisible;
    DrawSoundRowSurface(at, row_max, master, row_hovered, slider_focused);

    bool changed = false;
    if (DrawSoundMuteButton("##mute", {at.x + (master ? 8.0F : 4.0F),
                                       narrow ? row.label_mid - 22.0F : at.y + (row.height - 44.0F) * 0.5F},
                            muted, level)) {
        SetSoundMuted(channel, !muted);
        changed = true;
    }
    const float dim = !master && silenced ? 0.5F : 1.0F;
    DrawSoundRowLabel(row, label, master, dim);

    PaddockRangeLook look;
    if (master) {
        look.groove = 14.0F;
        look.thumb = 30.0F;
    }
    if (muted) {
        look.fill = 0x5D7682;
        look.ring = 0x7D949D;
    }
    // 5 % steps, like the study's range input.
    int steps = static_cast<int>(std::lround(level * 20.0F));
    ImGui::SetCursorScreenPos({row.slider_x, row.slider_y});
    if (dim < 1.0F) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * dim);
    const PaddockRangeResult slider =
        PaddockRange("##level", &steps, 0, 20, row.slider_width, look);
    if (dim < 1.0F) ImGui::PopStyleVar();
    if (slider.changed) {
        SetSoundLevel(channel, static_cast<float>(steps) / 20.0F);
        changed = true;
    } else if (slider.activated) {
        SetSoundMuted(channel, !muted);
        changed = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (g_sound_muted[channel]) {
        PaddockType tag = PaddockReading(12.0F, true, 1.0F);
        tag.tracking = 1.2F;
        const float tag_width = PaddockMeasure(tag, "MUTED") - tag.tracking;
        PaddockDrawRun(draw, tag, {std::round(row.value_right - tag_width),
                                   std::round(row.label_mid - tag.line * 0.5F)},
                       PaddockCol(kSndMuteRed, static_cast<unsigned>(255.0F * dim)),
                       "MUTED", "MUTED" + 5);
    } else {
        if (dim < 1.0F) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * dim);
        DrawSoundValue(draw, row.value_right, row.label_mid,
                       std::to_string(static_cast<int>(std::lround(SoundLevel(channel) * 100.0F))),
                       "%", master ? 25.0F : 20.0F);
        if (dim < 1.0F) ImGui::PopStyleVar();
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, row.height});
    return changed;
}

// One EQ band: boosts and cuts grow out of a 0 dB centre mark.
void DrawSoundBandRow(std::size_t band, const char* label, ImVec2 at, float width, bool narrow) {
    const SoundRowLayout row = LayoutSoundRow(at, width, narrow, true, false);
    const ImVec2 row_max{at.x + width, at.y + row.height};
    ImGui::PushID(label);
    const bool row_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                             ImGui::IsMouseHoveringRect(at, row_max);
    const ImGuiID slider_id = ImGui::GetID("##band");
    const bool slider_focused = ImGui::GetFocusID() == slider_id && ImGui::GetIO().NavVisible;
    DrawSoundRowSurface(at, row_max, false, row_hovered, slider_focused);
    DrawSoundRowLabel(row, label, false, 1.0F);

    std::array<int, 3> eq = SoundCurve();
    int db = eq[band];
    PaddockRangeLook look;
    look.bipolar = true;
    ImGui::SetCursorScreenPos({row.slider_x, row.slider_y});
    const PaddockRangeResult slider = PaddockRange("##band", &db, -12, 12, row.slider_width, look);
    if (slider.activated || slider.reset) db = 0;
    if (db != eq[band]) {
        eq[band] = db;
        SetSoundCurve(eq);
        // A hand-made curve is kept, so trying a preset never loses it.
        if (MatchSoundPreset(eq) == nullptr) g_eq_custom = eq;
        SaveSettings();
    }
    const std::string number = db > 0 ? "+" + std::to_string(db)
                             : db < 0 ? "-" + std::to_string(-db) : "0";
    DrawSoundValue(ImGui::GetWindowDrawList(), row.value_right, row.label_mid, number, "dB",
                   20.0F);
    ImGui::PopID();
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, row.height});
}

// .snd-link: the card's quiet action (Reset mix / Undo reset).
bool DrawSoundLinkButton(const char* label, bool disabled, const char* hint) {
    const PaddockType type = PaddockReading(14.0F, true, 1.0F);
    const char* end = PaddockLabelEnd(label);
    const float width = std::ceil(PaddockMeasure(type, label, end) + 28.0F + 2.0F);
    OlDisabled scope(disabled);
    const PaddockPress press = PaddockBeginPress(label, {width, 44.0F});
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && hint != nullptr) {
        ImGui::SetTooltip("%s", hint);
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const PaddockRadii radii = PaddockRound(10.0F);
    PaddockFill(draw, press.min, press.max, radii,
                PaddockApply(PaddockMix(PaddockRgb(0xFFFFFF, 10U), PaddockRgb(0xFFFFFF, 23U),
                                        press.hover)));
    PaddockStroke(draw, press.min, press.max, radii,
                  PaddockApply(PaddockMix(PaddockRgb(0xFFFFFF, 41U), PaddockRgb(0xFFFFFF, 71U),
                                          press.hover)),
                  1.0F);
    const ImU32 ink = PaddockApply(PaddockMix(PaddockRgb(0xD3E7EE), PaddockRgb(0xFFFFFF),
                                              press.hover));
    PaddockDrawRun(draw, type, {press.min.x + 15.0F, std::round(press.min.y + (44.0F - type.line) * 0.5F)},
                   ink, label, end);
    PaddockEndPress(press, 10.0F);
    return press.pressed;
}

// An EQ preset: the MODS / HACKS painted sign without its chevron; the preset
// in use is the red sign (.snd-chip).
float SoundChipWidth(const char* label) {
    const PaddockType type = PaddockSign(18.0F, 1.4F);
    return std::ceil(PaddockMeasure(type, label, PaddockLabelEnd(label)) + 24.0F + 4.0F);
}

bool DrawSoundChip(const char* label, bool selected, float width) {
    const PaddockType type = PaddockSign(18.0F, 1.4F);
    const char* end = PaddockLabelEnd(label);
    const float height = std::ceil(type.line + 20.0F + 4.0F);
    const PaddockPress press = PaddockBeginPress(label, {width, height});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto state = [&](unsigned resting, unsigned hover, unsigned chosen) {
        return selected ? PaddockRgb(chosen)
                        : PaddockMix(PaddockRgb(resting), PaddockRgb(hover), press.hover);
    };
    PaddockPanelStyle panel;
    panel.radii = {8.0F, 16.0F, 8.0F, 8.0F};
    panel.fill = state(0x075478, 0x08698F, 0xAA3322);
    panel.border = state(0x2987AA, 0xFFCA56, 0xFFC454);
    panel.border_width = 2.0F;
    panel.drop = PaddockRgb(0x03121C);
    panel.drop_offset = 3.0F;
    panel.highlight = PaddockRgb(0xFFFFFF, 20U);
    panel.highlight_width = 2.0F;
    PaddockPanel(draw, press.min, press.max, panel);
    const ImU32 text = PaddockApply(state(0xFFF2C8, 0xFFF7DC, 0xFFF5D5));
    const float text_width = PaddockMeasure(type, label, end);
    const ImVec2 at{std::round(press.min.x + (width - text_width) * 0.5F),
                    std::round(press.min.y + (height - type.line) * 0.5F)};
    PaddockDrawRun(draw, type, {at.x, at.y + 2.0F}, PaddockCol(0x031623), label, end);
    PaddockDrawRun(draw, type, at, text, label, end);
    PaddockEndPress(press, 12.0F);
    return press.pressed;
}

// A card's heading, its caption, and optionally its own action on the right
// (.snd-head). Returns the head's height.
template <typename Action>
float DrawSoundCardHead(const char* title, std::string_view caption, float width,
                        float action_width, Action&& action) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float text_width = std::max(width - (action_width > 0.0F ? action_width + 16.0F : 0.0F), 1.0F);
    ImGui::BeginGroup();
    PaddockText(PaddockSign(26.0F, 1.05F, 0.02F), PaddockRgb(kSndText), title, text_width, true);
    PaddockGap(6.0F);
    PaddockText(PaddockReading(14.0F, false, 1.45F), PaddockRgb(kSndMuted), caption, text_width);
    ImGui::EndGroup();
    float height = ImGui::GetItemRectSize().y;
    if (action_width > 0.0F) {
        ImGui::SetCursorScreenPos({at.x + width - action_width, at.y});
        action();
        height = std::max(height, 44.0F);
    }
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, height});
    return height;
}

// One of the Sound page's cards (.card.snd-card): 20 x 22 px of padding
// inside a 2 px border, 18 px corners.
template <typename Content>
void DrawSoundCard(float width, Content&& content) {
    PaddockBox box(width, {24.0F, 20.0F});
    PaddockGap(2.0F);
    content(box.Inner());
    box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
        PaddockPanel(draw, a, b,
                     {PaddockRound(18.0F), PaddockRgb(0x0B2E40, 245U),
                      PaddockRgb(0x296B70), 2.0F});
    });
}

// The 3-4 player race music option, in the page header (.snd-multiplayer).
void DrawSoundMultiplayerMusic(float width) {
    bool value = dkr::runtime::enhancements::multiplayer_race_music_requested();
    const PaddockType strong = PaddockReading(15.0F, true, 1.4F);
    const PaddockType note_type = PaddockReading(14.0F, false, 1.45F);
    constexpr std::string_view kLabel = "Multiplayer music";
    constexpr std::string_view kDescription = "Keep music on with 3\xE2\x80\x93" "4 players.";
    const float text_width = std::ceil(std::max(PaddockMeasure(strong, kLabel),
                                                PaddockMeasure(note_type, kDescription)));
    const float text_height = strong.line + 3.0F + note_type.line;
    const float item_width = std::min(24.0F + 12.0F + text_width, width);
    const float height = std::max(44.0F, text_height);
    const PaddockPress press = PaddockBeginPress("##snd-music34", {item_width, height});
    if (press.pressed) {
        value = !value;
        dkr::runtime::enhancements::set_multiplayer_race_music_enabled(value);
        SaveSettings();
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 box{press.min.x, std::round(press.min.y + (height - 24.0F) * 0.5F)};
    PaddockCheckBox(draw, box, value);
    const float text_x = press.min.x + 36.0F;
    const float text_y = std::round(press.min.y + (height - text_height) * 0.5F);
    PaddockDrawRun(draw, strong, {text_x, text_y}, PaddockCol(kSndText), kLabel.data(),
                   kLabel.data() + kLabel.size());
    PaddockDrawRun(draw, note_type, {text_x, text_y + strong.line + 3.0F}, PaddockCol(kSndMuted),
                   kDescription.data(), kDescription.data() + kDescription.size());
    PaddockEndPress(press, 6.0F, 1.0F, false);
    if (press.focused) PaddockCheckFocus(draw, box);
    if (!press.hovered && !press.focused) return;
    // The tooltip opens under the setting (.snd-multiplayer-tip).
    constexpr std::string_view kTip =
        "The original game turns race music off with 3 or 4 players. "
        "Leave this on to keep it playing.";
    const PaddockType tip = PaddockReading(14.0F, false, 1.45F);
    const float tip_width = std::min(300.0F, ImGui::GetIO().DisplaySize.x - 64.0F);
    const float tip_height = PaddockTextHeight(tip, kTip, tip_width - 28.0F) + 24.0F;
    const ImVec2 a{press.min.x, press.max.y};
    const ImVec2 b{a.x + tip_width, a.y + tip_height};
    ImDrawList* front = ImGui::GetForegroundDrawList();
    for (int layer = 3; layer >= 1; --layer) {
        const float spread = static_cast<float>(layer) * 4.0F;
        PaddockFill(front, {a.x - spread * 0.5F, a.y + 8.0F - spread * 0.5F},
                    {b.x + spread * 0.5F, b.y + 8.0F + spread},
                    PaddockRound(8.0F + spread), PaddockRgb(0x000000, 22U));
    }
    PaddockFill(front, a, b, PaddockRound(8.0F), PaddockRgb(0x071C29));
    PaddockStroke(front, a, b, PaddockRound(8.0F), PaddockRgb(0xFFFFFF, 38U), 1.0F);
    PaddockTextStyle style;
    style.colour = PaddockRgb(kSndText);
    PaddockTextAt(front, tip, {a.x + 14.0F, a.y + 12.0F}, tip_width - 28.0F, kTip, style);
}

void DrawSoundPage(float available_width) {
    SoundPageState& state = g_sound_page;
    const int frame = ImGui::GetFrameCount();
    const bool entered = state.context != ImGui::GetCurrentContext() ||
                         state.last_frame != frame - 1;
    if (entered) {
        state.context = ImGui::GetCurrentContext();
        state.undo_levels.reset();
    }
    state.last_frame = frame;
    const bool modern = dkr::runtime::enhancements::modern_options_visible(
        dkr::runtime::enhancements::presentation_profile());
    const float width = std::min(available_width, 1240.0F);
    // Container query: narrow panels put each slider under its label.
    const bool narrow = width <= 600.0F;
    // A focus request for the page (a new page, LB / RB) lands on Master,
    // not on the setting above it.
    const bool focus_master = entered && GImGui->NavMoveScoringItems &&
                              (GImGui->NavMoveFlags & ImGuiNavMoveFlags_FocusApi) != 0;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});

    DrawPageHeading("SOUND");
    PaddockGap(4.0F);
    PaddockText(PaddockReading(14.0F, false, 1.5F), PaddockRgb(kSndMuted),
                "Set how loud the game is and shape its sound. Changes apply right away.", width);
    PaddockGap(16.0F);
    if (modern) {
        DrawSoundMultiplayerMusic(width);
        PaddockGap(16.0F);
    }

    // ------------------------------------------------------------ volume
    DrawSoundCard(width, [&](float inner) {
        bool mix_changed = false;
        const char* reset_label = state.undo_levels ? "Undo reset" : "Reset mix";
        const bool reset_disabled = !state.undo_levels && SoundMixIsOriginal();
        const char* reset_hint = reset_disabled ? "The mix is already at 100%."
            : state.undo_levels ? "Put the mix back how it was."
            : "Music, effects, vehicles and ambience back to 100%. Master stays.";
        const float reset_width = modern
            ? std::ceil(PaddockMeasure(PaddockReading(14.0F, true, 1.0F), reset_label) + 30.0F)
            : 0.0F;
        DrawSoundCardHead("Volume",
                          modern ? "Master sets how loud everything is. The mix balances the parts."
                                 : "Master sets how loud everything is.",
                          inner, reset_width, [&] {
            if (!DrawSoundLinkButton(reset_label, reset_disabled, reset_hint)) return;
            if (state.undo_levels) {
                for (std::size_t index = 0; index < 4U; ++index) {
                    SetSoundLevel(index + 1U, (*state.undo_levels)[index]);
                    SetSoundMuted(index + 1U, state.undo_muted[index]);
                }
                state.undo_levels.reset();
                OlNotify("Mix restored.");
            } else {
                std::array<float, 4> levels{};
                for (std::size_t index = 0; index < 4U; ++index) {
                    levels[index] = SoundLevel(index + 1U);
                    state.undo_muted[index] = g_sound_muted[index + 1U];
                    SetSoundLevel(index + 1U, 1.0F);
                }
                state.undo_levels = levels;
                OlNotify("Mix back to 100%. Master is unchanged.");
            }
            SaveSettings();
        });
        PaddockGap(4.0F + 10.0F);

        const bool master_silent = g_sound_muted[0] || std::lround(SoundLevel(0U) * 100.0F) == 0;
        if (focus_master) ImGui::SetKeyboardFocusHere();
        const ImVec2 master_at = ImGui::GetCursorScreenPos();
        if (DrawSoundVolumeRow(0U, "Master", {master_at.x - 4.0F, master_at.y}, inner + 8.0F,
                               narrow, false)) {
            SaveSettings();
        }
        if (!modern) {
            PaddockGap(10.0F);
            PaddockText(PaddockReading(14.0F, false, 1.45F), PaddockRgb(kSndMuted),
                        "Accurate keeps the original island mix: music, effects, vehicles and "
                        "the EQ stay at their authored values.",
                        inner);
            return;
        }

        // The mix: a unit_type label and hairline, with the "nothing plays"
        // warning riding on it (.snd-mix-label).
        PaddockGap(10.0F + 8.0F);
        {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            PaddockType tag = PaddockReading(11.0F, true, 1.0F);
            tag.tracking = 11.0F * 0.14F;
            const float mid = at.y + 10.0F;
            const float tag_width = PaddockMeasure(tag, "MIX") - tag.tracking;
            PaddockDrawRun(draw, tag, {at.x + 4.0F, std::round(mid - tag.line * 0.5F)},
                           PaddockCol(0x8FB3BD), "MIX", "MIX" + 3);
            float line_end = at.x + inner - 4.0F;
            if (master_silent) {
                const PaddockType note = PaddockReading(13.0F, true, 1.3F);
                const std::string_view text = g_sound_muted[0]
                    ? "Master is muted, so nothing plays."
                    : "Master is at 0%, so nothing plays.";
                const float note_width = PaddockMeasure(note, text);
                PaddockDrawRun(draw, note, {std::round(line_end - note_width),
                                            std::round(mid - note.line * 0.5F)},
                               PaddockCol(0xFFD690), text.data(), text.data() + text.size());
                line_end -= note_width + 12.0F;
            }
            draw->AddRectFilled({at.x + 4.0F + tag_width + 12.0F, std::round(mid)},
                                {line_end, std::round(mid) + 1.0F}, PaddockCol(0xFFFFFF, 26U));
            ImGui::Dummy({inner, 20.0F});
        }
        PaddockGap(10.0F - 2.0F);
        constexpr std::array<const char*, 4> kChannels{{
            "Music", "Sound effects", "Vehicles", "Nature & ambience"}};
        for (std::size_t index = 0; index < kChannels.size(); ++index) {
            if (index > 0U) PaddockGap(2.0F);
            if (DrawSoundVolumeRow(index + 1U, kChannels[index], ImGui::GetCursorScreenPos(),
                                   inner, narrow, master_silent)) {
                mix_changed = true;
            }
        }
        if (mix_changed) {
            state.undo_levels.reset();
            SaveSettings();
        }
    });
    if (!modern) {
        ImGui::PopStyleVar();
        return;
    }

    // ------------------------------------------------------------ equalizer
    PaddockGap(16.0F);
    DrawSoundCard(width, [&](float inner) {
        DrawSoundCardHead("Equalizer", "Pick a sound, or fine-tune the three bands.", inner,
                          0.0F, [] {});
        PaddockGap(4.0F + 10.0F);

        const std::array<int, 3> eq = SoundCurve();
        const SoundPreset* preset = MatchSoundPreset(eq);
        const bool custom_kept = g_eq_custom && MatchSoundPreset(*g_eq_custom) == nullptr;
        std::vector<const char*> chips;
        for (const SoundPreset& entry : kSoundPresets) chips.push_back(entry.label);
        if (preset == nullptr || custom_kept) chips.push_back("Custom");
        // Natural widths, wrapping like the MODS / HACKS bars; narrow panels
        // stretch each row to fill (flex: 1 1 110px).
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        float x = 0.0F;
        float y = 0.0F;
        const float chip_height = std::ceil(PaddockSign(18.0F, 1.4F).line + 24.0F);
        for (const char* label : chips) {
            float chip_width = narrow ? std::max(SoundChipWidth(label), 110.0F) : SoundChipWidth(label);
            if (x > 0.0F && x + chip_width > inner) {
                x = 0.0F;
                y += chip_height + 10.0F;
            }
            ImGui::SetCursorScreenPos({origin.x + x, origin.y + y});
            const bool is_custom = std::strcmp(label, "Custom") == 0;
            const bool selected = is_custom ? preset == nullptr
                                            : preset != nullptr && std::strcmp(preset->label, label) == 0;
            if (DrawSoundChip(label, selected, chip_width)) {
                if (is_custom) {
                    if (g_eq_custom) SetSoundCurve(*g_eq_custom);
                } else {
                    for (const SoundPreset& entry : kSoundPresets) {
                        if (std::strcmp(entry.label, label) == 0) SetSoundCurve(entry.eq);
                    }
                }
                SaveSettings();
            }
            x += chip_width + 10.0F;
        }
        ImGui::SetCursorScreenPos({origin.x, origin.y + y + chip_height});
        ImGui::Dummy({inner, 0.0F});
        PaddockGap(10.0F);

        // "<strong>Flat</strong> · The original game sound."
        {
            const SoundPreset* shown = MatchSoundPreset(SoundCurve());
            const std::string name = shown != nullptr ? shown->label : "Custom";
            const std::string rest = std::string(" \xC2\xB7 ") +
                (shown != nullptr ? shown->about : kSoundCustomAbout);
            const PaddockType bold = PaddockReading(14.0F, true, 1.45F);
            const PaddockType plain = PaddockReading(14.0F, false, 1.45F);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            PaddockDrawRun(draw, bold, at, PaddockCol(kSndText), name.data(), name.data() + name.size());
            const float lead = PaddockMeasure(bold, name);
            const auto lines = PaddockWrap(plain, rest, std::max(inner - lead, 1.0F));
            PaddockTextStyle style;
            style.colour = PaddockCol(kSndMuted);
            PaddockDrawLines(draw, plain, {at.x + lead, at.y}, inner - lead, lines, style);
            ImGui::Dummy({inner, plain.line * static_cast<float>(std::max<std::size_t>(lines.size(), 1U))});
        }
        PaddockGap(10.0F + 4.0F);

        constexpr std::array<const char*, 3> kBands{{"Bass", "Mid", "Treble"}};
        for (std::size_t band = 0; band < kBands.size(); ++band) {
            if (band > 0U) PaddockGap(2.0F);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            DrawSoundBandRow(band, kBands[band], {at.x - 12.0F, at.y}, inner + 24.0F, narrow);
        }
    });
    ImGui::PopStyleVar();
}
