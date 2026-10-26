#pragma once
#include "camera_presentation.hpp"
#include "atomic_snapshot.hpp"
#include <array>
#include <map>

namespace dkr::runtime::camera {
using Json = mods::code::Json;
struct EditorField {
    std::string id, name, type;
    double minimum = 0, maximum = 0, step = 1;
    std::vector<std::string> choices;
};
struct EditorGroup {
    std::string name;
    std::vector<EditorField> fields;
};
struct EditorPreset {
    std::string name;
    bool defaults = false;
    Json values;
};
struct LocalConfiguration {
    std::uint64_t generation = 0;
    std::array<Json, 4> views;
};
// One UI writer publishes immutable configurations; the presentation owner
// acquires one for a whole view batch, never halfway through a stereo pair.
class SettingsChannel final {
  public:
    std::shared_ptr<const LocalConfiguration> current() const {
        return current_.load();
    }
    void publish(const std::array<Json, 4>&);

  private:
    std::uint64_t generation_ = 0;
    AtomicSnapshot<const LocalConfiguration> current_;
};
// Generic presentation editor model. Storage belongs to LibraryJobs, never
// this model or the guest mod. No filesystem work occurs while drawing UI.
class SettingsEditor final {
  public:
    SettingsEditor(std::shared_ptr<const mods::code::Package>, const Json& base, const Json& documents,
                   SettingsChannel&);
    const std::vector<EditorGroup>& groups() const {
        return groups_;
    }
    const std::vector<EditorPreset>& presets() const {
        return presets_;
    }
    const Json& draft(unsigned view) const {
        return draft_.at(view);
    }
    bool edit(unsigned view, std::string_view id, const Json&);
    bool preset(unsigned view, unsigned preset);
    void reset(unsigned view);
    void revert();
    void apply();
    bool pending() const {
        return draft_ != applied_;
    }
    bool unsaved() const {
        return applied_ != saved_;
    }
    const std::string& error() const {
        return error_;
    }
    // Schedule LibraryJobs::data with this document. Acknowledge ONLY after
    // successful storage, using this exact captured document. Newer edits
    // made while the worker ran must remain correctly marked unsaved.
    Json save_document() const;
    void saved(const Json& exact_document);

  private:
    std::shared_ptr<const mods::code::Package> package_;
    SettingsChannel& channel_;
    Json documents_;
    std::array<Json, 4> draft_, applied_, saved_;
    std::vector<EditorGroup> groups_;
    std::vector<EditorPreset> presets_;
    std::string error_;
    std::array<Json, 4> decode(const Json&) const;
};
} // namespace dkr::runtime::camera
