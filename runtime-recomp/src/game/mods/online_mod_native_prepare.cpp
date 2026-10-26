#include "online_mod_prepare.hpp"
#include "online_mod_bundle.hpp"
#include "../custom_tracks.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <set>

namespace dkr::mods::online {
void prepare_track_lab_library(const Manifest& input, const std::filesystem::path& input_payload_directory,
                               const std::filesystem::path& input_destination, std::stop_token stop,
                               const ProgressCallback& progress) {
    const auto manifest = canonical(input);
    if (!manifest.code.empty() || manifest.payloads.empty() ||
        std::any_of(manifest.payloads.begin(), manifest.payloads.end(),
                    [](const auto& p) { return p.kind != PayloadKind::TrackLab; }) ||
        std::any_of(manifest.content.begin(), manifest.content.end(),
                    [](const auto& c) { return c.kind != Kind::TrackLab; }))
        throw Error("Native course preparation requires only Track Lab packages.");
    const auto payloads = private_storage_path(input_payload_directory),
               destination = private_storage_path(input_destination);
    check_storage(payloads, true);
    check_storage(destination.parent_path(), true);
    if (std::filesystem::exists(destination))
        throw Error("Native course preparation needs fresh private storage.");
    const auto folder = [](const auto& path) {
        std::filesystem::create_directory(path);
        check_storage(path, true);
    };
    folder(destination);
    folder(destination / "legacy");
    folder(destination / "tracks");
    std::set<std::string> found;
    unsigned index = 0, files = 0;
    std::uint64_t staged_bytes = 0;
    for (const auto& payload : manifest.payloads) {
        if (stop.stop_requested())
            throw Error("Native course preparation cancelled.");
        ++index;
        if (progress &&
            !progress({index, static_cast<unsigned>(manifest.payloads.size()), "Verifying course files and music"}))
            throw Error("Native course preparation cancelled.");
        check_storage(payloads / payload.digest, false);
        const auto bytes = read_file(payloads / payload.digest, static_cast<std::size_t>(payload.size));
        if (bytes.size() != payload.size || sha256(bytes) != payload.digest)
            throw Error("Downloaded course failed its SHA/size check.");
        // Keep the payload name stable across jobs and unique when several
        // per-payload caches are retained together in the offline library.
        const auto directory = destination / "tracks" / (payload.digest + ".dkrmap");
        unpack_track_lab(bytes, directory, stop);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (++files > 32768)
                throw Error("Native course preparation exceeds its file-count budget.");
            check_storage(entry.path(), entry.is_directory());
            if (entry.is_regular_file()) {
                const auto size = entry.file_size();
                if (size > MaxStaged - staged_bytes)
                    throw Error("Native course preparation exceeds its storage budget.");
                staged_bytes += size;
            }
        }
        dkr::runtime::custom_tracks::Track track;
        std::string error;
        if (!dkr::runtime::custom_tracks::inspect_package(directory, track, error))
            throw Error("Downloaded course files or music are invalid: " + error);
        const auto key = std::string("dkrmap:") + track.id;
        const auto id = sha256(View(reinterpret_cast<const std::uint8_t*>(key.data()), key.size()));
        const auto expected =
            std::find_if(manifest.content.begin(), manifest.content.end(), [&](const auto& c) { return c.id == id; });
        if (expected == manifest.content.end() || expected->name != track.name || expected->payload != payload.digest ||
            expected->bank != payload.digest ||
            expected->artifact != sha256(read_file(directory / "manifest.json", MiB)) || !found.insert(id).second)
            throw Error("Downloaded course differs from the host's exact selection.");
    }
    if (stop.stop_requested())
        throw Error("Native course preparation cancelled.");
    if (found.size() != manifest.content.size())
        throw Error("Native course preparation is missing required host tracks.");
    // No executable import is involved. Bounded package readers and the final
    // profile/resource gates validate every file before this can be activated.
    const auto receipt = nlohmann::json{{"schema", ManifestSchema}, {"manifest", identity(manifest)}}.dump();
    write_new_file(destination / "verified.json",
                   View(reinterpret_cast<const std::uint8_t*>(receipt.data()), receipt.size()));
}
} // namespace dkr::mods::online
