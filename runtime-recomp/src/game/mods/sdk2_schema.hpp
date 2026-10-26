#pragma once
#include "magic_code_package.hpp"
namespace dkr::mods::code {
inline void sdk2_schema(const Package& package) {
    const auto found = package.resources.find("tools-ui");
    if (found == package.resources.end())
        return;
    if (package.api != 2)
        throw Error("Tools panels require SDK API 2.");
    const auto ui = parse_document(found->second, 256 * 1024);
    if (!ui.is_object() || ui.value("schema", 0) != 2 || !ui.contains("pages") || !ui["pages"].is_array() ||
        ui["pages"].empty() || ui["pages"].size() > 16)
        throw Error("SDK tools UI must contain 1..16 dedicated pages.");
    unsigned fields = 0;
    std::set<std::string> page_ids;
    for (const auto& page : ui["pages"]) {
        if (!page.is_object() || !page.contains("id") || !page["id"].is_string() ||
            !valid_id(page["id"].get<std::string>()) || !page_ids.insert(page["id"].get<std::string>()).second ||
            !page.contains("label") || !page["label"].is_string() ||
            page["label"].get_ref<const std::string&>().size() > 80 || !page.contains("fields") ||
            !page["fields"].is_array())
            throw Error("SDK tools page has an invalid ID, label or fields list.");
        std::set<std::string> ids;
        for (const auto& field : page["fields"]) {
            if (++fields > 256 || !field.is_object() || !field.contains("id") || !field["id"].is_string() ||
                !valid_id(field["id"].get<std::string>()) || !ids.insert(field["id"].get<std::string>()).second ||
                !field.contains("label") || !field["label"].is_string() ||
                field["label"].get_ref<const std::string&>().size() > 160)
                throw Error("SDK tools field has an invalid ID/label or exceeds the 256-field budget.");
            const auto type = field.value("type", std::string{});
            const std::set<std::string> types{"text",      "button",   "number",  "integer", "toggle",    "enum",
                                              "vector",    "colour",   "search",  "table",   "inspector", "diagnostics",
                                              "authority", "document", "console", "camera",  "debug",     "reload"};
            if (type == "reload" && !package.modules.contains("reload2"))
                throw Error("Live reload UI requires reload2.");
            if (!types.contains(type))
                throw Error("Unknown SDK tools control: " + type);
            if ((type == "camera" || type == "debug") && !package.modules.contains("presentation2"))
                throw Error("Local camera/debug tools require presentation2.");
            if (type == "document" && !package.modules.contains("documents2"))
                throw Error("Document tools require documents2.");
            if (type == "document" &&
                (!field.contains("document") || !field["document"].is_string() ||
                 !valid_id(field["document"].get<std::string>()) || !field.contains("import_action") ||
                 !field["import_action"].is_number_unsigned() || field["import_action"].get<unsigned>() < 1 ||
                 field["import_action"].get<unsigned>() >= 65532))
                throw Error("SDK document needs a scoped ID and import action below 65532.");
            for (const auto* flag : {"readonly", "confirm", "select", "racer_only"})
                if (field.contains(flag) && !field[flag].is_boolean())
                    throw Error("SDK tools flag must be boolean.");
            if (field.contains("edit_action") &&
                (!field["edit_action"].is_number_unsigned() || field["edit_action"].get<std::uint64_t>() < 1 ||
                 field["edit_action"].get<std::uint64_t>() > 65535))
                throw Error("SDK inspector action ID is invalid.");
            for (const auto* caption : {"help", "visible"})
                if (field.contains(caption) &&
                    (!field[caption].is_string() || field[caption].get_ref<const std::string&>().size() > 512))
                    throw Error("SDK tools caption must be bounded text.");
            if (type == "button" &&
                (!field.contains("action") || !field["action"].is_number_unsigned() ||
                 field["action"].get<std::uint64_t>() < 1 || field["action"].get<std::uint64_t>() >= 65532))
                throw Error("SDK tools button needs an action ID in 1..65535.");
            if (field.contains("argument") &&
                (!field["argument"].is_number_unsigned() || field["argument"].get<unsigned>() > 11))
                throw Error("SDK tools argument must be 0..11; the final word is scene ownership.");
            if (type == "enum" && (!field.contains("choices") || !field["choices"].is_array() ||
                                   field["choices"].empty() || field["choices"].size() > 128))
                throw Error("SDK enum needs 1..128 choices.");
            if (type == "enum")
                for (const auto& choice : field["choices"])
                    if (!choice.is_string() || choice.get_ref<const std::string&>().size() > 160)
                        throw Error("SDK enum choice is invalid.");
            for (const auto* range : {"min", "max", "default"})
                if (field.contains(range) && (!field[range].is_number() || !std::isfinite(field[range].get<double>())))
                    throw Error("SDK tools numeric range must be finite.");
            for (const auto* range : {"min", "max", "default"})
                if (field.contains(range) && std::abs(field[range].get<double>()) > 1e7)
                    throw Error("SDK tools numeric range exceeds its portable profile.");
            if (field.value("min", 0.0) > field.value("max", 100.0))
                throw Error("SDK tools numeric range is reversed.");
            if (type == "vector" || type == "colour")
                if (field.value("argument", 13U) > (type == "vector" ? 9U : 8U))
                    throw Error("SDK vector/colour arguments exceed the command record.");
        }
    }
}
} // namespace dkr::mods::code
