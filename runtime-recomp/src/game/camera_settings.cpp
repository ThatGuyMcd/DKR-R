#include "camera_settings.hpp"
#include <cmath>
#include <set>

namespace dkr::runtime::camera {
namespace {
[[noreturn]] void invalid(const char* text) {
    throw mods::Error(std::string("Camera settings: ") + text);
}
std::string label(const Json& value, const char* key) {
    const auto& text = value.at(key);
    if (!text.is_string() || text.get_ref<const std::string&>().empty() ||
        text.get_ref<const std::string&>().size() > 160 ||
        text.get_ref<const std::string&>().find('\0') != std::string::npos)
        invalid("invalid editor label");
    return text.get<std::string>();
}
} // namespace
void SettingsChannel::publish(const std::array<Json, 4>& views) {
    auto next = std::make_shared<LocalConfiguration>();
    next->views = views;
    if (++generation_ == 0)
        ++generation_;
    next->generation = generation_;
    current_.store(std::move(next));
}
SettingsEditor::SettingsEditor(std::shared_ptr<const mods::code::Package> package, const Json& base,
                               const Json& documents, SettingsChannel& channel)
    : package_(std::move(package)), channel_(channel), documents_(mods::code::structured_data(documents)) {
    if (!package_ || !package_->modules.contains("presentation"))
        invalid("not a presentation package");
    const auto defaults = mods::code::configuration(*package_, Json::object());
    for (auto& view : draft_)
        view = mods::code::configuration(*package_, base);
    if (documents_.contains("camera_profiles"))
        draft_ = decode(documents_.at("camera_profiles"));
    applied_ = saved_ = draft_;
    const auto resource = package_->resources.find("camera_editor");
    if (resource == package_->resources.end())
        invalid("missing editor schema");
    const auto schema = mods::code::parse_document(resource->second, 128 * 1024);
    if (!schema.contains("groups") || !schema["groups"].is_array() || schema["groups"].size() > 16)
        invalid("invalid editor groups");
    std::map<std::string, Json> options;
    for (const auto& option : package_->options)
        options.emplace(option.at("id").get<std::string>(), option);
    std::set<std::string> seen;
    for (const auto& group : schema["groups"]) {
        EditorGroup entry;
        entry.name = label(group, "name");
        const auto& fields = group.at("options");
        if (!fields.is_array() || fields.empty() || fields.size() > 128)
            invalid("invalid group options");
        for (const auto& field : fields) {
            EditorField out;
            out.id = label(field, "id");
            out.name = label(field, "name");
            const auto found = options.find(out.id);
            if (found == options.end() || !seen.insert(out.id).second)
                invalid("unknown or duplicate editor option");
            const auto& canonical = found->second;
            out.type = canonical.at("type").get<std::string>();
            if (field.value("type", std::string("Number")) != out.type)
                invalid("editor type differs from executable manifest");
            if (mods::code::configuration(*package_, Json{{out.id, field.at("default")}}).at(out.id) !=
                defaults.at(out.id))
                invalid("editor default differs from executable manifest");
            if (out.type == "Number") {
                out.minimum = canonical.at("min").get<double>();
                out.maximum = canonical.at("max").get<double>();
                if (field.at("min") != canonical.at("min") || field.at("max") != canonical.at("max"))
                    invalid("editor bounds differ from executable manifest");
                out.step = field.value("step", out.maximum - out.minimum <= 2    ? 0.05
                                               : out.maximum - out.minimum <= 10 ? 0.1
                                                                                 : 1.0);
                if (!std::isfinite(out.step) || out.step <= 0 || out.step > 1.e7)
                    invalid("invalid editor step");
            } else if (out.type == "Enum") {
                if (field.at("options") != canonical.at("options"))
                    invalid("editor choices differ from executable manifest");
                out.choices = canonical.at("options").get<std::vector<std::string>>();
            } else if (out.type != "String")
                invalid("unsupported editor field type");
            entry.fields.push_back(std::move(out));
        }
        groups_.push_back(std::move(entry));
    }
    if (seen.size() != options.size())
        invalid("editor omits executable settings");
    const auto presets = schema.value("presets", Json::array());
    if (!presets.is_array() || presets.size() > 32)
        invalid("invalid presets");
    std::set<std::string> names;
    for (const auto& preset : presets) {
        EditorPreset out;
        out.name = label(preset, "name");
        if (!names.insert(out.name).second)
            invalid("duplicate preset name");
        out.defaults = preset.value("defaults", false);
        out.values = preset.at("values");
        (void)mods::code::configuration(*package_, out.values);
        presets_.push_back(std::move(out));
    }
    channel_.publish(applied_);
}
std::array<Json, 4> SettingsEditor::decode(const Json& document) const {
    if (!document.is_object() || document.value("schema", 0) != 1 || !document.contains("views") ||
        !document["views"].is_array() || document["views"].size() != 4)
        invalid("invalid saved view profiles");
    std::array<Json, 4> profiles;
    for (unsigned i = 0; i < 4; ++i)
        profiles[i] = mods::code::configuration(*package_, document["views"][i]);
    return profiles;
}
bool SettingsEditor::edit(unsigned view, std::string_view id, const Json& value) {
    try {
        if (view >= 4)
            invalid("invalid local player");
        auto next = draft_[view];
        next[std::string(id)] = value;
        next = mods::code::configuration(*package_, next);
        draft_[view] = std::move(next);
        error_.clear();
        return true;
    } catch (const std::exception& e) {
        error_ = e.what();
        return false;
    }
}
bool SettingsEditor::preset(unsigned view, unsigned index) {
    try {
        if (view >= 4 || index >= presets_.size())
            invalid("invalid preset selection");
        const auto& preset = presets_[index];
        auto next = preset.defaults ? mods::code::configuration(*package_, Json::object()) : draft_[view];
        for (const auto& [key, value] : preset.values.items())
            next[key] = value;
        draft_[view] = mods::code::configuration(*package_, next);
        error_.clear();
        return true;
    } catch (const std::exception& e) {
        error_ = e.what();
        return false;
    }
}
void SettingsEditor::reset(unsigned view) {
    draft_.at(view) = mods::code::configuration(*package_, Json::object());
}
void SettingsEditor::apply() {
    channel_.publish(draft_);
    applied_ = draft_;
    error_.clear();
}
void SettingsEditor::revert() {
    draft_ = saved_;
    apply();
}
Json SettingsEditor::save_document() const {
    auto out = documents_;
    out["camera_profiles"] = {{"schema", 1}, {"views", applied_}};
    return mods::code::structured_data(out);
}
void SettingsEditor::saved(const Json& document) {
    auto checked = mods::code::structured_data(document);
    auto profiles = decode(checked.at("camera_profiles"));
    documents_ = std::move(checked);
    saved_ = std::move(profiles);
}
} // namespace dkr::runtime::camera
