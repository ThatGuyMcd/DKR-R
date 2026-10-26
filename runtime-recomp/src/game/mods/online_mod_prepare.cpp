#include "online_mod_prepare.hpp"
#include "online_mod_bundle.hpp"
#include "legacy_track_prepare_job.hpp"
#include "../custom_tracks.hpp"
#include <json/json.hpp>
#include <map>
#include <set>
#include <algorithm>

namespace dkr::mods::online {
namespace {
using nlohmann::json;
void write(const std::filesystem::path& path, const json& doc) {
    const auto bytes = doc.dump();
    if (bytes.size() > MaxManifestBytes)
        throw Error("Online mod preparation metadata exceeds its limit.");
    write_new_file(path, View(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}
json read(const std::filesystem::path& path, std::size_t limit = MiB) {
    check_storage(path, false);
    const auto bytes = read_file(path, limit);
    return json::parse(bytes.begin(), bytes.end(), [](int depth, json::parse_event_t, json&) {
        if (depth > 12)
            throw Error("Online mod receipt nesting exceeds its limit.");
        return true;
    });
}
void folder(const std::filesystem::path& path) {
    std::filesystem::create_directory(path);
    check_storage(path, true);
}
void storage_budget(const std::filesystem::path& path) {
    std::uint64_t bytes = 0;
    unsigned count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
        if (++count > 32768)
            throw Error("Online preparation exceeded its private file-count budget.");
        check_storage(entry.path(), entry.is_directory());
        if (!entry.is_regular_file())
            continue;
        const auto size = entry.file_size();
        if (size > std::uint64_t(1024 * MiB) - bytes)
            throw Error("Online preparation exceeded its 1 GiB private-storage budget.");
        bytes += size;
    }
}
} // namespace
void prepare_library(const Manifest& input, const std::filesystem::path& input_payload_directory,
                     const std::vector<std::filesystem::path>& owned_roms,
                     const std::filesystem::path& input_destination, const ProgressCallback& progress,
                     ImportPreparation* shared_sources) {
    const auto manifest = canonical(input);
    const auto digest = identity(manifest);
    if (!manifest.payloads.empty() && std::all_of(manifest.payloads.begin(), manifest.payloads.end(),
                                                  [](const auto& p) { return p.kind == PayloadKind::TrackLab; })) {
        prepare_track_lab_library(manifest, input_payload_directory, input_destination, {}, progress);
        return;
    }
    if (!manifest.code.empty() || std::any_of(manifest.payloads.begin(), manifest.payloads.end(),
                                              [](const auto& p) { return p.kind == PayloadKind::Code; }))
        throw Error("Portable code packages must use the code admission path, never the legacy ROM importer.");
    const auto payload_directory = private_storage_path(input_payload_directory);
    const auto destination = private_storage_path(input_destination);
    if (owned_roms.empty() || owned_roms.size() > 8 || std::filesystem::exists(destination))
        throw Error("Online preparation needs imported original Game Paks and fresh staging.");
    check_storage(payload_directory, true);
    check_storage(destination.parent_path(), true);
    for (const auto& path : owned_roms)
        check_storage(path, false);
    ImportPreparation local;
    if (!shared_sources)
        local = load_import_sources(owned_roms);
    auto& preparation = shared_sources ? *shared_sources : local;
    // Only locally verified worker memory can be shared, never host artifacts.
    // Release the previous patch's scratch before decoding the next one.
    preparation.target.clear();
    preparation.analysis = {};
    std::set<std::string> revisions;
    for (const auto& [revision, bytes] : preparation.bases)
        revisions.insert(revision);
    if (!revisions.contains(manifest.revision))
        throw Error("Import the lobby's original Game Pak revision before preparing these mods.");
    for (const auto& payload : manifest.payloads)
        if (payload.kind == PayloadKind::Xdelta && !revisions.contains(payload.source_revision))
            throw Error(
                "A required patch source Game Pak revision is not imported. No ROM will be downloaded from the host.");
    folder(destination);
    folder(destination / "legacy");
    folder(destination / "tracks");
    const auto root = destination / "legacy";
    for (const auto* name : {"reviews", "prepared", "prepared-characters", "jobs"})
        folder(root / name);
    std::map<std::pair<Kind, std::string>, Content> expected;
    std::set<std::pair<Kind, std::string>> found;
    std::set<std::string> tracks, characters;
    for (const auto& content : manifest.content) {
        expected.emplace(std::make_pair(content.kind, content.id), content);
        if (content.kind == Kind::Track)
            tracks.insert(content.id);
        else if (content.kind == Kind::Character)
            characters.insert(content.id);
    }
    unsigned index = 0;
    for (const auto& payload : manifest.payloads) {
        ++index;
        const auto report = [&](const char* stage) {
            if (progress && !progress({index, static_cast<unsigned>(manifest.payloads.size()), stage}))
                throw Error("Online mod preparation cancelled.");
        };
        report("Verifying downloaded mod payloads");
        const auto path = payload_directory / payload.digest;
        check_storage(path, false);
        const auto bytes = read_file(path, static_cast<std::size_t>(payload.size));
        if (bytes.size() != payload.size || sha256(bytes) != payload.digest)
            throw Error("Downloaded online mod payload verification failed.");
        if (payload.kind == PayloadKind::TrackLab) {
            report("Validating Track Lab course assets");
            const auto dir = destination / "tracks" / (payload.digest + ".dkrmap");
            unpack_track_lab(bytes, dir);
            std::string error;
            dkr::runtime::custom_tracks::Track track;
            if (!dkr::runtime::custom_tracks::inspect_package(dir, track, error))
                throw Error("Downloaded Track Lab course is invalid: " + error);
            const auto prefix = std::string("dkrmap:") + track.id;
            const auto id = sha256(View(reinterpret_cast<const std::uint8_t*>(prefix.data()), prefix.size()));
            const auto key = std::make_pair(Kind::TrackLab, id);
            const auto entry = expected.find(key);
            if (entry == expected.end() || entry->second.payload != payload.digest ||
                entry->second.name != track.name || entry->second.bank != payload.digest ||
                entry->second.artifact != sha256(read_file(dir / "manifest.json", MiB)))
                throw Error("Downloaded Track Lab course does not match the frozen lobby manifest.");
            found.insert(key);
            storage_budget(destination);
            continue;
        }
        const auto job = root / "jobs" / payload.digest;
        folder(job);
        const auto patch = job / (payload.digest + ".xdelta");
        write_new_file(patch, bytes);
        std::map<std::string, std::string> labels;
        if (!payload.source_label.empty())
            labels.emplace(payload.digest, payload.source_label);
        report("Reconstructing mods against locally imported Game Paks");
        reconstruct_import(preparation, bytes);
        stage_import(patch, owned_roms, job / "review", progress, labels, &preparation);
        const auto review_bytes = read_file(job / "review" / "review.json", 8 * MiB);
        const auto review_hash = sha256(review_bytes);
        const auto review = root / "reviews" / review_hash;
        if (std::filesystem::exists(review))
            throw Error("Duplicate online review publication.");
        std::filesystem::rename(job / "review", review);
        for (const auto kind : {Kind::Track, Kind::Character}) {
            bool required = false, behaviour_enabled = false, asset_only = false;
            for (const auto& content : manifest.content)
                if (content.payload == payload.digest && content.kind == kind) {
                    required = true;
                    behaviour_enabled |= content.behaviour_abi != 0;
                    asset_only |= content.behaviour_abi == 0;
                }
            if (!required)
                continue;
            // Old installed artifacts keep their explicitly frozen asset-only
            // policy and identities. Never silently discard a host's behaviour
            // contract if a new adapter fails. A mixed package can legitimately
            // contain one adapted racer and another purely visual racer. Build
            // each explicitly requested policy in its own transaction; no
            // failure ever causes a retry under a more permissive policy.
            for (const bool enabled : {false, true}) {
                if (!(enabled ? behaviour_enabled : asset_only))
                    continue;
                report(kind == Kind::Track ? "Preparing exact custom course selection"
                                           : "Preparing exact custom character selection");
                // Old host selections retain v2 IDs/save associations. Rebuild
                // that exact edition only when requested, never accept a mismatch.
                for (const bool previous_portraits : {false, true}) {
                    if (previous_portraits && kind != Kind::Character)
                        continue;
                    bool pending = false;
                    for (const auto& content : manifest.content)
                        if (content.payload == payload.digest && content.kind == kind &&
                            (content.behaviour_abi != 0) == enabled && !found.contains({kind, content.id}))
                            pending = true;
                    if (!pending)
                        continue;
                    const auto prepared = job / (std::string(previous_portraits ? "previous-portraits-" : "") +
                                                 std::string(kind == Kind::Track ? "tracks" : "characters") +
                                                 (enabled ? "-behaviour" : "-assets"));
                    if (kind == Kind::Track)
                        prepare_imported_tracks(review, owned_roms, prepared, progress, &preparation, enabled);
                    else
                        prepare_imported_characters(review, owned_roms, prepared, progress, &preparation, enabled,
                                                    previous_portraits);
                    auto receipt = read(prepared / "prepared.json");
                    const char* entries = kind == Kind::Track ? "tracks" : "characters";
                    auto selected = json::array();
                    for (const auto& item : receipt.at(entries)) {
                        if (kind == Kind::Track && item.at("revision") != manifest.revision)
                            continue;
                        const auto key = std::make_pair(kind, item.at("id").get<std::string>());
                        const auto entry = expected.find(key);
                        if (entry == expected.end() || entry->second.payload != payload.digest || found.contains(key))
                            continue;
                        const auto& wanted = entry->second;
                        if ((wanted.behaviour_abi != 0) != enabled)
                            continue;
                        if (item.at("patch") != wanted.payload || item.at("artifact") != wanted.artifact ||
                            item.at("name") != wanted.name ||
                            item.value("behaviour_identity", std::string{}) != wanted.behaviour ||
                            item.value("behaviour_abi", 0U) != wanted.behaviour_abi ||
                            (kind == Kind::Track ? item.at("bank").get<std::string>() : wanted.id) != wanted.bank ||
                            !found.insert(key).second)
                            throw Error("Locally prepared mod assets differ from the frozen host selection.");
                        selected.push_back(item);
                    }
                    // Do not expose extra roots or duplicate asset-only editions from
                    // the other policy as members of the frozen session collection.
                    // A corrected pass may have no roots matching an old host ID.
                    // Leave that scratch unpublished; empty groups are not catalogues.
                    if (selected.empty())
                        continue;
                    receipt[entries] = std::move(selected);
                    const auto original_receipt = prepared / "prepared.json";
                    const auto retained_receipt = prepared / "prepared-all.json";
                    std::filesystem::rename(original_receipt, retained_receipt);
                    write(original_receipt, receipt);
                    const auto hash = sha256(read_file(prepared / "prepared.json", MiB));
                    std::filesystem::rename(prepared,
                                            root / (kind == Kind::Track ? "prepared" : "prepared-characters") / hash);
                }
            }
        }
        storage_budget(destination);
    }
    if (found.size() != expected.size())
        throw Error("Not all required mods produced compatible assets for the lobby's Game Pak revision.");
    write(root / "track-catalog.json", {{"schema", 1}, {"enabled", tracks}});
    write(root / "character-catalog.json", {{"schema", 1}, {"enabled", characters}});
    // Receipt is written last. A partial tree is never a launchable profile.
    write(destination / "verified.json", {{"schema", ManifestSchema}, {"manifest", digest}});
}
} // namespace dkr::mods::online
