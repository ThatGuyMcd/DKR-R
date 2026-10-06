#pragma once
#include "online_mod_manifest.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <stop_token>

namespace dkr::mods::online {
// Built on a worker. Immutable payloads remain pinned throughout the lobby.
struct Bundle {
    Manifest manifest;
    std::map<std::string,std::shared_ptr<const Bytes>> payloads;
};
Bundle export_legacy(const std::filesystem::path& library,std::string revision,
                     std::stop_token stop={});
Bytes pack_track_lab(const std::filesystem::path& directory,std::stop_token stop={});
void unpack_track_lab(View bytes,const std::filesystem::path& fresh_directory,
                      std::stop_token stop={});
// Reject links/reparse points on every existing path component.
void check_storage(const std::filesystem::path& path,bool directory);
}
