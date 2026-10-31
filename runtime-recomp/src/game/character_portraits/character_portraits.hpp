#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace dkr::character_portraits {
struct Request {
    std::filesystem::path root;
    std::string id, artifact, group, storage, revision;
    std::vector<std::filesystem::path> roms;
    std::string key() const;
};
struct Image {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    std::string error;
};
// Background-only, read-only decode of one verified portrait blob. No gameplay
// asset owner, patch importer, renderer or native guest is created.
Image load(const Request& request);
class Service {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Service();
    ~Service();
    std::shared_ptr<const Image> request(Request request);
};
} // namespace dkr::character_portraits
