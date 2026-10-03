// Included by runtime_ui.cpp inside its UI namespace, after
// runtime_online_ui.inl. Presentation only.
//
// The settings fields of tools/launcher-html, shared by the settings pages:
// titled cards (.card with an h2 and caption), labelled drop-downs
// (.rom-select.is-compact and its .picker list), described checkboxes
// (.gfx-option), slider fields (.settings-slider), disclosures
// (.gfx-advanced) and quiet race buttons. Sizes are CSS px.

constexpr unsigned kSetText = 0xFFF6DA;    // --text
constexpr unsigned kSetMuted = 0xB0C9CC;   // --muted
constexpr unsigned kSetWarm = 0xFFAB14;    // --warm
constexpr unsigned kSetBorder = 0x296B70;  // --border
constexpr unsigned kSetFocus = 0xFFD11F;

// Pages whose field labels use the regular weight (Controls) raise this.
int g_settings_regular_labels = 0;

inline PaddockType SetLabelType() {
    return PaddockReading(16.0F, g_settings_regular_labels == 0, 1.4F);
}
inline PaddockType SetHelpType() { return PaddockReading(14.0F, false, 1.5F); }

// A field's label (.field > label): 600 16px, 8 px above its control.
void SettingsLabel(std::string_view label, float width) {
    if (label.empty()) return;
    PaddockText(SetLabelType(), PaddockRgb(kSetText), label, width);
    PaddockGap(8.0F);
}

// Help under a control (.gfx-help).
float SettingsHelp(std::string_view text, float width, unsigned colour = kSetMuted) {
    if (text.empty()) return 0.0F;
    return PaddockText(SetHelpType(), PaddockRgb(colour), text, width);
}

// ------------------------------------------------------------ drop-down

struct SettingsChoice {
    std::string label;
    std::string detail;
    bool disabled = false;
    bool action = false;     // .picker-option.is-action: an amber "+" row
    bool separator = false;  // .picker-sep: a hairline, not a choice

    SettingsChoice(const char* text) : label(text) {}
    SettingsChoice(std::string text, std::string more = {}, bool off = false)
        : label(std::move(text)), detail(std::move(more)), disabled(off) {}

    static SettingsChoice Action(std::string text) {
        SettingsChoice choice(std::move(text));
        choice.action = true;
        return choice;
    }
    static SettingsChoice Separator() {
        SettingsChoice choice(std::string{});
        choice.separator = true;
        return choice;
    }
};

// The open list (.picker): a dark panel under its field, with a check on
// the current choice. Returns the picked index, or -1.
int DrawSettingsPicker(const char* popup, ImVec2 anchor_min, ImVec2 anchor_max, int current,
                       const std::vector<SettingsChoice>& choices, float minimum_width = 220.0F) {
    const PaddockType label_type = PaddockReading(15.0F, true, 1.35F);
    const PaddockType detail_type = PaddockReading(12.5F, false, 1.35F);
    const float width = std::max(anchor_max.x - anchor_min.x, minimum_width);
    const auto row_height = [&](const SettingsChoice& choice) {
        if (choice.separator) return 13.0F;
        return std::max(44.0F, label_type.line + (choice.detail.empty() ? 0.0F : detail_type.line + 1.0F) + 18.0F);
    };
    float natural = 12.0F;
    for (const SettingsChoice& choice : choices) natural += row_height(choice);
    // Below the field, or above it when the window has no room underneath.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float below = display.y - anchor_max.y - 6.0F - 12.0F;
    const float above = anchor_min.y - 6.0F - 12.0F;
    const bool flip = natural > below && above > below;
    const float height = std::min(natural, std::max(flip ? above : below, 120.0F));
    const ImVec2 at{anchor_min.x, flip ? anchor_min.y - 6.0F - height : anchor_max.y + 6.0F};
    ImGui::SetNextWindowPos(at);
    ImGui::SetNextWindowSize({width, height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6.0F, 6.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PaddockRgb(0x000000, 0U));
    int picked = -1;
    if (ImGui::BeginPopup(popup, ImGuiWindowFlags_NoMove)) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetWindowPos();
        const ImVec2 b{a.x + width, a.y + height};
        // box-shadow: 0 14px 36px #00000080, 0 0 0 1px #0000004d
        draw->PushClipRect({0.0F, 0.0F}, display, false);
        for (int layer = 4; layer >= 1; --layer) {
            const float spread = static_cast<float>(layer) * 6.0F;
            PaddockFill(draw, {a.x - spread * 0.5F, a.y + 14.0F - spread * 0.5F},
                        {b.x + spread * 0.5F, b.y + 14.0F + spread * 0.5F},
                        PaddockRound(12.0F + spread), PaddockRgb(0x000000, 26U));
        }
        PaddockStroke(draw, {a.x - 1.0F, a.y - 1.0F}, {b.x + 1.0F, b.y + 1.0F}, PaddockRound(13.0F),
                      PaddockRgb(0x000000, 77U), 1.0F);
        PaddockFill(draw, a, b, PaddockRound(12.0F), PaddockRgb(0x071C29));
        PaddockStroke(draw, a, b, PaddockRound(12.0F), PaddockRgb(0x3C6478), 1.0F);
        draw->PopClipRect();
        const float inner = width - 12.0F;
        for (int index = 0; index < static_cast<int>(choices.size()); ++index) {
            const SettingsChoice& choice = choices[static_cast<std::size_t>(index)];
            const float row = row_height(choice);
            if (choice.separator) {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                draw->AddRectFilled({at.x + 8.0F, at.y + 6.0F}, {at.x + inner - 8.0F, at.y + 7.0F},
                                    PaddockRgb(0xFFFFFF, 26U));
                ImGui::Dummy({inner, row});
                continue;
            }
            ImGui::PushID(index);
            if (choice.disabled) ImGui::BeginDisabled();
            const PaddockPress press = PaddockBeginPress("##choice", {inner, row}, 0.12F, 0.12F);
            if (choice.disabled) ImGui::EndDisabled();
            if (index == current && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            const float alpha = choice.disabled ? 0.45F : 1.0F;
            const auto tone = [&](unsigned rgb, unsigned a8 = 255U) {
                return PaddockRgb(rgb, static_cast<unsigned>(static_cast<float>(a8) * alpha));
            };
            const PaddockRadii radii = PaddockRound(7.0F);
            if (press.focused) {
                PaddockFill(draw, press.min, press.max, radii, PaddockRgb(0x1F4A62));
                PaddockStroke(draw, press.min, press.max, radii, PaddockRgb(kSetFocus), 2.0F);
            } else if (!choice.disabled) {
                PaddockFill(draw, press.min, press.max, radii,
                            PaddockMix(PaddockRgb(0x163A4F, 0U), PaddockRgb(0x163A4F), press.hover));
            }
            if (index == current) {
                // An 11 x 6 tick of 2.5 px strokes, turned -45 degrees.
                const ImVec2 centre{press.min.x + 14.0F + 5.5F, press.min.y + 19.0F + 3.0F};
                const auto turn = [&](float x, float y) {
                    constexpr float kHalf = 0.70710678F;
                    return ImVec2{centre.x + (x + y) * kHalf, centre.y + (y - x) * kHalf};
                };
                const std::array<ImVec2, 3> tick{{turn(-4.25F, -3.0F), turn(-4.25F, 1.75F),
                                                  turn(5.5F, 1.75F)}};
                draw->AddPolyline(tick.data(), 3, tone(0x54C9AD), 0, 2.5F);
            }
            if (choice.action) {
                const PaddockType plus = PaddockReading(20.0F, true, 1.0F);
                PaddockDrawRun(draw, plus, {press.min.x + 14.0F, std::round((press.min.y + press.max.y) * 0.5F - plus.line * 0.52F)},
                               tone(0xFFC453), "+", "+" + 1);
            }
            const float text_x = press.min.x + 36.0F;
            const float text_width = std::max(press.max.x - 12.0F - text_x, 1.0F);
            const std::string label = PaddockEllipsize(label_type, choice.label, text_width);
            const float top = choice.detail.empty() ? std::round(press.min.y + (row - label_type.line) * 0.5F)
                                                    : press.min.y + 9.0F;
            PaddockDrawRun(draw, label_type, {text_x, top}, tone(choice.action ? 0xFFC453 : 0xEAF3F5), label.data(),
                           label.data() + label.size());
            if (!choice.detail.empty()) {
                const std::string detail = PaddockEllipsize(detail_type, choice.detail, text_width);
                PaddockDrawRun(draw, detail_type, {text_x, top + label_type.line + 1.0F},
                               tone(0x9FB8C4), detail.data(), detail.data() + detail.size());
            }
            if (press.pressed && !choice.disabled) {
                picked = index;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    return picked;
}

// A labelled drop-down: the field shows the current choice and opens the
// list (dropdownRow). Returns true when the choice changed.
bool SettingsDropdown(const char* id, std::string_view label, int* value,
                      const std::vector<SettingsChoice>& choices, float width,
                      bool disabled = false, std::string_view help = {}) {
    SettingsLabel(label, width);
    *value = std::clamp(*value, 0, std::max(static_cast<int>(choices.size()) - 1, 0));
    ImGui::PushID(id);
    const char* popup = "##choices";
    const bool open = ImGui::IsPopupOpen(popup);
    if (disabled) ImGui::BeginDisabled();
    const PaddockPress press = PaddockBeginPress("##field", {width, 52.0F});
    if (disabled) ImGui::EndDisabled();
    if (press.pressed && !disabled) ImGui::OpenPopup(popup);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int first = draw->VtxBuffer.Size;
    const PaddockRadii radii = PaddockRound(8.0F);
    PaddockFill(draw, press.min, press.max, radii,
                PaddockMix(PaddockRgb(0x061D2C), PaddockRgb(0x0A2A3D), disabled ? 0.0F : press.hover));
    PaddockStroke(draw, press.min, press.max, radii,
                  open ? PaddockRgb(kSetFocus)
                       : PaddockMix(PaddockRgb(0x2F6A86), PaddockRgb(0x5A97B1), disabled ? 0.0F : press.hover),
                  1.0F);
    const PaddockType type = PaddockReading(16.0F, true, 1.25F);
    const std::string shown = PaddockEllipsize(
        type, choices.empty() ? "Unavailable" : choices[static_cast<std::size_t>(*value)].label,
        width - 14.0F - 14.0F - 12.0F - 16.0F);
    PaddockDrawRun(draw, type, {press.min.x + 15.0F, std::round(press.min.y + (52.0F - type.line) * 0.5F)},
                   PaddockRgb(0xFFFFFF), shown.data(), shown.data() + shown.size());
    if (!disabled) {
        PaddockChevron(draw, {press.max.x - 14.0F - 2.0F - 6.0F, press.min.y + 52.0F * 0.5F - 3.0F},
                       45.0F, 10.0F, PaddockRgb(0xFFFFFF), 2.5F);
    }
    OlFade(draw, first, disabled ? 0.6F : 1.0F);
    PaddockEndPress(press, 8.0F, 0.98F);
    const int picked = DrawSettingsPicker(popup, press.min, press.max, *value, choices);
    ImGui::PopID();
    bool changed = false;
    if (picked >= 0 && picked != *value) {
        *value = picked;
        changed = true;
    }
    if (!help.empty()) {
        PaddockGap(8.0F);
        SettingsHelp(help, width);
    }
    return changed;
}

bool SettingsDropdown(const char* id, std::string_view label, int* value,
                      std::initializer_list<const char*> items, float width,
                      bool disabled = false, std::string_view help = {}) {
    std::vector<SettingsChoice> choices;
    for (const char* item : items) choices.push_back({item, {}, false});
    return SettingsDropdown(id, label, value, choices, width, disabled, help);
}

// ------------------------------------------------------------ checkbox

// A checkbox with its label, and its help 36 px in under it (.gfx-option).
bool SettingsCheck(const char* id, std::string_view label, bool* value, float width,
                   std::string_view help = {}, bool disabled = false) {
    const PaddockType type = SetLabelType();
    const float text_width = std::max(width - 36.0F, 1.0F);
    const auto lines = PaddockWrap(type, label, text_width);
    float label_width = 0.0F;
    for (const auto& line : lines) label_width = std::max(label_width, line.width);
    const float text_height = type.line * static_cast<float>(lines.size());
    const float height = std::max(44.0F, text_height);
    if (disabled) ImGui::BeginDisabled();
    const PaddockPress press = PaddockBeginPress(id, {std::min(36.0F + std::ceil(label_width) + 4.0F, width), height});
    if (disabled) ImGui::EndDisabled();
    bool changed = false;
    if (press.pressed && !disabled) {
        *value = !*value;
        changed = true;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int first = draw->VtxBuffer.Size;
    const ImVec2 box{press.min.x, std::round(press.min.y + (height - 24.0F) * 0.5F)};
    PaddockCheckBox(draw, box, *value);
    PaddockTextStyle style;
    style.colour = PaddockRgb(kSetText);
    PaddockDrawLines(draw, type, {press.min.x + 36.0F, std::round(press.min.y + (height - text_height) * 0.5F)},
                     text_width, lines, style);
    if (disabled) OlFade(draw, first, 0.6F);
    PaddockEndPress(press, 6.0F, 1.0F, false);
    if (press.focused) PaddockCheckFocus(draw, box);
    if (!help.empty()) {
        PaddockGap(2.0F);
        ImGui::Indent(36.0F);
        SettingsHelp(help, std::max(width - 36.0F, 1.0F));
        ImGui::Unindent(36.0F);
    }
    return changed;
}

// ------------------------------------------------------------ slider field

// A slider field (.settings-slider): label and value on one line, the
// slider under them, then its help. The whole field carries the focus ring.
struct SettingsRangeValue {
    std::string number;
    std::string unit;
};

template <typename Format>
bool SettingsRange(const char* id, std::string_view label, int* value, int minimum, int maximum,
                   float width, Format&& format, std::string_view help = {},
                   bool disabled = false) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // The ring and tint go under the field once its height is known.
    ImDrawListSplitter splitter;
    splitter.Split(draw, 2);
    splitter.SetCurrentChannel(draw, 1);
    const PaddockType type = SetLabelType();
    const SettingsRangeValue shown = format(*value);
    const PaddockType digits = PaddockSign(20.0F, 1.0F, 0.0F);
    const PaddockType unit_type = PaddockReading(13.0F, true, 1.0F);
    const float value_width = PaddockMeasure(digits, shown.number) +
        (shown.unit.empty() ? 0.0F : 2.0F + PaddockMeasure(unit_type, shown.unit));
    const int first = draw->VtxBuffer.Size;
    PaddockText(type, PaddockRgb(kSetText), label, std::max(width - value_width - 12.0F, 1.0F));
    const float label_bottom = ImGui::GetItemRectMax().y;
    {
        const float mid = at.y + type.line * 0.5F;
        const float right = at.x + width;
        const float top = std::round(mid - digits.line * 0.5F);
        const float baseline = top + (digits.line - digits.content) * 0.5F + digits.ascent;
        const float unit_width = shown.unit.empty() ? 0.0F : PaddockMeasure(unit_type, shown.unit);
        const float left = std::round(right - value_width);
        PaddockDrawRun(draw, digits, {left, top}, PaddockCol(kSetText), shown.number.data(),
                       shown.number.data() + shown.number.size());
        if (!shown.unit.empty()) {
            PaddockDrawRun(draw, unit_type,
                           {right - unit_width,
                            baseline - (unit_type.line - unit_type.content) * 0.5F - unit_type.ascent},
                           PaddockCol(kSetMuted), shown.unit.data(), shown.unit.data() + shown.unit.size());
        }
    }
    ImGui::SetCursorScreenPos({at.x, label_bottom + 8.0F});
    if (disabled) ImGui::BeginDisabled();
    const PaddockRangeResult range = PaddockRange(id, value, minimum, maximum, width);
    if (disabled) ImGui::EndDisabled();
    if (disabled) OlFade(draw, first, 0.6F);
    if (!help.empty()) {
        PaddockGap(8.0F);
        SettingsHelp(help, width);
    }
    const ImVec2 end{at.x + width, ImGui::GetItemRectMax().y};
    splitter.SetCurrentChannel(draw, 0);
    if (range.focused) {
        PaddockFill(draw, at, end, PaddockRound(12.0F), PaddockCol(0xFFFFFF, 15U));
        draw->AddRect({at.x - 4.5F, at.y - 4.5F}, {end.x + 4.5F, end.y + 4.5F},
                      PaddockCol(kSetFocus), 16.5F, 0, 3.0F);
    }
    splitter.Merge(draw);
    return range.changed;
}

// ------------------------------------------------------------ cards

// A card's title (.gfx-section h2: 26 px lettering; an ampersand is set in
// the reading face, as the study's .gfx-heading-symbol).
float SettingsCardTitle(std::string_view title, float width) {
    const PaddockType sign = PaddockSign(26.0F, 1.05F, 0.02F);
    const PaddockType symbol = PaddockReading(20.0F, true, 1.05F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float symbol_scale = 26.0F / 20.0F;
    PaddockType amp = symbol;
    amp.size *= symbol_scale;
    amp.ascent *= symbol_scale;
    amp.content *= symbol_scale;
    amp.line = sign.line;
    const std::size_t split = title.find('&');
    if (split == std::string_view::npos ||
        PaddockMeasure(sign, title) > width) {
        return PaddockText(sign, PaddockRgb(kSetText), title, width, true);
    }
    float x = at.x;
    const std::string_view before = title.substr(0, split);
    const std::string_view after = title.substr(split + 1U);
    PaddockDrawRun(draw, sign, {x, at.y}, PaddockCol(kSetText), before.data(), before.data() + before.size());
    x += PaddockMeasure(sign, before);
    // Line the symbol's baseline up with the lettering's.
    const float baseline = at.y + (sign.line - sign.content) * 0.5F + sign.ascent;
    PaddockDrawRun(draw, amp, {x, baseline - (amp.line - amp.content) * 0.5F - amp.ascent},
                   PaddockCol(kSetText), "&", "&" + 1);
    x += PaddockMeasure(amp, "&");
    PaddockDrawRun(draw, sign, {x, at.y}, PaddockCol(kSetText), after.data(), after.data() + after.size());
    ImGui::Dummy({width, sign.line});
    return sign.line;
}

// The card's head: title, 6 px, caption; 18 px under it.
void SettingsCardHead(std::string_view title, std::string_view caption, float width) {
    SettingsCardTitle(title, width);
    if (!caption.empty()) {
        PaddockGap(6.0F);
        SettingsHelp(caption, width);
    }
    PaddockGap(18.0F);
}

// A settings card (.card.gfx-section): 20 px of padding inside a 2 px
// border. `min_height` lets cards in one row stretch to the tallest.
template <typename Content>
float SettingsCard(float width, Content&& content, float min_height = 0.0F) {
    PaddockBox box(width, {22.0F, 22.0F}, min_height);
    content(box.Inner());
    return box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
        PaddockPanel(draw, a, b,
                     {PaddockRound(18.0F), PaddockRgb(0x0B2E40, 245U), PaddockRgb(kSetBorder), 2.0F});
    });
}

// Two cards side by side whose heights match (.gfx-columns, align-items:
// stretch), or stacked when the panel is narrow. Heights come from the
// previous frame.
std::map<std::string, float> g_settings_card_heights;

template <typename Left, typename Right>
void SettingsCardPair(const char* key, float width, bool stacked, float gap,
                      Left&& left, Right&& right) {
    if (stacked) {
        SettingsCard(width, left);
        PaddockGap(gap);
        SettingsCard(width, right);
        return;
    }
    const std::string left_key = std::string(key) + "/left";
    const std::string right_key = std::string(key) + "/right";
    const float stretch = std::max(g_settings_card_heights[left_key], g_settings_card_heights[right_key]);
    const float half = std::floor((width - gap) * 0.5F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    // Measured without the stretch, so a card can also shrink.
    float left_height = 0.0F;
    float right_height = 0.0F;
    {
        PaddockBox box(half, {22.0F, 22.0F}, stretch);
        left(box.Inner());
        const float content = ImGui::GetCursorScreenPos().y - (at.y + 22.0F);
        left_height = content + 44.0F;
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanel(draw, a, b, {PaddockRound(18.0F), PaddockRgb(0x0B2E40, 245U),
                                      PaddockRgb(kSetBorder), 2.0F});
        });
    }
    ImGui::EndGroup();
    const float bottom_left = ImGui::GetItemRectMax().y;
    ImGui::SetCursorScreenPos({at.x + half + gap, at.y});
    ImGui::BeginGroup();
    {
        PaddockBox box(width - half - gap, {22.0F, 22.0F}, stretch);
        right(box.Inner());
        const float content = ImGui::GetCursorScreenPos().y - (at.y + 22.0F);
        right_height = content + 44.0F;
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanel(draw, a, b, {PaddockRound(18.0F), PaddockRgb(0x0B2E40, 245U),
                                      PaddockRgb(kSetBorder), 2.0F});
        });
    }
    ImGui::EndGroup();
    const float bottom = std::max(bottom_left, ImGui::GetItemRectMax().y);
    g_settings_card_heights[left_key] = left_height;
    g_settings_card_heights[right_key] = right_height;
    ImGui::SetCursorScreenPos({at.x, bottom});
    ImGui::Dummy({width, 0.0F});
}

// ------------------------------------------------------------ disclosure

// A disclosure row (.gfx-advanced summary): a hairline above, a turning
// marker and a semibold label. Returns the open state.
bool SettingsDisclosure(const char* id, std::string_view label, bool& open, float width) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, {at.x + width, at.y + 1.0F}, PaddockCol(kSetBorder));
    ImGui::SetCursorScreenPos({at.x, at.y + 1.0F});
    const PaddockType type = SetLabelType();
    const float text_width = PaddockMeasure(type, label);
    const float height = std::max(44.0F, 14.0F + type.line + 4.0F);
    const PaddockPress press = PaddockBeginPress(id, {std::min(4.0F + 17.0F + text_width + 8.0F, width), height});
    if (press.pressed) open = !open;
    const ImU32 colour = PaddockApply(PaddockMix(PaddockRgb(kSetText), PaddockRgb(kSetWarm), press.hover));
    const float top = press.min.y + 14.0F;
    const ImVec2 marker{press.min.x + 4.0F + 4.0F, top + type.line * 0.5F};
    const float turn = PaddockEase(PaddockTween(PaddockKey("open", press.id), open, 0.15F, 0.15F));
    // A small solid triangle that turns to point down.
    const float radians = turn * 1.5707963F;
    const auto point = [&](float x, float y) {
        return ImVec2{marker.x + x * std::cos(radians) - y * std::sin(radians),
                      marker.y + x * std::sin(radians) + y * std::cos(radians)};
    };
    draw->AddTriangleFilled(point(-3.0F, -4.5F), point(4.5F, 0.0F), point(-3.0F, 4.5F), colour);
    PaddockDrawRun(draw, type, {press.min.x + 4.0F + 17.0F, top}, colour, label.data(),
                   label.data() + label.size());
    PaddockEndPress(press, 6.0F, 1.0F);
    ImGui::SetCursorScreenPos({at.x, press.max.y});
    ImGui::Dummy({width, 0.0F});
    return open;
}

// ------------------------------------------------------------ buttons

// A quiet race button for page footers (.gfx-reset-actions .race-button).
bool SettingsRaceButton(const char* label, bool disabled, const char* reason = nullptr,
                        unsigned fill = 0x123E50) {
    RaceButtonLook look;
    look.fill = fill;
    look.hover_in = 0.12F;
    const float width = PaddockRaceButtonWidth(label);
    OlDisabled scope(disabled, 0.6F);
    const bool pressed = PaddockRaceButton(label, {width, 44.0F}, look);
    if (disabled && reason != nullptr &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", reason);
    }
    return pressed && !disabled;
}

// Two columns of fields (.gfx-fields), row by row; one column when narrow.
struct SettingsColumns {
    ImVec2 origin{};
    float width = 0.0F;
    float column = 0.0F;
    float gap = 18.0F;
    bool single = false;
    float row_top = 0.0F;
    float row_bottom = 0.0F;
    int index = 0;

    SettingsColumns(float total, bool one_column, float column_gap = 18.0F)
        : origin(ImGui::GetCursorScreenPos()), width(total), gap(column_gap), single(one_column) {
        column = single ? total : std::floor((total - gap) * 0.5F);
        row_top = origin.y;
        row_bottom = origin.y;
    }

    // Starts the next cell; draw the field, then call End().
    void Begin() {
        if (single) {
            if (index > 0) {
                ImGui::SetCursorScreenPos({origin.x, row_bottom + gap});
                row_top = row_bottom + gap;
            }
        } else if (index % 2 == 0) {
            if (index > 0) row_top = row_bottom + gap;
            ImGui::SetCursorScreenPos({origin.x, row_top});
        } else {
            ImGui::SetCursorScreenPos({origin.x + column + gap, row_top});
        }
        ImGui::BeginGroup();
    }

    void End() {
        ImGui::EndGroup();
        const float bottom = ImGui::GetItemRectMax().y;
        row_bottom = single || index % 2 == 0 ? bottom : std::max(row_bottom, bottom);
        ++index;
    }

    void Finish() {
        ImGui::SetCursorScreenPos({origin.x, std::max(row_bottom, origin.y)});
        ImGui::Dummy({width, 0.0F});
    }
};

// ------------------------------------------------------------ quiet buttons

enum class SettingsButtonTone {
    Plain,      // .settings-button
    Selected,   // .settings-button.is-selected
    Outline,    // a binding's alternate column
    Unbound,    // an empty binding: dashed and muted
};

inline float SettingsButtonWidth(std::string_view label) {
    return std::ceil(PaddockMeasure(PaddockReading(14.0F, true, 1.35F), label) + 28.0F + 2.0F);
}

// .settings-button: the quiet action of the settings pages. width <= 0 sizes
// to the label; a long label wraps and grows the height.
bool SettingsButton(const char* label, float width = 0.0F, bool disabled = false,
                    SettingsButtonTone tone = SettingsButtonTone::Plain, bool regular = false,
                    const char* reason = nullptr, float padding_x = 14.0F) {
    const char* end = PaddockLabelEnd(label);
    const std::string_view text(label, static_cast<std::size_t>(end - label));
    const PaddockType type = PaddockReading(14.0F, !regular, 1.35F);
    if (width <= 0.0F) width = std::ceil(PaddockMeasure(type, text) + padding_x * 2.0F + 2.0F);
    const auto lines = PaddockWrap(type, text, std::max(width - padding_x * 2.0F - 2.0F, 1.0F));
    const float height = std::max(44.0F, type.line * static_cast<float>(lines.size()) + 20.0F + 2.0F);
    OlDisabled scope(disabled, 0.55F);
    const PaddockPress press = PaddockBeginPress(label, {width, height}, 0.14F, 0.14F);
    if (disabled && reason != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", reason);
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const PaddockRadii radii = PaddockRound(8.0F);
    const float hover = disabled ? 0.0F : press.hover;
    unsigned ink = kSetText;
    if (tone == SettingsButtonTone::Selected) {
        PaddockFill(draw, press.min, press.max, radii,
                    PaddockApply(PaddockMix(PaddockRgb(0xFFD078), PaddockRgb(0xFFDC99), hover)));
        ink = 0x053373;
    } else {
        const ImU32 fill = tone == SettingsButtonTone::Plain
            ? PaddockMix(PaddockRgb(0x173A4E), PaddockRgb(0x234E65), hover)
            : PaddockMix(PaddockRgb(0x173A4E, 0U), PaddockRgb(0x234E65), hover);
        PaddockFill(draw, press.min, press.max, radii, PaddockApply(fill));
        const unsigned rest = tone == SettingsButtonTone::Plain ? 0x476373U : 0x365361U;
        const ImU32 edge = PaddockApply(PaddockMix(PaddockRgb(rest), PaddockRgb(0xB9CDD7), hover));
        if (tone == SettingsButtonTone::Unbound) {
            // border-style: dashed
            const float x0 = press.min.x + 8.0F;
            const float x1 = press.max.x - 8.0F;
            PaddockDashes(draw, {x0, press.min.y}, x1 - x0, edge, 1.0F);
            PaddockDashes(draw, {x0, press.max.y - 1.0F}, x1 - x0, edge, 1.0F);
            for (float y = press.min.y + 8.0F; y + 3.0F <= press.max.y - 8.0F; y += 5.0F) {
                draw->AddRectFilled({press.min.x, y}, {press.min.x + 1.0F, y + 3.0F}, edge);
                draw->AddRectFilled({press.max.x - 1.0F, y}, {press.max.x, y + 3.0F}, edge);
            }
            ink = kSetMuted;
        } else {
            PaddockStroke(draw, press.min, press.max, radii, edge, 1.0F);
        }
    }
    PaddockTextStyle style;
    style.colour = PaddockCol(ink);
    style.centre = true;
    const float text_height = type.line * static_cast<float>(lines.size());
    PaddockDrawLines(draw, type, {press.min.x + padding_x + 1.0F,
                                  std::round(press.min.y + (height - text_height) * 0.5F)},
                     width - padding_x * 2.0F - 2.0F, lines, style);
    PaddockEndPress(press, 8.0F, 0.96F, false);
    if (press.focused) {
        draw->AddRect({press.min.x - 4.5F, press.min.y - 4.5F}, {press.max.x + 4.5F, press.max.y + 4.5F},
                      PaddockCol(0xFFD078), 12.5F, 0, 3.0F);
    }
    return press.pressed && !disabled;
}

// ------------------------------------------------------------ secondary nav

// Sections within a page (.secondary-nav): a dark track of quiet tabs; the
// current one is lifted and underlined in amber. Returns the picked index.
int SettingsSecondaryNav(const char* id, const std::vector<const char*>& labels, int current,
                         float max_width) {
    const PaddockType type = PaddockReading(14.0F, true, 1.35F);
    float total = 8.0F + 4.0F * static_cast<float>(labels.size() - 1U);
    std::vector<float> widths;
    for (const char* label : labels) {
        widths.push_back(std::ceil(PaddockMeasure(type, label) + 32.0F));
        total += widths.back();
    }
    total = std::min(total, max_width);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 track_end{at.x + total, at.y + 44.0F + 8.0F};
    PaddockFill(draw, at, track_end, PaddockRound(12.0F), PaddockCol(0x081923));
    // box-shadow: inset 0 1px 3px #00000052, 0 1px 0 #ffffff0f
    draw->PushClipRect(at, {track_end.x, at.y + 3.0F}, true);
    PaddockStroke(draw, at, track_end, PaddockRound(12.0F), PaddockCol(0x000000, 82U), 2.0F);
    draw->PopClipRect();
    draw->AddRectFilled({at.x + 10.0F, track_end.y}, {track_end.x - 10.0F, track_end.y + 1.0F},
                        PaddockCol(0xFFFFFF, 15U));
    int picked = -1;
    ImGui::PushID(id);
    float x = at.x + 4.0F;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        ImGui::SetCursorScreenPos({x, at.y + 4.0F});
        const bool selected = static_cast<int>(index) == current;
        ImGui::PushID(static_cast<int>(index));
        const PaddockPress press = PaddockBeginPress("##tab", {widths[index], 44.0F});
        ImGui::PopID();
        if (press.pressed) picked = static_cast<int>(index);
        const float lit = std::max(press.hover, press.focused ? 1.0F : 0.0F);
        const ImU32 fill = selected ? PaddockMix(PaddockRgb(0x1B4053), PaddockRgb(0x234C60), lit)
                                    : PaddockMix(PaddockRgb(0x112E3E, 0U), PaddockRgb(0x112E3E), lit);
        PaddockFill(draw, press.min, press.max, PaddockRound(8.0F), PaddockApply(fill));
        if (selected) {
            draw->PushClipRect(press.min, {press.max.x, press.min.y + 1.0F}, true);
            PaddockFill(draw, press.min, press.max, PaddockRound(8.0F), PaddockCol(0xFFFFFF, 23U));
            draw->PopClipRect();
            const float centre = std::round((press.min.x + press.max.x) * 0.5F);
            PaddockFill(draw, {centre - 7.0F, press.max.y - 6.0F}, {centre + 7.0F, press.max.y - 4.0F},
                        PaddockRound(1.0F), PaddockCol(kSetWarm));
        }
        const ImU32 ink = selected ? PaddockCol(0xFFF0C2)
                                   : PaddockApply(PaddockMix(PaddockRgb(kSetMuted), PaddockRgb(0xFFF0C2), lit));
        const float text_width = PaddockMeasure(type, labels[index]);
        PaddockDrawRun(draw, type, {std::round(press.min.x + (widths[index] - text_width) * 0.5F),
                                    std::round(press.min.y + (44.0F - type.line) * 0.5F)},
                       ink, labels[index], labels[index] + std::strlen(labels[index]));
        PaddockEndPress(press, 8.0F, 0.96F, false);
        if (press.focused) {
            draw->AddRect({press.min.x + 1.0F, press.min.y + 1.0F}, {press.max.x - 1.0F, press.max.y - 1.0F},
                          PaddockCol(0xFFD078), 7.0F, 0, 2.0F);
        }
        x += widths[index] + 4.0F;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({total, 44.0F + 8.0F});
    return picked;
}

// ------------------------------------------------------------ section signs

// A row of painted section signs (.mods-section-nav), wrapping, with the
// heavy hairline under it. Returns the picked index, or -1.
int SettingsSectionSigns(const std::vector<const char*>& labels, int current, float width) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float tab_height = PaddockSectionTabHeight();
    float x = at.x;
    float y = at.y;
    int picked = -1;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const float tab = PaddockSectionTabWidth(labels[index]);
        if (x > at.x && x + tab > at.x + width) {
            x = at.x;
            y += tab_height + 10.0F;
        }
        ImGui::SetCursorScreenPos({x, y});
        if (PaddockSectionTab(labels[index], static_cast<int>(index) == current)) {
            picked = static_cast<int>(index);
        }
        x += tab + 10.0F;
    }
    const float bottom = y + tab_height + 16.0F;
    ImGui::GetWindowDrawList()->AddRectFilled({at.x, bottom}, {at.x + width, bottom + 2.0F}, PaddockCol(0x24495B));
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, bottom + 2.0F - at.y});
    return picked;
}

// ------------------------------------------------------------ dialogs

// The study's <dialog>: a dark panel with its heading over an amber rule,
// paragraphs, then the actions on the right. Open with ImGui::OpenPopup(name).
bool BeginSettingsDialog(const char* name, std::string_view heading, float max_width = 620.0F,
                         unsigned fill = 0x06336E) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(max_width, display.x - 32.0F);
    ImGui::SetNextWindowSize({width, 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({display.x * 0.5F, display.y * 0.5F}, ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {26.0F, 26.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0F);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PaddockRgb(fill));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(kSetBorder));
    const bool visible = ImGui::BeginPopupModal(name, nullptr,
                                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                    ImGuiWindowFlags_NoSavedSettings |
                                                    ImGuiWindowFlags_AlwaysAutoResize |
                                                    ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!visible) return false;
    g_paddock_modal_frame = ImGui::GetFrameCount();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    const float inner = width - 52.0F;
    PaddockText(PaddockSign(19.0F, 1.2F, 0.0F), PaddockRgb(kSetText), heading, inner);
    PaddockGap(13.0F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + inner, at.y + 1.0F}, PaddockCol(kSetWarm));
    PaddockGap(1.0F + 22.0F);
    return true;
}

inline float SettingsDialogInner() {
    return ImGui::GetWindowWidth() - 52.0F;
}

void EndSettingsDialog() {
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

// ------------------------------------------------------------ ROM drop-down

// The Game ROM field (.rom-select): the selected ROM, and a list of every ROM
// DKR-R has accepted plus "Add a ROM...". The pass look sits on Play's cream
// card; the compact one in Online's header.
RomSelectResult DrawRomSelect(const char* id, float width, bool compact, bool disabled, const char* reason) {
    if (g_launcher_rom == nullptr) return RomSelectResult::None;
    const PlayPageContext& rom = *g_launcher_rom;
    const std::string selected_key = rom.rom_ready ? RomPathKey(rom.selected_rom) : std::string{};
    int current = -1;
    for (std::size_t index = 0; index < rom.rom_catalog.size(); ++index) {
        if (rom.rom_catalog[index].key == selected_key) current = static_cast<int>(index);
    }
    const PaddockType strong = PaddockReading(16.0F, true, 1.25F);
    const PaddockType small_type = PaddockReading(13.0F, false, 1.3F);
    const float pad_x = compact ? 14.0F : 16.0F;
    const float pad_y = compact ? 7.0F : 9.0F;
    const float edge = compact ? 1.0F : 2.0F;
    const float height = std::max(compact ? 52.0F : 60.0F,
                                  edge * 2.0F + pad_y * 2.0F + strong.line + 2.0F + small_type.line);
    ImGui::PushID(id);
    const char* popup = "##rom-choices";
    const bool open = ImGui::IsPopupOpen(popup);
    if (disabled) ImGui::BeginDisabled();
    const PaddockPress press = PaddockBeginPress("##rom-field", {width, height});
    if (disabled) ImGui::EndDisabled();
    if (disabled && reason != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", reason);
    }
    if (press.pressed && !disabled) ImGui::OpenPopup(popup);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int first = draw->VtxBuffer.Size;
    const float hover = disabled ? 0.0F : press.hover;
    const PaddockRadii radii = PaddockRound(compact ? 8.0F : 10.0F);
    unsigned ink = 0x091B24;
    unsigned soft = 0x45595F;
    if (compact) {
        PaddockFill(draw, press.min, press.max, radii,
                    PaddockMix(PaddockRgb(0x061D2C), PaddockRgb(0x0A2A3D), hover));
        PaddockStroke(draw, press.min, press.max, radii,
                      open ? PaddockRgb(kSetFocus) : PaddockMix(PaddockRgb(0x2F6A86), PaddockRgb(0x5A97B1), hover), 1.0F);
        ink = 0xFFFFFF;
        soft = 0xB3C8D3;
    } else {
        // box-shadow: 0 3px 0 rgb(9 27 36 / .22)
        PaddockFill(draw, {press.min.x, press.min.y + 3.0F}, {press.max.x, press.max.y + 3.0F}, radii,
                    PaddockRgb(0x091B24, 56U));
        PaddockFill(draw, press.min, press.max, radii,
                    PaddockMix(PaddockRgb(0xFFFFFF), PaddockRgb(0xF4FBFB), hover));
        PaddockStroke(draw, press.min, press.max, radii,
                      PaddockMix(PaddockRgb(0x091B24), PaddockRgb(0x0B5963), hover), 2.0F);
    }
    const float text_width = width - edge * 2.0F - pad_x * 2.0F - 14.0F - 12.0F;
    const std::string title = current >= 0 ? rom.rom_catalog[static_cast<std::size_t>(current)].label
                                           : std::string("Choose a ROM");
    const std::string detail = current >= 0
        ? PathUtf8(rom.rom_catalog[static_cast<std::size_t>(current)].path.filename())
        : std::to_string(rom.rom_catalog.size()) + " in your list";
    const float text_height = strong.line + 2.0F + small_type.line;
    const float top = std::round(press.min.y + (height - text_height) * 0.5F);
    const std::string shown_title = PaddockEllipsize(strong, title, text_width);
    const std::string shown_detail = PaddockEllipsize(small_type, detail, text_width);
    PaddockDrawRun(draw, strong, {press.min.x + edge + pad_x, top}, PaddockRgb(ink), shown_title.data(),
                   shown_title.data() + shown_title.size());
    PaddockDrawRun(draw, small_type, {press.min.x + edge + pad_x, top + strong.line + 2.0F}, PaddockRgb(soft),
                   shown_detail.data(), shown_detail.data() + shown_detail.size());
    if (!(compact && disabled)) {
        PaddockChevron(draw, {press.max.x - edge - pad_x - 2.0F - 5.0F, press.min.y + height * 0.5F - 3.0F}, 45.0F,
                       10.0F, PaddockRgb(ink), 2.5F);
    }
    OlFade(draw, first, disabled ? 0.6F : 1.0F);
    PaddockEndPress(press, compact ? 8.0F : 10.0F, 0.98F, false);
    if (press.focused) {
        draw->AddRect({press.min.x - 4.5F, press.min.y - 4.5F}, {press.max.x + 4.5F, press.max.y + 4.5F},
                      PaddockRgb(compact ? kSetFocus : 0x091B24), (compact ? 8.0F : 10.0F) + 4.5F, 0, 3.0F);
    }
    std::vector<SettingsChoice> choices;
    for (const RomCatalogEntry& entry : rom.rom_catalog) choices.push_back({entry.label, PathUtf8(entry.path), false});
    choices.push_back(SettingsChoice::Separator());
    choices.push_back(SettingsChoice::Action("Add a ROM\xE2\x80\xA6"));
    const int picked = DrawSettingsPicker(popup, press.min, press.max, current, choices, 320.0F);
    ImGui::PopID();
    if (picked < 0) return RomSelectResult::None;
    if (picked >= static_cast<int>(rom.rom_catalog.size())) {
        OpenRomBrowser(rom.selected_rom);
        return RomSelectResult::Add;
    }
    if (picked == current) return RomSelectResult::None;
    const RomCatalogEntry entry = rom.rom_catalog[static_cast<std::size_t>(picked)];
    rom.rom_ready = SelectCatalogRom(entry, rom.selected_rom, rom.rom_catalog, rom.rom_status);
    return RomSelectResult::Switched;
}
