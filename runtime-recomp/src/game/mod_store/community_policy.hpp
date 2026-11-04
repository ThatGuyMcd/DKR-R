#pragma once
#include <algorithm>
#include <cstdint>
#include <json/json.hpp>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dkr::store::community {
using Json = nlohmann::json;
inline constexpr const char* SnapshotKey = "__community_comments";
inline bool identifier(std::string_view value) {
    return !value.empty() && value.size() <= 96 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
inline bool operation(std::string_view value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
inline std::string operation_id() {
    std::random_device source;
    constexpr char hex[] = "0123456789abcdef";
    std::string value;
    for (unsigned i = 0; i < 4; ++i) {
        const auto word = std::uint32_t(source());
        for (unsigned shift = 0; shift < 32; shift += 4) value += hex[(word >> shift) & 15U];
    }
    return value;
}
inline std::string count(const Json& row, const char* key) {
    if (!row.is_object() || !row.contains(key) || !row[key].is_number_unsigned()) return "unavailable";
    auto value = std::to_string(row[key].get<std::uint64_t>());
    for (int i = int(value.size()) - 3; i > 0; i -= 3) value.insert(std::size_t(i), ",");
    return value;
}
inline std::string cursor(const Json& input) {
    const auto value = input.value("cursor", std::string{});
    if (value.size() > 220 || !std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_';
    })) throw std::runtime_error("Invalid comment page. Refresh the discussion.");
    return value;
}
inline Json page(const std::string& item, const Json& response, const std::string& next = {}) {
    if (!identifier(item) || !response.is_object() || !response.contains("comments") ||
        !response["comments"].is_array() || response["comments"].size() > 25 ||
        !response.contains("total") || !response["total"].is_number_unsigned())
        throw std::runtime_error("The library returned an invalid comment page.");
    const auto next_cursor = cursor({{"cursor", response.value("nextCursor", Json{}).is_string() ? response["nextCursor"] : Json("")}});
    for (const auto& comment : response["comments"]) {
        if (!comment.is_object() || !identifier(comment.value("id", std::string{})) ||
            comment.value("itemId", std::string{}) != item || !comment.contains("text") ||
            !comment["text"].is_string() || comment["text"].get_ref<const std::string&>().size() > 8000 ||
            !comment.contains("revision") || !comment["revision"].is_number_unsigned() ||
            comment["revision"].get<std::uint64_t>() < 1 || !comment.contains("isOwn") || !comment["isOwn"].is_boolean())
            throw std::runtime_error("The library returned an invalid comment.");
    }
    return {{"itemId", item}, {"comments", response["comments"]}, {"total", response["total"]},
        {"cursor", next}, {"nextCursor", next_cursor}, {"loading", false}, {"error", ""}};
}
inline bool current(unsigned account, unsigned expected_account, std::uint64_t serial, std::uint64_t expected_serial) {
    return account == expected_account && serial == expected_serial;
}
} // namespace dkr::store::community
