// Included by runtime_ui.cpp inside its UI namespace, after runtime_play_ui.inl.
// Presentation only: every session action goes through DirectSession and every
// friend action through FriendService, exactly as before.
//
// DKR-R ONLINE in the look of tools/launcher-html (pages/online.js,
// styles/online.css): section tabs, a main column for hosting, joining and the
// lobby, and a side column for your profile and the guide. Sizes are CSS px.

// ------------------------------------------------------------ palette

constexpr unsigned kOlInk = 0xEAF3F5;
constexpr unsigned kOlSoft = 0xB3C8D3;
constexpr unsigned kOlCream = 0xFFF0C2;
constexpr unsigned kOlAmber = 0xFFC453;
constexpr unsigned kOlGo = 0x54C9AD;
constexpr unsigned kOlGoText = 0x83E4C4;
constexpr unsigned kOlStop = 0xE8452F;
constexpr unsigned kOlPanel = 0x0A2638;
constexpr unsigned kOlRing = 0x1F4C63;
constexpr unsigned kOlError = 0xFFB3A1;

inline PaddockType OlRead(float px, bool semibold = false, float line = 1.5F) {
    return PaddockReading(px, semibold, line);
}

// Headings and signs use the launcher lettering with a little tracking.
inline PaddockType OlSignType(float px, float line = 1.5F, float tracking = 0.015F) {
    return PaddockSign(px, line, tracking);
}

inline float OlText(std::string_view text, float px, unsigned colour, float width,
                    bool semibold = false, float line = 1.5F) {
    return PaddockText(OlRead(px, semibold, line), PaddockRgb(colour), text, width);
}

inline float OlHeading(std::string_view text, float px, unsigned colour, float width,
                       float line = 1.5F, bool shadow = false) {
    return PaddockText(OlSignType(px, line), PaddockRgb(colour), text, width, true,
                       shadow ? PaddockRgb(0x031623) : 0U);
}

// Rotates what was drawn since `first_vertex` about `centre`.
inline void OlRotate(ImDrawList* draw, int first_vertex, ImVec2 centre, float degrees) {
    const float radians = degrees * 3.14159265F / 180.0F;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    for (int index = first_vertex; index < draw->VtxBuffer.Size; ++index) {
        ImVec2& p = draw->VtxBuffer[index].pos;
        const float x = p.x - centre.x;
        const float y = p.y - centre.y;
        p = {centre.x + x * c - y * s, centre.y + x * s + y * c};
    }
}

// Multiplies the alpha of what was drawn since `first_vertex`.
inline void OlFade(ImDrawList* draw, int first_vertex, float alpha) {
    if (alpha >= 1.0F) return;
    for (int index = first_vertex; index < draw->VtxBuffer.Size; ++index) {
        ImDrawVert& v = draw->VtxBuffer[index];
        const ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 0xFFU;
        v.col = (v.col & ~IM_COL32_A_MASK) |
                (static_cast<ImU32>(std::lround(a * std::max(alpha, 0.0F))) << IM_COL32_A_SHIFT);
    }
}

// .ol-btn:disabled and friends dim to .45.
class OlDisabled {
public:
    explicit OlDisabled(bool disabled, float alpha = 0.45F) : disabled_(disabled) {
        if (!disabled_) return;
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, alpha);
        ImGui::BeginDisabled();
    }
    ~OlDisabled() {
        if (!disabled_) return;
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    }
    OlDisabled(const OlDisabled&) = delete;
    OlDisabled& operator=(const OlDisabled&) = delete;

private:
    bool disabled_;
};

inline bool OlItemDisabled() {
    return (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
}

// ------------------------------------------------------------ toast

// The launcher study's #notification: one message at a time, bottom centre.
struct OlToast {
    std::string text;
    double at = -100.0;
    float seconds = 3.2F;
};
OlToast g_ol_toast;

void OlNotify(std::string text, float seconds = 0.0F) {
    if (text.empty()) return;
    if (seconds <= 0.0F) {
        seconds = std::clamp(1.2F + 0.045F * static_cast<float>(text.size()), 3.2F, 9.0F);
    }
    g_ol_toast = {std::move(text), PaddockClock(), seconds};
}

void DrawOlToast() {
    if (g_ol_toast.text.empty()) return;
    if (PaddockClock() - g_ol_toast.at > g_ol_toast.seconds) {
        g_ol_toast.text.clear();
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const PaddockType type = PaddockSign(19.0F, 1.3F, 0.0F);
    const float maximum = std::min(700.0F, display.x * 0.9F) - 48.0F - 4.0F;
    const auto lines = PaddockWrap(type, g_ol_toast.text, maximum);
    float text_width = 0.0F;
    for (const auto& line : lines) text_width = std::max(text_width, line.width);
    const ImVec2 size{std::ceil(text_width) + 48.0F + 4.0F,
                      type.line * static_cast<float>(lines.size()) + 36.0F + 4.0F};
    const ImVec2 a{std::round((display.x - size.x) * 0.5F), display.y - 24.0F - size.y};
    const ImVec2 b{a.x + size.x, a.y + size.y};
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    PaddockPanelStyle panel;
    panel.radii = PaddockRound(12.0F);
    panel.fill = PaddockRgb(0x0C2734);
    panel.border = PaddockRgb(0xFFAB14);
    panel.border_width = 2.0F;
    PaddockFill(draw, a, b, panel.radii, panel.fill);
    PaddockStroke(draw, a, b, panel.radii, panel.border, 2.0F);
    PaddockTextStyle style;
    style.colour = PaddockRgb(0xFFF6DA);
    PaddockDrawLines(draw, type, {a.x + 26.0F, a.y + 20.0F}, text_width, lines, style);
}

// ------------------------------------------------------------ buttons

enum class OlTone { Plain, Strong, Go, Danger, Icon, PlateStrong, PlatePlain };

struct OlToneLook {
    unsigned fill, fill_hover, text, text_hover;
    unsigned fill_alpha = 255U, fill_hover_alpha = 255U;
    unsigned ring = 0U;
    bool has_ring = false;
};

inline OlToneLook OlLook(OlTone tone) {
    switch (tone) {
    case OlTone::Strong: return {kOlAmber, 0xFFD683, 0x2A1D05, 0x2A1D05};
    case OlTone::Go: return {0x1AC2A3, 0x3DDBBD, 0x03261F, 0x03261F};
    case OlTone::Danger:
        return {0x3A1812, 0x3A1812, kOlError, 0xFFD2C6, 0U, 255U, 0x8A3A2C, true};
    case OlTone::PlateStrong: return {0xC8361F, 0xE04A30, 0xFFFFFF, 0xFFFFFF};
    case OlTone::PlatePlain: return {0x10222C, 0x22404F, kOlCream, kOlCream};
    case OlTone::Plain:
    case OlTone::Icon:
    default:
        return {0x16384C, 0x1F4A62, kOlInk, kOlInk, 255U, 255U, 0x3C6478, true};
    }
}

inline float OlButtonWidth(const char* label, OlTone tone = OlTone::Plain) {
    if (tone == OlTone::Icon) return 40.0F;
    return std::ceil(PaddockMeasure(OlRead(14.0F, true, 1.2F), label, PaddockLabelEnd(label)) + 28.0F);
}

// .ol-btn: quiet buttons for everything but the one main action.
bool OlButton(const char* label, OlTone tone = OlTone::Plain, float width = 0.0F,
              const char* tooltip = nullptr, float height = 40.0F) {
    if (width <= 0.0F) width = OlButtonWidth(label, tone);
    const PaddockPress press = PaddockBeginPress(label, {width, height});
    if (tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    const OlToneLook look = OlLook(tone);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const PaddockRadii radii = PaddockRound(8.0F);
    PaddockFill(draw, press.min, press.max, radii,
                PaddockApply(PaddockMix(PaddockRgb(look.fill, look.fill_alpha),
                                        PaddockRgb(look.fill_hover, look.fill_hover_alpha),
                                        press.hover)));
    if (look.has_ring) PaddockStroke(draw, press.min, press.max, radii, PaddockCol(look.ring), 1.0F);
    const ImU32 text = PaddockApply(PaddockMix(PaddockRgb(look.text), PaddockRgb(look.text_hover),
                                               press.hover));
    const char* end = PaddockLabelEnd(label);
    if (tone == OlTone::Icon) {
        // Segoe's x sits low in its line; lift it back optically.
        const PaddockType type = OlRead(22.0F, false, 1.2F);
        const float glyph = PaddockMeasure(type, label, end);
        PaddockDrawRun(draw, type,
                       {std::round(press.min.x + (width - glyph) * 0.5F),
                        std::round(press.min.y + (height - type.line) * 0.5F - 2.5F)},
                       text, label, end);
    } else {
        const PaddockType type = OlRead(14.0F, true, 1.2F);
        const float glyph = PaddockMeasure(type, label, end);
        PaddockDrawRun(draw, type,
                       {std::round(press.min.x + (width - glyph) * 0.5F),
                        std::round(press.min.y + (height - type.line) * 0.5F)},
                       text, label, end);
    }
    PaddockEndPress(press, 8.0F);
    return press.pressed;
}

// The one main action of a card (.ol-cta): a race button with depth.
enum class OlCtaColour { Blue, Green, Red };

bool OlCta(const char* label, OlCtaColour colour, float width, bool disabled = false) {
    RaceButtonLook look;
    look.fill = colour == OlCtaColour::Green ? 0x1AC2A3U
              : colour == OlCtaColour::Red ? 0xE82E21U : 0x0A6EA1U;
    look.font_px = 22.0F;
    look.drop = 3.0F;
    look.drop_colour = 0x031623;
    look.highlight_alpha = 41U;
    const OlDisabled scope(disabled, 0.6F);
    return PaddockRaceButton(label, {width, 52.0F}, look);
}

// A launcher-lettered section tab (.ol-section-nav .ol-btn).
float OlSectionTabWidth(const char* label, float px) {
    return std::ceil(PaddockMeasure(OlSignType(px, 1.4F), label, PaddockLabelEnd(label)) +
                     (px < 19.0F ? 24.0F : 32.0F) + 4.0F);
}

bool OlSectionTab(const char* label, bool current, float width, float px) {
    const PaddockType type = OlSignType(px, 1.4F);
    const char* end = PaddockLabelEnd(label);
    const float height = std::ceil(std::max(46.0F, type.line + 20.0F + 4.0F));
    const PaddockPress press = PaddockBeginPress(label, {width, height});
    const float on = PaddockEase(PaddockTween(PaddockKey("current", press.id), current, 0.15F, 0.15F));
    const auto state = [&](unsigned rest, unsigned hover, unsigned chosen) {
        return PaddockMix(PaddockMix(PaddockRgb(rest), PaddockRgb(hover), press.hover),
                          PaddockRgb(chosen), on);
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
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockPanel(draw, press.min, press.max, panel);
    const float text_width = PaddockMeasure(type, label, end);
    const ImVec2 at{std::round(press.min.x + (width - text_width) * 0.5F),
                    std::round(press.min.y + (height - type.line) * 0.5F)};
    PaddockDrawRun(draw, type, {at.x, at.y + 2.0F}, PaddockCol(0x031623), label, end);
    PaddockDrawRun(draw, type, at, PaddockApply(state(0xFFF2C8, 0xFFF7DC, 0xFFF5D5)), label, end);
    PaddockEndPress(press, 12.0F);
    return press.pressed;
}

// An underlined amber link (.ol-link).
bool OlLink(const char* label) {
    const PaddockType type = OlRead(15.0F, true, 1.5F);
    const char* end = PaddockLabelEnd(label);
    const float width = std::ceil(PaddockMeasure(type, label, end));
    const PaddockPress press = PaddockBeginPress(label, {width, 40.0F});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 colour = PaddockApply(PaddockMix(PaddockRgb(kOlAmber), PaddockRgb(0xFFE7B0), press.hover));
    const float top = std::round(press.min.y + (40.0F - type.line) * 0.5F);
    PaddockDrawRun(draw, type, {press.min.x, top}, colour, label, end);
    const float baseline = top + (type.line - type.content) * 0.5F + type.ascent;
    draw->AddRectFilled({press.min.x, std::round(baseline + 3.0F)},
                        {press.max.x, std::round(baseline + 4.0F)}, colour);
    PaddockEndPress(press, 3.0F, 1.0F);
    return press.pressed;
}

// ------------------------------------------------------------ fields

inline float OlFieldLabel(const char* label, float width) {
    if (label == nullptr || *label == '\0') return 0.0F;
    const float height = OlText(label, 14.0F, kOlSoft, width, true, 1.4F);
    PaddockGap(6.0F);
    return height + 6.0F;
}

// A described setting (.ol-switch): the launcher's checkbox, its title and
// its explanation.
bool OlSwitchRow(const char* id, const char* title, const char* description, bool* value,
                 float width, bool rule_above, bool disabled = false) {
    const PaddockType strong = OlRead(15.0F, true, 1.5F);
    const PaddockType caption = OlRead(13.0F, false, 1.4F);
    const float text_width = std::max(width - 24.0F - 12.0F, 1.0F);
    const float text_height = PaddockTextHeight(strong, title, text_width) +
                              PaddockTextHeight(caption, description, text_width);
    const float content = std::max(44.0F, text_height);
    const float height = 12.0F + content + 12.0F;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (rule_above) {
        draw->AddRectFilled(origin, {origin.x + width, origin.y + 1.0F}, PaddockCol(0xFFFFFF, 18U));
    }
    if (disabled) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    const PaddockPress press = PaddockBeginPress(id, {width, height});
    if (disabled) ImGui::PopItemFlag();
    bool changed = false;
    if (press.pressed && !disabled) {
        *value = !*value;
        changed = true;
    }
    const int first = draw->VtxBuffer.Size;
    // align-items: center, so the box sits mid-row beside the text.
    const ImVec2 box{origin.x, std::round(origin.y + 12.0F + (content - 24.0F) * 0.5F)};
    PaddockCheckBox(draw, box, *value);
    const float text_top = std::round(origin.y + 12.0F + (content - text_height) * 0.5F);
    PaddockTextStyle title_style;
    title_style.colour = PaddockCol(0xFFFFFF);
    const float title_height = PaddockTextAt(draw, strong, {box.x + 36.0F, text_top},
                                             text_width, title, title_style);
    PaddockTextStyle small_style;
    small_style.colour = PaddockCol(kOlSoft);
    PaddockTextAt(draw, caption, {box.x + 36.0F, text_top + title_height},
                  text_width, description, small_style);
    OlFade(draw, first, disabled ? 0.6F : 1.0F);
    if (press.focused) PaddockCheckFocus(draw, box);
    return changed;
}

// A labelled drop-down (.ol-field select).
bool OlSelect(const char* id, const char* label, int* value, const std::vector<std::string>& items,
              float width, bool disabled = false) {
    OlFieldLabel(label, width);
    *value = std::clamp(*value, 0, std::max(static_cast<int>(items.size()) - 1, 0));
    const PaddockType type = OlRead(16.0F, true, 1.3F);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::PushID(id);
    if (disabled) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    const PaddockPress press = PaddockBeginPress("##select", {width, 44.0F});
    if (disabled) ImGui::PopItemFlag();
    const char* popup = "##select-popup";
    if (press.pressed && !disabled) ImGui::OpenPopup(popup);
    const bool open = ImGui::IsPopupOpen(popup);
    const int first = draw->VtxBuffer.Size;
    const PaddockRadii radii = PaddockRound(8.0F);
    PaddockFill(draw, press.min, press.max, radii, PaddockCol(0x061D2C));
    PaddockStroke(draw, press.min, press.max, radii,
                  open ? PaddockCol(0xFFD11F)
                       : PaddockApply(PaddockMix(PaddockRgb(0x2F6A86), PaddockRgb(0x5A97B1), press.hover)),
                  1.0F);
    const std::string shown = PaddockEllipsize(type, items.empty() ? "" : items[*value], width - 12.0F - 36.0F);
    PaddockDrawRun(draw, type, {press.min.x + 13.0F, std::round(press.min.y + (44.0F - type.line) * 0.5F)},
                   PaddockCol(0xFFFFFF), shown.data(), shown.data() + shown.size());
    PaddockChevron(draw, {press.max.x - 17.0F, press.min.y + 20.0F}, 45.0F, 7.0F,
                   PaddockCol(0xFFFFFF), 1.6F);
    OlFade(draw, first, disabled ? 0.5F : 1.0F);
    PaddockEndPress(press, 8.0F, 1.0F);
    bool changed = false;
    constexpr float row = 34.0F;
    ImGui::SetNextWindowPos({press.min.x, press.max.y + 4.0F});
    ImGui::SetNextWindowSizeConstraints(
        {width, 0.0F}, {std::max(width, 1.0F),
                        std::min(row * static_cast<float>(items.size()) + 10.0F, 360.0F)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {5.0F, 5.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PaddockRgb(0x061D2C));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(0x2F6A86));
    if (ImGui::BeginPopup(popup, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0.0F, 0.5F});
        ImGui::PushStyleColor(ImGuiCol_Header, PaddockRgb(0x0D3550));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, PaddockRgb(0x16384C));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, PaddockRgb(0x1F4A62));
        ImGui::PushStyleColor(ImGuiCol_Text, PaddockRgb(0xFFFFFF));
        ImGui::PushFont(type.font);
        for (int index = 0; index < static_cast<int>(items.size()); ++index) {
            ImGui::PushID(index);
            const bool selected = index == *value;
            const std::string row_label = "  " + items[index];
            if (ImGui::Selectable(row_label.c_str(), selected, 0, {width - 10.0F, row})) {
                changed = *value != index;
                *value = index;
            }
            if (selected && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    ImGui::PopID();
    return changed;
}

enum class OlInputResult { None, Edited, Committed, Entered, Gamepad };

struct OlInputOptions {
    const char* hint = "";
    bool mono = false;
    bool disabled = false;
    ImGuiInputTextFlags flags = 0;
    ImGuiInputTextCallback callback = nullptr;
};

// A text field (.online-page input[type=text]). A controller cannot type into
// it, so a gamepad activation is handed back for an on-screen keyboard.
OlInputResult OlInput(const char* id, const char* label, char* buffer, std::size_t capacity,
                      float width, const OlInputOptions& options = {}) {
    OlFieldLabel(label, width);
    const PaddockType type = options.mono ? PaddockMono(16.0F, true, 1.3F) : OlRead(16.0F, true, 1.3F);
    const float font_size = type.font->FontSize;
    ImGui::PushFont(type.font);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12.0F, std::max((44.0F - font_size) * 0.5F, 0.0F)});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, PaddockRgb(0x061D2C));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PaddockRgb(0x061D2C));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, PaddockRgb(0x061D2C));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(0x2F6A86));
    ImGui::PushStyleColor(ImGuiCol_Text, PaddockRgb(0xFFFFFF));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, PaddockRgb(0x6F8E9C));
    ImGui::PushStyleColor(ImGuiCol_NavHighlight, PaddockRgb(0, 0U));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemWidth(width);
    if (options.disabled) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int first = draw->VtxBuffer.Size;
    const bool entered = ImGui::InputTextWithHint(
        id, options.hint, buffer, capacity,
        options.flags | ImGuiInputTextFlags_EnterReturnsTrue |
            (options.callback != nullptr ? ImGuiInputTextFlags_CallbackCharFilter : 0),
        options.callback);
    if (options.disabled) ImGui::PopItemFlag();
    const bool edited = ImGui::IsItemEdited();
    const bool committed = ImGui::IsItemDeactivatedAfterEdit();
    const bool activated = ImGui::IsItemActivated();
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    const bool focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
    OlFade(draw, first, options.disabled ? 0.5F : 1.0F);
    if (hovered && !active && !options.disabled) {
        PaddockStroke(draw, origin, max, PaddockRound(8.0F), PaddockCol(0x5A97B1), 1.0F);
    }
    if (activated && GImGui->ActiveIdSource == ImGuiInputSource_Gamepad) {
        ImGui::ClearActiveID();
        return OlInputResult::Gamepad;
    }
    if (active || focused) {
        draw->AddRect({origin.x - 4.5F, origin.y - 4.5F}, {max.x + 4.5F, max.y + 4.5F},
                      PaddockCol(0xFFD11F), 12.5F, 0, 3.0F);
    }
    if (entered) return OlInputResult::Entered;
    if (committed) return OlInputResult::Committed;
    return edited ? OlInputResult::Edited : OlInputResult::None;
}

// A labelled range with its value on the right (.ol-field input[type=range]).
bool OlRange(const char* id, const char* label, int* value, int minimum, int maximum,
             float width, bool disabled = false) {
    const PaddockType type = OlRead(14.0F, true, 1.4F);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const std::string number = std::to_string(*value);
    PaddockTextStyle label_style;
    label_style.colour = PaddockCol(kOlSoft);
    const float number_width = PaddockMeasure(type, number);
    PaddockTextAt(draw, type, origin, width - number_width - 8.0F, label, label_style);
    PaddockDrawRun(draw, type, {origin.x + width - number_width, origin.y}, PaddockCol(kOlAmber),
                   number.data(), number.data() + number.size());
    ImGui::Dummy({width, type.line + 6.0F});
    ImGui::SetCursorScreenPos({origin.x, origin.y + type.line + 6.0F});
    ImGui::PushID(id);
    if (disabled) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    const PaddockPress press = PaddockBeginPress("##range", {width, 40.0F});
    if (disabled) ImGui::PopItemFlag();
    bool changed = false;
    const float left = press.min.x + 8.0F;
    const float span = std::max(width - 16.0F, 1.0F);
    const auto set = [&](int next) {
        next = std::clamp(next, minimum, maximum);
        if (next != *value) {
            *value = next;
            changed = true;
        }
    };
    if (!disabled && press.held) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - left) / span, 0.0F, 1.0F);
        set(minimum + static_cast<int>(std::lround(t * static_cast<float>(maximum - minimum))));
    }
    if (!disabled && ImGui::IsItemFocused()) {
        const int step = std::max((maximum - minimum) / 100, 1);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft)) {
            set(*value - step);
            ImGui::NavMoveRequestCancel();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight)) {
            set(*value + step);
            ImGui::NavMoveRequestCancel();
        }
    }
    const float t = maximum > minimum
        ? static_cast<float>(*value - minimum) / static_cast<float>(maximum - minimum) : 0.0F;
    const float y = press.min.y + 20.0F;
    const float thumb = left + span * t;
    const int first = draw->VtxBuffer.Size;
    const unsigned filled = disabled ? 0x8A9499U : kOlGo;
    PaddockFill(draw, {left - 6.0F, y - 2.5F}, {left + span + 6.0F, y + 2.5F}, PaddockRound(2.5F),
                PaddockCol(0x55636A));
    PaddockFill(draw, {left - 6.0F, y - 2.5F}, {thumb, y + 2.5F}, PaddockRound(2.5F), PaddockCol(filled));
    draw->AddCircleFilled({thumb, y}, 8.0F, PaddockCol(filled), 24);
    OlFade(draw, first, disabled ? 0.5F : 1.0F);
    if (press.focused) PaddockFocusRing(draw, {thumb - 8.0F, y - 8.0F}, {thumb + 8.0F, y + 8.0F}, 8.0F);
    ImGui::PopID();
    return changed;
}

// ------------------------------------------------------------ caption pieces

inline ImU32 OlHsl(float hue, float saturation, float lightness) {
    const float q = lightness < 0.5F ? lightness * (1.0F + saturation)
                                     : lightness + saturation - lightness * saturation;
    const float p = 2.0F * lightness - q;
    const auto channel = [&](float t) {
        t = t - std::floor(t);
        float v = p;
        if (t < 1.0F / 6.0F) v = p + (q - p) * 6.0F * t;
        else if (t < 0.5F) v = q;
        else if (t < 2.0F / 3.0F) v = p + (q - p) * (2.0F / 3.0F - t) * 6.0F;
        return static_cast<int>(std::lround(v * 255.0F));
    };
    const float h = hue / 360.0F;
    return IM_COL32(channel(h + 1.0F / 3.0F), channel(h), channel(h - 1.0F / 3.0F), 255);
}

// A racer's initial on a colour picked from their name (.ol-avatar).
void DrawOlAvatar(ImDrawList* draw, ImVec2 at, float size, std::string_view name, bool dimmed = false) {
    int hue = 0;
    for (const unsigned char character : name) hue = (hue * 31 + character) % 360;
    ImU32 fill = OlHsl(static_cast<float>(hue), 0.5F, 0.36F);
    if (dimmed) {
        // filter: saturate(.25); opacity: .7
        const float r = static_cast<float>((fill >> IM_COL32_R_SHIFT) & 0xFFU);
        const float g = static_cast<float>((fill >> IM_COL32_G_SHIFT) & 0xFFU);
        const float b = static_cast<float>((fill >> IM_COL32_B_SHIFT) & 0xFFU);
        const float grey = 0.2126F * r + 0.7152F * g + 0.0722F * b;
        fill = IM_COL32(static_cast<int>(grey + (r - grey) * 0.25F),
                        static_cast<int>(grey + (g - grey) * 0.25F),
                        static_cast<int>(grey + (b - grey) * 0.25F), 255);
    }
    const int first = draw->VtxBuffer.Size;
    const float radius = size * 0.5F;
    const ImVec2 centre{at.x + radius, at.y + radius};
    draw->AddCircleFilled(centre, radius, PaddockApply(fill), 32);
    draw->AddCircle(centre, radius - 1.0F, PaddockCol(0xFFFFFF, 46U), 32, 2.0F);
    std::string initial = "?";
    if (!name.empty()) {
        unsigned int character = 0U;
        const int bytes = ImTextCharFromUtf8(&character, name.data(), name.data() + name.size());
        initial.assign(name.data(), static_cast<std::size_t>(std::max(bytes, 1)));
        if (initial.size() == 1U) {
            initial[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(initial[0])));
        }
    }
    const PaddockType type = PaddockSign(size >= 50.0F ? 28.0F : 19.0F, 1.0F, 0.0F);
    const float width = PaddockMeasure(type, initial);
    const ImVec2 text{std::round(centre.x - width * 0.5F),
                      std::round(centre.y - type.line * 0.5F)};
    PaddockDrawRun(draw, type, {text.x, text.y + 1.0F}, PaddockCol(0x000000, 77U),
                   initial.data(), initial.data() + initial.size());
    PaddockDrawRun(draw, type, text, PaddockCol(0xFFFFFF), initial.data(),
                   initial.data() + initial.size());
    if (dimmed) OlFade(draw, first, 0.7F);
}

// A status pill (.ol-soft-tag). Returns its size.
ImVec2 DrawOlSoftTag(ImDrawList* draw, ImVec2 at, std::string_view text) {
    const PaddockType type = OlRead(12.0F, true, 1.5F);
    const ImVec2 size{std::ceil(PaddockMeasure(type, text) + 16.0F), type.line + 8.0F};
    PaddockFill(draw, at, {at.x + size.x, at.y + size.y}, PaddockRound(4.0F), PaddockCol(0xFFFFFF, 15U));
    PaddockDrawRun(draw, type, {at.x + 8.0F, at.y + 4.0F}, PaddockCol(kOlSoft), text.data(),
                   text.data() + text.size());
    return size;
}

// A HOST / YOU tag beside a name (.ol-tag).
ImVec2 DrawOlTag(ImDrawList* draw, ImVec2 at, std::string_view text, bool you) {
    const PaddockType type = OlRead(10.5F, true, 1.0F);
    const float tracking = 10.5F * 0.06F;
    float width = 0.0F;
    for (const char character : text) {
        width += PaddockMeasure(type, std::string_view(&character, 1U)) + tracking;
    }
    const ImVec2 size{std::ceil(width - tracking + 12.0F), 17.0F};
    PaddockFill(draw, at, {at.x + size.x, at.y + size.y}, PaddockRound(4.0F),
                PaddockCol(you ? 0x8FD3EAU : kOlAmber));
    float x = at.x + 6.0F;
    for (const char character : text) {
        PaddockDrawRun(draw, type, {x, at.y + 3.0F}, PaddockCol(you ? 0x06283AU : 0x2A1D05U),
                       &character, &character + 1);
        x += PaddockMeasure(type, std::string_view(&character, 1U)) + tracking;
    }
    return size;
}

// Starting lights in their housing (.ol-lights). 0 off, 1 waiting, 2 go, 3 lit red.
float DrawOlLights(ImDrawList* draw, ImVec2 at, const std::vector<int>& lights, float size = 18.0F,
                   float gap = 8.0F) {
    const float pad = gap;
    const float width = pad * 2.0F + size * lights.size() + gap * (lights.size() - 1U);
    const float height = size + pad * 2.0F;
    PaddockFill(draw, at, {at.x + width, at.y + height}, PaddockRound(height * 0.5F), PaddockCol(0x02101A));
    float x = at.x + pad + size * 0.5F;
    for (const int light : lights) {
        const ImVec2 centre{x, at.y + pad + size * 0.5F};
        unsigned colour = 0x27353D;
        unsigned glow = 0U;
        if (light == 1 || light == 3) {
            colour = kOlStop;
            glow = kOlStop;
        } else if (light == 2) {
            colour = 0x20DA81;
            glow = 0x20DA81;
        }
        if (glow != 0U) {
            for (int ring = 3; ring >= 1; --ring) {
                draw->AddCircleFilled(centre, size * 0.5F + ring * 2.5F,
                                      PaddockCol(glow, static_cast<unsigned>(28 / ring)), 24);
            }
        }
        draw->AddCircleFilled(centre, size * 0.5F, PaddockCol(colour), 24);
        if (glow == 0U) {
            draw->PushClipRect({centre.x - size, centre.y + size * 0.5F - 2.0F},
                               {centre.x + size, centre.y + size}, true);
            draw->AddCircleFilled(centre, size * 0.5F, PaddockCol(0x000000, 77U), 24);
            draw->PopClipRect();
        }
        x += size + gap;
    }
    return width;
}

// Three bars for a round trip (.ol-bars), then the milliseconds.
float DrawOlPing(ImDrawList* draw, ImVec2 at, float line, unsigned ping) {
    const int level = ping < 60U ? 3 : ping < 120U ? 2 : 1;
    const float base = at.y + std::round((line + 12.0F) * 0.5F);
    const std::array<float, 3> heights{{5.0F, 8.0F, 12.0F}};
    for (int bar = 0; bar < 3; ++bar) {
        unsigned colour = 0x3C5F70;
        if (level == 3) colour = kOlGo;
        else if (level == 2 && bar < 2) colour = kOlAmber;
        else if (level == 1 && bar == 0) colour = 0xFF7A5C;
        const float x = at.x + bar * 5.0F;
        PaddockFill(draw, {x, base - heights[static_cast<std::size_t>(bar)]}, {x + 3.0F, base},
                    PaddockRound(1.0F), PaddockCol(colour));
    }
    const std::string text = std::to_string(ping) + " ms";
    const PaddockType type = OlRead(13.0F, false, 1.5F);
    PaddockDrawRun(draw, type, {at.x + 13.0F + 6.0F, at.y}, PaddockCol(kOlSoft), text.data(),
                   text.data() + text.size());
    return 19.0F + PaddockMeasure(type, text);
}

struct OlTileLook {
    float height = 64.0F;
    float gap = 8.0F;
    float tile_width = 0.0F;     // 0 shares the width
    float font_px = 32.0F;
    bool plate = false;          // dark on white, inside the cream code plate
    bool hovered = false;
    bool error = false;
    int caret = -1;              // the tile with the caret, while typing
};

// Five characters on readable tiles (.ol-code-tiles).
float DrawOlCodeTiles(ImDrawList* draw, ImVec2 at, float width, std::string_view code,
                      const OlTileLook& look) {
    constexpr int kTiles = 5;
    const float tile = look.tile_width > 0.0F ? look.tile_width
                                              : (width - look.gap * (kTiles - 1)) / kTiles;
    const PaddockType type = PaddockMono(look.font_px, true, 1.0F);
    const bool blink = std::fmod(PaddockClock(), 1.0) < 0.5;
    for (int index = 0; index < kTiles; ++index) {
        const ImVec2 a{std::round(at.x + index * (tile + look.gap)), at.y};
        const ImVec2 b{std::round(a.x + tile), at.y + look.height};
        const bool filled = index < static_cast<int>(code.size());
        const bool caret = index == look.caret;
        const PaddockRadii radii = PaddockRound(10.0F);
        unsigned border = look.plate ? 0x10222CU : 0x2F6A86U;
        if (!look.plate) {
            if (filled) border = 0x6FB3CC;
            if (look.hovered) border = 0x5A97B1;
            if (look.error) border = 0xFF7A5C;
            if (caret) border = kOlAmber;
        }
        PaddockFill(draw, a, b, radii, PaddockCol(look.plate ? 0xFFFFFFU : 0x04192AU));
        // Inset shade: along the top of a field tile, the bottom of a plate tile.
        const float shade_top = look.plate ? b.y - 5.0F : a.y + 2.0F;
        draw->PushClipRect({a.x, shade_top}, {b.x, shade_top + 3.0F}, true);
        PaddockFill(draw, {a.x + 2.0F, a.y + 2.0F}, {b.x - 2.0F, b.y - 2.0F}, PaddockRound(8.0F),
                    PaddockCol(0x000000, look.plate ? 20U : 89U));
        draw->PopClipRect();
        PaddockStroke(draw, a, b, radii, PaddockCol(border), 2.0F);
        if (caret) {
            PaddockStroke(draw, {a.x - 3.0F, a.y - 3.0F}, {b.x + 3.0F, b.y + 3.0F}, PaddockRound(13.0F),
                          PaddockCol(kOlAmber, 77U), 3.0F);
        }
        if (filled) {
            const char* glyph = code.data() + index;
            const float glyph_width = PaddockMeasure(type, std::string_view(glyph, 1U));
            PaddockDrawRun(draw, type,
                           {std::round(a.x + (tile - glyph_width) * 0.5F),
                            std::round(a.y + (look.height - type.line) * 0.5F)},
                           PaddockCol(look.plate ? 0x10222CU : 0xFFF4D0U), glyph, glyph + 1);
        } else if (caret && blink) {
            const float centre = (a.x + b.x) * 0.5F;
            const float middle = (a.y + b.y) * 0.5F;
            PaddockFill(draw, {centre - 1.5F, middle - 15.0F}, {centre + 1.5F, middle + 15.0F},
                        PaddockRound(2.0F), PaddockCol(kOlAmber));
        }
    }
    return tile * kTiles + look.gap * (kTiles - 1);
}

// A colour stop of a CSS linear-gradient.
struct OlStop {
    float at;
    unsigned rgb;
};

// A panel for measured content (.ol-card and friends).
struct OlPanelLook {
    PaddockRadii radii = PaddockRound(12.0F);
    unsigned fill = kOlPanel;
    unsigned fill_alpha = 255U;
    unsigned ring = kOlRing;
    float ring_width = 2.0F;
    unsigned ring_alpha = 255U;
    unsigned border = 0U;
    float border_width = 0.0F;
    unsigned drop = 0U;
    float drop_offset = 0.0F;
    float gradient_degrees = 0.0F;
    std::vector<OlStop> gradient;
};

void PaintOlPanel(ImDrawList* draw, ImVec2 a, ImVec2 b, const OlPanelLook& look) {
    if (look.drop_offset > 0.0F) {
        PaddockFill(draw, {a.x, a.y + look.drop_offset}, {b.x, b.y + look.drop_offset}, look.radii,
                    PaddockCol(look.drop));
    }
    const int first = draw->VtxBuffer.Size;
    PaddockFill(draw, a, b, look.radii, PaddockCol(look.fill, look.fill_alpha));
    if (!look.gradient.empty()) {
        const float radians = look.gradient_degrees * 3.14159265F / 180.0F;
        const ImVec2 direction{std::sin(radians), -std::cos(radians)};
        const float length = std::abs((b.x - a.x) * direction.x) + std::abs((b.y - a.y) * direction.y);
        const ImVec2 centre{(a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F};
        for (int index = first; index < draw->VtxBuffer.Size; ++index) {
            ImDrawVert& vertex = draw->VtxBuffer[index];
            const float t = ((vertex.pos.x - centre.x) * direction.x +
                             (vertex.pos.y - centre.y) * direction.y) / std::max(length, 1.0F) + 0.5F;
            ImU32 colour = PaddockRgb(look.gradient.front().rgb);
            for (std::size_t stop = 1U; stop < look.gradient.size(); ++stop) {
                const OlStop& from = look.gradient[stop - 1U];
                const OlStop& to = look.gradient[stop];
                if (t <= from.at) break;
                colour = t >= to.at ? PaddockRgb(to.rgb)
                                    : PaddockMix(PaddockRgb(from.rgb), PaddockRgb(to.rgb),
                                                 (t - from.at) / std::max(to.at - from.at, 0.0001F));
            }
            vertex.col = PaddockApply(colour);
        }
    }
    if (look.ring_width > 0.0F) {
        const float edge = look.border_width;
        PaddockStroke(draw, {a.x + edge, a.y + edge}, {b.x - edge, b.y - edge},
                      PaddockInset(look.radii, edge), PaddockCol(look.ring, look.ring_alpha),
                      look.ring_width);
    }
    if (look.border_width > 0.0F) {
        PaddockStroke(draw, a, b, look.radii, PaddockCol(look.border), look.border_width);
    }
}

// ------------------------------------------------------------ reading text

// CSS `text-wrap: pretty`: a paragraph never ends on a lone word when the
// previous line can give it company.
std::vector<PaddockLine> OlWrap(const PaddockType& type, std::string_view text, float width) {
    std::vector<PaddockLine> lines = PaddockWrap(type, text, width);
    if (lines.size() < 2U) return lines;
    PaddockLine& last = lines.back();
    PaddockLine& previous = lines[lines.size() - 2U];
    if (std::find(last.begin, last.end, ' ') != last.end) return lines;
    if (previous.end == nullptr || previous.end > last.begin) return lines;
    const char* space = previous.end;
    while (space > previous.begin && *(space - 1) != ' ') --space;
    if (space <= previous.begin) return lines;
    const char* moved = space;
    const char* kept_end = space - 1;
    while (kept_end > previous.begin && *(kept_end - 1) == ' ') --kept_end;
    if (kept_end <= previous.begin) return lines;
    const float grown = PaddockMeasure(type, moved, last.end);
    if (grown > width) return lines;
    previous.end = kept_end;
    previous.width = PaddockMeasure(type, previous.begin, previous.end);
    last.begin = moved;
    last.width = grown;
    return lines;
}

float OlParagraphHeight(std::string_view text, float px, float width, bool semibold = false,
                        float line = 1.5F) {
    if (text.empty()) return 0.0F;
    const PaddockType type = OlRead(px, semibold, line);
    return type.line * static_cast<float>(OlWrap(type, text, width).size());
}

float OlParagraphAt(ImDrawList* draw, ImVec2 at, std::string_view text, float px, unsigned colour,
                    float width, bool semibold = false, float line = 1.5F, bool centre = false) {
    if (text.empty()) return 0.0F;
    const PaddockType type = OlRead(px, semibold, line);
    const auto lines = OlWrap(type, text, width);
    PaddockTextStyle style;
    style.colour = PaddockCol(colour);
    style.centre = centre;
    PaddockDrawLines(draw, type, at, width, lines, style);
    return type.line * static_cast<float>(lines.size());
}

// A paragraph as one layout item at the cursor.
float OlParagraph(std::string_view text, float px, unsigned colour, float width,
                  bool semibold = false, float line = 1.5F, bool centre = false) {
    const float height = OlParagraphAt(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), text,
                                       px, colour, width, semibold, line, centre);
    ImGui::Dummy({width, height});
    return height;
}

// ------------------------------------------------------------ page state

enum class OlSection { Lobbies, Play, Settings, Profile, Friends, Overlays, Lobby, Connection };

struct OnlinePageState {
    OlSection section = OlSection::Lobbies;
    bool was_active = false;
    bool was_joining = false;
    bool join_attempted = false;       // a failed session shows under the code tiles
    std::string join_error;
    std::string phase_key;
    double entered_at = -10.0;         // a new section or phase slides in
    ImGuiContext* context = nullptr;
    int last_frame = -100;
    bool open_close = false;
    bool open_manage = false;
    bool open_block = false;
    bool open_remove = false;
    bool open_results = false;
    bool open_match_end = false;
    bool match_end_lobby_retained = false;
    std::string match_end_notice;
    std::uint32_t observed_test_generation = 0U;
    std::uint32_t countdown_second = 0U;
    double countdown_changed_at = -10.0;
    float sign_lean = 0.0F;            // the OR sign leans toward the card you're on
};
OnlinePageState g_online_page;

struct OnlineFrame {
    const dkr::runtime::netplay::SessionView& view;
    bool launcher = true;
    bool rom_ready = false;
    bool active = false;       // hosting, joining or in a lobby
    bool joining = false;      // knocking on a host's door
    bool in_lobby = false;     // hosting or admitted
    bool busy = false;         // a test, countdown or race is running
    bool has_space = false;    // the host can admit one more racer
};

// Content that slides in after a section or phase change (.is-entering).
class OlEnter {
public:
    explicit OlEnter(int index)
        : fade_(g_online_page.entered_at, 0.07F * static_cast<float>(std::min(index, 6)), 0.32F,
                10.0F) {}

private:
    PaddockFadeUp fade_;
};

std::string OlSaveSeedDescription(bool previous_available) {
    switch (static_cast<dkr::runtime::saves::OnlineSaveSeedMode>(g_online_save_seed_mode)) {
    case dkr::runtime::saves::OnlineSaveSeedMode::Fresh:
        return "Start a new online Adventure. Your single-player save is kept.";
    case dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession:
        return previous_available ? "Continue the last host-owned online Adventure save."
                                  : "No previous online session save is available yet.";
    case dkr::runtime::saves::OnlineSaveSeedMode::CopySinglePlayer:
    default:
        return "Copy your progress into a separate online save. Your single-player file is kept.";
    }
}

// Save validation can hit slow storage or antivirus: keep one background read
// in flight and refresh the cache, never read the file at frame rate.
bool OlPreviousOnlineSaveAvailable() {
    static std::future<dkr::runtime::saves::SaveInfo> read;
    static dkr::runtime::saves::SaveInfo info;
    static auto next_read = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (read.valid() && read.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try { info = read.get(); } catch (...) { info = {}; }
        next_read = now + std::chrono::seconds(2);
    }
    if (!read.valid() && now >= next_read) {
        read = std::async(std::launch::async,
                          [] { return dkr::runtime::saves::previous_online_adventure_info(); });
    }
    return info.valid;
}

const char* OlMenuOwnershipLabel(dkr::runtime::netplay::HostControlPolicy policy) {
    using dkr::runtime::netplay::HostControlPolicy;
    switch (policy) {
    case HostControlPolicy::HostSharedMenus: return "Host controls shared menus";
    case HostControlPolicy::EveryAssignedPort: return "Every assigned port";
    case HostControlPolicy::GuidedUntilCharacterSelect:
    default: return "Host guides menus until character select";
    }
}

std::string OlRulesLine(const dkr::runtime::netplay::Rules& rules) {
    using dkr::runtime::netplay::SynchronizationMode;
    std::string line = rules.synchronization == SynchronizationMode::ExperimentalRollback ? "Experimental rollback" :
        (rules.synchronization == SynchronizationMode::Rollback ? "Host prediction" : "Strict input sync");
    line += " \xC2\xB7 ";
    line += rules.automatic_input_delay ? std::string("Automatic input delay")
                                        : std::to_string(rules.manual_input_delay) + " frame delay";
    line += " \xC2\xB7 ";
    line += OlMenuOwnershipLabel(rules.host_control);
    return line;
}

std::string OlTrimmed(const char* text) {
    std::string value(text);
    const auto first = value.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(' ') - first + 1U);
}

template <std::size_t Size>
void OlCommitName(char (&buffer)[Size], const char* fallback) {
    std::string name = OlTrimmed(buffer);
    if (name.empty()) name = fallback;
    const std::size_t length = std::min(name.size(), Size - 1U);
    std::memcpy(buffer, name.data(), length);
    buffer[length] = '\0';
    SaveSettings();
}

// The newest permanent Friend Code: the one the profile card shows.
std::optional<dkr::runtime::netplay::FriendInviteView> OlPermanentFriendCode(
        const dkr::runtime::netplay::FriendServiceSnapshot& snapshot) {
    std::optional<dkr::runtime::netplay::FriendInviteView> newest;
    for (const auto& invite : snapshot.invitations) {
        if (invite.lifetime != dkr::runtime::netplay::FriendInviteLifetime::Permanent) continue;
        if (!newest || invite.invite_id > newest->invite_id) newest = invite;
    }
    return newest;
}

std::string OlFormatRemaining(std::uint64_t seconds) {
    const std::uint64_t minutes = (seconds + 59U) / 60U;
    const std::uint64_t hours = minutes / 60U;
    char text[48]{};
    if (hours > 0U) {
        std::snprintf(text, sizeof(text), "%llu h %02llu min", static_cast<unsigned long long>(hours),
                      static_cast<unsigned long long>(minutes % 60U));
    } else {
        std::snprintf(text, sizeof(text), "%llu min", static_cast<unsigned long long>(minutes));
    }
    return text;
}

// ------------------------------------------------------------ section tabs

struct OlTab {
    OlSection id;
    const char* label;
};

void DrawOlTabs(const std::vector<OlTab>& tabs, float width) {
#if defined(__ANDROID__)
    const char* selected = tabs.front().label;
    for (const auto& tab : tabs) if (tab.id == g_online_page.section) selected = tab.label;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##mobile-online-section", selected)) {
        for (const auto& tab : tabs) if (ImGui::Selectable(tab.label, tab.id == g_online_page.section))
            g_online_page.section = tab.id;
        ImGui::EndCombo();
    }
    return;
#endif
    const bool compact = width <= 540.0F;
    const float px = compact ? 18.0F : 19.0F;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    std::vector<float> widths;
    for (const OlTab& tab : tabs) {
        const float natural = OlSectionTabWidth(tab.label, px);
        widths.push_back(compact ? std::max(natural, 135.0F) : natural);
    }
    // Lay the tabs out in rows; compact rows share their spare width.
    std::vector<std::pair<std::size_t, std::size_t>> rows;
    std::size_t start = 0U;
    float used = 0.0F;
    for (std::size_t index = 0U; index < tabs.size(); ++index) {
        const float next = used + (index > start ? 10.0F : 0.0F) + widths[index];
        if (index > start && next > width) {
            rows.push_back({start, index});
            start = index;
            used = widths[index];
        } else {
            used = next;
        }
    }
    rows.push_back({start, tabs.size()});
    const float height = std::ceil(std::max(46.0F, OlSignType(px, 1.4F).line + 24.0F));
    float y = origin.y;
    for (const auto& [first, last] : rows) {
        float total = 0.0F;
        for (std::size_t index = first; index < last; ++index) total += widths[index];
        total += 10.0F * static_cast<float>(last - first - 1U);
        const float spare = compact ? (width - total) / static_cast<float>(last - first) : 0.0F;
        float x = origin.x;
        for (std::size_t index = first; index < last; ++index) {
            ImGui::SetCursorScreenPos({x, y});
            const float tab_width = widths[index] + std::max(spare, 0.0F);
            const std::string id = std::string(tabs[index].label) + "##online-section";
            if (OlSectionTab(id.c_str(), g_online_page.section == tabs[index].id, tab_width, px)) {
                g_online_page.section = tabs[index].id;
            }
            x += tab_width + 10.0F;
        }
        y += height + 10.0F;
    }
    const float bottom = y - 10.0F + 16.0F;
    ImGui::GetWindowDrawList()->AddRectFilled({origin.x, bottom}, {origin.x + width, bottom + 2.0F},
                                              PaddockCol(0x24495B));
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, bottom + 2.0F - origin.y});
}

// ------------------------------------------------------------ cards

// .ol-card: a padded panel around content measured as it is drawn.
template <typename Content>
void DrawOlCard(float width, const OlPanelLook& look, Content&& content, float padding = 18.0F) {
    PaddockBox box(width, {padding, padding});
    content(box.Inner());
    box.End([&](ImDrawList* draw, ImVec2 a, ImVec2 b) { PaintOlPanel(draw, a, b, look); });
}

// A feature card (.ol-feature): its heading, an explanation, then content.
template <typename Content>
void DrawOlFeature(float width, const char* title, const char* description, Content&& content) {
    DrawOlCard(width, OlPanelLook{}, [&](float inner) {
        OlHeading(title, 24.0F, kOlCream, inner);
        if (description != nullptr && *description != '\0') {
            PaddockGap(16.0F);
            OlParagraph(description, 15.0F, kOlSoft, inner);
        }
        content(inner);
    });
}

void DrawOlGuideCard(float width, const OnlineFrame& frame) {
    DrawOlFeature(width, "Online guide", "Two to four racers. One private lobby.", [&](float inner) {
        PaddockGap(16.0F);
        OlParagraph(frame.active ? "Gameplay settings are fixed until you leave this session."
                                 : "Host settings apply to the next lobby you create.",
                    15.0F, kOlSoft, inner);
        PaddockGap(16.0F);
        if (OlButton("Read the online guide", OlTone::Plain, inner)) {
            g_online_guide_reopen_requested = true;
        }
    });
}

void DrawOlProfileCard(float width, const OnlineFrame& frame, bool profile_section) {
    using namespace dkr::runtime::netplay;
    FriendService& service = friend_service();
    const auto snapshot = service.snapshot();
    OlPanelLook look;
    look.gradient_degrees = 160.0F;
    look.gradient = {{0.0F, 0x0F3D5C}, {0.75F, kOlPanel}};
    DrawOlCard(width, look, [&](float inner) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        // Your racer: avatar and name.
        {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            constexpr float head = 66.0F;
            DrawOlAvatar(draw, {at.x, at.y + 5.0F}, 56.0F, g_online_player_name);
            const float name_x = at.x + 56.0F + 14.0F;
            const float name_width = inner - 70.0F;
            PaddockTextStyle label;
            label.colour = PaddockCol(kOlSoft);
            PaddockTextAt(draw, OlRead(12.0F, true, 1.5F), {name_x, at.y}, name_width, "Racer name", label);
            ImGui::SetCursorScreenPos({name_x, at.y + 18.0F + 4.0F});
            OlInputOptions options;
            options.disabled = frame.active;
            const OlInputResult result = OlInput("##online-racer-name", nullptr, g_online_player_name,
                                                 sizeof(g_online_player_name), name_width, options);
            if (result == OlInputResult::Gamepad) RequestTextEntryKeyboard(TextEntryTarget::RacerName);
            if (result == OlInputResult::Committed || result == OlInputResult::Entered) {
                OlCommitName(g_online_player_name, "Racer");
            }
            ImGui::SetCursorScreenPos(at);
            ImGui::Dummy({inner, head});
        }
        // Presence.
        PaddockGap(14.0F);
        {
            const bool hidden = service.appear_offline();
            const bool available = service.presence_available();
            const char* text = hidden ? "Friends see you as offline"
                             : !available ? "Connecting to friends\xE2\x80\xA6"
                             : frame.in_lobby ? "Online \xC2\xB7 in a lobby" : "Online";
            const PaddockType type = OlRead(13.0F, true, 1.5F);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float pill = std::ceil(10.0F + 9.0F + 8.0F + PaddockMeasure(type, text) + 12.0F);
            const float height = type.line + 10.0F;
            const bool grey = hidden || !available;
            PaddockFill(draw, at, {at.x + pill, at.y + height}, PaddockRound(height * 0.5F),
                        PaddockCol(grey ? 0x26333BU : 0x104E43U));
            draw->AddCircleFilled({at.x + 10.0F + 4.5F, at.y + height * 0.5F}, 4.5F,
                                  PaddockCol(grey ? 0x7D8D95U : kOlGo), 16);
            PaddockDrawRun(draw, type, {at.x + 27.0F, at.y + 5.0F}, PaddockCol(grey ? 0xB3C3CBU : 0xB4F4DDU),
                           text, text + std::strlen(text));
            ImGui::Dummy({pill, height});
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", service.status().c_str());
        }
        PaddockGap(12.0F);
        // Your friend code.
        {
            const auto code = OlPermanentFriendCode(*snapshot);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            constexpr float height = 54.0F;
            PaddockFill(draw, at, {at.x + inner, at.y + height}, PaddockRound(14.0F), PaddockCol(0x061D2C));
            const char* action = code ? "Copy##friend-code" : "Create##friend-code";
            const float button = OlButtonWidth(action);
            const float text_width = inner - button - 30.0F;
            PaddockTextStyle caption;
            caption.colour = PaddockCol(kOlSoft);
            PaddockTextAt(draw, OlRead(12.0F, false, 1.5F), {at.x + 12.0F, at.y + 6.0F}, text_width,
                          "Your friend code", caption);
            if (code) {
                const PaddockType mono = PaddockMono(16.0F, true, 1.5F);
                const std::string shown = PaddockEllipsize(mono, code->code, text_width);
                PaddockDrawRun(draw, mono, {at.x + 12.0F, at.y + 24.0F}, PaddockCol(kOlAmber),
                               shown.data(), shown.data() + shown.size());
            } else {
                PaddockTextStyle none;
                none.colour = PaddockCol(kOlInk);
                PaddockTextAt(draw, OlRead(15.0F, true, 1.5F), {at.x + 12.0F, at.y + 24.0F}, text_width,
                              "No code yet", none);
            }
            ImGui::SetCursorScreenPos({at.x + inner - 6.0F - button, at.y + 7.0F});
            if (OlButton(action)) {
                if (code) {
                    CopyFriendCodeToClipboard(code->code);
                    g_online_action_status.clear();
                    OlNotify("Friend code copied.");
                } else {
                    FriendInviteView invite{};
                    std::string error;
                    if (service.create_invite(FriendInviteLifetime::Permanent, std::chrono::minutes(60),
                                              invite, error)) {
                        CopyFriendCodeToClipboard(invite.code);
                        g_online_action_status.clear();
                        OlNotify("Friend code created and copied.");
                    } else {
                        OlNotify(error);
                    }
                }
            }
            ImGui::SetCursorScreenPos(at);
            ImGui::Dummy({inner, height});
        }
        OlParagraph("Device identity: " + service.identity_label(), 15.0F, kOlSoft, inner);
        PaddockGap(13.0F);
        if (profile_section) {
            const OlInputResult result = OlInput("##online-display-name", "Display name", g_online_profile_name,
                                                 sizeof(g_online_profile_name), inner);
            if (result == OlInputResult::Gamepad) {
                RequestTextEntryKeyboard(TextEntryTarget::OnlineProfileName);
            }
            if (OlButton("Save online profile")) {
                std::string error;
                if (service.set_display_name(g_online_profile_name, error)) {
                    std::memcpy(g_online_player_name, g_online_profile_name, sizeof(g_online_player_name));
                    g_online_player_name[sizeof(g_online_player_name) - 1U] = '\0';
                    SaveSettings();
                    OlNotify("Online profile saved.");
                } else {
                    OlNotify(error);
                }
            }
        }
    });
}

// ------------------------------------------------------------ open lobbies

// One room hosted by a friend (.ol-room). Returns whether its main action was pressed.
struct OlRoom {
    std::string id;
    bool invited = false;
    std::string tag;
    std::string title;
    std::string summary;
    std::string note;          // a soft line, e.g. an invitation's status
    std::string error;
    const char* action = nullptr;
    bool action_disabled = false;
    const char* secondary = nullptr;
};

enum class OlRoomPress { None, Action, Secondary };

OlRoomPress DrawOlRoom(const OlRoom& room, float width) {
    OlRoomPress pressed = OlRoomPress::None;
    ImGui::PushID(room.id.c_str());
    OlPanelLook look;
    look.fill = room.invited ? 0x312911U : 0x061D2CU;
    look.ring = room.invited ? 0x9B7932U : 0xFFFFFFU;
    look.ring_alpha = room.invited ? 255U : 26U;
    look.ring_width = 1.0F;
    DrawOlCard(width, look, [&](float inner) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 tag = DrawOlSoftTag(draw, at, room.tag);
        ImGui::Dummy({inner, tag.y});
        PaddockGap(12.0F);
        PaddockText(PaddockSign(24.0F, 1.2F, 0.0F), PaddockRgb(kOlCream), room.title, inner, true);
        PaddockGap(12.0F);
        OlParagraph(room.summary, 15.0F, kOlInk, inner);
        if (!room.note.empty()) {
            PaddockGap(12.0F);
            OlParagraph(room.note, 15.0F, kOlSoft, inner);
        }
        if (!room.error.empty()) {
            PaddockGap(12.0F);
            OlParagraph(room.error, 14.0F, kOlError, inner, true);
        }
        if (room.action != nullptr) {
            PaddockGap(12.0F);
            const ImVec2 row = ImGui::GetCursorScreenPos();
            {
                const OlDisabled disabled(room.action_disabled);
                if (OlButton(room.action, OlTone::Go)) pressed = OlRoomPress::Action;
            }
            if (room.secondary != nullptr) {
                ImGui::SetCursorScreenPos({row.x + OlButtonWidth(room.action) + 8.0F, row.y});
                if (OlButton(room.secondary)) pressed = OlRoomPress::Secondary;
            }
        }
    });
    ImGui::PopID();
    return pressed;
}

void DrawOlOpenLobbies(float width, const OnlineFrame& frame) {
    using namespace dkr::runtime::netplay;
    const auto snapshot = friend_service().snapshot();
    const bool can_join = frame.launcher && frame.rom_ready && !frame.active;
    DrawOlFeature(width, "Open lobbies", "Find rooms hosted by friends. Invitations appear first.",
                  [&](float inner) {
        std::size_t shown = 0U;
        for (const FriendLobbyInviteView& invite : snapshot->incoming_lobby_invites) {
            if (invite.status == FriendLobbyInviteStatus::Expired ||
                invite.status == FriendLobbyInviteStatus::Cancelled) continue;
            ++shown;
            const bool compatible = invite.compatibility == DKR_NETWORK_RELEASE_VERSION;
            const bool delivered = invite.status == FriendLobbyInviteStatus::Delivered;
            OlRoom room;
            room.id = "invite-" + std::to_string(invite.invite_id);
            room.invited = true;
            room.tag = "Lobby invitation";
            room.title = invite.friend_display_name;
            room.summary = std::to_string(invite.players) + " / " + std::to_string(invite.maximum_players) +
                           " racers \xC2\xB7 " + invite.synchronization;
            if (!delivered) {
                room.note = std::string("Invitation ") +
                            LowerAscii(FriendLobbyInviteStatusLabel(invite.status)) + ".";
            }
            if (!compatible) room.error = "This invitation was created by a different DKR-R network build.";
            if (delivered) {
                room.action = "Accept and join";
                room.action_disabled = !can_join || !compatible;
                room.secondary = "Decline";
            }
            PaddockGap(16.0F);
            const OlRoomPress pressed = DrawOlRoom(room, inner);
            std::string error;
            if (pressed == OlRoomPress::Action) {
                g_online_page.join_attempted = true;
                g_online_page.join_error.clear();
                if (session().join_friend_invite(invite.lobby_code, g_online_player_name,
                                                 invite.admission, error)) {
                    g_joining_friend_invite = invite;
                } else {
                    OlNotify(error);
                }
            } else if (pressed == OlRoomPress::Secondary) {
                FriendLobbyInviteView declined{};
                if (!friend_service().respond_lobby_invite(invite.invite_id, false, declined, error)) {
                    OlNotify(error);
                }
            }
        }
        for (const FriendView& racer : snapshot->friends) {
            if (racer.blocked || !racer.online || !racer.hosting || racer.lobby_code.empty()) continue;
            ++shown;
            OlRoom room;
            room.id = "lobby-" + racer.identity;
            room.tag = "Friend lobby";
            room.title = FriendDisplayLabel(racer);
            room.summary = std::to_string(racer.players) + " / " + std::to_string(racer.maximum_players) +
                           " racers";
            if (racer.ping_ms > 0U) room.summary += " \xC2\xB7 " + std::to_string(racer.ping_ms) + " ms";
            room.action = "Request to join";
            room.action_disabled = !can_join;
            PaddockGap(16.0F);
            if (DrawOlRoom(room, inner) == OlRoomPress::Action) {
                std::string error;
                g_online_page.join_attempted = true;
                g_online_page.join_error.clear();
                if (!session().join(racer.lobby_code, g_online_player_name, error)) OlNotify(error);
            }
        }
        if (shown == 0U) {
            PaddockGap(16.0F);
            OlParagraph("No friends are hosting an open lobby right now.", 15.0F, kOlInk, inner);
        }
        if (!frame.launcher) {
            PaddockGap(16.0F);
            OlParagraph("Return to the launcher before joining another lobby.", 15.0F, kOlSoft, inner);
        } else if (!frame.rom_ready) {
            PaddockGap(16.0F);
            if (OlLink("Choose a ROM before joining")) g_page_navigation_request = kPagePlay;
        }
    });
}

// ------------------------------------------------------------ host / join

// The content panel's inner width. The study's breakpoints are container
// queries on that panel, not on the column a card sits in.
float g_ol_page_width = 1400.0F;

inline bool OlNarrow(float at_most) { return g_ol_page_width <= at_most; }

// Choice cards, the lobby and the checklist tighten on narrow panels.
struct OlSignboard {
    float pad_x = 24.0F;
    float pad_top = 26.0F;
    float pad_bottom = 24.0F;
    float title_px = 34.0F;
};

inline OlSignboard OlSignboardMetrics() {
    OlSignboard metrics;
    if (OlNarrow(440.0F)) {
        metrics.pad_x = 16.0F;
        metrics.pad_top = 20.0F;
        metrics.pad_bottom = 20.0F;
        metrics.title_px = 28.0F;
    }
    return metrics;
}

// Whether keyboard or controller focus sits inside a rectangle (:focus-within).
bool OlFocusWithin(ImVec2 a, ImVec2 b) {
    ImGuiContext& g = *GImGui;
    if (g.NavWindow == nullptr || g.NavId == 0 || g.NavWindow != ImGui::GetCurrentWindow()) return false;
    const ImRect rect = ImGui::WindowRectRelToAbs(g.NavWindow, g.NavWindow->NavRectRel[0]);
    return rect.Min.x >= a.x - 1.0F && rect.Max.x <= b.x + 1.0F && rect.Min.y >= a.y - 1.0F &&
           rect.Max.y <= b.y + 1.0F;
}

struct OlChoiceLook {
    unsigned check;
    unsigned border;
    unsigned from;
    unsigned to;
};

// .ol-choice: a signboard with a checker strip in its top-right corner.
void PaintOlChoice(ImDrawList* draw, ImVec2 a, ImVec2 b, const OlChoiceLook& look) {
    OlPanelLook panel;
    panel.radii = {12.0F, 28.0F, 12.0F, 12.0F};
    panel.fill = look.from;
    panel.gradient_degrees = 160.0F;
    panel.gradient = {{0.0F, look.from}, {0.7F, look.to}};
    panel.ring_width = 0.0F;
    panel.border = look.border;
    panel.border_width = 2.0F;
    panel.drop = 0x041822;
    panel.drop_offset = 5.0F;
    PaintOlPanel(draw, a, b, panel);
    // Ten-pixel squares every twenty, clipped by the 26 px inner corner.
    constexpr float kRadius = 26.0F;
    const float top = a.y + 2.0F;
    const float right = b.x - 2.0F;
    for (float x = right - 96.0F + 10.0F; x < right; x += 20.0F) {
        const float end = std::min(x + 10.0F, right);
        for (float column = x; column < end; column += 1.0F) {
            const float dx = right - (column + 0.5F);
            float start = top;
            if (dx < kRadius) {
                start = top + kRadius - std::sqrt(kRadius * kRadius - (kRadius - dx) * (kRadius - dx));
            }
            if (start < top + 10.0F) {
                draw->AddRectFilled({column, start}, {std::min(column + 1.0F, end), top + 10.0F},
                                    PaddockCol(look.check));
            }
        }
    }
}

// The HOST / JOIN tag (.ol-choice-tag).
float DrawOlChoiceTag(ImDrawList* draw, ImVec2 at, std::string_view text) {
    const PaddockType type = OlSignType(15.0F, 1.5F, 0.08F);
    const float width = std::ceil(PaddockMeasure(type, text) - type.tracking + 20.0F);
    const float height = type.line + 6.0F;
    PaddockFill(draw, at, {at.x + width, at.y + height}, PaddockRound(4.0F), PaddockCol(0x042233));
    PaddockDrawRun(draw, type, {at.x + 10.0F, at.y + 3.0F}, PaddockCol(kOlAmber), text.data(),
                   text.data() + text.size());
    return height;
}

// .ol-choice p { max-width: 36ch }
float OlChoiceTextWidth(float inner) {
    return std::min(inner, 36.0F * PaddockMeasure(OlRead(15.0F), "0"));
}

std::string OlHostInvitation() {
    const int friends = std::max(g_online_maximum_players - 1, 1);
    return "Share a 5-character code with up to " + std::to_string(friends) +
           (friends == 1 ? " friend." : " friends.");
}

float OlHostNaturalHeight(float inner, bool previous_save) {
    const OlSignboard board = OlSignboardMetrics();
    const float text = OlChoiceTextWidth(inner);
    const float field = OlParagraphHeight("Lobby name", 14.0F, inner, true, 1.4F) + 6.0F + 44.0F;
    const float select = OlParagraphHeight("Online Adventure save", 14.0F, inner, true, 1.4F) + 6.0F + 44.0F;
    return 2.0F + board.pad_top + OlSignType(15.0F).line + 6.0F + 10.0F +
           PaddockTextHeight(OlSignType(board.title_px, 1.1F), "Start a lobby", inner, true) + 10.0F +
           OlParagraphHeight(OlHostInvitation(), 15.0F, text) + 14.0F + 10.0F + field + 10.0F + select +
           10.0F + OlParagraphHeight(OlSaveSeedDescription(previous_save), 15.0F, text) + 14.0F + 10.0F +
           40.0F + 10.0F + 52.0F + board.pad_bottom + 2.0F;
}

std::string OlJoinError(const OnlineFrame& frame) {
    if (!g_online_page.join_error.empty()) return g_online_page.join_error;
    if (g_online_page.join_attempted &&
        frame.view.state == dkr::runtime::netplay::ConnectionState::Failed) {
        return frame.view.status;
    }
    return {};
}

float OlJoinActionsHeight(float inner) {
    const float paste = OlButtonWidth("Paste code");
    const float keyboard = OlButtonWidth("On-screen keyboard");
    return paste + 8.0F + keyboard <= inner ? 40.0F : 88.0F;
}

float OlJoinNaturalHeight(float inner, const std::string& error) {
    const OlSignboard board = OlSignboardMetrics();
    const float text = OlChoiceTextWidth(inner);
    const float tile = OlNarrow(440.0F) ? 54.0F : 64.0F;
    return 2.0F + board.pad_top + OlSignType(15.0F).line + 6.0F + 10.0F +
           PaddockTextHeight(OlSignType(board.title_px, 1.1F), "Join a friend", inner, true) + 10.0F +
           OlParagraphHeight("Type the code your host sent you.", 15.0F, text) + 10.0F + 4.0F + tile +
           10.0F + (error.empty() ? 0.0F : OlParagraphHeight(error, 14.0F, text, true) + 10.0F) +
           OlJoinActionsHeight(inner) + 10.0F + 52.0F + board.pad_bottom + 2.0F;
}

void OlRequestJoin(const OnlineFrame& frame) {
    if (!frame.launcher || !frame.rom_ready || frame.active) return;
    const std::string code = NormalizeOnlineInvite(g_online_invite);
    if (code.size() < 5U) {
        g_online_page.join_error = "Enter all 5 characters of the code.";
        return;
    }
    g_online_page.join_error.clear();
    g_online_page.join_attempted = true;
    std::string error;
    if (!dkr::runtime::netplay::session().join(code, g_online_player_name, error)) {
        g_online_page.join_error = error;
    }
}

// Start a lobby, or join one with a code.
void DrawOlHostCard(ImVec2 at, float width, float height, const OnlineFrame& frame, bool previous_save) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaintOlChoice(draw, at, {at.x + width, at.y + height}, {0x9FF0D9, 0x3FB79E, 0x0C5A5A, 0x083A45});
    const OlSignboard board = OlSignboardMetrics();
    const float x = at.x + 2.0F + board.pad_x;
    const float inner = width - 4.0F - board.pad_x * 2.0F;
    const float text = OlChoiceTextWidth(inner);
    ImGui::SetCursorScreenPos({x, at.y + 2.0F + board.pad_top});
    ImGui::PushID("host");
    ImGui::BeginGroup();
    ImGui::Dummy({inner, DrawOlChoiceTag(draw, ImGui::GetCursorScreenPos(), "HOST")});
    PaddockGap(10.0F);
    PaddockText(OlSignType(board.title_px, 1.1F), PaddockRgb(kOlCream), "Start a lobby", inner, true,
                PaddockRgb(0x031623));
    PaddockGap(10.0F);
    OlParagraph(OlHostInvitation(), 15.0F, 0xD3E5EC, text);
    PaddockGap(14.0F + 10.0F);
    const OlInputResult name = OlInput("##lobby-name", "Lobby name", g_online_room_name,
                                       sizeof(g_online_room_name), inner);
    if (name == OlInputResult::Gamepad) RequestTextEntryKeyboard(TextEntryTarget::LobbyName);
    if (name == OlInputResult::Committed || name == OlInputResult::Entered) {
        OlCommitName(g_online_room_name, "DKR-R Grand Prix");
    }
    PaddockGap(10.0F);
    static const std::vector<std::string> seeds{
        "Copy my single-player save", "Start with a fresh save", "Continue with previous session"};
    if (OlSelect("save-seed", "Online Adventure save", &g_online_save_seed_mode, seeds, inner)) {
        SaveSettings();
    }
    PaddockGap(10.0F);
    OlParagraph(OlSaveSeedDescription(previous_save), 15.0F, 0xD3E5EC, text);
    PaddockGap(14.0F + 10.0F);
    if (OlButton("Host settings")) g_online_page.section = OlSection::Settings;
    ImGui::EndGroup();
    const bool continue_unavailable =
        g_online_save_seed_mode ==
            static_cast<int>(dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession) &&
        !previous_save;
    ImGui::SetCursorScreenPos({x, at.y + height - 2.0F - board.pad_bottom - 52.0F});
    if (OlCta("CREATE LOBBY", OlCtaColour::Green, inner,
              !frame.launcher || !frame.rom_ready || continue_unavailable || frame.active)) {
        g_online_page.join_attempted = false;
        g_online_page.join_error.clear();
        CreateOnlineLobby();
    }
    ImGui::PopID();
}

void DrawOlJoinCard(ImVec2 at, float width, float height, const OnlineFrame& frame,
                    const std::string& error) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaintOlChoice(draw, at, {at.x + width, at.y + height}, {0xFFD98A, 0x3A8FB5, 0x0B4D78, 0x082F4C});
    const OlSignboard board = OlSignboardMetrics();
    const float x = at.x + 2.0F + board.pad_x;
    const float inner = width - 4.0F - board.pad_x * 2.0F;
    const float text = OlChoiceTextWidth(inner);
    ImGui::SetCursorScreenPos({x, at.y + 2.0F + board.pad_top});
    ImGui::PushID("join");
    ImGui::BeginGroup();
    ImGui::Dummy({inner, DrawOlChoiceTag(draw, ImGui::GetCursorScreenPos(), "JOIN")});
    PaddockGap(10.0F);
    PaddockText(OlSignType(board.title_px, 1.1F), PaddockRgb(kOlCream), "Join a friend", inner, true,
                PaddockRgb(0x031623));
    PaddockGap(10.0F);
    OlParagraph("Type the code your host sent you.", 15.0F, 0xD3E5EC, text);
    PaddockGap(10.0F + 4.0F);
    // One real input laid over five readable tiles.
    {
        const float tile_height = OlNarrow(440.0F) ? 54.0F : 64.0F;
        const ImVec2 tiles = ImGui::GetCursorScreenPos();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0.0F, std::max((tile_height - ImGui::GetFontSize()) * 0.5F, 0.0F)});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
        for (const ImGuiCol colour : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
                                      ImGuiCol_Text, ImGuiCol_TextSelectedBg, ImGuiCol_NavHighlight,
                                      ImGuiCol_TextDisabled}) {
            ImGui::PushStyleColor(colour, PaddockRgb(0, 0U));
        }
        ImGui::SetNextItemWidth(inner);
        const OlDisabled locked(!frame.launcher || frame.active, 1.0F);
        const bool entered = ImGui::InputText(
            "##join-code", g_online_invite, 6U,
            ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CallbackCharFilter |
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll,
            FilterOnlineCodeCharacter);
        const bool edited = ImGui::IsItemEdited();
        const bool gamepad = ImGui::IsItemActivated() && GImGui->ActiveIdSource == ImGuiInputSource_Gamepad;
        const bool typing = ImGui::IsItemActive() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(2);
        if (gamepad) {
            ImGui::ClearActiveID();
            RequestOnlineCodeKeyboard();
        }
        if (edited) {
            g_online_page.join_error.clear();
            g_online_page.join_attempted = false;
        }
        if (entered) OlRequestJoin(frame);
        const std::string code = NormalizeOnlineInvite(g_online_invite).substr(0U, 5U);
        OlTileLook look;
        look.height = tile_height;
        look.font_px = tile_height > 60.0F ? 32.0F : 26.0F;
        look.hovered = hovered;
        look.error = !error.empty();
        look.caret = typing ? std::min(static_cast<int>(code.size()), 4) : -1;
        DrawOlCodeTiles(draw, tiles, inner, code, look);
        ImGui::SetCursorScreenPos(tiles);
        ImGui::Dummy({inner, tile_height});
    }
    PaddockGap(10.0F);
    if (!error.empty()) {
        OlParagraph(error, 14.0F, 0xD3E5EC, text, true);
        PaddockGap(10.0F);
    }
    {
        const ImVec2 row = ImGui::GetCursorScreenPos();
        if (OlButton("Paste code")) {
            if (PasteOnlineInviteFromClipboard()) {
                g_online_action_status.clear();
                g_online_page.join_error.clear();
                g_online_page.join_attempted = false;
            }
        }
        const bool wrap = OlJoinActionsHeight(inner) > 40.0F;
        ImGui::SetCursorScreenPos(wrap ? ImVec2{row.x, row.y + 48.0F}
                                       : ImVec2{row.x + OlButtonWidth("Paste code") + 8.0F, row.y});
        if (OlButton("On-screen keyboard")) RequestOnlineCodeKeyboard();
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos({x, at.y + height - 2.0F - board.pad_bottom - 52.0F});
    if (OlCta("REQUEST TO JOIN", OlCtaColour::Blue, inner, !frame.launcher || !frame.rom_ready || frame.active)) {
        OlRequestJoin(frame);
    }
    ImGui::PopID();
}

// The fork between hosting and joining: lane lines and a leaning road sign.
void DrawOlOrSign(ImDrawList* draw, ImVec2 centre, float lean) {
    const ImVec2 a{centre.x - 24.0F, centre.y - 24.0F};
    const ImVec2 b{centre.x + 24.0F, centre.y + 24.0F};
    // drop-shadow(0 3px 0): the diamond's silhouette, straight down.
    const int shadow = draw->VtxBuffer.Size;
    PaddockFill(draw, a, b, PaddockRound(7.0F), PaddockCol(0x041822));
    OlRotate(draw, shadow, centre, 45.0F + lean);
    for (int index = shadow; index < draw->VtxBuffer.Size; ++index) draw->VtxBuffer[index].pos.y += 3.0F;
    const int diamond = draw->VtxBuffer.Size;
    PaddockFill(draw, a, b, PaddockRound(7.0F), PaddockCol(kOlAmber));
    PaddockStroke(draw, {a.x + 3.0F, a.y + 3.0F}, {b.x - 3.0F, b.y - 3.0F}, PaddockRound(4.0F),
                  PaddockCol(0x2A1D05), 2.0F);
    OlRotate(draw, diamond, centre, 45.0F + lean);
    // Jumpman capitals sit low in their line box.
    const int text = draw->VtxBuffer.Size;
    const PaddockType type = OlSignType(23.0F, 1.0F, 0.02F);
    const float width = PaddockMeasure(type, "OR") - type.tracking;
    PaddockDrawRun(draw, type, {std::round(centre.x - width * 0.5F), std::round(centre.y - type.line * 0.5F - 1.5F)},
                   PaddockCol(0x2A1D05), "OR", "OR" + 2);
    OlRotate(draw, text, centre, lean);
}

void DrawOlLane(ImDrawList* draw, ImVec2 from, ImVec2 direction, float length) {
    // Dashes start at the sign and fade out toward the far end.
    for (float offset = 0.0F; offset < length; offset += 22.0F) {
        const float dash = std::min(12.0F, length - offset);
        const float middle = (offset + dash * 0.5F) / std::max(length, 1.0F);
        const float alpha = middle <= 0.55F ? 1.0F : std::max(0.0F, 1.0F - (middle - 0.55F) / 0.45F);
        const ImVec2 p0{from.x + direction.x * offset, from.y + direction.y * offset};
        const ImVec2 p1{from.x + direction.x * (offset + dash), from.y + direction.y * (offset + dash)};
        const ImVec2 a{std::min(p0.x, p1.x) - (direction.x == 0.0F ? 1.5F : 0.0F),
                       std::min(p0.y, p1.y) - (direction.y == 0.0F ? 1.5F : 0.0F)};
        const ImVec2 b{std::max(p0.x, p1.x) + (direction.x == 0.0F ? 1.5F : 0.0F),
                       std::max(p0.y, p1.y) + (direction.y == 0.0F ? 1.5F : 0.0F)};
        draw->AddRectFilled(a, b, PaddockCol(0x2F6A86, static_cast<unsigned>(255.0F * alpha)));
    }
}

void DrawOlChoices(float width, const OnlineFrame& frame) {
    const bool previous_save = OlPreviousOnlineSaveAvailable();
    const std::string error = OlJoinError(frame);
    const bool stacked = OlNarrow(760.0F);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float card = stacked ? width : std::floor((width - 68.0F - 28.0F) * 0.5F);
    const float card_inner = card - 4.0F - OlSignboardMetrics().pad_x * 2.0F;
    const float host_natural = OlHostNaturalHeight(card_inner, previous_save);
    const float join_natural = OlJoinNaturalHeight(card_inner, error);
    const float shared = std::max(host_natural, join_natural);
    const ImVec2 host_at = origin;
    const float host_height = stacked ? host_natural : shared;
    const ImVec2 join_at = stacked ? ImVec2{origin.x, origin.y + host_natural + 14.0F + 68.0F + 14.0F}
                                   : ImVec2{origin.x + card + 14.0F + 68.0F + 14.0F, origin.y};
    const float join_height = stacked ? join_natural : shared;
    {
        const OlEnter enter(0);
        DrawOlHostCard(host_at, card, host_height, frame, previous_save);
    }
    const bool host_hover = ImGui::IsMouseHoveringRect(host_at, {host_at.x + card, host_at.y + host_height}) &&
                            ImGui::IsWindowHovered();
    const bool host_focus = OlFocusWithin(host_at, {host_at.x + card, host_at.y + host_height});
    {
        const OlEnter enter(2);
        DrawOlJoinCard(join_at, card, join_height, frame, error);
    }
    const bool join_hover = ImGui::IsMouseHoveringRect(join_at, {join_at.x + card, join_at.y + join_height}) &&
                            ImGui::IsWindowHovered();
    const bool join_focus = OlFocusWithin(join_at, {join_at.x + card, join_at.y + join_height});
    // Focus says where you are; hover wins while the pointer is exploring.
    float target = 0.0F;
    if (host_focus) target = -10.0F;
    if (join_focus) target = 10.0F;
    if (host_hover) target = -10.0F;
    if (join_hover) target = 10.0F;
    const float step = ImGui::GetIO().DeltaTime * 80.0F;
    g_online_page.sign_lean += std::clamp(target - g_online_page.sign_lean, -step, step);
    {
        const OlEnter enter(1);
        if (stacked) {
            const float y = origin.y + host_natural + 14.0F + 34.0F;
            const float lane = (width - 88.0F) * 0.5F;
            DrawOlLane(draw, {origin.x + lane, y}, {-1.0F, 0.0F}, lane);
            DrawOlLane(draw, {origin.x + width - lane, y}, {1.0F, 0.0F}, lane);
            DrawOlOrSign(draw, {origin.x + width * 0.5F, y}, g_online_page.sign_lean);
        } else {
            const float x = origin.x + card + 14.0F + 34.0F;
            const float lane = std::max((shared - 88.0F) * 0.5F, 24.0F);
            const float middle = origin.y + shared * 0.5F;
            DrawOlLane(draw, {x, middle - 44.0F}, {0.0F, -1.0F}, lane);
            DrawOlLane(draw, {x, middle + 44.0F}, {0.0F, 1.0F}, lane);
            DrawOlOrSign(draw, {x, middle}, g_online_page.sign_lean);
        }
    }
    const float bottom = stacked ? join_at.y + join_natural : origin.y + shared;
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, bottom - origin.y + 5.0F});
}

// How online races work (.ol-steps).
void DrawOlSteps(float width) {
    constexpr std::array<std::array<const char*, 2>, 4> steps{{
        {{"Open", "The host creates a lobby"}},
        {{"Share", "Friends type the code"}},
        {{"Approve", "The host lets them in"}},
        {{"Race", "Everyone readies up"}},
    }};
    const int columns = OlNarrow(440.0F) ? 1 : OlNarrow(760.0F) ? 2 : 4;
    const float cell = (width - 10.0F * (columns - 1)) / columns;
    const PaddockType title = OlSignType(18.0F, 1.2F);
    const float text_width = cell - 24.0F - 28.0F - 10.0F;
    std::array<float, 4> heights{};
    for (std::size_t index = 0U; index < steps.size(); ++index) {
        heights[index] = std::max(28.0F, PaddockTextHeight(title, steps[index][0], text_width) +
                                             OlParagraphHeight(steps[index][1], 13.0F, text_width, false, 1.4F));
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float y = origin.y;
    for (int row = 0; row < 4 / columns; ++row) {
        float row_height = 0.0F;
        for (int column = 0; column < columns; ++column) {
            row_height = std::max(row_height, heights[static_cast<std::size_t>(row * columns + column)]);
        }
        row_height += 24.0F;
        for (int column = 0; column < columns; ++column) {
            const std::size_t index = static_cast<std::size_t>(row * columns + column);
            const ImVec2 a{origin.x + column * (cell + 10.0F), y};
            PaddockFill(draw, a, {a.x + cell, a.y + row_height}, PaddockRound(10.0F), PaddockCol(0xFFFFFF, 10U));
            draw->AddCircleFilled({a.x + 12.0F + 14.0F, a.y + 12.0F + 14.0F}, 14.0F, PaddockCol(kOlAmber), 24);
            const std::string number = std::to_string(index + 1U);
            const PaddockType digit = OlRead(14.0F, true, 1.0F);
            const float digit_width = PaddockMeasure(digit, number);
            PaddockDrawRun(draw, digit, {std::round(a.x + 26.0F - digit_width * 0.5F),
                                         std::round(a.y + 26.0F - digit.line * 0.5F)},
                           PaddockCol(0x2A1D05), number.data(), number.data() + number.size());
            const float text_x = a.x + 12.0F + 28.0F + 10.0F;
            PaddockTextStyle title_style;
            title_style.colour = PaddockCol(kOlCream);
            const float title_height = PaddockTextAt(draw, title, {text_x, a.y + 12.0F}, text_width,
                                                     steps[index][0], title_style);
            OlParagraphAt(draw, {text_x, a.y + 12.0F + title_height}, steps[index][1], 13.0F, kOlSoft,
                          text_width, false, 1.4F);
        }
        y += row_height + 10.0F;
    }
    ImGui::Dummy({width, y - 10.0F - origin.y});
}

// ------------------------------------------------------------ before you race

void DrawOlCheckIcon(ImDrawList* draw, ImVec2 centre, int kind) {
    // 0 ok, 1 attention, 2 information.
    const unsigned fill = kind == 0 ? 0x104E43U : kind == 1 ? 0x5A1F16U : 0x173E57U;
    const unsigned ink = kind == 0 ? kOlGoText : kind == 1 ? kOlError : 0x8FD3EAU;
    draw->AddCircleFilled(centre, 14.0F, PaddockCol(fill), 24);
    if (kind == 0) {
        const std::array<ImVec2, 3> tick{{{centre.x - 5.0F, centre.y + 0.5F},
                                          {centre.x - 1.5F, centre.y + 4.0F},
                                          {centre.x + 5.0F, centre.y - 4.0F}}};
        draw->AddPolyline(tick.data(), 3, PaddockCol(ink), 0, 2.0F);
        return;
    }
    const PaddockType type = OlRead(15.0F, true, 1.0F);
    const char* glyph = kind == 1 ? "!" : "i";
    const float width = PaddockMeasure(type, glyph);
    PaddockDrawRun(draw, type, {std::round(centre.x - width * 0.5F), std::round(centre.y - type.line * 0.5F)},
                   PaddockCol(ink), glyph, glyph + 1);
}

void DrawOlChecklist(float width, const OnlineFrame& frame) {
    const std::uint32_t mask = dkr::runtime::magic_codes::selected_mask();
    unsigned codes = 0U;
    for (std::uint32_t bits = mask; bits != 0U; bits &= bits - 1U) ++codes;
    struct Item {
        int kind;
        const char* title;
        std::string text;
        const char* link;
        const char* tag;
        int target;
    };
    const std::array<Item, 3> items{{
        {frame.rom_ready ? 0 : 1, "Game ROM", frame.rom_ready ? "Loaded and ready" : "Not loaded yet",
         frame.rom_ready ? nullptr : "Choose ROM", nullptr, kPagePlay},
        {2, "Magic Codes",
         codes > 0U ? std::to_string(codes) + " on. Friends need the same ones."
                    : std::string("None on. Friends need none on too."),
         frame.active ? nullptr : "Change", frame.active ? "Locked in lobby" : nullptr, kPageModsHacks},
        {2, "Adventure save", "The host supplies a separate online save. Your single-player progress is kept.",
         nullptr, nullptr, -1},
    }};
    OlPanelLook look;
    PaddockBox box(width, OlNarrow(440.0F) ? ImVec2{16.0F, 20.0F} : ImVec2{24.0F, 22.0F});
    const float inner = box.Inner();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockText(OlSignType(26.0F, 1.5F), PaddockRgb(kOlCream), "Before you race", inner, true);
    PaddockGap(2.0F);
    OlParagraph("Everyone in a lobby must match on these. DKR-R checks it for you when someone joins.", 15.0F,
                kOlSoft, inner);
    PaddockGap(14.0F);
    for (std::size_t index = 0U; index < items.size(); ++index) {
        const Item& item = items[index];
        if (index > 0U) PaddockGap(8.0F);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        float trailing = 0.0F;
        if (item.link != nullptr) trailing = PaddockMeasure(OlRead(15.0F, true), item.link);
        if (item.tag != nullptr) trailing = PaddockMeasure(OlRead(12.0F, true), item.tag) + 16.0F;
        const float text_width = inner - 24.0F - 28.0F - 12.0F - (trailing > 0.0F ? trailing + 12.0F : 0.0F);
        const PaddockType strong = OlRead(15.0F, true, 1.4F);
        const float text_height = PaddockTextHeight(strong, item.title, text_width) +
                                  OlParagraphHeight(item.text, 14.0F, text_width, false, 1.4F);
        const float height = std::max(56.0F, text_height + 16.0F);
        const bool bad = item.kind == 1;
        PaddockFill(draw, at, {at.x + inner, at.y + height}, PaddockRound(8.0F),
                    PaddockCol(bad ? 0x2E1F22U : 0x0D2F45U));
        DrawOlCheckIcon(draw, {at.x + 12.0F + 14.0F, at.y + height * 0.5F}, item.kind);
        const float text_x = at.x + 12.0F + 28.0F + 12.0F;
        const float text_y = at.y + std::round((height - text_height) * 0.5F);
        PaddockTextStyle title_style;
        title_style.colour = PaddockCol(0xFFFFFF);
        const float title_height = PaddockTextAt(draw, strong, {text_x, text_y}, text_width, item.title, title_style);
        OlParagraphAt(draw, {text_x, text_y + title_height}, item.text, 14.0F, bad ? kOlError : kOlSoft,
                      text_width, false, 1.4F);
        if (item.link != nullptr) {
            ImGui::SetCursorScreenPos({at.x + inner - 12.0F - trailing, at.y + (height - 40.0F) * 0.5F});
            ImGui::PushID(static_cast<int>(index));
            if (OlLink(item.link)) {
                if (item.target == kPageModsHacks) g_mods_page.entry_section = kModsSectionMagic;
                g_page_navigation_request = item.target;
            }
            ImGui::PopID();
        } else if (item.tag != nullptr) {
            const PaddockType tag = OlRead(12.0F, true, 1.5F);
            DrawOlSoftTag(draw, {at.x + inner - 12.0F - trailing, at.y + (height - tag.line - 8.0F) * 0.5F},
                          item.tag);
        }
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({inner, height});
    }
    // You race with.
    PaddockGap(16.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        draw->AddRectFilled(at, {at.x + inner, at.y + 1.0F}, PaddockCol(0xFFFFFF, 20U));
        const bool one_row = inner >= 240.0F + 16.0F + 220.0F;
        const float text_width = one_row ? inner - 16.0F - 220.0F : inner;
        const PaddockType strong = OlRead(15.0F, true, 1.5F);
        const std::string_view explanation = "Your controls stay yours, whatever player number you get.";
        const float text_height = PaddockTextHeight(strong, "You race with", text_width) +
                                  OlParagraphHeight(explanation, 14.0F, text_width);
        const float row = one_row ? std::max(text_height, 44.0F) : text_height + 10.0F + 44.0F;
        const float text_y = at.y + 17.0F + (one_row ? std::round((row - text_height) * 0.5F) : 0.0F);
        PaddockTextStyle title_style;
        title_style.colour = PaddockCol(0xFFFFFF);
        const float title_height = PaddockTextAt(draw, strong, {at.x, text_y}, text_width, "You race with", title_style);
        OlParagraphAt(draw, {at.x, text_y + title_height}, explanation, 14.0F, kOlSoft, text_width);
        ImGui::SetCursorScreenPos(one_row ? ImVec2{at.x + inner - 220.0F, at.y + 17.0F + (row - 44.0F) * 0.5F}
                                          : ImVec2{at.x, at.y + 17.0F + text_height + 10.0F});
        static const std::vector<std::string> profiles{"Player 1 profile", "Player 2 profile", "Player 3 profile",
                                                       "Player 4 profile"};
        if (OlSelect("local-profile", nullptr, &g_online_input_profile, profiles, 220.0F, frame.active)) {
            g_online_input_profile = std::clamp(g_online_input_profile, 0, 3);
            dkr::runtime::platform::set_online_input_profile(static_cast<std::size_t>(g_online_input_profile));
            SaveSettings();
        }
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({inner, 17.0F + row});
    }
    box.End([&](ImDrawList* target, ImVec2 a, ImVec2 b) { PaintOlPanel(target, a, b, look); });
}

// ------------------------------------------------------------ host settings

void DrawOlHostSettings(float width) {
    using dkr::runtime::netplay::SynchronizationMode;
    DrawOlFeature(width, "Host settings", "These settings are fixed when you create a lobby.", [&](float inner) {
        bool changed = false;
        static const std::vector<std::string> ownership{
            "Host guides menus until character select", "Host controls shared menus", "Every assigned port"};
        static const std::vector<std::string> racers{"2", "3", "4"};
        static const std::vector<std::string> modes{"Host prediction (legacy Rollback)", "Strict input sync (Lockstep)",
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
                                                     "Experimental rollback"};
#else
                                                     "Experimental rollback (in development)"};
#endif
        PaddockGap(16.0F);
        changed |= OlSelect("menu-ownership", "Menu ownership", &g_online_host_control, ownership, inner);
        PaddockGap(16.0F);
        int racer_option = g_online_maximum_players - 2;
        if (OlSelect("maximum-racers", "Maximum racers", &racer_option, racers, inner)) {
            g_online_maximum_players = racer_option + 2;
            changed = true;
        }
        PaddockGap(16.0F);
        OlParagraph("Races and minigames support 2\xE2\x80\x93" "4 racers. Adventure supports 2.", 15.0F, kOlSoft,
                    inner);
        PaddockGap(16.0F);
        int selected_mode = static_cast<int>(CurrentOnlineSynchronization());
        if (OlSelect("synchronization", "Synchronization", &selected_mode, modes, inner)) {
            changed |= dkr::runtime::netplay::experimental::select_mode(
                selected_mode, g_online_synchronization, g_online_experimental_rollback);
        }
        PaddockGap(16.0F);
        if (CurrentOnlineSynchronization() == SynchronizationMode::ExperimentalRollback) {
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
            changed |= OlRange("experimental-window", "Rollback prediction window (frames)", &g_online_rollback_window, 2, 20, inner);
            OlParagraph("Uses this lobby, Quick Join, friend invites, countdown, controls and online-only save. "
                        "Late actual inputs are corrected by replaying owned game state. The previous backends remain available. "
                        "This candidate currently requires US v 1.0 and unmodded gameplay; whole-game release qualification is pending.",
                        14.0F,kOlSoft,inner,true);
#else
            OlParagraph(dkr::runtime::netplay::experimental::runtime_admission_error(SynchronizationMode::ExperimentalRollback),
                        14.0F, kOlError, inner, true);
#endif
            PaddockGap(8.0F);
            if (OlButton("Use previous online mode", OlTone::Plain, inner)) {
                g_online_experimental_rollback = false;
                changed = true;
            }
        } else if (CurrentOnlineSynchronization() == SynchronizationMode::Rollback) {
            changed |= OlRange("rollback-window", "Prediction allowance (frames)", &g_online_rollback_window, 2, 20, inner);
            OlParagraph("The host may predict missing input briefly. Clients follow confirmed host frames; "
                        "this mode does not rewind gameplay. Distance still affects response time.",
                        14.0F, kOlSoft, inner);
        } else {
            OlParagraph("Lockstep suits very stable, low-latency connections. Network variation can cause stalls.",
                        14.0F, kOlError, inner, true);
        }
        PaddockGap(16.0F);
        changed |= OlSwitchRow("##automatic-delay", "Automatic input delay",
                               "Measures the slowest racer before launch. Delay stays fixed during the race.",
                               &g_online_automatic_delay, inner, false);
        PaddockGap(16.0F);
        changed |= OlRange("input-delay", "Input delay (frames)", &g_online_manual_delay, 0, 9, inner,
                           g_online_automatic_delay);
        if (!g_online_automatic_delay) {
            PaddockGap(16.0F);
            if (OlButton("Use automatic input delay", OlTone::Plain, inner)) {
                g_online_automatic_delay = true;
                changed = true;
            }
        }
        PaddockGap(16.0F);
        changed |= OlSwitchRow("##record-replay", "Record deterministic replay", "Keep a replay of the shared race.",
                               &g_online_record_replay, inner, false);
        if (changed) SaveSettings();
    });
}

// ------------------------------------------------------------ online profile

void DrawOlPrivacy(float width) {
    using namespace dkr::runtime::netplay;
    FriendService& service = friend_service();
    DrawOlCard(width, OlPanelLook{}, [&](float inner) {
        OlHeading("Privacy", 24.0F, kOlCream, inner);
        PaddockGap(8.0F);
        bool appear_offline = service.appear_offline();
        if (OlSwitchRow("##appear-offline", "Appear offline",
                        "Friends see you as offline. You can still join with a code.", &appear_offline, inner,
                        false)) {
            std::string error;
            if (!service.set_appear_offline(appear_offline, error)) OlNotify(error);
        }
        bool allow_invites = service.allow_lobby_invites();
        if (OlSwitchRow("##allow-invites", "Allow lobby invites",
                        "Friends can invite you to their lobby with one click.", &allow_invites, inner, true)) {
            std::string error;
            if (!service.set_allow_lobby_invites(allow_invites, error)) OlNotify(error);
        }
        if (OlSwitchRow("##friend-alerts", "Friend online alerts", "Show a notice when a friend comes online.",
                        &g_friend_online_notifications, inner, true)) {
            SaveSettings();
        }
        static const std::vector<std::string> corners{"Top left", "Top right", "Bottom left", "Bottom right"};
        if (OlSelect("alert-position", "Online alert position", &g_friend_online_notification_position, corners,
                     inner)) {
            g_friend_online_notification_position = std::clamp(g_friend_online_notification_position, 0, 3);
            SaveSettings();
        }
        PaddockGap(8.0F);
        const std::string_view note =
            "Races connect PC to PC. Players in the same lobby can see each other\xE2\x80\x99s IP address, "
            "so only share codes with people you trust.";
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float height = OlParagraphHeight(note, 12.5F, inner - 24.0F) + 20.0F;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        PaddockFill(draw, at, {at.x + inner, at.y + height}, PaddockRound(8.0F), PaddockCol(0xFFFFFF, 10U));
        OlParagraphAt(draw, {at.x + 12.0F, at.y + 10.0F}, note, 12.5F, kOlSoft, inner - 24.0F);
        ImGui::Dummy({inner, height});
    });
}

void DrawOlFriendCodes(float width) {
    using namespace dkr::runtime::netplay;
    FriendService& service = friend_service();
    const auto snapshot = service.snapshot();
    DrawOlFeature(width, "Share a Friend Code", "Friend Codes add trusted racers. Quick Join codes open a lobby.",
                  [&](float inner) {
        static const std::vector<std::string> lifetimes{"Permanent", "One use", "Timed"};
        PaddockGap(16.0F);
        OlSelect("code-lifetime", "Code lifetime", &g_friend_invite_lifetime, lifetimes, inner);
        if (g_friend_invite_lifetime == 2) {
            PaddockGap(16.0F);
            OlRange("code-minutes", "Active minutes", &g_friend_invite_minutes, 1, 43200, inner);
        }
        PaddockGap(16.0F);
        if (OlButton("Generate Friend Code", OlTone::Strong, inner)) {
            FriendInviteView invite{};
            std::string error;
            const auto lifetime = static_cast<FriendInviteLifetime>(std::clamp(g_friend_invite_lifetime, 0, 2));
            if (service.create_invite(lifetime, std::chrono::minutes(g_friend_invite_minutes), invite, error)) {
                CopyFriendCodeToClipboard(invite.code);
                g_online_action_status.clear();
                OlNotify("Friend Code generated and copied.");
            } else {
                OlNotify(error);
            }
        }
        const auto now = static_cast<std::uint64_t>(std::time(nullptr));
        for (const FriendInviteView& invite : snapshot->invitations) {
            PaddockGap(16.0F);
            ImGui::PushID(static_cast<int>(invite.invite_id & 0x7FFFFFFFU));
            const bool expired = invite.expires_unix != 0U && invite.expires_unix <= now;
            std::string lifetime = invite.lifetime == FriendInviteLifetime::Permanent ? "Permanent"
                                 : invite.lifetime == FriendInviteLifetime::SingleUse ? "One use" : "Timed";
            if (invite.expires_unix != 0U) {
                lifetime += expired ? " \xC2\xB7 Expired"
                                    : " \xC2\xB7 " + OlFormatRemaining(invite.expires_unix - now) + " remaining";
            }
            OlPanelLook room;
            room.fill = 0x061D2C;
            room.ring = 0xFFFFFF;
            room.ring_alpha = 26U;
            room.ring_width = 1.0F;
            DrawOlCard(inner, room, [&](float card) {
                PaddockText(PaddockMono(15.0F, true, 1.5F, 0.06F), PaddockRgb(kOlInk), invite.code, card);
                PaddockGap(12.0F);
                OlParagraph(lifetime, 15.0F, kOlSoft, card);
                if (!invite.connection_status.empty()) OlParagraph(invite.connection_status, 13.0F, kOlSoft, card);
                PaddockGap(12.0F);
                const ImVec2 row = ImGui::GetCursorScreenPos();
                {
                    const OlDisabled disabled(expired);
                    if (OlButton("Copy code")) {
                        CopyFriendCodeToClipboard(invite.code);
                        g_online_action_status.clear();
                        OlNotify("Friend Code copied.");
                    }
                }
                ImGui::SetCursorScreenPos({row.x + OlButtonWidth("Copy code") + 8.0F, row.y});
                if (OlButton("Revoke", OlTone::Danger)) {
                    std::string error;
                    if (!service.revoke_invite(invite.invite_id, error)) OlNotify(error);
                }
            });
            ImGui::PopID();
        }
        PaddockGap(16.0F);
        if (OlButton("Copy friend connection diagnostics", OlTone::Plain, inner)) {
            ImGui::SetClipboardText(service.diagnostics().c_str());
            OlNotify("Diagnostics copied. No names, codes or addresses included.");
        }
    });
}

// ------------------------------------------------------------ friends

const char* OlInviteStateLabel(dkr::runtime::netplay::FriendLobbyInviteStatus status) {
    using dkr::runtime::netplay::FriendLobbyInviteStatus;
    switch (status) {
    case FriendLobbyInviteStatus::Sent: return "Invitation sent";
    case FriendLobbyInviteStatus::Delivered: return "Invitation delivered";
    case FriendLobbyInviteStatus::Accepted: return "In your lobby";
    case FriendLobbyInviteStatus::Declined: return "Invitation declined";
    case FriendLobbyInviteStatus::Expired: return "Invitation expired";
    case FriendLobbyInviteStatus::Cancelled: return "Invitation cancelled";
    }
    return "Online";
}

struct OlFriendAction {
    std::string label;
    OlTone tone = OlTone::Plain;
    bool disabled = false;
    const char* tooltip = nullptr;
    std::function<void()> run;
};

struct OlFriendRow {
    std::string key;
    std::string name;          // for the avatar
    std::string label;
    std::string status;
    unsigned status_colour = kOlSoft;
    unsigned dot = 0x5D6D76;
    bool dimmed = false;
    std::vector<OlFriendAction> actions;
};

void DrawOlFriendRow(const OlFriendRow& row, float width) {
    // Rows bleed six pixels past the card's padding (margin: 0 -6px).
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 at{origin.x - 6.0F, origin.y};
    const float row_width = width + 12.0F;
    const PaddockType strong = OlRead(15.0F, true, 1.5F);
    const PaddockType caption = OlRead(13.0F, false, 1.5F);
    float actions_width = 0.0F;
    for (const OlFriendAction& action : row.actions) {
        actions_width += (actions_width > 0.0F ? 10.0F : 0.0F) + OlButtonWidth(action.label.c_str(), action.tone);
    }
    const float fixed = 6.0F + 38.0F + 10.0F;
    const bool wrap = !row.actions.empty() && row_width - 12.0F - 48.0F - 140.0F - 10.0F - actions_width < 0.0F;
    const float text_width = std::max(
        wrap ? row_width - fixed - 6.0F : row_width - fixed - 6.0F - (row.actions.empty() ? 0.0F : actions_width + 10.0F),
        1.0F);
    const float text_height = PaddockTextHeight(strong, row.label, text_width) + caption.line;
    const float line = std::max({38.0F, text_height, row.actions.empty() ? 0.0F : 40.0F});
    const float height = 6.0F + line + (wrap ? 10.0F + 40.0F : 0.0F) + 6.0F;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool hovered = ImGui::IsWindowHovered() &&
                         ImGui::IsMouseHoveringRect(at, {at.x + row_width, at.y + height});
    const float hover = PaddockEase(PaddockTween(ImGui::GetID(("row-" + row.key).c_str()), hovered, 0.15F, 0.15F));
    PaddockFill(draw, at, {at.x + row_width, at.y + height}, PaddockRound(14.0F),
                PaddockCol(0xFFFFFF, static_cast<unsigned>(10.0F * hover)));
    const ImVec2 avatar{at.x + 6.0F, at.y + 6.0F + std::round((line - 38.0F) * 0.5F)};
    DrawOlAvatar(draw, avatar, 38.0F, row.name, row.dimmed);
    const ImVec2 dot{avatar.x + 38.0F + 1.0F - 6.0F, avatar.y + 38.0F + 1.0F - 6.0F};
    draw->AddCircleFilled(dot, 8.0F, PaddockCol(kOlPanel), 16);
    draw->AddCircleFilled(dot, 6.0F, PaddockCol(row.dot), 16);
    const float text_x = avatar.x + 38.0F + 10.0F;
    const float text_y = at.y + 6.0F + std::round((line - text_height) * 0.5F);
    PaddockTextStyle title;
    title.colour = PaddockCol(0xFFFFFF);
    const float title_height = PaddockTextAt(draw, strong, {text_x, text_y}, text_width, row.label, title);
    PaddockTextStyle status;
    status.colour = PaddockCol(row.status_colour);
    PaddockTextAt(draw, caption, {text_x, text_y + title_height}, text_width, row.status, status);
    float x = wrap ? at.x + 6.0F : at.x + row_width - 6.0F - actions_width;
    const float y = wrap ? at.y + 6.0F + line + 10.0F : at.y + 6.0F + std::round((line - 40.0F) * 0.5F);
    ImGui::PushID(row.key.c_str());
    for (const OlFriendAction& action : row.actions) {
        ImGui::SetCursorScreenPos({x, y});
        const OlDisabled disabled(action.disabled);
        if (OlButton(action.label.c_str(), action.tone, 0.0F, action.tooltip) && action.run) action.run();
        x += OlButtonWidth(action.label.c_str(), action.tone) + 10.0F;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, height});
}

void OlAddFriend() {
    using namespace dkr::runtime::netplay;
    std::string code = OlTrimmed(g_friend_code_entry);
    std::transform(code.begin(), code.end(), code.begin(),
                   [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    if (!valid_friend_code(code)) {
        OlNotify("Enter an eight-character Friend Code, such as DKR-7K2Q94XM.");
        return;
    }
    std::string error;
    if (friend_service().submit_friend_code(code, error)) {
        g_friend_code_entry[0] = '\0';
        OlNotify("Friend request sent.");
    } else {
        OlNotify(error);
    }
}

void DrawOlFriends(float width, const OnlineFrame& frame) {
    using namespace dkr::runtime::netplay;
    FriendService& service = friend_service();
    DirectSession& online = session();
    const auto snapshot = service.snapshot();
    const bool can_invite = frame.in_lobby && frame.view.host;
    const FriendLobbyAdvertisement lobby = FriendLobbyFromSession(frame.view);
    const bool invite_space = can_invite && lobby.hosting && lobby.players < lobby.maximum_players && !frame.busy;
    std::map<std::string, FriendLobbyInviteView> latest;
    for (const FriendLobbyInviteView& invite : snapshot->outgoing_lobby_invites) {
        if (!latest.contains(invite.friend_identity)) latest[invite.friend_identity] = invite;
    }
    std::size_t online_count = 0U;
    for (const FriendView& racer : snapshot->friends) {
        if (racer.online && !racer.blocked) ++online_count;
    }
    // Search, filter and sort.
    const std::string query = LowerAscii(OlTrimmed(g_friend_search));
    std::vector<FriendView> racers;
    for (const FriendView& racer : snapshot->friends) {
        const bool filter = g_friend_filter == 0 || (g_friend_filter == 3 && racer.blocked) ||
                            (!racer.blocked && ((g_friend_filter == 1 && racer.online) ||
                                                (g_friend_filter == 2 && !racer.online)));
        if (!filter) continue;
        if (!query.empty() &&
            LowerAscii(FriendDisplayLabel(racer) + " " + racer.display_name + " " + racer.identity).find(query) ==
                std::string::npos) {
            continue;
        }
        racers.push_back(racer);
    }
    const auto name_less = [](const FriendView& left, const FriendView& right) {
        return LowerAscii(FriendDisplayLabel(left)) < LowerAscii(FriendDisplayLabel(right));
    };
    if (g_friend_sort == 1) {
        std::stable_sort(racers.begin(), racers.end(), name_less);
    } else if (g_friend_sort == 2) {
        std::stable_sort(racers.begin(), racers.end(),
                         [&](const FriendView& left, const FriendView& right) { return name_less(right, left); });
    } else if (g_friend_sort == 3) {
        std::stable_sort(racers.begin(), racers.end(), [](const FriendView& left, const FriendView& right) {
            return left.last_seen_unix > right.last_seen_unix;
        });
    } else {
        std::stable_sort(racers.begin(), racers.end(), [&](const FriendView& left, const FriendView& right) {
            const int left_rank = left.blocked ? 2 : left.online ? 0 : 1;
            const int right_rank = right.blocked ? 2 : right.online ? 0 : 1;
            if (left_rank != right_rank) return left_rank < right_rank;
            return name_less(left, right);
        });
    }

    std::vector<OlFriendRow> incoming;
    std::vector<OlFriendRow> outgoing;
    for (const FriendRequestView& request : snapshot->requests) {
        OlFriendRow row;
        row.key = "request-" + std::to_string(request.request_id);
        row.name = request.display_name;
        row.label = request.display_name;
        const std::uint64_t id = request.request_id;
        if (request.incoming) {
            row.status = "Wants to be friends";
            row.status_colour = kOlAmber;
            row.dot = kOlAmber;
            row.actions.push_back({"Accept", OlTone::Go, false, nullptr, [&service, id] {
                std::string error;
                if (!service.accept_request(id, error)) OlNotify(error);
            }});
            row.actions.push_back({"\xC3\x97", OlTone::Icon, false, "Decline", [&service, id] {
                std::string error;
                if (!service.reject_request(id, false, error)) OlNotify(error);
            }});
            row.actions.push_back({"Block", OlTone::Plain, false, nullptr, [&service, id] {
                std::string error;
                if (!service.reject_request(id, true, error)) OlNotify(error);
            }});
            incoming.push_back(std::move(row));
        } else {
            row.status = request.delivery_status.empty() ? "Request sent" : request.delivery_status;
            row.dimmed = true;
            row.actions.push_back({"Retry now", OlTone::Plain, !request.can_retry, nullptr, [&service, id] {
                std::string error;
                if (!service.retry_request(id, error)) OlNotify(error);
            }});
            row.actions.push_back({"Cancel request", OlTone::Plain, false, nullptr, [&service, id] {
                std::string error;
                if (!service.reject_request(id, false, error)) OlNotify(error);
            }});
            outgoing.push_back(std::move(row));
        }
    }
    std::vector<OlFriendRow> friends;
    for (const FriendView& racer : racers) {
        OlFriendRow row;
        row.key = "friend-" + racer.identity;
        row.name = racer.display_name;
        row.label = FriendDisplayLabel(racer);
        const std::string identity = racer.identity;
        if (racer.blocked) {
            row.status = "Blocked";
            row.dimmed = true;
            row.actions.push_back({"Unblock", OlTone::Plain, false, nullptr, [&service, identity] {
                std::string error;
                if (!service.unblock_friend(identity, error)) OlNotify(error);
            }});
        } else if (racer.online) {
            row.dot = kOlGo;
            row.status_colour = kOlGoText;
            const auto invite = can_invite ? latest.find(racer.identity) : latest.end();
            row.status = invite != latest.end() ? OlInviteStateLabel(invite->second.status) : "Online";
            const bool pending = invite != latest.end() &&
                                 (invite->second.status == FriendLobbyInviteStatus::Sent ||
                                  invite->second.status == FriendLobbyInviteStatus::Delivered);
            const bool invitable = can_invite && (invite == latest.end() ||
                                                  invite->second.status == FriendLobbyInviteStatus::Declined ||
                                                  invite->second.status == FriendLobbyInviteStatus::Expired ||
                                                  invite->second.status == FriendLobbyInviteStatus::Cancelled);
            if (invitable) {
                row.actions.push_back({"Invite", OlTone::Strong, !invite_space, nullptr, [&, identity] {
                    secure::Key admission{};
                    std::string error;
                    if (!online.create_friend_admission(admission, std::chrono::minutes(5), error)) {
                        OlNotify(error);
                        return;
                    }
                    FriendLobbyInviteView sent{};
                    if (!service.send_lobby_invite(identity, lobby, admission, sent, error)) {
                        online.revoke_friend_admission(admission);
                        OlNotify(error);
                    }
                }});
            } else if (pending) {
                const FriendLobbyInviteView sent = invite->second;
                row.actions.push_back({"Cancel invite", OlTone::Plain, frame.busy, nullptr, [&, sent] {
                    online.revoke_friend_admission(sent.admission);
                    std::string error;
                    if (!service.cancel_lobby_invite(sent.invite_id, error)) OlNotify(error);
                }});
            }
        } else {
            row.status = "Offline";
            row.dimmed = true;
        }
        row.actions.push_back({"Manage", OlTone::Plain, false, nullptr, [identity, racer] {
            g_friend_action_identity = identity;
            const std::size_t length = std::min(racer.nickname.size(), sizeof(g_friend_nickname) - 1U);
            std::memcpy(g_friend_nickname, racer.nickname.data(), length);
            g_friend_nickname[length] = '\0';
            g_online_page.open_manage = true;
        }});
        friends.push_back(std::move(row));
    }

    DrawOlCard(width, OlPanelLook{}, [&](float inner) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            OlHeading("Friends", 24.0F, kOlCream, inner - 120.0F);
            const std::string count = std::to_string(online_count) + " online";
            const PaddockType type = OlRead(13.0F, true, 1.5F);
            PaddockDrawRun(draw, type, {at.x + inner - PaddockMeasure(type, count), at.y + 14.9F},
                           PaddockCol(kOlGoText), count.data(), count.data() + count.size());
        }
        PaddockGap(8.0F);
        if (can_invite) {
            OlParagraph("Invites grant one admission and expire after five minutes. Locking the lobby prevents new "
                        "invitations.", 15.0F, kOlSoft, inner);
        } else {
            OlParagraph("Create a lobby to invite friends with one click.", 13.0F, kOlSoft, inner);
        }
        PaddockGap(18.0F);
        // Toolbar.
        OlInputOptions search;
        search.hint = "Search names or nicknames";
        if (OlInput("##friend-search", nullptr, g_friend_search, sizeof(g_friend_search), inner, search) ==
            OlInputResult::Gamepad) {
            RequestFriendSearchKeyboard();
        }
        PaddockGap(12.0F);
        static const std::vector<std::string> filters{"All friends", "Online", "Offline", "Blocked"};
        static const std::vector<std::string> sorts{"Online first", "Name A-Z", "Name Z-A", "Recently seen"};
        if (OlNarrow(540.0F)) {
            OlSelect("friend-filter", "Friend filter", &g_friend_filter, filters, inner);
            PaddockGap(12.0F);
            OlSelect("friend-sort", "Sort friends", &g_friend_sort, sorts, inner);
        } else {
            const ImVec2 row = ImGui::GetCursorScreenPos();
            const float half = (inner - 12.0F) * 0.5F;
            ImGui::BeginGroup();
            OlSelect("friend-filter", "Friend filter", &g_friend_filter, filters, half);
            ImGui::EndGroup();
            ImGui::SetCursorScreenPos({row.x + half + 12.0F, row.y});
            ImGui::BeginGroup();
            OlSelect("friend-sort", "Sort friends", &g_friend_sort, sorts, half);
            ImGui::EndGroup();
        }
        PaddockGap(18.0F);
        // Groups.
        const std::array<std::pair<const char*, const std::vector<OlFriendRow>*>, 3> groups{{
            {"FRIEND REQUESTS", &incoming}, {"OUTGOING REQUESTS", &outgoing}, {"YOUR FRIENDS", &friends}}};
        bool first_group = true;
        for (const auto& [title, rows] : groups) {
            if (rows->empty()) continue;
            if (!first_group) PaddockGap(14.0F);
            first_group = false;
            PaddockText(OlRead(11.5F, true, 1.0F), PaddockRgb(0x8FB0BF), title, inner);
            PaddockGap(4.0F);
            for (std::size_t index = 0U; index < rows->size(); ++index) {
                if (index > 0U) PaddockGap(2.0F);
                DrawOlFriendRow((*rows)[index], inner);
            }
        }
        if (racers.empty()) {
            OlParagraph("No friends match this search or filter.", 15.0F, kOlSoft, inner);
        }
        // Add a friend.
        PaddockGap(14.0F);
        {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            draw->AddRectFilled(at, {at.x + inner, at.y + 1.0F}, PaddockCol(0xFFFFFF, 20U));
            ImGui::Dummy({inner, 15.0F});
        }
        OlParagraph("Add a friend", 12.0F, kOlSoft, inner, true);
        PaddockGap(6.0F);
        {
            const ImVec2 row = ImGui::GetCursorScreenPos();
            const float add = OlButtonWidth("Add", OlTone::Strong);
            OlInputOptions code;
            code.hint = "DKR-XXXX-XXXX";
            code.mono = true;
            code.flags = ImGuiInputTextFlags_CharsUppercase;
            const OlInputResult result =
                OlInput("##add-friend", nullptr, g_friend_code_entry, sizeof(g_friend_code_entry), inner - add - 8.0F, code);
            if (result == OlInputResult::Gamepad) g_friend_code_keyboard_pending = true;
            if (result == OlInputResult::Entered) OlAddFriend();
            ImGui::SetCursorScreenPos({row.x + inner - add, row.y});
            if (OlButton("Add", OlTone::Strong, add, nullptr, 44.0F)) OlAddFriend();
        }
    });
}

// ------------------------------------------------------------ overlays

void DrawOlOverlayPreview(float width) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 end{at.x + width, at.y + 300.0F};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    OlPanelLook look;
    look.fill = 0x246578;
    look.gradient_degrees = 155.0F;
    look.gradient = {{0.0F, 0x246578}, {0.5F, 0x173A52}, {1.0F, 0x0B202E}};
    look.ring = 0xFFFFFF;
    look.ring_alpha = 41U;
    look.ring_width = 1.0F;
    PaintOlPanel(draw, at, end, look);
    OlParagraphAt(draw, {at.x, at.y + 300.0F * 0.45F}, "In-game preview", 15.0F, 0xC0D9E2, width, false, 1.5F, true);

    const PaddockType strong = PaddockMono(12.0F, true, 1.6F);
    const PaddockType plain = PaddockMono(12.0F, false, 1.6F);
    const bool arrows = plain.font->FindGlyphNoFallback(0x2192) != nullptr;
    const std::string arrow = arrows ? "\xE2\x86\x92" : "->";
    struct Line {
        std::string text;
        bool bold;
    };
    const auto sample = [&](std::vector<Line> lines, int position, unsigned colour, float shift) {
        const float maximum = width - 16.0F - 20.0F;
        float text_width = 0.0F;
        for (const Line& line : lines) {
            text_width = std::max(text_width, PaddockMeasure(line.bold ? strong : plain, line.text));
        }
        text_width = std::min(text_width, maximum);
        float height = 0.0F;
        std::vector<std::vector<PaddockLine>> wrapped;
        for (const Line& line : lines) {
            wrapped.push_back(PaddockWrap(line.bold ? strong : plain, line.text, text_width));
            height += plain.line * static_cast<float>(wrapped.back().size());
        }
        const ImVec2 size{std::ceil(text_width) + 20.0F, height + 20.0F};
        ImVec2 a{position % 2 == 0 ? at.x + 8.0F : end.x - 8.0F - size.x,
                 position < 2 ? at.y + 8.0F : end.y - 8.0F - size.y};
        a.y += shift;
        PaddockFill(draw, a, {a.x + size.x, a.y + size.y}, PaddockRound(4.0F), PaddockCol(0x000000, 191U));
        float y = a.y + 10.0F;
        for (std::size_t index = 0U; index < lines.size(); ++index) {
            PaddockTextStyle style;
            style.colour = PaddockCol(colour);
            PaddockDrawLines(draw, lines[index].bold ? strong : plain, {a.x + 10.0F, y}, text_width, wrapped[index], style);
            y += plain.line * static_cast<float>(wrapped[index].size());
        }
    };
    if (g_network_overlay_enabled) {
        std::vector<Line> lines{{"NETWORK \xC2\xB7 42 ms", true}};
        const int detail = std::clamp(g_network_overlay_detail, 0, 2);
        if (g_network_overlay_single_row) {
            std::string row = lines.front().text;
            if (detail >= 1) row += " Jitter 3 ms \xC2\xB7 Delay 2f";
            if (detail >= 2) row += " Loss 0% \xC2\xB7 Rollback 0 \xC2\xB7 Lead 1.2f";
            lines = {{row, detail == 0}};
        } else {
            if (detail >= 1) lines.push_back({"Jitter 3 ms \xC2\xB7 Delay 2f", false});
            if (detail >= 2) lines.push_back({"Loss 0% \xC2\xB7 Rollback 0 \xC2\xB7 Lead 1.2f", false});
        }
        sample(lines, std::clamp(g_network_overlay_position, 0, 3), 0x91E7D1, 0.0F);
    }
    if (g_controller_input_overlay_enabled) {
        const int position = std::clamp(g_controller_input_overlay_position, 0, 3);
        const float shift = g_network_overlay_enabled ? (position < 2 ? 75.0F : -75.0F) : 0.0F;
        sample({{"CONTROLLER", true}, {"Local: A + " + arrow, false}, {"Player 1 committed: A + " + arrow, false}},
               position, 0xFFE0A0, shift);
    }
    ImGui::Dummy({width, 300.0F});
}

void DrawOlOverlays(float width) {
    DrawOlFeature(width, "Online overlays", "Presentation settings stay available during a session.",
                  [&](float inner) {
        static const std::vector<std::string> corners{"Top left", "Top right", "Bottom left", "Bottom right"};
        static const std::vector<std::string> details{"Compact", "Standard", "Detailed"};
        bool changed = false;
        PaddockGap(16.0F);
        changed |= OlSwitchRow("##network-overlay", "Show networking overlay", "See connection quality while racing.",
                               &g_network_overlay_enabled, inner, false);
        bool after_switch = true;
        if (g_network_overlay_enabled) {
            PaddockGap(16.0F);
            changed |= OlSelect("network-position", "Network overlay position", &g_network_overlay_position, corners,
                                inner);
            PaddockGap(16.0F);
            changed |= OlSelect("network-detail", "Network overlay detail", &g_network_overlay_detail, details, inner);
            PaddockGap(16.0F);
            changed |= OlSwitchRow("##network-single-row", "Network details on one row",
                                   "Use a compact horizontal display.", &g_network_overlay_single_row, inner, false);
        }
        PaddockGap(16.0F);
        changed |= OlSwitchRow("##controller-overlay", "Show controller input overlay",
                               "Compare your local input with the input committed by Player 1.",
                               &g_controller_input_overlay_enabled, inner, after_switch);
        if (g_controller_input_overlay_enabled) {
            PaddockGap(16.0F);
            changed |= OlSelect("controller-position", "Controller overlay position",
                                &g_controller_input_overlay_position, corners, inner);
        }
        PaddockGap(16.0F);
        DrawOlOverlayPreview(inner);
        if (changed) SaveSettings();
    });
}

// ------------------------------------------------------------ connection

void OlRequestConnectionTest() {
    std::string error;
    if (!dkr::runtime::netplay::session().request_connection_test(error)) OlNotify(error);
}

bool OlCanTestConnection(const OnlineFrame& frame) {
    using dkr::runtime::netplay::ConnectionState;
    return frame.view.host && !frame.busy &&
           (frame.view.state == ConnectionState::Hosting || frame.view.state == ConnectionState::Lobby);
}

bool OlHasTestResults(const OnlineFrame& frame) {
    return std::any_of(frame.view.connection_test_results.begin(), frame.view.connection_test_results.end(),
                       [](const auto& result) { return result.valid; });
}

std::string OlTestCountdown(const OnlineFrame& frame) {
    return std::to_string((frame.view.connection_test_remaining_ms + 999U) / 1000U) + " seconds";
}

void DrawOlConnection(float width, const OnlineFrame& frame) {
    using namespace dkr::runtime::netplay;
    const SessionView& view = frame.view;
    const bool rollback = view.room.rules.synchronization == SynchronizationMode::Rollback;
    const bool running = view.state == ConnectionState::Running;
    DrawOlFeature(width, "Connection status", "Quick Join \xC2\xB7 Host is Player 1.", [&](float inner) {
        PaddockGap(16.0F);
        char loss[16]{};
        std::snprintf(loss, sizeof(loss), view.network_loss_percent == std::floor(view.network_loss_percent)
                                              ? "%.0f%%" : "%.1f%%",
                      static_cast<double>(view.network_loss_percent));
        const std::string measuring = "\xE2\x80\x94";
        const std::array<std::pair<const char*, std::string>, 6> metrics{{
            {"Synchronization", synchronization_name(view.room.rules.synchronization)},
            {"Input delay", std::to_string(view.input_delay_frames) +
                                (view.input_delay_frames == 1U ? " frame" : " frames")},
            {"App RTT", view.network_rtt_ms > 0U ? std::to_string(view.network_rtt_ms) + " ms" : measuring},
            {"Jitter", view.network_rtt_ms > 0U ? std::to_string(view.network_jitter_ms) + " ms" : measuring},
            {"Expired probes", loss},
            {"Input stalls", std::to_string(view.input_stalls)},
        }};
        const int columns = OlNarrow(540.0F) ? 2 : 3;
        const float cell = (inner - 12.0F * (columns - 1)) / columns;
        const PaddockType caption = OlRead(12.5F, false, 1.5F);
        const PaddockType value = OlRead(20.0F, true, 1.5F);
        const float cell_height = 16.0F + caption.line + 8.0F + value.line + 16.0F;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (std::size_t index = 0U; index < metrics.size(); ++index) {
            const int column = static_cast<int>(index) % columns;
            const int row = static_cast<int>(index) / columns;
            const ImVec2 a{at.x + column * (cell + 12.0F), at.y + row * (cell_height + 12.0F)};
            PaddockFill(draw, a, {a.x + cell, a.y + cell_height}, PaddockRound(10.0F), PaddockCol(0x061D2C));
            PaddockTextStyle label;
            label.colour = PaddockCol(kOlSoft);
            PaddockTextAt(draw, caption, {a.x + 16.0F, a.y + 16.0F}, cell - 32.0F, metrics[index].first, label);
            PaddockTextStyle number;
            number.colour = PaddockCol(kOlInk);
            PaddockTextAt(draw, value, {a.x + 16.0F, a.y + 16.0F + caption.line + 8.0F}, cell - 32.0F,
                          metrics[index].second, number);
        }
        const int rows = (static_cast<int>(metrics.size()) + columns - 1) / columns;
        ImGui::Dummy({inner, rows * cell_height + (rows - 1) * 12.0F});
        PaddockGap(16.0F);
        std::string summary = "Waiting for the race to start.";
        if (running) {
            if (view.rollback_certified) {
                summary = "Determinism verified";
                if (view.last_verified_frame > 0U) {
                    summary += " through frame " + std::to_string(view.last_verified_frame);
                }
                if (rollback) {
                    char lead[48]{};
                    std::snprintf(lead, sizeof(lead), " \xC2\xB7 Prediction lead %.1f frames",
                                  static_cast<double>(rollback_metrics().frames_ahead));
                    summary += lead;
                }
                summary += " \xC2\xB7 Corrections " + std::to_string(view.authoritative_corrections);
            } else {
                summary = "Waiting for Player 1 authority validation\xE2\x80\xA6";
            }
        }
        OlParagraph(summary, 15.0F, kOlSoft, inner);
        if (running && view.recovering) {
            PaddockGap(16.0F);
            OlParagraph("Connection variation detected. DKR-R is preparing a synchronized recovery point; keep racing.",
                        14.0F, kOlAmber, inner, true);
        }
        if (!view.room.rules.automatic_input_delay && view.network_rtt_ms > 0U) {
            const auto recommended = host_authoritative_input_delay_frames(
                view.network_rtt_ms, view.network_jitter_ms, view.network_loss_percent,
                view.method == ConnectionMethod::Lan);
            if (view.input_delay_frames < recommended) {
                PaddockGap(16.0F);
                OlParagraph("Manual delay is below the current route estimate (" + std::to_string(recommended) +
                                " frames). Consider automatic input delay in Host settings before creating the "
                                "next lobby.",
                            14.0F, kOlAmber, inner, true);
            }
        }
        if (view.room.rules.record_replay) {
            PaddockGap(16.0F);
            OlParagraph("Deterministic replay recording enabled.", 15.0F, kOlInk, inner);
        }
        if (view.host) {
            PaddockGap(16.0F);
            const OlDisabled disabled(!OlCanTestConnection(frame));
            if (OlButton("Test session connection", OlTone::Plain, inner)) OlRequestConnectionTest();
        }
        if (view.connection_test_active) {
            PaddockGap(16.0F);
            OlParagraph("Testing real session load\xE2\x80\xA6 " + OlTestCountdown(frame), 15.0F, kOlInk, inner);
        }
        if (OlHasTestResults(frame)) {
            PaddockGap(16.0F);
            if (OlButton("View test results", OlTone::Plain, inner)) g_online_page.open_results = true;
        }
    });
}

// ------------------------------------------------------------ lobby

// A pulsing HOSTING / JOINED pill (.ol-live).
float DrawOlLive(ImDrawList* draw, ImVec2 at, std::string_view text) {
    const PaddockType type = OlSignType(15.0F, 1.5F, 0.08F);
    const float width = std::ceil(10.0F + 8.0F + 8.0F + PaddockMeasure(type, text) - type.tracking + 12.0F);
    const float height = type.line + 8.0F;
    PaddockFill(draw, at, {at.x + width, at.y + height}, PaddockRound(height * 0.5F), PaddockCol(0x104E43));
    const ImVec2 dot{at.x + 10.0F + 4.0F, at.y + height * 0.5F};
    const float pulse = static_cast<float>(std::fmod(PaddockClock(), 1.6) / 1.6);
    const float ease_out = 1.0F - (1.0F - pulse) * (1.0F - pulse);
    draw->AddCircleFilled(dot, 4.0F + 8.0F * ease_out, PaddockCol(kOlGo, static_cast<unsigned>(178.0F * (1.0F - ease_out))), 20);
    draw->AddCircleFilled(dot, 4.0F, PaddockCol(kOlGo), 16);
    PaddockDrawRun(draw, type, {at.x + 26.0F, at.y + 4.0F}, PaddockCol(0xB4F4DD), text.data(), text.data() + text.size());
    return height;
}

// The code plate: dark on cream, like a racer's number board.
constexpr float kOlPlateWidth = 16.0F * 2.0F + 5.0F * 50.0F + 4.0F * 6.0F;

float OlPlateHeight(bool host) {
    return 14.0F + OlSignType(15.0F).line + 10.0F + 58.0F + (host ? 10.0F + 40.0F : 0.0F) + 12.0F;
}

void DrawOlPlate(ImVec2 at, float width, const OnlineFrame& frame) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool host = frame.view.host;
    const float height = OlPlateHeight(host);
    const ImVec2 end{at.x + width, at.y + height};
    PaddockFill(draw, {at.x, at.y + 4.0F}, {end.x, end.y + 4.0F}, PaddockRound(12.0F), PaddockCol(0x9C7A2C));
    PaddockFill(draw, at, end, PaddockRound(12.0F), PaddockCol(kOlCream));
    PaddockStroke(draw, at, end, PaddockRound(12.0F), PaddockCol(0xFFFFFF, 128U), 2.0F);
    const PaddockType label = OlSignType(15.0F, 1.5F, 0.08F);
    PaddockDrawRun(draw, label, {at.x + 16.0F, at.y + 14.0F}, PaddockCol(0xA3341F), "LOBBY CODE", "LOBBY CODE" + 10);
    const std::string code = NormalizeOnlineInvite(frame.view.invite).substr(0U, 5U);
    OlTileLook look;
    look.height = 58.0F;
    look.gap = 6.0F;
    look.plate = true;
    look.tile_width = OlNarrow(440.0F) ? 0.0F : 50.0F;
    DrawOlCodeTiles(draw, {at.x + 16.0F, at.y + 14.0F + label.line + 10.0F}, width - 32.0F, code, look);
    if (host) {
        const float y = at.y + 14.0F + label.line + 10.0F + 58.0F + 10.0F;
        ImGui::SetCursorScreenPos({at.x + 16.0F, y});
        if (OlButton("Copy code", OlTone::PlateStrong)) {
            if (CopyOnlineInviteToClipboard(frame.view.invite)) {
                g_online_action_status.clear();
                OlNotify("Code " + code + " copied. Share it only with friends.");
            }
        }
        ImGui::SetCursorScreenPos({at.x + 16.0F + OlButtonWidth("Copy code") + 8.0F, y});
        const OlDisabled disabled(frame.busy);
        if (OlButton("New code", OlTone::PlatePlain, 0.0F, "Stops the old code working. Racers already here stay.")) {
            std::string error;
            if (dkr::runtime::netplay::session().revoke_invitation(error)) {
                OlNotify("New code ready. The old code no longer works; racers already here stay.");
            } else {
                OlNotify(error);
            }
        }
    }
}

// Items in a wrapping flex row; returns the rows' total height.
struct OlMetaItem {
    float width;
    std::function<void(ImVec2)> draw;
};

float DrawOlMetaRow(ImVec2 at, float width, float line, const std::vector<OlMetaItem>& items) {
    float x = at.x;
    float y = at.y;
    for (const OlMetaItem& item : items) {
        if (x > at.x && x + item.width > at.x + width) {
            x = at.x;
            y += line + 4.0F;
        }
        item.draw({x, y});
        x += item.width + 14.0F;
    }
    return y + line - at.y;
}

void DrawOlGrid(float width, const OnlineFrame& frame) {
    using namespace dkr::runtime::netplay;
    const SessionView& view = frame.view;
    const int maximum = std::clamp<int>(view.room.rules.maximum_players, 2, static_cast<int>(view.room.players.size()));
    const int columns = OlNarrow(760.0F) ? 1 : 2;
    const float cell = (width - 14.0F * (columns - 1)) / columns;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const PaddockType strong = OlRead(16.0F, true, 1.5F);
    const PaddockType meta = OlRead(13.0F, false, 1.5F);
    const PaddockType meta_bold = OlRead(13.0F, true, 1.5F);
    // Measure every slot, then lay rows out at their tallest.
    std::vector<float> heights(static_cast<std::size_t>(maximum), 76.0F);
    const auto body_width = [&](const Player& player, bool removable) {
        return cell - 54.0F - 12.0F - (player.occupied ? 38.0F + 12.0F : 0.0F) - 10.0F -
               (removable ? 40.0F + 12.0F : 0.0F);
    };
    const auto meta_items = [&](std::size_t slot) {
        const Player& player = view.room.players[slot];
        const bool you = slot == view.local_slot;
        std::vector<OlMetaItem> items;
        const bool loaded = player.loaded;
        const char* ready_text = loaded ? "Loaded" : player.ready ? "Ready" : "Not ready yet";
        const PaddockType& ready_type = player.ready ? meta_bold : meta;
        items.push_back({13.0F + PaddockMeasure(ready_type, ready_text), [&, ready_text, player](ImVec2 at) {
            draw->AddCircleFilled({at.x + 3.5F, at.y + meta.line * 0.5F}, 3.5F,
                                  PaddockCol(player.ready ? kOlGo : kOlStop), 12);
            PaddockDrawRun(draw, player.ready ? meta_bold : meta, {at.x + 13.0F, at.y},
                           PaddockCol(player.ready ? kOlGoText : kOlSoft), ready_text,
                           ready_text + std::strlen(ready_text));
        }});
        const char* save_text = view.online_save_ready[slot] ? "Online save verified"
                                                             : "Verifying online save\xE2\x80\xA6";
        items.push_back({PaddockMeasure(meta, save_text), [&, save_text](ImVec2 at) {
            PaddockDrawRun(draw, meta, at, PaddockCol(kOlSoft), save_text, save_text + std::strlen(save_text));
        }});
        if (you) {
            items.push_back({PaddockMeasure(meta, "This PC"), [&](ImVec2 at) {
                PaddockDrawRun(draw, meta, at, PaddockCol(kOlSoft), "This PC", "This PC" + 7);
            }});
        } else {
            const std::string ping = std::to_string(player.ping_ms) + " ms";
            items.push_back({19.0F + PaddockMeasure(meta, ping), [&, player](ImVec2 at) {
                DrawOlPing(draw, at, meta.line, player.ping_ms);
                if (ImGui::IsWindowHovered() &&
                    ImGui::IsMouseHoveringRect(at, {at.x + 19.0F + 60.0F, at.y + meta.line})) {
                    ImGui::SetTooltip("%s \xC2\xB7 %u ms round trip \xC2\xB7 jitter %u ms \xC2\xB7 %.1f%% expired probes",
                                      OnlineRouteName(player.route), static_cast<unsigned>(player.ping_ms),
                                      static_cast<unsigned>(player.jitter_ms),
                                      static_cast<double>(player.packet_loss_percent));
                }
            }});
        }
        return items;
    };
    const auto measure_meta = [&](const std::vector<OlMetaItem>& items, float width_available) {
        float x = 0.0F;
        int lines = 1;
        for (const OlMetaItem& item : items) {
            if (x > 0.0F && x + item.width > width_available) {
                x = 0.0F;
                ++lines;
            }
            x += item.width + 14.0F;
        }
        return lines * meta.line + (lines - 1) * 4.0F;
    };
    for (int slot = 0; slot < maximum; ++slot) {
        const Player& player = view.room.players[static_cast<std::size_t>(slot)];
        if (!player.occupied) continue;
        const bool removable = view.host && static_cast<std::size_t>(slot) != view.local_slot;
        const float body = body_width(player, removable);
        const float body_height = strong.line + 3.0F + measure_meta(meta_items(static_cast<std::size_t>(slot)), body);
        heights[static_cast<std::size_t>(slot)] = std::max(76.0F, body_height + 20.0F);
    }
    float y = origin.y;
    for (int row = 0; row * columns < maximum; ++row) {
        float row_height = 0.0F;
        for (int column = 0; column < columns && row * columns + column < maximum; ++column) {
            row_height = std::max(row_height, heights[static_cast<std::size_t>(row * columns + column)]);
        }
        for (int column = 0; column < columns && row * columns + column < maximum; ++column) {
            const std::size_t slot = static_cast<std::size_t>(row * columns + column);
            const Player& player = view.room.players[slot];
            const bool you = slot == view.local_slot && player.occupied;
            const ImVec2 a{origin.x + column * (cell + 14.0F), y};
            const ImVec2 b{a.x + cell, a.y + row_height};
            ImGui::PushID(static_cast<int>(slot));
            const float ready = PaddockEase(PaddockTween(ImGui::GetID("ready"), player.occupied && player.ready, 0.2F, 0.2F));
            // Slot, its ring, then the number block over the ring's left side.
            if (player.occupied) {
                PaddockFill(draw, a, b, PaddockRound(10.0F), PaddockCol(you ? 0x0D3550U : kOlPanel));
                PaddockStroke(draw, a, b, PaddockRound(10.0F), PaddockMix(PaddockRgb(kOlRing), PaddockRgb(kOlGo), ready), 2.0F);
            } else {
                PaddockStroke(draw, a, b, PaddockRound(10.0F), PaddockCol(0x1A3B4D), 2.0F);
            }
            PaddockFill(draw, a, {a.x + 54.0F, b.y}, {10.0F, 0.0F, 0.0F, 10.0F},
                        player.occupied ? PaddockMix(PaddockRgb(0x174A63), PaddockRgb(0x10705E), ready)
                                        : PaddockCol(0xFFFFFF, 8U));
            const std::string number = "P" + std::to_string(slot + 1U);
            ImFont* jumpman = g_font_heading != nullptr ? g_font_heading : ImGui::GetFont();
            const ImVec2 number_size = jumpman->CalcTextSizeA(26.0F, FLT_MAX, 0.0F, number.c_str());
            const ImVec2 number_at{std::round(a.x + (54.0F - number_size.x) * 0.5F),
                                   std::round(a.y + (row_height - number_size.y) * 0.5F)};
            if (player.occupied) {
                draw->AddText(jumpman, 26.0F, {number_at.x, number_at.y + 2.0F}, PaddockCol(0x031623), number.c_str());
            }
            draw->AddText(jumpman, 26.0F, number_at, PaddockCol(player.occupied ? kOlCream : 0x4F7D92U), number.c_str());
            float x = a.x + 54.0F + 12.0F;
            if (!player.occupied) {
                const char* empty = view.host ? (view.lobby_locked ? "Lobby locked" : "Waiting for a friend")
                                              : "Waiting for racers";
                const float body = strong.line + 3.0F + OlRead(13.0F).line;
                const float top = a.y + std::round((row_height - body) * 0.5F);
                PaddockDrawRun(draw, strong, {x, top}, PaddockCol(0x8FB0BF), "Open slot", "Open slot" + 9);
                PaddockDrawRun(draw, OlRead(13.0F), {x, top + strong.line + 3.0F}, PaddockCol(0x7F9DAC), empty,
                               empty + std::strlen(empty));
                ImGui::PopID();
                continue;
            }
            DrawOlAvatar(draw, {x, a.y + std::round((row_height - 38.0F) * 0.5F)}, 38.0F, player.display_name);
            x += 38.0F + 12.0F;
            const bool removable = view.host && !you;
            const float body = body_width(player, removable);
            const auto items = meta_items(slot);
            const float body_height = strong.line + 3.0F + measure_meta(items, body);
            const float top = a.y + std::round((row_height - body_height) * 0.5F);
            // Name and its tags.
            const std::string name = PaddockEllipsize(strong, player.display_name, std::max(body - 90.0F, 40.0F));
            PaddockDrawRun(draw, strong, {x, top}, PaddockCol(0xFFFFFF), name.data(), name.data() + name.size());
            float tag_x = x + PaddockMeasure(strong, name) + 6.0F;
            const float tag_y = top + std::round((strong.line - 17.0F) * 0.5F);
            if (player.host) tag_x += DrawOlTag(draw, {tag_x, tag_y}, "HOST", false).x + 6.0F;
            if (you) DrawOlTag(draw, {tag_x, tag_y}, "YOU", true);
            DrawOlMetaRow({x, top + strong.line + 3.0F}, body, meta.line, items);
            if (removable) {
                ImGui::SetCursorScreenPos({b.x - 10.0F - 40.0F, a.y + std::round((row_height - 40.0F) * 0.5F)});
                const std::string tooltip = "Remove " + player.display_name;
                const OlDisabled disabled(frame.busy);
                if (OlButton("\xC3\x97##remove", OlTone::Icon, 40.0F, tooltip.c_str())) {
                    std::string error;
                    if (!dkr::runtime::netplay::session().kick_player(static_cast<std::uint8_t>(slot), error)) {
                        OlNotify(error);
                    }
                }
            }
            ImGui::PopID();
        }
        y += row_height + 12.0F;
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, y - 12.0F - origin.y});
}

void DrawOlGate(float width, const OnlineFrame& frame) {
    using namespace dkr::runtime::netplay;
    const SessionView& view = frame.view;
    DirectSession& online = session();
    OlPanelLook look;
    look.radii = PaddockRound(10.0F);
    look.fill = 0x3A2C14;
    look.ring = 0xC08A2C;
    look.ring_width = 2.0F;
    PaddockBox box(width, {16.0F, 14.0F});
    const float inner = box.Inner();
    PaddockText(OlSignType(20.0F, 1.5F), PaddockRgb(kOlAmber), "Wants to join", inner, true);
    PaddockGap(6.0F);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (const PendingJoinView& pending : view.pending_joins) {
        ImGui::PushID(static_cast<int>(pending.request_id & 0x7FFFFFFFU));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const std::string reason = !pending.compatible ? pending.compatibility
                                 : frame.has_space ? "Used your lobby code" : "Lobby is full";
        struct Button {
            const char* label;
            OlTone tone;
            bool disabled;
        };
        const std::array<Button, 3> buttons{{
            {"Let in", OlTone::Go, !frame.has_space || !pending.compatible || view.lobby_locked},
            {"Decline", OlTone::Plain, frame.busy},
            {"Block for session", OlTone::Plain, frame.busy},
        }};
        float buttons_width = 0.0F;
        for (const Button& button : buttons) buttons_width += OlButtonWidth(button.label) + 10.0F;
        const bool wrap = inner - 38.0F - 10.0F - 140.0F - 10.0F - buttons_width < 0.0F;
        const float name_width = wrap ? inner - 48.0F : inner - 48.0F - buttons_width;
        const PaddockType strong = OlRead(16.0F, true, 1.5F);
        const PaddockType caption = OlRead(13.0F, false, 1.5F);
        const float name_height = strong.line + PaddockTextHeight(caption, reason, name_width);
        const float line = std::max({38.0F, name_height, 40.0F});
        DrawOlAvatar(draw, {at.x, at.y + 6.0F + std::round((line - 38.0F) * 0.5F)}, 38.0F, pending.display_name);
        const float text_y = at.y + 6.0F + std::round((line - name_height) * 0.5F);
        PaddockDrawRun(draw, strong, {at.x + 48.0F, text_y}, PaddockCol(0xFFFFFF), pending.display_name.data(),
                       pending.display_name.data() + pending.display_name.size());
        PaddockTextStyle style;
        style.colour = PaddockCol(pending.compatible ? 0xE3C998U : kOlError);
        PaddockTextAt(draw, caption, {at.x + 48.0F, text_y + strong.line}, name_width, reason, style);
        float x = wrap ? at.x : at.x + inner - buttons_width + 10.0F;
        const float y = wrap ? at.y + 6.0F + line + 10.0F : at.y + 6.0F + std::round((line - 40.0F) * 0.5F);
        const float stretched = OlNarrow(540.0F) && wrap ? (inner - 20.0F) / 3.0F : 0.0F;
        for (std::size_t index = 0U; index < buttons.size(); ++index) {
            ImGui::SetCursorScreenPos({x, y});
            const OlDisabled disabled(buttons[index].disabled);
            if (OlButton(buttons[index].label, buttons[index].tone, stretched)) {
                std::string error;
                const bool done = index == 0U ? online.approve_join(pending.request_id, error)
                                              : online.reject_join(pending.request_id, index == 2U, error);
                if (!done) OlNotify(error);
                else if (index == 0U) OlNotify(pending.display_name + " joined your lobby.");
            }
            x += (stretched > 0.0F ? stretched : OlButtonWidth(buttons[index].label)) + 10.0F;
        }
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({inner, 6.0F + line + (wrap ? 50.0F : 0.0F) + 6.0F});
        ImGui::PopID();
    }
    box.End([&](ImDrawList* target, ImVec2 a, ImVec2 b) { PaintOlPanel(target, a, b, look); });
}

void DrawOlStartBar(float width, const OnlineFrame& frame) {
    using namespace dkr::runtime::netplay;
    const SessionView& view = frame.view;
    DirectSession& online = session();
    const int maximum = std::clamp<int>(view.room.rules.maximum_players, 2, static_cast<int>(view.room.players.size()));
    std::size_t total = 0U;
    std::size_t ready = 0U;
    bool saves_ready = true;
    std::vector<int> lights;
    for (int slot = 0; slot < maximum; ++slot) {
        const Player& player = view.room.players[static_cast<std::size_t>(slot)];
        lights.push_back(!player.occupied ? 0 : player.ready ? 2 : 1);
        if (!player.occupied) continue;
        ++total;
        if (player.ready) ++ready;
        saves_ready = saves_ready && view.online_save_ready[static_cast<std::size_t>(slot)];
    }
    const Player* local = view.local_slot < view.room.players.size() &&
                                  view.room.players[view.local_slot].occupied
                              ? &view.room.players[view.local_slot] : nullptr;
    const bool local_ready = local != nullptr && local->ready;
    const std::size_t profile = dkr::runtime::platform::online_input_profile();
    const bool input_available = dkr::runtime::platform::player_controller_status(profile).connected ||
                                 dkr::runtime::input::keyboard_player() == static_cast<int>(profile);
    std::string hint;
    if (view.owned_match_ending) {
        hint = "Ending the previous match and verifying its online save. Please wait...";
    } else if (!local_ready && !input_available) {
        hint = "Connect a controller or assign the keyboard to Player " + std::to_string(profile + 1U) +
               " before readying.";
    } else if (total < 2U) {
        hint = view.host ? "You need at least one friend to start." : "Waiting for more racers.";
    } else if (ready < total) {
        hint = std::to_string(ready) + " of " + std::to_string(total) + " racers ready";
    } else {
        hint = view.host ? "Everyone is ready. Start when you like!" : "Everyone is ready. The host is starting\xE2\x80\xA6";
    }
    OlPanelLook look;
    look.radii = PaddockRound(10.0F);
    look.fill = 0x061D2C;
    look.ring = 0xFFFFFF;
    look.ring_alpha = 18U;
    look.ring_width = 1.0F;
    PaddockBox box(width, {16.0F, 14.0F});
    const float inner = box.Inner();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float lights_width = 8.0F * 2.0F + 18.0F * lights.size() + 8.0F * (lights.size() - 1U);
        const float hint_width = inner - lights_width - 16.0F;
        const float hint_height = OlParagraphHeight(hint, 16.0F, hint_width, true);
        const float row = std::max(34.0F, hint_height);
        DrawOlLights(draw, {at.x, at.y + std::round((row - 34.0F) * 0.5F)}, lights);
        OlParagraphAt(draw, {at.x + lights_width + 16.0F, at.y + std::round((row - hint_height) * 0.5F)}, hint, 16.0F,
                      kOlCream, hint_width, true);
        ImGui::Dummy({inner, row});
    }
    PaddockGap(14.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool pair = view.host && inner >= 190.0F * 2.0F + 10.0F;
        const float button = view.host && pair ? (inner - 10.0F) * 0.5F : inner;
        const bool can_ready = !frame.busy && view.local_online_save_ready && (local_ready || input_available) &&
                               local != nullptr;
        if (OlCta(local_ready ? "NOT READY" : "READY TO RACE", local_ready ? OlCtaColour::Blue : OlCtaColour::Green,
                  button, !can_ready)) {
            std::string error;
            if (!online.set_ready(!local_ready, error)) OlNotify(error);
        }
        if (view.host) {
            ImGui::SetCursorScreenPos(pair ? ImVec2{at.x + button + 10.0F, at.y} : ImVec2{at.x, at.y + 52.0F + 10.0F});
            const bool can_start = !frame.busy && total >= 2U && ready == total && saves_ready;
            if (OlCta("START RACE", OlCtaColour::Red, button, !can_start)) {
                std::string error;
                if (!online.request_start(error)) OlNotify(error);
            }
        }
    }
    box.End([&](ImDrawList* target, ImVec2 a, ImVec2 b) { PaintOlPanel(target, a, b, look); });
}

void DrawOlCountdown(ImVec2 a, ImVec2 b, const OnlineFrame& frame) {
    const auto& view = frame.view;
    if (!view.launch_countdown_active || view.launch_countdown_remaining_ms == 0U) return;
    const std::uint32_t second = std::clamp((view.launch_countdown_remaining_ms + 999U) / 1000U, 1U, 5U);
    if (second != g_online_page.countdown_second) {
        g_online_page.countdown_second = second;
        g_online_page.countdown_changed_at = PaddockClock();
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockFill(draw, a, b, {12.0F, 28.0F, 12.0F, 12.0F}, PaddockCol(0x03101A, 235U));
    const bool narrow = OlNarrow(440.0F);
    const PaddockType label = OlSignType(24.0F, 1.5F, 0.06F);
    ImFont* font = g_font_title != nullptr ? g_font_title : ImGui::GetFont();
    const float number_size = narrow ? 110.0F : 150.0F;
    const std::string number = std::to_string(second);
    const ImVec2 extent = font->CalcTextSizeA(number_size, FLT_MAX, 0.0F, number.c_str());
    const float lights_height = 28.0F + 24.0F;
    const float total = label.line + 12.0F + extent.y + 12.0F + lights_height;
    float y = std::round((a.y + b.y - total) * 0.5F);
    const std::string_view heading = "DKR-R ONLINE STARTING IN";
    const float heading_width = PaddockMeasure(label, heading) - label.tracking;
    PaddockDrawRun(draw, label, {std::round((a.x + b.x - heading_width) * 0.5F), y}, PaddockCol(kOlCream),
                   heading.data(), heading.data() + heading.size());
    y += label.line + 12.0F;
    const int first = draw->VtxBuffer.Size;
    const ImVec2 at{std::round((a.x + b.x - extent.x) * 0.5F), y};
    const float stroke = number_size / 30.0F;
    for (int step = 0; step < 16; ++step) {
        const float angle = static_cast<float>(step) * 3.14159265F / 8.0F;
        draw->AddText(font, number_size, {at.x + std::cos(angle) * stroke, at.y + std::sin(angle) * stroke},
                      PaddockCol(0xFFAB14), number.c_str());
    }
    draw->AddText(font, number_size, at, PaddockCol(0xE82E21), number.c_str());
    // Each new second pops in.
    const float pop = PaddockEase(static_cast<float>(PaddockClock() - g_online_page.countdown_changed_at) / 0.45F);
    if (pop < 1.0F) {
        const float scale = 1.5F - 0.5F * pop;
        const ImVec2 centre{at.x + extent.x * 0.5F, at.y + extent.y * 0.5F};
        for (int index = first; index < draw->VtxBuffer.Size; ++index) {
            ImVec2& p = draw->VtxBuffer[index].pos;
            p = {centre.x + (p.x - centre.x) * scale, centre.y + (p.y - centre.y) * scale};
        }
        OlFade(draw, first, pop);
    }
    y += extent.y + 12.0F;
    std::vector<int> lights;
    for (std::uint32_t light = 0U; light < 5U; ++light) lights.push_back(light < 5U - second ? 3 : 0);
    const float lights_width = 12.0F * 2.0F + 28.0F * 5.0F + 12.0F * 4.0F;
    DrawOlLights(draw, {std::round((a.x + b.x - lights_width) * 0.5F), y}, lights, 28.0F, 12.0F);
    // A partially clipped lobby countdown still owns this frame. Requiring
    // the entire panel inside the clip rectangle also drew the fallback popup
    // on shorter/scrolled windows, leaving two countdowns on screen.
    // Keep the fallback when the lobby countdown is completely off screen.
    const ImRect clip(draw->GetClipRectMin(), draw->GetClipRectMax());
    if (clip.Overlaps(ImRect(a, b))) g_online_countdown_panel_frame = ImGui::GetFrameCount();
}

void DrawOlLobby(float width, const OnlineFrame& frame) {
    using namespace dkr::runtime::netplay;
    const SessionView& view = frame.view;
    const bool host = view.host;
    const bool launched = view.state == ConnectionState::Loading || view.state == ConnectionState::Running;
    const int maximum = std::clamp<int>(view.room.rules.maximum_players, 2, static_cast<int>(view.room.players.size()));
    std::size_t total = 0U;
    std::string host_name = "Host";
    for (int slot = 0; slot < maximum; ++slot) {
        const Player& player = view.room.players[static_cast<std::size_t>(slot)];
        if (!player.occupied) continue;
        ++total;
        if (player.host) host_name = player.display_name;
    }
    OlPanelLook look;
    look.radii = {12.0F, 28.0F, 12.0F, 12.0F};
    look.fill = 0x0B304B;
    look.border = 0x3581A2;
    look.border_width = 2.0F;
    look.ring_width = 0.0F;
    look.drop = 0x041822;
    look.drop_offset = 5.0F;
    const float pad = OlNarrow(440.0F) ? 16.0F : 24.0F;
    PaddockBox box(width, {pad + 2.0F, (OlNarrow(440.0F) ? 20.0F : 24.0F) + 2.0F});
    const float inner = box.Inner();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    int chunk = 0;
    // Title and code plate.
    {
        const OlEnter enter(chunk++);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool narrow = OlNarrow(440.0F);
        const float plate_width = narrow ? inner : kOlPlateWidth;
        const bool side = !narrow && inner - plate_width - 20.0F >= 260.0F;
        const float title_width = side ? inner - plate_width - 20.0F : inner;
        ImGui::BeginGroup();
        ImGui::Dummy({title_width, DrawOlLive(draw, at, host ? "HOSTING" : "JOINED")});
        PaddockGap(10.0F);
        const std::string title = host ? view.room.name : host_name + "\xE2\x80\x99s lobby";
        PaddockText(OlSignType(narrow ? 30.0F : 38.0F, 1.1F), PaddockRgb(kOlCream), title, title_width, true,
                    PaddockRgb(0x031623));
        PaddockGap(4.0F);
        const std::string line = host
            ? std::to_string(total) + " of " + std::to_string(maximum) + " racers. " +
                  (view.lobby_locked ? "Locked: nobody new can ask to join." : "Friends can join with the code.")
            : std::string("The host starts the race when everyone is ready.");
        OlParagraph(line, 15.0F, kOlSoft, title_width);
        ImGui::EndGroup();
        const float title_height = ImGui::GetItemRectSize().y;
        const ImVec2 plate_at = side ? ImVec2{at.x + inner - plate_width, at.y} : ImVec2{at.x, at.y + title_height + 20.0F};
        DrawOlPlate(plate_at, plate_width, frame);
        const float height = side ? std::max(title_height, OlPlateHeight(host) + 4.0F)
                                  : title_height + 20.0F + OlPlateHeight(host) + 4.0F;
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({inner, height});
    }
    PaddockGap(20.0F);
    {
        const OlEnter enter(chunk++);
        OlParagraph(OlRulesLine(view.room.rules), 15.0F, kOlSoft, inner);
    }
    PaddockGap(13.0F);
    if (host && !view.pending_joins.empty()) {
        PaddockGap(20.0F);
        const OlEnter enter(chunk++);
        DrawOlGate(inner, frame);
    }
    PaddockGap(20.0F);
    {
        const OlEnter enter(chunk++);
        DrawOlGrid(inner, frame);
    }
    if (host) {
        PaddockGap(20.0F);
        const OlEnter enter(chunk++);
        const ImVec2 row = ImGui::GetCursorScreenPos();
        {
            const OlDisabled disabled(!OlCanTestConnection(frame));
            if (OlButton("Test session connection")) OlRequestConnectionTest();
        }
        ImGui::SetCursorScreenPos({row.x + OlButtonWidth("Test session connection") + 8.0F, row.y});
        const OlDisabled disabled(!frame.has_space);
        if (OlButton("Invite friends")) g_online_page.section = OlSection::Friends;
    }
    if (view.connection_test_active) {
        PaddockGap(20.0F);
        const OlEnter enter(chunk++);
        const std::string text = "Testing session traffic\xE2\x80\xA6 " + OlTestCountdown(frame) +
                                 ". Ready, Start and lobby changes are paused.";
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float height = OlParagraphHeight(text, 15.0F, inner - 28.0F) + 28.0F;
        PaddockFill(draw, at, {at.x + inner, at.y + height}, PaddockRound(8.0F), PaddockCol(0x3A2C14));
        OlParagraphAt(draw, {at.x + 14.0F, at.y + 14.0F}, text, 15.0F, kOlAmber, inner - 28.0F);
        ImGui::Dummy({inner, height});
        PaddockGap(13.0F);
    }
    if (OlHasTestResults(frame)) {
        PaddockGap(20.0F);
        const OlEnter enter(chunk++);
        if (OlButton("View test results")) g_online_page.open_results = true;
    }
    PaddockGap(20.0F);
    if (launched) {
        const OlEnter enter(chunk++);
        const bool running = view.state == ConnectionState::Running;
        OlPanelLook room;
        room.fill = 0x061D2C;
        room.ring = 0xFFFFFF;
        room.ring_alpha = 26U;
        room.ring_width = 1.0F;
        DrawOlCard(inner, room, [&](float card) {
            PaddockText(PaddockSign(24.0F, 1.2F, 0.0F), PaddockRgb(kOlCream), running ? "Race running" : "Loading the race",
                        card, true);
            PaddockGap(12.0F);
            OlParagraph(running ? "All racers loaded." : "Waiting for every racer to load.", 15.0F, kOlInk, card);
        });
    } else {
        const OlEnter enter(chunk++);
        DrawOlStartBar(inner, frame);
    }
    // Footer: lock, and leave or close.
    PaddockGap(20.0F);
    {
        const OlEnter enter(chunk++);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        PaddockDashes(draw, at, inner, PaddockCol(0x2F6A86), 2.0F);
        const char* leave = host ? "Close lobby" : "Leave lobby";
        const float leave_width = OlButtonWidth(leave, OlTone::Danger);
        float height = 40.0F;
        if (host) {
            const bool wrap = inner - leave_width - 20.0F < 280.0F;
            const float lock_width = wrap ? inner : inner - leave_width - 20.0F;
            ImGui::SetCursorScreenPos({at.x, at.y + 2.0F + 14.0F - 12.0F});
            bool locked = view.lobby_locked;
            ImGui::BeginGroup();
            if (OlSwitchRow("##lock-lobby", "Lock lobby", "Nobody new can ask to join. Racers already here stay.",
                            &locked, lock_width, false, frame.busy)) {
                std::string error;
                if (!dkr::runtime::netplay::session().set_lobby_locked(locked, error)) OlNotify(error);
            }
            ImGui::EndGroup();
            const float lock_height = ImGui::GetItemRectSize().y - 24.0F;
            height = wrap ? lock_height + 12.0F + 40.0F : std::max(lock_height, 40.0F);
            ImGui::SetCursorScreenPos(wrap ? ImVec2{at.x + inner - leave_width, at.y + 16.0F + lock_height + 12.0F}
                                           : ImVec2{at.x + inner - leave_width, at.y + 16.0F + std::round((height - 40.0F) * 0.5F)});
        } else {
            ImGui::SetCursorScreenPos({at.x + inner - leave_width, at.y + 16.0F});
        }
        if (OlButton(leave, OlTone::Danger)) {
            if (host) {
                g_online_page.open_close = true;
            } else {
                dkr::runtime::netplay::session().disconnect("You left the online lobby.");
            }
        }
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({inner, 16.0F + height});
    }
    ImVec2 panel_min{};
    ImVec2 panel_max{};
    box.End([&](ImDrawList* target, ImVec2 a, ImVec2 b) {
        PaintOlPanel(target, a, b, look);
        panel_min = a;
        panel_max = b;
    });
    DrawOlCountdown(panel_min, panel_max, frame);
}

// Knocking on the host's door (.ol-waiting).
void DrawOlWaiting(float width, const OnlineFrame& frame) {
    OlPanelLook look;
    look.radii = {12.0F, 28.0F, 12.0F, 12.0F};
    look.fill = 0x082F4C;
    look.border = 0x3A8FB5;
    look.border_width = 2.0F;
    look.ring_width = 0.0F;
    look.drop = 0x041822;
    look.drop_offset = 5.0F;
    const OlEnter enter(0);
    PaddockBox box(width, {26.0F, 42.0F});
    const float inner = box.Inner();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // Three lights run amber in turn.
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float lights = 10.0F * 2.0F + 20.0F * 3.0F + 10.0F * 2.0F;
        const ImVec2 a{std::round(at.x + (inner - lights) * 0.5F), at.y};
        PaddockFill(draw, a, {a.x + lights, a.y + 40.0F}, PaddockRound(20.0F), PaddockCol(0x02101A));
        const double now = PaddockClock();
        for (int light = 0; light < 3; ++light) {
            const double delay = 0.25 * light;
            float glow = 0.0F;
            if (now >= delay) {
                const float phase = static_cast<float>(std::fmod(now - delay, 1.5) / 1.5);
                if (phase < 0.2F) glow = PaddockEase(phase / 0.2F);
                else if (phase < 0.6F) glow = 1.0F - PaddockEase((phase - 0.2F) / 0.4F);
            }
            const ImVec2 centre{a.x + 10.0F + 10.0F + light * 30.0F, a.y + 20.0F};
            if (glow > 0.0F) {
                for (int ring = 3; ring >= 1; --ring) {
                    draw->AddCircleFilled(centre, 10.0F + ring * 3.0F,
                                          PaddockCol(kOlAmber, static_cast<unsigned>(glow * 40.0F / ring)), 24);
                }
            }
            draw->AddCircleFilled(centre, 10.0F, PaddockMix(PaddockRgb(0x27353D), PaddockRgb(kOlAmber), glow), 24);
        }
        ImGui::Dummy({inner, 40.0F});
    }
    PaddockGap(8.0F + 8.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        PaddockTextStyle style;
        style.colour = PaddockCol(kOlCream);
        style.centre = true;
        const float height = PaddockTextAt(draw, OlSignType(OlNarrow(440.0F) ? 28.0F : 34.0F, 1.5F), at, inner,
                                           "Knocking on the door\xE2\x80\xA6", style, true);
        ImGui::Dummy({inner, height});
    }
    PaddockGap(8.0F);
    const float text_width = std::min(inner, 46.0F * PaddockMeasure(OlRead(15.0F), "0"));
    const float text_x = std::round((inner - text_width) * 0.5F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const std::string code = NormalizeOnlineInvite(frame.view.invite).substr(0U, 5U);
        const PaddockType plain = OlRead(15.0F, false, 1.5F);
        const PaddockType mono = PaddockMono(15.0F, true, 1.5F, 0.06F);
        const std::string lead = "Waiting for the host to let you in with code ";
        const float total = PaddockMeasure(plain, lead) + PaddockMeasure(mono, code) + PaddockMeasure(plain, ".");
        float height = plain.line;
        if (total <= text_width) {
            float x = std::round(at.x + (inner - total) * 0.5F);
            PaddockDrawRun(draw, plain, {x, at.y}, PaddockCol(kOlInk), lead.data(), lead.data() + lead.size());
            x += PaddockMeasure(plain, lead);
            PaddockDrawRun(draw, mono, {x, at.y}, PaddockCol(kOlAmber), code.data(), code.data() + code.size());
            x += PaddockMeasure(mono, code);
            PaddockDrawRun(draw, plain, {x, at.y}, PaddockCol(kOlInk), ".", "." + 1);
        } else {
            height = OlParagraphAt(draw, {at.x + text_x, at.y}, "Waiting for the host to let you in with code", 15.0F,
                                   kOlInk, text_width, false, 1.5F, true);
            const std::string tail = code + ".";
            const float tail_width = PaddockMeasure(mono, tail);
            PaddockDrawRun(draw, mono, {std::round(at.x + (inner - tail_width) * 0.5F), at.y + height}, PaddockCol(kOlAmber),
                           tail.data(), tail.data() + tail.size());
            height += plain.line;
        }
        ImGui::Dummy({inner, height});
    }
    PaddockGap(8.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float height = OlParagraphAt(draw, {at.x + text_x, at.y},
                                           "The host has to approve every request. This usually takes a few seconds.",
                                           15.0F, kOlSoft, text_width, false, 1.5F, true);
        ImGui::Dummy({inner, height});
    }
    PaddockGap(8.0F + 12.0F);
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float button = OlButtonWidth("Cancel request");
        ImGui::SetCursorScreenPos({std::round(at.x + (inner - button) * 0.5F), at.y});
        if (OlButton("Cancel request")) {
            dkr::runtime::netplay::session().disconnect("You cancelled the join request.");
        }
    }
    box.End([&](ImDrawList* target, ImVec2 a, ImVec2 b) { PaintOlPanel(target, a, b, look); });
}

// ------------------------------------------------------------ dialogs

// Dialog buttons sit at the right (.dialog-actions).
struct OlDialogButton {
    const char* label;
    bool red = false;
};

int DrawOlDialogActions(std::initializer_list<OlDialogButton> buttons) {
    // .race-button: 10 px 16 px of padding, 43 px tall, 12 px apart.
    const auto width = [](const char* label) {
        return std::ceil(ImGui::CalcTextSize(label, nullptr, true).x + 32.0F + 2.0F);
    };
    float total = 0.0F;
    for (const OlDialogButton& button : buttons) total += width(button.label);
    total += 12.0F * static_cast<float>(buttons.size() - 1U);
    ImGui::Dummy({0.0F, 24.0F - ImGui::GetStyle().ItemSpacing.y});
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - total, 0.0F));
    int pressed = -1;
    int index = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12.0F, 12.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0F);
    for (const OlDialogButton& button : buttons) {
        if (index > 0) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, button.red ? kRaceRed : kRaceBlue);
        if (ImGui::Button(button.label, {width(button.label), 43.0F})) pressed = index;
        ImGui::PopStyleColor();
        ++index;
    }
    ImGui::PopStyleVar(2);
    return pressed;
}

// The launcher study's <dialog>: a blue panel, its heading underlined in warm
// orange, over a dark backdrop.
bool BeginOlDialog(const char* name) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(680.0F, std::max(display.x - 32.0F, 1.0F)), 0.0F}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({0.0F, 0.0F}, {FLT_MAX, display.y * 0.85F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {24.0F, 24.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0F);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PaddockRgb(0x06336E));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(0x296B70));
    const bool visible = ImGui::BeginPopupModal(
        name, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!visible) return false;
    g_paddock_modal_frame = ImGui::GetFrameCount();
    ImGui::PushTextWrapPos(0.0F);
    const char* end = ImGui::FindRenderedTextEnd(name);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted(name, end);
    const float rule = ImGui::GetItemRectMax().y + 13.0F;
    ImGui::GetWindowDrawList()->AddRectFilled({at.x, rule}, {at.x + width, rule + 1.0F},
                                              ImGui::GetColorU32(kWarm));
    ImGui::SetCursorScreenPos({at.x, rule + 1.0F + 22.0F - ImGui::GetStyle().ItemSpacing.y});
    ImGui::Dummy({0.0F, 0.0F});
    ImGui::PopTextWrapPos();
    return true;
}

void DrawOlModals(const OnlineFrame& frame) {
    using namespace dkr::runtime::netplay;
    OnlinePageState& state = g_online_page;
    FriendService& service = friend_service();
    constexpr const char* kClose = "Close this lobby?";
    constexpr const char* kBlock = "Block racer?";
    constexpr const char* kRemove = "Remove friend?";
    constexpr const char* kResults = "Session pre-flight results";
    constexpr const char* kManage = "###online-manage-friend";
    constexpr const char* kMatchEnd = "Online match ended";
    if (std::exchange(state.open_match_end, false)) ImGui::OpenPopup(kMatchEnd);
    if (std::exchange(state.open_close, false)) ImGui::OpenPopup(kClose);
    if (std::exchange(state.open_block, false)) ImGui::OpenPopup(kBlock);
    if (std::exchange(state.open_remove, false)) ImGui::OpenPopup(kRemove);
    if (std::exchange(state.open_results, false)) ImGui::OpenPopup(kResults);
    if (std::exchange(state.open_manage, false)) ImGui::OpenPopup(kManage);

    if (BeginOlDialog(kMatchEnd)) {
        ImGui::TextWrapped("%s", state.match_end_notice.c_str());
        ImGui::Dummy({0.0F, 12.0F});
        ImGui::TextWrapped("%s", state.match_end_lobby_retained
            ? "The remaining racers are back in this lobby. Once the online save is verified, everyone can Ready and the host can start again. Single-player saves are unchanged."
            : "The host's lobby has closed. You can create or join another lobby without restarting DKR-R. Single-player saves are unchanged.");
        if (DrawOlDialogActions({{state.match_end_lobby_retained ? "BACK TO LOBBY" : "BACK TO ONLINE"}}) >= 0)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (BeginOlDialog(kClose)) {
        ImGui::TextWrapped("Everyone in the lobby will be disconnected and the code will stop working.");
        const int pressed = DrawOlDialogActions({{"KEEP LOBBY"}, {"CLOSE LOBBY", true}});
        if (pressed == 1) session().disconnect("You closed the lobby.");
        if (pressed >= 0) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const auto snapshot = service.snapshot();
    const auto racer = std::find_if(snapshot->friends.begin(), snapshot->friends.end(), [](const FriendView& view) {
        return view.identity == g_friend_action_identity;
    });
    const std::string label = racer != snapshot->friends.end() ? FriendDisplayLabel(*racer) : std::string("Racer");
    const std::string manage_title = label + kManage;
    if (BeginOlDialog(manage_title.c_str())) {
        if (racer == snapshot->friends.end()) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextWrapped("Racer: %s", racer->display_name.c_str());
            ImGui::Dummy({0.0F, 8.0F});
            OlText("Nickname (only visible to you)", 14.0F, kOlSoft, ImGui::GetContentRegionAvail().x, true, 1.4F);
            ImGui::SetNextItemWidth(-1.0F);
            bool keyboard = false;
            {
                const ControlFontScope scope;
                ImGui::InputText("##friend-nickname", g_friend_nickname, sizeof(g_friend_nickname));
                keyboard = ImGui::IsItemActivated() && GImGui->ActiveIdSource == ImGuiInputSource_Gamepad;
            }
            const int pressed = DrawOlDialogActions(
                {{"SAVE NICKNAME"}, {racer->blocked ? "UNBLOCK" : "BLOCK"}, {"REMOVE", true}, {"CLOSE"}});
            std::string error;
            if (keyboard) {
                ImGui::ClearActiveID();
                RequestTextEntryKeyboard(TextEntryTarget::FriendNickname);
                ImGui::CloseCurrentPopup();
            } else if (pressed == 0) {
                if (service.set_friend_nickname(g_friend_action_identity, g_friend_nickname, error)) {
                    OlNotify("Friend nickname saved.");
                } else {
                    OlNotify(error);
                }
            } else if (pressed == 1) {
                if (racer->blocked) {
                    if (!service.unblock_friend(g_friend_action_identity, error)) OlNotify(error);
                } else {
                    state.open_block = true;
                }
            } else if (pressed == 2) {
                state.open_remove = true;
            }
            if (pressed >= 0) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (BeginOlDialog(kBlock)) {
        ImGui::TextWrapped("Their presence, invites and hosted lobbies will be hidden.");
        const int pressed = DrawOlDialogActions({{"CANCEL"}, {"BLOCK", true}});
        if (pressed == 1) {
            std::string error;
            if (!service.block_friend(g_friend_action_identity, error)) OlNotify(error);
        }
        if (pressed >= 0) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (BeginOlDialog(kRemove)) {
        ImGui::TextWrapped("Exchange another Friend Code to add this racer again.");
        const int pressed = DrawOlDialogActions({{"CANCEL"}, {"REMOVE", true}});
        if (pressed == 1) {
            std::string error;
            if (!service.remove_friend(g_friend_action_identity, error)) OlNotify(error);
        }
        if (pressed >= 0) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (BeginOlDialog(kResults)) {
        const SessionView& view = frame.view;
        std::uint8_t overall = 10U;
        bool any = false;
        for (const auto& result : view.connection_test_results) {
            if (!result.valid) continue;
            overall = std::min(overall, result.score);
            any = true;
        }
        const ImVec4 band = !any || overall <= 4U ? ImVec4{0.95F, 0.18F, 0.12F, 1.0F}
                          : overall <= 7U ? ImVec4{1.0F, 0.58F, 0.08F, 1.0F}
                                          : ImVec4{0.12F, 0.88F, 0.42F, 1.0F};
        ImGui::PushStyleColor(ImGuiCol_Text, band);
        ImGui::Text("Overall connection: %u / 10", static_cast<unsigned>(any ? overall : 0U));
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", !any || overall <= 4U ? "Poor \xE2\x80\x94 expect an unstable online experience"
                                 : overall <= 7U ? "Average \xE2\x80\x94 playable, but hitches may occur"
                                                 : "Excellent \xE2\x80\x94 best online experience");
        ImGui::Dummy({0.0F, 6.0F});
        for (std::size_t slot = 0U; slot < view.room.players.size(); ++slot) {
            const auto& result = view.connection_test_results[slot];
            const Player& player = view.room.players[slot];
            if (!result.valid || !player.occupied) continue;
            if (player.host) {
                ImGui::TextWrapped("%s: %u / 10 \xC2\xB7 Local host", player.display_name.c_str(),
                                   static_cast<unsigned>(result.score));
            } else {
                ImGui::TextWrapped("%s: %u / 10 \xC2\xB7 P95 %u ms \xC2\xB7 Jitter %u ms \xC2\xB7 Loss %.1f%% \xC2\xB7 "
                                   "Late %.1f%% \xC2\xB7 Queues %s",
                                   player.display_name.c_str(), static_cast<unsigned>(result.score),
                                   static_cast<unsigned>(result.p95_rtt_ms), static_cast<unsigned>(result.jitter_ms),
                                   static_cast<double>(result.loss_percent), static_cast<double>(result.late_percent),
                                   result.queues_drained ? "drained" : "backed up");
            }
        }
        ImGui::Dummy({0.0F, 6.0F});
        ImGui::TextWrapped("The launcher tests session traffic for seven seconds; this is not a CPU or GPU benchmark.");
        if (DrawOlDialogActions({{"CLOSE"}}) == 0) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void DrawOnlineGuideModal() {
    constexpr const char* kPopupName = "Welcome to DKR-R Online";
    const bool first_visit =
        g_online_guide_acknowledged_version < kOnlineGuideVersion && !g_online_guide_opened_this_run;
    if (first_visit || g_online_guide_reopen_requested) {
        g_online_guide_opened_this_run = true;
        g_online_guide_reopen_requested = false;
        g_online_guide_do_not_show_again = true;
        ImGui::OpenPopup(kPopupName);
    }
    constexpr std::array<const char*, 5> paragraphs{{
        "Host a private room, share its five-character code, approve requests and ready up. Player 1 starts the "
        "race for everyone.",
        "Open lobbies lists rooms hosted by friends. Friend Codes add racers to your list; lobby invitations grant "
        "one admission and expire after five minutes.",
        "Host Settings controls racer limits, shared menus, synchronization and input delay. Rollback and automatic "
        "delay are the defaults.",
        "Every racer needs the same ROM revision and gameplay settings. The host supplies a separate online "
        "Adventure save; single-player progress is kept.",
        "Online is in beta. Crashes or other issues may occur; please report them to ThatGuyMcd on GitHub or "
        "Discord with the details needed to reproduce them.",
    }};
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (!BeginOlDialog(kPopupName)) return;
    // Only the explanation may scroll; the acknowledgement stays pinned and
    // reachable so opening Online can never softlock the launcher.
    const float wrap = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    float content = 0.0F;
    for (const char* paragraph : paragraphs) {
        content += ImGui::CalcTextSize(paragraph, nullptr, false, wrap).y + spacing;
    }
    const float used = ImGui::GetCursorPosY();
    const float footer = ImGui::GetFrameHeight() + 24.0F + 43.0F + spacing * 2.0F + 24.0F;
    const float limit = std::max(display.y * 0.85F - used - footer, 80.0F);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, PaddockRgb(0, 0U));
    ImGui::BeginChild("##online-guide-scroll", {0.0F, std::min(content, limit)}, false, ImGuiWindowFlags_None);
    ImGui::PushTextWrapPos(0.0F);
    for (const char* paragraph : paragraphs) ImGui::TextWrapped("%s", paragraph);
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Checkbox("Do not show this guide again", &g_online_guide_do_not_show_again);
    if (DrawOlDialogActions({{"GOT IT"}}) == 0) {
        g_online_guide_acknowledged_version = g_online_guide_do_not_show_again ? kOnlineGuideVersion : 0;
        SaveSettings();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ------------------------------------------------------------ page

// A calm amber strip (.ol-test-status).
void DrawOlNote(std::string_view text, float width) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = OlParagraphHeight(text, 15.0F, width - 28.0F) + 28.0F;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockFill(draw, at, {at.x + width, at.y + height}, PaddockRound(8.0F), PaddockCol(0x3A2C14));
    OlParagraphAt(draw, {at.x + 14.0F, at.y + 14.0F}, text, 15.0F, kOlAmber, width - 28.0F);
    ImGui::Dummy({width, height});
}

void DrawOlMain(float width, const OnlineFrame& frame) {
    switch (g_online_page.section) {
    case OlSection::Lobbies: {
        const OlEnter enter(0);
        DrawOlOpenLobbies(width, frame);
        break;
    }
    case OlSection::Play:
        if (!frame.launcher) {
            DrawOlNote("Finish the current game and return to the launcher to host or join a lobby.", width);
            PaddockGap(20.0F);
        }
        DrawOlChoices(width, frame);
        PaddockGap(20.0F + 18.0F);
        {
            const OlEnter enter(3);
            DrawOlSteps(width);
        }
        break;
    case OlSection::Settings: {
        const OlEnter enter(0);
        DrawOlHostSettings(width);
        break;
    }
    case OlSection::Profile:
        {
            const OlEnter enter(0);
            DrawOlProfileCard(width, frame, true);
        }
        PaddockGap(20.0F);
        {
            const OlEnter enter(1);
            DrawOlPrivacy(width);
        }
        PaddockGap(20.0F);
        {
            const OlEnter enter(2);
            DrawOlFriendCodes(width);
        }
        PaddockGap(20.0F);
        {
            const OlEnter enter(3);
            DrawOlChecklist(width, frame);
        }
        break;
    case OlSection::Friends: {
        const OlEnter enter(0);
        DrawOlFriends(width, frame);
        break;
    }
    case OlSection::Overlays: {
        const OlEnter enter(0);
        DrawOlOverlays(width);
        break;
    }
    case OlSection::Connection: {
        const OlEnter enter(0);
        DrawOlConnection(width, frame);
        break;
    }
    case OlSection::Lobby:
        if (frame.joining) {
            DrawOlWaiting(width, frame);
        } else {
            DrawOlLobby(width, frame);
        }
        break;
    }
    if (!frame.active &&
        (g_online_page.section == OlSection::Play || g_online_page.section == OlSection::Lobbies)) {
        PaddockGap(20.0F);
        DrawOlChecklist(width, frame);
    }
}

void DrawOnlinePage(float available_width, bool launcher, bool rom_ready) {
    using namespace dkr::runtime::netplay;
    DirectSession& online = session();
    PumpDirectSessionIfDue();
    const SessionView view = online.presentation_view();
    OnlinePageState& state = g_online_page;
    const int frame_count = ImGui::GetFrameCount();
    const bool entered = state.context != ImGui::GetCurrentContext() || state.last_frame != frame_count - 1;
    state.context = ImGui::GetCurrentContext();
    state.last_frame = frame_count;

    const bool active = online.presentation_active();
    const bool joining = active && !view.host &&
                         (view.state == ConnectionState::Connecting || view.state == ConnectionState::AwaitingApproval);
    const bool in_lobby = active && !joining;
    const bool busy = view.owned_match_ending || view.connection_test_active || view.launch_countdown_active ||
                      view.launch_stage != LaunchStage::Idle || view.state == ConnectionState::Loading ||
                      view.state == ConnectionState::Running;
    const int maximum = std::clamp<int>(view.room.rules.maximum_players, 2, static_cast<int>(view.room.players.size()));
    int occupied = 0;
    for (const Player& player : view.room.players) {
        if (player.occupied) ++occupied;
    }
    const bool has_space = in_lobby && view.host && !view.lobby_locked && !busy && occupied < maximum &&
                           (view.state == ConnectionState::Hosting || view.state == ConnectionState::Lobby);
    const OnlineFrame frame{view, launcher, rom_ready, active, joining, in_lobby, busy, has_space};

    // A new session opens its lobby; leaving goes back to where you were.
    if (active && !state.was_active) state.section = OlSection::Lobby;
    if (!active && state.was_active) state.section = state.was_joining ? OlSection::Play : OlSection::Lobbies;
    if (in_lobby && view.host && std::exchange(g_open_host_friend_invites, false)) state.section = OlSection::Friends;
    state.was_active = active;
    state.was_joining = joining;

    std::vector<OlTab> tabs;
    if (!active) {
        tabs = {{OlSection::Lobbies, "Open lobbies"}, {OlSection::Play, "Host / Quick Join"},
                {OlSection::Settings, "Host settings"}, {OlSection::Profile, "Online profile"},
                {OlSection::Friends, "Friends"}, {OlSection::Overlays, "Overlays"}};
    } else {
        tabs.push_back({OlSection::Lobby, "Lobby"});
        if (in_lobby) {
            if (view.host) tabs.push_back({OlSection::Friends, "Invite friends"});
            tabs.push_back({OlSection::Connection, "Connection"});
            tabs.push_back({OlSection::Overlays, "Overlays"});
        }
    }
    const auto valid_section = [&] {
        return std::any_of(tabs.begin(), tabs.end(), [&](const OlTab& tab) { return tab.id == state.section; });
    };
    if (!valid_section()) state.section = tabs.front().id;

    // Messages from shared helpers become the study's notification.
    if (!g_online_action_status.empty()) OlNotify(std::exchange(g_online_action_status, {}));
    if (view.connection_test_result_generation != 0U &&
        view.connection_test_result_generation != state.observed_test_generation) {
        state.observed_test_generation = view.connection_test_result_generation;
        state.open_results = true;
    }

    const float page = std::min(available_width, 1400.0F);
    g_ol_page_width = available_width;
    const float indent = std::floor((available_width - page) * 0.5F);
    if (indent > 0.0F) ImGui::Indent(indent);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});

    // The header (.ol-header): the title and lede, and in the launcher the
    // Game ROM beside them (flex 1 1 340px / 0 1 360px), wrapping under them
    // on a narrow panel.
    {
        const bool rom_slot = launcher && g_launcher_rom != nullptr;
        const bool side = rom_slot && page >= 340.0F + 28.0F + 280.0F;
        const float slot_width = !rom_slot ? 0.0F
                               : side ? std::clamp(page - 340.0F - 28.0F, 280.0F, 360.0F)
                                      : std::min(page, 360.0F);
        const float text_width = side ? page - slot_width - 28.0F : page;
        const ImVec2 head = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        DrawPageHeading("ONLINE");
        PaddockGap(6.0F);
        OlParagraph("Race friends over the internet. No account, no port forwarding: just share a code.", 16.0F,
                    kOlSoft, text_width);
        ImGui::EndGroup();
        float bottom = ImGui::GetItemRectMax().y;
        if (rom_slot) {
            const PlayPageContext& rom = *g_launcher_rom;
            const bool known = !rom.rom_catalog.empty();
            const PaddockType label = OlSignType(15.0F, 1.0F, 0.08F);
            const PaddockType note = OlRead(12.5F, false, 1.4F);
            const float field = known ? 55.0F : 56.0F;
            const float slot_height = label.line + 6.0F + field + 6.0F + note.line;
            const float top = side ? std::max(head.y, bottom - slot_height) : bottom + 16.0F;
            const float x = side ? head.x + page - slot_width : head.x;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            PaddockDrawRun(draw, label, {x, top}, PaddockCol(kOlAmber), "GAME ROM", "GAME ROM" + 8);
            ImGui::SetCursorScreenPos({x, top + label.line + 6.0F});
            if (known) {
                DrawRomSelect("online-rom", slot_width, true, active,
                              "Locked while you are in a lobby. Everyone races on the same revision.");
            } else if (PaddockImportButton("Choose a ROM...##online-rom")) {
                OpenRomBrowser(rom.selected_rom);
            }
            const std::string_view hint = active ? "Locked while you are in a lobby."
                                        : known ? "Everyone in a lobby needs the same revision."
                                                : "You need a ROM to host or join.";
            PaddockTextStyle hint_style;
            hint_style.colour = PaddockCol(kOlSoft);
            PaddockTextAt(draw, note, {x, top + label.line + 6.0F + field + 6.0F}, slot_width, hint, hint_style);
            bottom = std::max(bottom, top + slot_height);
        }
        ImGui::SetCursorScreenPos({head.x, bottom});
        ImGui::Dummy({page, 0.0F});
    }
    PaddockGap(22.0F);
    DrawOlTabs(tabs, page);
    if (!valid_section()) state.section = tabs.front().id;
    // Content slides in when the section or the session phase changes.
    const std::string phase = std::string(!active ? "idle" : joining ? "joining" : "lobby") +
                              (active && view.host ? "-host" : "") + "-" +
                              std::to_string(static_cast<int>(state.section));
    if (!entered && !state.phase_key.empty() && phase != state.phase_key) state.entered_at = PaddockClock();
    state.phase_key = phase;
    PaddockGap(24.0F);

    const bool two_columns = available_width > 1000.0F;
    const float main_width = two_columns ? page - 340.0F - 24.0F : page;
    const bool profile_section = state.section == OlSection::Profile;
    const ImVec2 top = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    DrawOlMain(main_width, frame);
    ImGui::EndGroup();
    float bottom = ImGui::GetItemRectMax().y;
    if (two_columns) {
        ImGui::SetCursorScreenPos({top.x + main_width + 24.0F, top.y});
        ImGui::BeginGroup();
        if (!profile_section) {
            DrawOlProfileCard(340.0F, frame, false);
            PaddockGap(20.0F);
        }
        DrawOlGuideCard(340.0F, frame);
        ImGui::EndGroup();
        bottom = std::max(bottom, ImGui::GetItemRectMax().y);
    } else {
        const float y = bottom + 24.0F;
        const bool pair = !profile_section && page >= 290.0F * 2.0F + 20.0F;
        if (pair) {
            const float half = std::floor((page - 20.0F) * 0.5F);
            ImGui::SetCursorScreenPos({top.x, y});
            ImGui::BeginGroup();
            DrawOlProfileCard(half, frame, false);
            ImGui::EndGroup();
            bottom = ImGui::GetItemRectMax().y;
            ImGui::SetCursorScreenPos({top.x + half + 20.0F, y});
            ImGui::BeginGroup();
            DrawOlGuideCard(half, frame);
            ImGui::EndGroup();
            bottom = std::max(bottom, ImGui::GetItemRectMax().y);
        } else {
            ImGui::SetCursorScreenPos({top.x, y});
            ImGui::BeginGroup();
            if (!profile_section) {
                DrawOlProfileCard(page, frame, false);
                PaddockGap(20.0F);
            }
            DrawOlGuideCard(page, frame);
            ImGui::EndGroup();
            bottom = ImGui::GetItemRectMax().y;
        }
    }
    ImGui::SetCursorScreenPos({top.x, bottom});
    ImGui::Dummy({page, 0.0F});
    ImGui::PopStyleVar();
    if (indent > 0.0F) ImGui::Unindent(indent);

    DrawOlModals(frame);
    DrawOnlineCodeKeyboard();
    DrawFriendCodeKeyboard();
    DrawFriendSearchKeyboard();
    DrawOnlineGuideModal();
}
