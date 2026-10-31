#include "character_portraits.hpp"
#include "legacy_character_artifact.hpp"
#include "texture_decode.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <condition_variable>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <cstdio>
#include <sstream>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace dkr::character_portraits {
namespace {
using namespace mods;
bool digest(std::string_view s) {
    return s.size() == 64 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
void ordinary(const std::filesystem::path& path, bool directory) {
    const auto type = std::filesystem::symlink_status(path).type();
    if (type != (directory ? std::filesystem::file_type::directory : std::filesystem::file_type::regular))
        throw Error("Portrait storage is not an ordinary file or directory.");
#ifdef _WIN32
    const auto attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT))
        throw Error("Portrait storage cannot be a reparse point.");
#endif
}
Bytes palette(const Request& request) {
    // Only palette-based portraits need the player's existing imported ROM.
    // Keep the small palette section for the two revisions, never the ROM.
    static thread_local std::map<std::string, Bytes> palettes;
    if (auto it = palettes.find(request.revision); it != palettes.end())
        return it->second;
    auto roms = request.roms;
    if (roms.empty()) {
        const auto catalog = request.root.parent_path().parent_path() / "rom-catalog.txt";
        std::error_code error;
        if (std::filesystem::is_regular_file(catalog, error)) {
            const auto data = read_file(catalog, MiB);
            std::istringstream lines(std::string(data.begin(), data.end()));
            std::string line;
            while (roms.size() < 8 && std::getline(lines, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty())
                    continue;
                auto path = utf8_path(line);
                if (path.is_absolute())
                    roms.push_back(std::move(path));
            }
        }
    }
    for (const auto& path : roms)
        try {
            auto rom = read_file(path, MaxImage);
            canonicalize_rom(rom);
            if (verified_revision(rom) != request.revision)
                continue;
            const AssetImage image(rom, request.revision);
            const auto section = image.sections[14];
            if (section.size() > MiB)
                throw Error("Portrait palette exceeds its limit.");
            Bytes bytes(section.begin(), section.end());
            if (palettes.size() < 2)
                palettes.emplace(request.revision, bytes);
            return bytes;
        } catch (const Error&) {
        }
    throw Error("The matching imported Game ROM is needed for this portrait palette.");
}
} // namespace
std::string Request::key() const {
    const auto path = root.generic_u8string();
    return std::string(path.begin(), path.end()) + ":" + id + ":" + artifact + ":" + group + ":" + storage + ":" +
           revision;
}
Image load(const Request& request) {
    using nlohmann::json;
    if (!digest(request.id) || !digest(request.artifact) || !digest(request.group) || !digest(request.storage) ||
        (request.revision != "us.v77" && request.revision != "us.v80"))
        throw Error("Invalid portrait source identity.");
    const auto root = private_storage_path(request.root);
    ordinary(root, true);
    const auto prepared = root / "prepared-characters", group = prepared / request.group,
               folder = group / request.storage;
    for (const auto& path : {prepared, group, folder, folder / "blobs"})
        ordinary(path, true);
    ordinary(folder / "character.json", false);
    const auto bytes = read_file(folder / "character.json", MiB);
    if (sha256(bytes) != request.artifact)
        throw Error("Character portrait manifest integrity failed.");
    const auto document = json::parse(bytes.begin(), bytes.end(), [](int depth, json::parse_event_t, json&) {
        if (depth > 8)
            throw Error("Portrait metadata is nested too deeply.");
        return true;
    });
    if (document.at("schema") != 1 || document.at("adapter") != CharacterAdapterVersion ||
        document.at("id") != request.id || document.at("revision") != request.revision)
        throw Error("Character portrait metadata differs from its catalogue entry.");
    const auto portrait = document.at("portrait").get<unsigned>();
    const auto& records = document.at("records");
    if (!records.is_array() || records.size() > 32768)
        throw Error("Portrait record table exceeds its limit.");
    for (const auto& record : records)
        if (record.at("section") == 4 && record.at("record") == portrait) {
            const auto hash = record.at("hash").get<std::string>();
            const auto size = record.at("size").get<std::uint64_t>();
            if (!digest(hash) || size < 32 || size > 4 * MiB)
                throw Error("Invalid portrait blob descriptor.");
            const auto path = folder / "blobs" / hash;
            ordinary(path, false);
            auto source = read_file(path, 4 * MiB);
            if (source.size() != size || sha256(source) != hash)
                throw Error("Character portrait blob integrity failed.");
            const auto compressed = source[0x1d];
            auto inflated = compressed ? inflate_asset(View(source).subspan(32), 4 * MiB) : Bytes{};
            const View frame = compressed ? View(inflated) : View(source);
            const auto header = slice(frame, 0, 32);
            const unsigned width = header[0], height = header[1], format = header[2] & 15;
            if (!width || !height || width > 256 || height > 256 || format > 8)
                throw Error("Invalid character portrait dimensions or format.");
            const auto palettes = format >= 7 ? palette(request) : Bytes{};
            View colours;
            if (format >= 7)
                colours = slice(palettes, be16(header, 8), format == 7 ? 32 : 512);
            const auto picture = runtime::texture_dump::decode_asset(frame.subspan(32), width, height, format,
                                                                     (be16(header, 6) & 0x400) != 0, colours);
            return {picture.width, picture.height, picture.rgba, {}};
        }
    throw Error("This character has no prepared portrait record.");
}
struct Service::Impl {
    struct Entry {
        std::shared_ptr<const Image> image;
        std::uint64_t touched = 0;
    };
    std::mutex mutex;
    std::condition_variable_any changed;
    std::map<std::string, Request> pending;
    std::set<std::string> active;
    std::map<std::string, Entry> cache;
    std::uint64_t serial = 0;
    std::jthread thread;
    Impl()
        : thread([this](std::stop_token stop) {
              try {
                  while (!stop.stop_requested()) {
                      Request request;
                      std::string key;
                      {
                          std::unique_lock lock(mutex);
                          changed.wait(lock, stop, [&] { return !pending.empty(); });
                          if (stop.stop_requested())
                              break;
                          auto it = pending.begin();
                          key = it->first;
                          request = std::move(it->second);
                          pending.erase(it);
                      }
                      auto image = std::make_shared<Image>();
                      try {
                          *image = load(request);
                      } catch (const std::exception& error) {
                          image->error = error.what();
                      }
                      {
                          std::lock_guard lock(mutex);
                          active.erase(key);
                          if (cache.size() >= 64) {
                              auto oldest =
                                  std::min_element(cache.begin(), cache.end(), [](const auto& a, const auto& b) {
                                      return a.second.touched < b.second.touched;
                                  });
                              cache.erase(oldest);
                          }
                          cache[key] = {std::move(image), ++serial};
                      }
                  }
              } catch (...) {
                  std::fprintf(stderr, "[mods][portraits] optional preview worker stopped safely\n");
              }
          }) {}
    ~Impl() {
        thread.request_stop();
        changed.notify_all();
        thread.join();
    }
};
Service::Service() : impl_(std::make_unique<Impl>()) {}
Service::~Service() = default;
std::shared_ptr<const Image> Service::request(Request request) {
    const auto key = request.key();
    std::lock_guard lock(impl_->mutex);
    if (auto it = impl_->cache.find(key); it != impl_->cache.end()) {
        it->second.touched = ++impl_->serial;
        return it->second.image;
    }
    if (impl_->active.size() < 32 && impl_->active.insert(key).second) {
        impl_->pending.emplace(key, std::move(request));
        impl_->changed.notify_one();
    }
    return {};
}
} // namespace dkr::character_portraits
