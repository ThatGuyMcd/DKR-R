#pragma once
#include "legacy_import_library.hpp"
#include "legacy_track_catalog.hpp"

namespace dkr::mods {
struct ModLibraryView {
    bool busy = false, modal = false, succeeded = false;
    unsigned completed = 0, total = 0;
    unsigned imports_completed = 0, imports_total = 0;
    std::string stage, result;
    std::shared_ptr<const ImportLibraryView> imports;
    std::shared_ptr<const TrackCatalogView> tracks, characters;
};
// Owned by the frontend thread. Tick only inspects immutable worker snapshots:
// no ROM reads, decompression or catalogue scans run in the frame loop.
class ModLibrary {
  public:
    void configure(const std::filesystem::path& root, const std::filesystem::path& worker);
    void tick();
    bool import_file(std::filesystem::path source, std::vector<std::filesystem::path> roms);
    bool prepare_review(std::string review, std::vector<std::filesystem::path> roms);
    // Serial bounded transactions; existing editions/activation/save routes
    // are never replaced. Failed imports do not abort unrelated sources.
    bool prepare_all_reviews(std::vector<std::filesystem::path> roms);
    bool set_enabled(TrackCatalog::Kind kind, std::string id, bool enabled);
    bool set_hidden(TrackCatalog::Kind kind, std::string id, bool hidden);
    bool remove(TrackCatalog::Kind kind, std::string id);
    bool disable_all(TrackCatalog::Kind kind);
    bool refresh();
    void cancel();
    void dismiss();
    ModLibraryView snapshot() const;

  private:
    enum class Phase { Idle, Scan, Import, Tracks, Characters, ToggleTracks, ToggleCharacters };
    ImportLibrary imports_;
    TrackCatalog tracks_, characters_{TrackCatalog::Kind::Character};
    Phase phase_ = Phase::Idle;
    bool modal_ = false, success_ = false, cancelled_ = false, need_tracks_ = false, need_characters_ = false;
    std::string review_, result_;
    std::vector<std::filesystem::path> roms_;
    struct Source {
        std::string review;
        bool tracks = false, characters = false;
    };
    std::vector<Source> batch_;
    unsigned batch_at_ = 0, batch_ok_ = 0;
    std::string batch_result_;
    void advance();
    void next_source();
    void finish_source();
    void finish(bool success, std::string message);
};
} // namespace dkr::mods
