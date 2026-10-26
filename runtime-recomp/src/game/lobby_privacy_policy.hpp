#pragma once
#include <array>
#include <string>
#include <string_view>
namespace dkr::runtime::ui::privacy {
inline std::string redact(std::string_view text, bool hidden, std::array<std::string_view, 2> codes) {
    std::string result(text);
    if (!hidden)
        return result;
    for (auto code : codes) {
        if (code.size() != 5)
            continue;
        for (std::size_t at = 0; (at = result.find(code, at)) != std::string::npos; at += 5)
            result.replace(at, 5, "*****");
    }
    return result;
}
} // namespace dkr::runtime::ui::privacy
