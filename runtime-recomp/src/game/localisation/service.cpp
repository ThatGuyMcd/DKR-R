#include "service.hpp"
#include "localisation_data.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dkr::localisation {
namespace {
std::mutex preference_mutex;
std::string chosen = "auto";
std::string system = "en";
std::atomic<std::size_t> current{0};
std::atomic<std::uint64_t> serial{0};
std::atomic<bool> pseudo_enabled{false};

std::string_view pseudo(std::string_view value) {
    static std::mutex mutex;
    static std::map<std::string, std::string, std::less<>> pool;
    static std::set<std::string_view> rendered;
    std::lock_guard lock(mutex);
    if (const auto found = rendered.find(value); found != rendered.end())
        return *found;
    if (const auto found = pool.find(value); found != pool.end())
        return found->second;
    std::string result = "[";
    std::size_t visible = 0;
    bool argument = false;
    for (const auto character : value) {
        if (character == '{')
            argument = true;
        if (argument)
            result += character;
        else {
            switch (character) {
            case 'a':
                result += "áá";
                break;
            case 'e':
                result += "ëë";
                break;
            case 'i':
                result += "ïï";
                break;
            case 'o':
                result += "óó";
                break;
            case 'u':
                result += "üü";
                break;
            default:
                result += character;
                break;
            }
            if ((static_cast<unsigned char>(character) & 0xC0U) != 0x80U)
                ++visible;
        }
        if (character == '}')
            argument = false;
    }
    result += ' ';
    result.append(visible / 4 + 1, '~');
    result += ']';
    const auto& saved = pool.emplace(std::string(value), std::move(result)).first->second;
    rendered.insert(saved);
    return saved;
}

std::size_t index(std::string_view code) {
    const auto found = std::find_if(data::languages.begin(), data::languages.end(),
                                    [&](const Language& language) { return language.code == code; });
    return found == data::languages.end() ? 0 : std::size_t(found - data::languages.begin());
}
void publish(std::string_view code) {
    const auto next = index(code);
    if (current.exchange(next) != next)
        ++serial;
}
const data::Message* message(std::string_view id) {
    const auto found = std::lower_bound(data::messages.begin(), data::messages.end(), id,
                                        [](const data::Message& item, std::string_view key) { return item.id < key; });
    return found != data::messages.end() && found->id == id ? &*found : nullptr;
}
std::string render(std::string_view pattern, std::initializer_list<Argument> args, std::string_view count = {}) {
    std::string result;
    for (std::size_t pos = 0; pos < pattern.size();) {
        if (pattern[pos] != '{') {
            result += pattern[pos++];
            continue;
        }
        const auto end = pattern.find('}', pos + 1);
        if (end == pattern.npos)
            throw std::invalid_argument("Unclosed localisation placeholder");
        const auto key = pattern.substr(pos + 1, end - pos - 1);
        if (key == "count" && !count.empty())
            result += count;
        else {
            const auto arg =
                std::find_if(args.begin(), args.end(), [&](const Argument& item) { return item.name == key; });
            if (arg == args.end())
                throw std::invalid_argument("Missing localisation argument: " + std::string(key));
            // Values are appended once and never interpreted as copy or format.
            result += arg->value;
        }
        pos = end + 1;
    }
    return result;
}
} // namespace

std::span<const Language> languages() {
    return data::languages;
}
std::string normalise(std::string_view locale) {
    if (locale.empty() || locale.size() > 96)
        return {};
    std::string result;
    for (const char character : locale) {
        if (character == '.' || character == '@')
            break;
        if (character == '_')
            result += '-';
        else if ((character >= 'A' && character <= 'Z'))
            result += char(character + ('a' - 'A'));
        else if ((character >= 'a' && character <= 'z') || character == '-' || (character >= '0' && character <= '9'))
            result += character;
        else
            return {};
    }
    if (result == "c" || result == "posix")
        return "en";
    if (result == "pt-br" || result.starts_with("pt-br-"))
        return "pt-BR";
    if (result == "pt" || result.starts_with("pt-"))
        return "pt-PT";
    const auto dash = result.find('-');
    if (dash != result.npos)
        result.resize(dash);
    return result;
}
std::string resolve(std::span<const std::string> preferences) {
    for (const auto& preference : preferences) {
        const auto candidate = normalise(preference);
        if (std::any_of(data::languages.begin(), data::languages.end(),
                        [&](const Language& language) { return language.code == candidate; }))
            return candidate;
    }
    return "en";
}
void system_preferences(std::span<const std::string> preferences) {
    std::lock_guard lock(preference_mutex);
    system = resolve(preferences);
    if (chosen == "auto")
        publish(system);
}
std::string resolve_posix(std::string_view all, std::string_view messages, std::string_view lang,
                          std::string_view language_list) {
    const auto effective = !all.empty() ? all : !messages.empty() ? messages : lang;
    const auto suffix = effective.find_first_of(".@");
    std::string base(effective.substr(0, suffix));
    for (auto& character : base)
        if (character >= 'A' && character <= 'Z')
            character += 'a' - 'A';
    std::vector<std::string> preferences;
    if (base != "c" && base != "posix") {
        for (std::size_t pos = 0; pos < language_list.size();) {
            const auto end = language_list.find(':', pos);
            preferences.emplace_back(language_list.substr(pos, end == language_list.npos ? end : end - pos));
            if (end == language_list.npos)
                break;
            pos = end + 1;
        }
    }
    preferences.emplace_back(effective);
    return resolve(preferences);
}
void detect_system_language() {
    std::vector<std::string> preferences;
#if defined(_WIN32)
    ULONG count = 0, length = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &length) && length && length < 65536) {
        std::vector<wchar_t> buffer(length);
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &length)) {
            for (std::size_t pos = 0; pos < buffer.size() && buffer[pos];) {
                std::string code;
                while (pos < buffer.size() && buffer[pos]) {
                    const auto character = buffer[pos++];
                    if (character < 128)
                        code += char(character);
                }
                ++pos;
                preferences.push_back(std::move(code));
            }
        }
    }
#else
    // LC_ALL overrides the category. LANGUAGE is a preference list, except
    // when the effective message locale is C/POSIX. No process setlocale().
    const auto env = [](const char* key) {
        const auto* value = std::getenv(key);
        return value ? std::string(value) : std::string{};
    };
    preferences.push_back(resolve_posix(env("LC_ALL"), env("LC_MESSAGES"), env("LANG"), env("LANGUAGE")));
#endif
    system_preferences(preferences);
}
bool select(std::string_view code) {
    std::lock_guard lock(preference_mutex);
    const auto next = code == "auto" ? std::string("auto") : normalise(code);
    if (next != "auto" && std::none_of(data::languages.begin(), data::languages.end(),
                                       [&](const Language& language) { return language.code == next; }))
        return false;
    chosen = next;
    publish(chosen == "auto" ? system : chosen);
    return true;
}
std::string_view preference() {
    return chosen;
} // UI thread owns settings access.
std::string_view active() {
    return data::languages[current.load()].code;
}
std::uint64_t generation() {
    return serial.load();
}
void debug_pseudo(bool enabled) {
    if (pseudo_enabled.exchange(enabled) != enabled)
        ++serial;
}
std::string_view text(std::string_view id) {
    const auto* found = message(id);
    if (!found)
        return id;
    const auto translated = found->values[current.load()];
    const auto value = translated.empty() ? found->values[0] : translated;
    return pseudo_enabled.load() ? pseudo(value) : value;
}
std::string_view source(std::string_view english) {
    // Preserve the original English sign casing and authored typography.
    if (current.load() == 0)
        return pseudo_enabled.load() ? pseudo(english) : english;
    const auto found =
        std::lower_bound(data::aliases.begin(), data::aliases.end(), english,
                         [](const data::Alias& alias, std::string_view key) { return alias.english < key; });
    if (found != data::aliases.end() && found->english == english)
        return text(found->id);
    return pseudo_enabled.load() ? pseudo(english) : english;
}
std::string_view printf_source(std::string_view english) {
    if (current.load() == 0)
        return english;
    const auto found =
        std::lower_bound(data::aliases.begin(), data::aliases.end(), english,
                         [](const data::Alias& alias, std::string_view key) { return alias.english < key; });
    if (found == data::aliases.end() || found->english != english)
        return english;
    const auto* item = message(found->id);
    // Only explicitly audited format messages may become a format string.
    // Ordinary translated prose is always passed to ImGui as text data.
    return item && item->printf_format ? item->values[current.load()] : english;
}
std::string label(std::string_view english) {
    if (english.empty() || english.starts_with("##"))
        return std::string(english);
    const auto end = english.find("##");
    const auto visible = english.substr(0, end);
    std::string translated(source(visible));
    // Owned navigation captions may carry a numeric badge, e.g. Downloads
    // (3). Treat the badge as data; this API is never applied to user content.
    if (translated == visible && visible.ends_with(')')) {
        const auto open = visible.rfind(" (");
        if (open != visible.npos && open + 2 < visible.size() - 1) {
            const auto count = visible.substr(open + 2, visible.size() - open - 3);
            if (std::all_of(count.begin(), count.end(), [](char c) { return c >= '0' && c <= '9'; }))
                translated = std::string(source(visible.substr(0, open))) + std::string(visible.substr(open));
        }
    }
    // Every locale, including English, uses the same ### identity suffix.
    // It also retains existing ##/### suffixes. ImGui hashes the ### marker
    // itself, so English must use this path even when the caption is unchanged.
    return translated + "###" + std::string(english);
}
std::string format(std::string_view id, std::initializer_list<Argument> arguments) {
    return render(text(id), arguments);
}
std::string directional_caption(std::string_view caption) {
    const auto space = caption.rfind(' ');
    if (space != caption.npos) {
        const auto direction = caption.substr(space + 1);
        if (direction == "←" || direction == "→" || direction == "↑" || direction == "↓")
            return std::string(source(caption.substr(0, space))) + std::string(caption.substr(space));
    }
    return std::string(source(caption));
}
std::string relay_caption(std::string_view caption, bool proxy) {
    if (proxy && caption.starts_with("Via "))
        return format("online.via", {{"name", caption.substr(4)}});
    if (caption == "Host")
        return std::string(text("online.host_role"));
    return std::string(source(caption));
}
std::string_view plural_category(std::string_view locale, std::uint64_t n) {
    if (locale == "ru" || locale == "uk") {
        if (n % 10 == 1 && n % 100 != 11)
            return "one";
        if (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14))
            return "few";
        return "many";
    }
    if (locale == "pl") {
        if (n == 1)
            return "one";
        if (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14))
            return "few";
        return "many";
    }
    if (locale == "fr" || locale == "pt-BR") {
        if (n <= 1)
            return "one";
        if (n % 1000000 == 0)
            return "many";
        return "other";
    }
    if ((locale == "es" || locale == "it" || locale == "pt-PT") && n && n % 1000000 == 0)
        return "many";
    return n == 1 ? "one" : "other";
}
std::string plural(std::string_view id, std::uint64_t count, std::initializer_list<Argument> arguments) {
    const auto category = plural_category(active(), count);
    auto variant = std::string(id) + '.' + std::string(category);
    if (!message(variant))
        variant = std::string(id) + ".other";
    return render(text(variant), arguments, std::to_string(count));
}
std::span<const unsigned char> fallback_font() {
    return data::font;
}
std::span<const std::uint16_t> fallback_ranges() {
    return data::glyph_ranges;
}
} // namespace dkr::localisation
