#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <json/json.hpp>

namespace dkr::file_picker {
using Json = nlohmann::json;
inline constexpr size_t MaxMessage = 128 * 1024;
inline void validate(const Json& request) {
    if (!request.is_object() || request.value("schema", 0) != 1 ||
        (request.value("kind", "") != "open" && request.value("kind", "") != "save" &&
         request.value("kind", "") != "folder"))
        throw std::runtime_error("Invalid picker request.");
    for (const auto* key : {"initial", "name"})
        if (!request.contains(key) || !request[key].is_string() ||
            request[key].get_ref<const std::string&>().size() > 32768 ||
            request[key].get_ref<const std::string&>().find('\0') != std::string::npos)
            throw std::runtime_error("Invalid picker text.");
    if (!request.contains("filters") || !request["filters"].is_array() || request["filters"].size() > 32)
        throw std::runtime_error("Invalid picker filters.");
    for (const auto& filter : request["filters"])
        for (const auto* key : {"name", "spec"})
            if (!filter.is_object() || !filter.contains(key) || !filter[key].is_string() ||
                filter[key].get_ref<const std::string&>().size() > 4096 ||
                filter[key].get_ref<const std::string&>().find('\0') != std::string::npos)
                throw std::runtime_error("Invalid picker filter text.");
    if (request.dump().size() > MaxMessage)
        throw std::runtime_error("Picker request is too large.");
}
Json exchange(const std::filesystem::path& helper, const Json& request, unsigned timeout_ms = 1800000);
} // namespace dkr::file_picker
