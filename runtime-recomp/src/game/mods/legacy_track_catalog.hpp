#pragma once
#include "legacy_track_artifact.hpp"
#include "legacy_character_artifact.hpp"
#include <mutex>
#include <thread>

namespace dkr::mods {
inline constexpr unsigned MaxActiveStageCharacters=MaxEnabledCharacters;
struct TrackCatalogItem {
    std::string id,name,revision,bank,artifact,group,patch;
    std::string storage,details,source_name,review;
    std::uint64_t managed_bytes=0,imported_at=0;
    unsigned carrier=0,vehicles=0,race_type=0,base_character=0;
    bool enabled=false,hidden=false;
};
struct TrackCatalogView {
    bool busy=false,modal=false,succeeded=false;
    std::string stage,result;
    unsigned completed=0,total=0;
    std::vector<TrackCatalogItem> tracks;
};
// Exact frozen selection, independent of a library's mutable enabled/hidden UI.
struct CatalogSource {std::filesystem::path library;TrackCatalogItem item;};
// Immutable UI snapshots; all decoding/preparation and catalogue I/O happens
// on the worker/job thread. Activation is only legal while the guest is stopped.
class TrackCatalog {
public:
    enum class Kind {Track,Character};
    explicit TrackCatalog(Kind kind=Kind::Track):kind_(kind){}
    ~TrackCatalog();
    void configure(std::filesystem::path root,std::filesystem::path worker);
    bool refresh();
    bool prepare_review(std::string review,std::vector<std::filesystem::path> roms);
    bool set_enabled(std::string id,bool enabled);
    bool set_hidden(std::string id,bool hidden);
    bool remove(std::string id);
    bool disable_all();
    void cancel();
    void dismiss();
    std::shared_ptr<const TrackCatalogView> snapshot() const;
    static bool has_enabled(const std::filesystem::path& root);
    // Worker-only, validated catalogue metadata for an immutable lobby export.
    static std::vector<TrackCatalogItem> selected_items(const std::filesystem::path& root,
        std::string_view revision,Kind kind);
    // Background-only read of validated installed metadata, including inactive
    // mods. Reuse never changes their activation or visibility settings.
    static std::vector<TrackCatalogItem> installed_items(const std::filesystem::path& root,
        std::string_view revision,Kind kind);
    // Worker-only preflight for keeping verified online assets. Does not
    // publish content or change activation/visibility metadata.
    static void validate_retained_merge(const std::filesystem::path& root,
        const std::filesystem::path& incoming);
    static std::vector<PreparedTrack> load_enabled(const std::filesystem::path& root,
        std::shared_ptr<const AssetBank> stock);
    static std::vector<PreparedCharacter> load_enabled_characters(const std::filesystem::path& root,
        std::shared_ptr<const AssetBank> stock);
    static std::vector<PreparedTrack> load_selected(std::span<const CatalogSource> sources,
        std::shared_ptr<const AssetBank> stock);
    static std::vector<PreparedCharacter> load_selected_characters(std::span<const CatalogSource> sources,
        std::shared_ptr<const AssetBank> stock);
private:
    const Kind kind_;
    enum class Action {Scan,Prepare,Enable,Disable,DisableAll,Hide,Restore,Remove};
    bool start(Action action,std::string id={},std::vector<std::filesystem::path> roms={});
    void publish();
    std::filesystem::path root_,worker_;
    mutable std::mutex mutex_;
    TrackCatalogView view_;
    std::shared_ptr<const TrackCatalogView> published_=std::make_shared<TrackCatalogView>();
    std::jthread thread_;
};
}
