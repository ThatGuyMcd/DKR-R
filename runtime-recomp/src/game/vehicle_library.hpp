#pragma once
#include "vehicle_package.hpp"
#include <mutex>
#include <stop_token>

namespace dkr::mods::vehicles {
struct Item {
    std::string id, digest, name, source_name;
    Type type = Type::Car;
    bool enabled = false;
    Image portrait;
};
struct Frozen {
    std::vector<std::shared_ptr<const Package>> packages;
    std::string identity;
};
enum class ImportPolicy { RejectDuplicates, KeepBoth, ReplaceSameName };
struct ImportConflict {
    std::string incoming, name;
    std::vector<std::string> installed;
    bool operator==(const ImportConflict&) const = default;
};
struct ImportBatch {
    std::vector<std::shared_ptr<const Package>> packages;
    std::vector<ImportConflict> conflicts;
};
// Blocking filesystem work; launch preparation and the UI job queue are its
// only callers. Gameplay and replay never open a package or a library file.
class Library {
  public:
    explicit Library(std::filesystem::path root) : root_(std::move(root)) {}
    std::vector<Item> scan(std::stop_token = {}) const;
    std::vector<std::string> import_file(const std::filesystem::path&);
    ImportBatch prepare_import(const std::filesystem::path&) const;
    std::vector<std::string> import_prepared(const ImportBatch&, ImportPolicy);
    std::string import(Input, bool enable = true);
    void enable(std::string_view digest, bool);
    void remove(std::string_view digest);
    Frozen freeze(std::stop_token = {}) const;
    std::shared_ptr<const Package> package(std::string_view digest) const;
    const std::filesystem::path& root() const {
        return root_;
    }

  private:
    std::filesystem::path root_;
};
Frozen freeze_packages(std::vector<std::shared_ptr<const Package>>);
} // namespace dkr::mods::vehicles
