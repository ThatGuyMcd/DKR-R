#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace dkr::runtime::texture_dump {
struct Source {
    std::string id, name, kind;
    bool enabled = true;
};
struct Options {
    std::filesystem::path config, rom, destination;
    bool game = true, characters = true, tracks = true, vehicles = true, include_disabled = true,
         source_artwork = false;
    bool only_selected = false;
    std::set<std::string> selected; // Empty means every source in the selected categories.
};
struct Thumbnail {
    std::string name, owner, path, status, preview_key, preview_error;
    unsigned width = 0, height = 0, frame = 0, preview_width = 0, preview_height = 0;
    std::vector<std::uint8_t> rgba;
};
struct View {
    bool busy = false, capturing = false, paused = false;
    std::uint64_t exported = 0, reused = 0, failed = 0, bytes = 0, queued = 0, observed = 0;
    std::filesystem::path folder;
    std::string status, error, revision;
    std::vector<Source> sources;
    std::vector<Thumbnail> thumbnails;
    unsigned gallery_page = 0, gallery_pages = 0;
    bool gallery_loading = false;
    std::filesystem::path gallery_folder;
    unsigned gallery_requested_page = 0;
    std::uint64_t gallery_matches = 0;
    std::string gallery_error;
    std::uint64_t gallery_generation = 0;
};
std::shared_ptr<const View> snapshot();
bool refresh(Options);
bool export_installed(Options);
bool capture(Options);
void stop_capture();
void resume_capture();
void cancel();
bool gallery(std::filesystem::path folder, std::string search, unsigned page, bool reload = false);
void close_gallery();
bool starter_pack(std::filesystem::path folder);
} // namespace dkr::runtime::texture_dump
