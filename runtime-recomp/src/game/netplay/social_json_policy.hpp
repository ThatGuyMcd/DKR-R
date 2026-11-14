#pragma once
#include <nlohmann/json.hpp>
#include <string_view>

namespace dkr::runtime::netplay {
// Validate before typed extraction so a valid JSON document with invalid field
// types cannot partially mutate authentication or lobby state.
inline bool valid_social_message(const nlohmann::json& message, unsigned depth = 0) {
    if (depth > 1U)
        return false;
    if (!message.is_object() || message.size() > 32U)
        return false;
    for (const auto key : {"kind", "public", "name", "nonce", "invite", "value", "reason", "code", "synchronization",
                           "compatibility", "admission", "action", "capability"}) {
        if (message.contains(key) &&
            (!message[key].is_string() || message[key].get_ref<const std::string&>().size() > 1024U))
            return false;
    }
    for (const auto key : {"protocol", "request_protocol", "request_id", "id", "expires", "players", "maximum"})
        if (message.contains(key) && !message[key].is_number_unsigned())
            return false;
    for (const auto key : {"online", "hosting", "legacy"})
        if (message.contains(key) && !message[key].is_boolean())
            return false;
    if (message.contains("lobby") && !valid_social_message(message["lobby"], depth + 1U))
        return false;
    return true;
}

inline bool valid_signaling_message(const nlohmann::json& message) {
    if (!message.is_object() || message.size() > 32U)
        return false;
    for (const auto key : {"type", "src", "dst"})
        if (message.contains(key) &&
            (!message[key].is_string() || message[key].get_ref<const std::string&>().size() > 128U))
            return false;
    if (!message.contains("payload"))
        return true;
    const auto& payload = message["payload"];
    if (!payload.is_object())
        return false;
    if (payload.contains("connectionId") &&
        (!payload["connectionId"].is_string() || payload["connectionId"].get_ref<const std::string&>().size() > 128U))
        return false;
    for (const auto key : {"sdp", "candidate"}) {
        if (!payload.contains(key))
            continue;
        const auto& object = payload[key];
        if (!object.is_object())
            return false;
        for (const auto field : {"sdp", "type", "candidate", "sdpMid"})
            if (object.contains(field) &&
                (!object[field].is_string() || object[field].get_ref<const std::string&>().size() > 49152U))
                return false;
    }
    return true;
}
} // namespace dkr::runtime::netplay
