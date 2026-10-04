#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace RT64 {
struct Application;
}

namespace dkr::runtime::texture_packs {

enum class Format {
    NativeRt64,
    RiceRt64,
    LegacyRice,
    LegacyJabo,
    Unknown,
};

// Where a managed pack came from. A User pack is one the player imported through
// the texture-pack library and browses there. A TrackPack is the <track>-hd.zip
// that shipped beside a custom track: it is filed against that track, kept out
// of the browser's default list, and stays enabled for the life of the track.
enum class Origin {
    User,
    TrackPack,
};

struct PackInfo {
    std::string id;
    std::string name;
    std::filesystem::path path;
    std::string zip_base_path;
    Format format = Format::Unknown;
    bool compatible = false;
    bool enabled = false;
    bool hidden = false;
    std::size_t image_count = 0;
    std::uintmax_t managed_size_bytes = 0;
    std::int64_t imported_at_unix_seconds = 0;
    std::string detail;
    Origin origin = Origin::User;
    std::string owner_track_id;   // custom track id, when origin == TrackPack
    std::string texture_digest;   // digest shared with the track, TrackPack only
};

struct ImportProgress {
    float fraction = 0.0F;
    std::string stage;
    bool commit_started = false;
};

// Return false from the callback to cancel before the commit begins. Once
// commit_started is true the managed destination is renamed atomically and
// cancellation is deliberately ignored so an import can never be left half
// installed.
using ImportProgressCallback = std::function<bool(const ImportProgress&)>;

void configure(const std::filesystem::path& config_directory);
// Starts one non-blocking validation pass when the texture library is first
// opened. Enabled packs are already validated synchronously by configure().
void request_background_refresh();
void refresh();
// Monotonically increases whenever the library or its visible state changes.
// UI code can use this cheap value to avoid copying and sorting an unchanged
// texture-pack catalog every rendered frame.
std::uint64_t generation();
std::vector<PackInfo> snapshot(bool include_hidden = false);

// Identifies the custom track a <track>-hd.zip belongs to. When passed to
// import_archive the pack is filed against the track (Origin::TrackPack), born
// enabled, kept out of the browser's default list, and re-importing the same
// archive (same entries, same contents) is a no-op - the authoring loop
// rescans on every save. The texture_digest alone is not enough to call two
// packs the same: it covers the 64x32 payloads, not the replacement names.
struct TrackPackOwner {
    std::string track_id;
    std::string texture_digest;
};

bool import_archive(const std::filesystem::path& source, std::string& status,
                    const ImportProgressCallback& progress = {},
                    const TrackPackOwner* owner = nullptr);

// What the browser filter and the Track Lab status line need: whether this
// track's HD pack is installed, enabled, and from the export the track's
// manifest names (digest match). pack_id is empty when nothing is installed.
struct TrackPackState {
    bool installed = false;
    bool enabled = false;
    bool digest_matches = false;
    std::string pack_id;
};
TrackPackState track_pack_state(const std::string& track_id,
                                const std::string& expected_digest);

// Permanently removes the TrackPack filed against `track_id`, if any. For use
// when a custom track is deleted, so its pack does not linger enabled and
// ownerless. A no-op when the track has no pack.
bool forget_track_pack(const std::string& track_id, std::string& status);

void set_enabled(const std::string& id, bool enabled);
bool toggle_last_selected(std::string& status);
bool set_hidden(const std::string& id, bool hidden, std::string& status);
bool delete_managed(const std::string& id, std::string& status);
void request_reload();
// Called on the renderer thread after a fresh RT64 Application is ready.
// Applied-cache bookkeeping belongs to that instance, not the saved settings.
void renderer_started();
void apply_pending(RT64::Application& application, bool modern_profile);
std::string status();

const char* format_name(Format format);

} // namespace dkr::runtime::texture_packs
