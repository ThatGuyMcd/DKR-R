#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <algorithm>

namespace dkr::runtime::custom_tracks::files {
inline bool member(std::string_view name) {
    if (name.empty() || name.size() > 240 || name.front() == '/' || name.back() == '/' ||
        name.find('\\') != name.npos || name.find(':') != name.npos)
        return false;
    for (unsigned char c : name)
        if (c < 32 || c == 127 || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    for (std::size_t at = 0; at < name.size();) {
        const auto end = name.find('/', at);
        const auto part = name.substr(at, end == name.npos ? name.size() - at : end - at);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
            return false;
        std::string stem(part.substr(0, part.find('.')));
        for (auto& c : stem)
            if (c >= 'a' && c <= 'z')
                c = char(c - 'a' + 'A');
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' &&
             stem[3] <= '9'))
            return false;
        if (end == name.npos)
            break;
        at = end + 1;
    }
    return true;
}
inline std::filesystem::path storage(const std::filesystem::path& input) {
    auto path = std::filesystem::absolute(input).lexically_normal().make_preferred();
#ifdef _WIN32
    const auto text = path.native();
    if (text.starts_with(L"\\\\?\\"))
        return path;
    return std::filesystem::path(text.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + text.substr(2) : L"\\\\?\\" + text);
#else
    return path;
#endif
}
inline bool resolve(const std::filesystem::path& root, const std::string& name, std::filesystem::path& result,
                    std::string& error) {
    if (!member(name)) {
        error = "unsafe package filename: " + name;
        return false;
    }
    std::error_code ec;
    const auto base = std::filesystem::canonical(storage(root), ec);
    if (ec) {
        error = "the course folder is unavailable: " + ec.message();
        return false;
    }
    result = std::filesystem::weakly_canonical(base / std::filesystem::u8path(name), ec);
    if (ec) {
        error = "cannot resolve " + name + ": " + ec.message();
        return false;
    }
    const auto ends = std::mismatch(base.begin(), base.end(), result.begin(), result.end());
    if (ends.first != base.end() || result == base) {
        error = name + " resolves outside the course folder";
        return false;
    }
    result = storage(result);
    if (!std::filesystem::is_regular_file(result, ec) || ec) {
        error = "required file \"" + name + "\" is missing or unreadable" + (ec ? ": " + ec.message() : std::string{});
        return false;
    }
    return true;
}
} // namespace dkr::runtime::custom_tracks::files
