// Compatibility boundary for the retained UI. Only literal arguments are
// application-owned copy; dynamic strings remain untouched. Widget identities
// are based on their original English/internal labels, never their translation.
// Keep these adapters outside the simulation, services and diagnostics.
template <class T> float UiPaddockMeasure(const PaddockType& type, T&& text) {
    return PaddockMeasure(type, LocalisedUi::copy(std::forward<T>(text)));
}
template <class T> float UiPaddockMeasure(const PaddockType& type, T&& begin, const char* end) {
    if constexpr (LocalisedUi::caption_array<T>) {
        if (end == begin + std::extent_v<std::remove_reference_t<T>> - 1) {
            const auto shown = LocalisedUi::copy(std::forward<T>(begin));
            return PaddockMeasure(type, shown);
        }
    }
    return PaddockMeasure(type, begin, end);
}
template <class T>
void UiPaddockDrawRun(ImDrawList* draw, const PaddockType& type, ImVec2 position, ImU32 colour, T&& begin,
                      const char* end) {
    if constexpr (LocalisedUi::caption_array<T>) {
        if (end == begin + std::extent_v<std::remove_reference_t<T>> - 1) {
            const auto shown = LocalisedUi::copy(std::forward<T>(begin));
            PaddockDrawRun(draw, type, position, colour, shown.data(), shown.data() + shown.size());
            return;
        }
    }
    PaddockDrawRun(draw, type, position, colour, begin, end);
}
template <class T> float UiPaddockTextHeight(const PaddockType& type, T&& text, float width, bool balance = false) {
    return PaddockTextHeight(type, LocalisedUi::copy(std::forward<T>(text)), width, balance);
}
template <class T>
float UiPaddockTextAt(ImDrawList* draw, const PaddockType& type, ImVec2 position, float width, T&& text,
                      const PaddockTextStyle& style, bool balance = false) {
    return PaddockTextAt(draw, type, position, width, LocalisedUi::copy(std::forward<T>(text)), style, balance);
}
template <class T>
float UiPaddockText(const PaddockType& type, ImU32 colour, T&& text, float width, bool balance = false,
                    ImU32 shadow = 0U) {
    return PaddockText(type, colour, LocalisedUi::copy(std::forward<T>(text)), width, balance, shadow);
}
template <class T> float UiPaddockHeading(T&& text, float px, unsigned colour, float width, float line = 1.25F) {
    return PaddockHeading(LocalisedUi::copy(std::forward<T>(text)), px, colour, width, line);
}
template <class T>
bool UiPaddockButton(T&& label, PaddockButtonKind kind = PaddockButtonKind::Plain, float width = 0.F,
                     float height = 42.F) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return PaddockButton(shown.c_str(), kind, width, height);
}
template <class T> float UiPaddockButtonWidth(T&& label, PaddockButtonKind kind) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return PaddockButtonWidth(shown.c_str(), kind);
}
template <class T> bool UiPaddockTextLink(T&& label) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return PaddockTextLink(shown.c_str());
}
template <class T> float UiPaddockSectionTabWidth(T&& label) {
    // Section tabs are owned registry captions, including counted headings.
    const auto shown = dkr::localisation::label(std::string_view(label));
    return PaddockSectionTabWidth(shown.c_str());
}
template <class T> bool UiPaddockSectionTab(T&& label, bool open, float width = 0.F) {
    const auto shown = dkr::localisation::label(std::string_view(label));
    return PaddockSectionTab(shown.c_str(), open, width);
}
template <class T>
bool UiPaddockCheckbox(const char* id, T&& label, bool* value, float width,
                       PaddockCheckKind kind = PaddockCheckKind::Plain, const PaddockType* type = nullptr,
                       unsigned colour = 0xFFF6DA) {
    const auto shown = LocalisedUi::copy(std::forward<T>(label));
    return PaddockCheckbox(id, shown.data(), value, width, kind, type, colour);
}
template <class T> void UiPaddockAlert(T&& text, float width) {
    PaddockAlert(LocalisedUi::copy(std::forward<T>(text)), width);
}
template <class T> void UiPaddockFeedback(T&& text, float width) {
    PaddockFeedback(LocalisedUi::copy(std::forward<T>(text)), width);
}
template <class T> void UiPaddockSeparatorText(T&& text, float width) {
    PaddockSeparatorText(LocalisedUi::copy(std::forward<T>(text)), width);
}
template <class T>
PaddockFieldResult UiPaddockSearch(const char* id, char* buffer, std::size_t capacity, T&& hint, float width) {
    const auto shown = LocalisedUi::copy(std::forward<T>(hint));
    return PaddockSearch(id, buffer, capacity, shown.data(), width);
}
template <class T>
bool UiPaddockSelect(const char* id, T&& label, int* value, const std::vector<std::string>& items, float width) {
    const auto caption = LocalisedUi::copy(std::forward<T>(label));
    return PaddockSelect(id, caption.data(), value, items, width);
}
template <class T>
bool UiPaddockSelect(const char* id, T&& label, int* value, std::initializer_list<const char*> choices, float width) {
    std::vector<std::string> items;
    for (const auto* choice : choices)
        items.emplace_back(dkr::localisation::source(choice));
    return UiPaddockSelect(id, std::forward<T>(label), value, items, width);
}

inline bool UiPaddockSwitch(const char* id, bool* value, const char* on, const char* off,
                            const char* description = nullptr) {
    return PaddockSwitch(id, value, dkr::localisation::source(on).data(), dkr::localisation::source(off).data(),
                         description);
}

#define PaddockText(...) UiPaddockText(__VA_ARGS__)
#define PaddockMeasure(...) UiPaddockMeasure(__VA_ARGS__)
#define PaddockDrawRun(...) UiPaddockDrawRun(__VA_ARGS__)
#define PaddockTextHeight(...) UiPaddockTextHeight(__VA_ARGS__)
#define PaddockTextAt(...) UiPaddockTextAt(__VA_ARGS__)
#define PaddockHeading(...) UiPaddockHeading(__VA_ARGS__)
#define PaddockButton(...) UiPaddockButton(__VA_ARGS__)
#define PaddockButtonWidth(...) UiPaddockButtonWidth(__VA_ARGS__)
#define PaddockTextLink(...) UiPaddockTextLink(__VA_ARGS__)
#define PaddockSectionTabWidth(...) UiPaddockSectionTabWidth(__VA_ARGS__)
#define PaddockSectionTab(...) UiPaddockSectionTab(__VA_ARGS__)
#define PaddockCheckbox(...) UiPaddockCheckbox(__VA_ARGS__)
#define PaddockAlert(...) UiPaddockAlert(__VA_ARGS__)
#define PaddockFeedback(...) UiPaddockFeedback(__VA_ARGS__)
#define PaddockSeparatorText(...) UiPaddockSeparatorText(__VA_ARGS__)
#define PaddockSearch(...) UiPaddockSearch(__VA_ARGS__)
#define PaddockSelect(...) UiPaddockSelect(__VA_ARGS__)
#define PaddockSwitch(...) UiPaddockSwitch(__VA_ARGS__)
#define Text(...) UiText(__VA_ARGS__)
#define TextWrapped(...) UiTextWrapped(__VA_ARGS__)
#define TextColored(...) UiTextColored(__VA_ARGS__)
#define TextUnformatted(...) UiTextUnformatted(__VA_ARGS__)
#define SetTooltip(...) UiSetTooltip(__VA_ARGS__)
#undef Button
#define Button(...) UiButton(__VA_ARGS__)
#define SmallButton(...) UiSmallButton(__VA_ARGS__)
#define Checkbox(...) UiCheckbox(__VA_ARGS__)
#define RadioButton(...) UiRadioButton(__VA_ARGS__)
#define SeparatorText(...) UiSeparatorText(__VA_ARGS__)
#define InputText(...) UiInputText(__VA_ARGS__)
#define InputTextWithHint(...) UiInputTextWithHint(__VA_ARGS__)
#define InputTextMultiline(...) UiInputTextMultiline(__VA_ARGS__)
#define SliderInt(...) UiSliderInt(__VA_ARGS__)
#define SliderFloat(...) UiSliderFloat(__VA_ARGS__)
#define Selectable(...) UiSelectable(__VA_ARGS__)
#define BeginPopupModal(...) UiBeginPopupModal(__VA_ARGS__)
