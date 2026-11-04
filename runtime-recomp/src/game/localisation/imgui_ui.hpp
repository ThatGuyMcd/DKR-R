#pragma once
#include "service.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <type_traits>
#include <utility>
#include <vector>

namespace LocalisedUi {
template <class T> inline constexpr bool caption_array =
    std::is_array_v<std::remove_reference_t<T>> &&
    std::is_const_v<std::remove_extent_t<std::remove_reference_t<T>>>;
template <class T> std::string_view copy(T&& value) {
    if constexpr (std::is_null_pointer_v<std::remove_cvref_t<T>>) return {};
    else {
        if constexpr (std::is_pointer_v<std::remove_cvref_t<T>>) if (!value) return {};
        return dkr::localisation::literal(std::forward<T>(value));
    }
}
template <class T> std::string widget(T&& value) {
    if constexpr (caption_array<T>)
        return dkr::localisation::label(std::string_view(value));
    else return std::string(value);
}
inline std::vector<std::string> enum_items(const std::vector<std::string>& items) {
    std::vector<std::string> result;
    result.reserve(items.size());
    for (const auto& item : items) result.emplace_back(dkr::localisation::source(item));
    return result;
}
template <class T> std::string_view format_copy(T&& value) {
    if constexpr (caption_array<T>)
        return dkr::localisation::printf_source(std::string_view(value));
    else return std::string_view(value);
}
}
namespace ImGui {
template <class T, class... Args> void UiText(T&& format, Args&&... args) {
    if constexpr (sizeof...(Args)>0) {
        Text(LocalisedUi::format_copy(std::forward<T>(format)).data(), std::forward<Args>(args)...);
    } else {
        const auto shown = LocalisedUi::copy(std::forward<T>(format));
        TextUnformatted(shown.data(), shown.data() + shown.size());
    }
}
template <class T, class... Args> void UiTextWrapped(T&& format, Args&&... args) {
    if constexpr (sizeof...(Args)>0) {
        TextWrapped(LocalisedUi::format_copy(std::forward<T>(format)).data(), std::forward<Args>(args)...);
    } else {
        const auto shown = LocalisedUi::copy(std::forward<T>(format));
        TextWrapped("%.*s", int(shown.size()), shown.data());
    }
}
template <class T, class... Args> void UiTextColored(const ImVec4& colour, T&& format, Args&&... args) {
    if constexpr (sizeof...(Args)>0) {
        TextColored(colour, LocalisedUi::format_copy(std::forward<T>(format)).data(), std::forward<Args>(args)...);
    } else {
        const auto shown = LocalisedUi::copy(std::forward<T>(format));
        TextColored(colour, "%.*s", int(shown.size()), shown.data());
    }
}
template <class T, class... Args> void UiSetTooltip(T&& format, Args&&... args) {
    if constexpr (sizeof...(Args)>0) {
        SetTooltip(LocalisedUi::format_copy(std::forward<T>(format)).data(), std::forward<Args>(args)...);
    } else {
        const auto shown = LocalisedUi::copy(std::forward<T>(format));
        SetTooltip("%.*s", int(shown.size()), shown.data());
    }
}
template <class T> void UiTextUnformatted(T&& text, const char* end = nullptr) {
    // Explicit ranges may be substrings of paths or user data.
    if (end) TextUnformatted(text, end);
    else TextUnformatted(LocalisedUi::copy(std::forward<T>(text)).data());
}
template <class T> bool UiButton(T&& label, const ImVec2& size = {}) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return Button(shown.c_str(), size);
}
template <class T> bool UiSmallButton(T&& label) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return SmallButton(shown.c_str());
}
template <class T> bool UiCheckbox(T&& label, bool* value) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return Checkbox(shown.c_str(), value);
}
template <class T> bool UiRadioButton(T&& label, bool selected) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return RadioButton(shown.c_str(), selected);
}
template <class T> bool UiRadioButton(T&& label, int* value, int selected) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return RadioButton(shown.c_str(), value, selected);
}
template <class T> void UiSeparatorText(T&& text) {
    const auto shown = LocalisedUi::copy(std::forward<T>(text));
    SeparatorText(shown.data());
}
template <class T> bool UiInputText(T&& label, char* buffer, std::size_t capacity,
                                  ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* user = nullptr) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return InputText(shown.c_str(), buffer, capacity, flags, callback, user);
}
template <class T, class H> bool UiInputTextWithHint(T&& label, H&& hint, char* buffer, std::size_t capacity,
                                  ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* user = nullptr) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    const auto placeholder = LocalisedUi::copy(std::forward<H>(hint));
    return InputTextWithHint(shown.c_str(), placeholder.data(), buffer, capacity, flags, callback, user);
}
template <class T> bool UiInputTextMultiline(T&& label, char* buffer, std::size_t capacity, const ImVec2& size = {},
                                  ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* user = nullptr) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return InputTextMultiline(shown.c_str(), buffer, capacity, size, flags, callback, user);
}
template <class T> bool UiSliderInt(T&& label, int* value, int minimum, int maximum,
                                  const char* format = "%d", ImGuiSliderFlags flags = 0) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return SliderInt(shown.c_str(), value, minimum, maximum, format, flags);
}
template <class T> bool UiSliderFloat(T&& label, float* value, float minimum, float maximum,
                                    const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return SliderFloat(shown.c_str(), value, minimum, maximum, format, flags);
}
template <class T> bool UiSelectable(T&& label, bool selected = false, ImGuiSelectableFlags flags = 0, const ImVec2& size = {}) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return Selectable(shown.c_str(), selected, flags, size);
}
template <class T> bool UiSelectable(T&& label, bool* selected, ImGuiSelectableFlags flags = 0, const ImVec2& size = {}) {
    const auto shown = LocalisedUi::widget(std::forward<T>(label));
    return Selectable(shown.c_str(), selected, flags, size);
}
template <class T> bool UiBeginPopupModal(T&& label, bool* open = nullptr, ImGuiWindowFlags flags = 0) {
    // Popup identity must match existing OpenPopup()/IsPopupOpen() calls.
    const bool visible = BeginPopupModal(label, open, flags);
    if constexpr (LocalisedUi::caption_array<T>) {
        if (visible && !(GetCurrentWindow()->Flags & ImGuiWindowFlags_NoTitleBar)) {
            const auto raw = std::string_view(label).substr(0, std::string_view(label).find("##"));
            const auto title = dkr::localisation::source(raw);
            if (title != raw) {
                auto* window = GetCurrentWindow();
                auto* draw = window->DrawList;
                const auto& style = GetStyle();
                const auto bar = window->TitleBarRect();
                const ImVec2 left{bar.Min.x + style.FramePadding.x, bar.Min.y + style.FramePadding.y};
                const ImVec2 right{bar.Max.x - style.FramePadding.x - (open ? GetFontSize() + style.FramePadding.x : 0.F), bar.Max.y};
                draw->PushClipRect({left.x, bar.Min.y + window->WindowBorderSize},
                    {right.x, bar.Max.y - window->WindowBorderSize}, true);
                draw->AddRectFilled(bar.Min, bar.Max, GetColorU32(ImGuiCol_TitleBgActive),
                    window->WindowRounding, ImDrawFlags_RoundCornersTop);
                draw->PopClipRect();
                RenderTextClipped(left, right, title.data(), title.data() + title.size(), nullptr, style.WindowTitleAlign);
            }
        }
    }
    return visible;
}
}
