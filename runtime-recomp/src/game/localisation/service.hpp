#pragma once

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace dkr::localisation {
struct Language {
    std::string_view code;
    std::string_view name;
};
struct Argument {
    std::string_view name;
    std::string_view value;
};

// Immutable, embedded catalogues. Returned views survive language changes and
// destruction/recreation of either ImGui context. Only presentation uses them.
std::span<const Language> languages();
std::string normalise(std::string_view locale);
std::string resolve(std::span<const std::string> preferences);
std::string resolve_posix(std::string_view all, std::string_view messages,
                          std::string_view lang, std::string_view language_list);
void system_preferences(std::span<const std::string> preferences);
void detect_system_language();
bool select(std::string_view code);
std::string_view preference();
std::string_view active();
std::uint64_t generation();
// Layout qualification only. Never offered as a language or persisted.
void debug_pseudo(bool enabled);
std::string_view text(std::string_view id);
std::string_view source(std::string_view english);
std::string_view printf_source(std::string_view english);
std::string label(std::string_view english);
std::string directional_caption(std::string_view owned_caption);
std::string relay_caption(std::string_view owned_caption,bool proxy);
std::string format(std::string_view id, std::initializer_list<Argument> arguments);
std::string plural(std::string_view id, std::uint64_t count,
                   std::initializer_list<Argument> arguments = {});
std::string_view plural_category(std::string_view locale, std::uint64_t count);

// Literals are application copy. Pointers, string_views and std::strings may
// contain user content and deliberately pass through without lookup.
template <std::size_t N>
std::string_view literal(const char (&value)[N]) {
    return source(std::string_view(value, N - 1));
}
template <std::size_t N>
std::string_view literal(char (&value)[N]) { return std::string_view(value); }
inline std::string_view literal(std::string_view value) { return value; }

std::span<const unsigned char> fallback_font();
std::span<const std::uint16_t> fallback_ranges();
} // namespace dkr::localisation
