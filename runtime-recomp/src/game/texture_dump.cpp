#include "texture_dump.hpp"
#include "texture_decode.hpp"
#include "legacy_track_catalog.hpp"
#include "vehicle_library.hpp"
#include "custom_tracks.hpp"
#include "runtime_texture_packs.hpp"
#include <miniz/miniz.h>
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include <json/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace dkr::runtime::texture_dump {
namespace {
using Json = nlohmann::json;
using namespace dkr::mods;
using Data = dkr::mods::View;
namespace car_assets = dkr::mods::vehicles;
using Clock = std::chrono::steady_clock;
std::string hex(std::uint64_t n, unsigned width = 16) {
    std::ostringstream s;
    s << std::hex << std::setfill('0') << std::setw(width) << n;
    return s.str();
}
std::string text_path(const std::filesystem::path& p) {
    auto s = p.generic_u8string();
    return {s.begin(), s.end()};
}
std::string slug(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        if (out.size() >= 72)
            break;
        out += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ? char(c) : '_';
    }
    return "asset_" + (out.empty() ? std::string("unnamed") : out); // No DOS device names, trailing dots or traversal.
}
bool link(const std::filesystem::path& p) {
    std::error_code ec;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(p, ec)))
        return true;
#ifdef _WIN32
    auto a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT))
        return true;
#endif
    return false;
}
void directory(const std::filesystem::path& p) {
    if (p.empty())
        throw Error("Choose a texture dump folder.");
    if (std::filesystem::exists(p)) {
        if (link(p) || !std::filesystem::is_directory(p))
            throw Error("The dump contains a link or a non-folder path.");
        return;
    }
    if (p.parent_path() == p || p.parent_path().empty())
        throw Error("The selected drive or parent folder is unavailable.");
    directory(p.parent_path());
    std::filesystem::create_directory(p);
}
std::filesystem::path member(const std::filesystem::path& root, std::string_view name) {
    if (name.empty() || name.find('\\') != name.npos || name.find(':') != name.npos || name.find('\0') != name.npos)
        throw Error("The dump manifest contains an unsafe path.");
    auto rel = utf8_path(name);
    if (rel.is_absolute() || rel.has_root_path())
        throw Error("The dump manifest contains an absolute path.");
    auto at = root;
    for (const auto& part : rel) {
        const auto name = part.string();
        if (part == ".." || part == "." || part.empty() || name.size() > 200 || name.back() == '.' ||
            name.back() == ' ')
            throw Error("The dump manifest contains an unsafe path.");
        for (unsigned char ch : name)
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' ||
                  ch == '-' || ch == '.'))
                throw Error("The dump manifest contains an unsupported filename.");
        auto stem = name.substr(0, name.find('.'));
        for (auto& ch : stem)
            ch = char(std::tolower(static_cast<unsigned char>(ch)));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '0' &&
             stem[3] <= '9'))
            throw Error("The dump manifest contains a reserved filename.");
        at /= part;
        if (link(at))
            throw Error("A dump path is a link.");
    }
    return at;
}
void atomic_file(const std::filesystem::path& path, Data bytes, bool replace = false) {
    directory(path.parent_path());
    if (link(path))
        throw Error("The output file is a link.");
    if (std::filesystem::exists(path) && !replace) {
        if (sha256(read_file(path, 64 * MiB)) != sha256(bytes))
            throw Error("An existing original has been edited. Choose a new dump folder; nothing was overwritten.");
        return;
    }
    if (std::filesystem::space(path.parent_path()).available < bytes.size() + 4 * MiB)
        throw Error("Not enough free disk space. Free some space, then resume.");
    static std::atomic<std::uint64_t> sequence{0};
    auto temp = path;
    temp += ".writing-" + std::to_string(Clock::now().time_since_epoch().count()) + "-" + std::to_string(++sequence);
    try {
        write_new_file(temp, bytes);
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), path.c_str(),
                         replace ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH : MOVEFILE_WRITE_THROUGH))
            throw Error("Could not commit the texture file. Check folder permissions.");
#else
        if (!replace && std::filesystem::exists(path))
            throw Error("The output was created by another writer.");
        std::filesystem::rename(temp, path);
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}
void atomic_json(const std::filesystem::path& p, const Json& j) {
    auto s = j.dump(2, ' ', false, Json::error_handler_t::replace);
    atomic_file(p, Data(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()), true);
}
Bytes png(const Picture& p) {
    Bytes out;
    const auto write = [](void* c, void* b, int n) {
        auto& v = *static_cast<Bytes*>(c);
        auto* a = static_cast<std::uint8_t*>(b);
        v.insert(v.end(), a, a + n);
    };
    if (!stbi_write_png_to_func(write, &out, int(p.width), int(p.height), 4, p.rgba.data(), int(p.width * 4)))
        throw Error("Could not encode the texture PNG.");
    return out;
}
Picture read_png(Data bytes) {
    constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < signature.size() || !std::equal(signature.begin(), signature.end(), bytes.begin()))
        throw Error("A workspace image is not a PNG.");
    int w = 0, h = 0, c = 0;
    if (bytes.size() > 64 * MiB || !stbi_info_from_memory(bytes.data(), int(bytes.size()), &w, &h, &c) || w <= 0 ||
        h <= 0 || std::uint64_t(w) * h > 16 * MiB)
        throw Error("Source image dimensions exceed the safe PNG workspace budget.");
    auto* raw = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &c, 4);
    if (!raw)
        throw Error("Source PNG could not be decoded.");
    struct Free {
        unsigned char* p;
        ~Free() {
            stbi_image_free(p);
        }
    } free{raw};
    return {unsigned(w), unsigned(h), Bytes(raw, raw + std::size_t(w) * h * 4)};
}
Bytes zip_member(Data bytes, const char* name) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
        throw Error("Source artwork archive is unreadable.");
    struct End {
        mz_zip_archive& z;
        ~End() {
            mz_zip_reader_end(&z);
        }
    } end{zip};
    const auto index = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
    mz_zip_archive_file_stat stat{};
    if (index < 0 || !mz_zip_reader_file_stat(&zip, unsigned(index), &stat) || stat.m_uncomp_size > 64 * MiB)
        throw Error("Source artwork entry is missing or too large.");
    Bytes out(std::size_t(stat.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(&zip, unsigned(index), out.data(), out.size(), 0))
        throw Error("Source artwork has an invalid archive checksum.");
    return out;
}
Bytes embedded_png(std::string_view source) {
    constexpr std::string_view prefix = "data:image/png;base64,";
    if (!source.starts_with(prefix) || source.size() > 8 * MiB)
        throw Error("Invalid embedded source PNG.");
    source.remove_prefix(prefix.size());
    Bytes bytes;
    unsigned accumulator = 0, bits = 0;
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (char c : source) {
        if (c == '=')
            break;
        const auto n = alphabet.find(c);
        if (n == alphabet.npos)
            throw Error("Invalid source PNG encoding.");
        accumulator = (accumulator << 6) | unsigned(n);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(std::uint8_t(accumulator >> bits));
        }
    }
    return bytes;
}
std::string pixel_key(const Picture& p) {
    return std::to_string(p.width) + "x" + std::to_string(p.height) + ":" + sha256(p.rgba);
}
std::uint64_t rice_alias(std::string_view identity) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : std::string("dkr-r:rice:") + std::string(identity))
        hash = (hash ^ c) * 1099511628211ULL;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    return hash ^ (hash >> 33);
}
// Conservative subset of DKR's standard material_init LoadBlock recipe.
// Narrow/odd rows, palettes, non-power-of-two masks and oversized images need
// a live observation; never hash bytes outside a source image to guess an ID.
std::string source_rice(Data pixels, unsigned w, unsigned h, unsigned format) {
    static constexpr unsigned fmts[]{0, 0, 4, 4, 3, 3, 3}, sizes[]{3, 2, 1, 0, 2, 1, 0};
    if (format >= 7 || !w || !h || (w & (w - 1)) || (h & (h - 1)) || w > 64 || h > 64)
        return {};
    unsigned row = (w << sizes[format]) >> 1;
    if (row < 8 || row % 8 || std::size_t(row) * h > 4096 || std::size_t(row) * h > pixels.size())
        return {};
    std::uint32_t crc = 0;
    std::size_t at = 0;
    for (int y = int(h) - 1; y >= 0; --y) {
        std::uint32_t v = 0;
        for (int x = int(row) - 4; x >= 0; x -= 4) {
            v = be32(pixels, at + x) ^ unsigned(x);
            crc = (crc << 4) + (crc >> 28);
            crc += v;
        }
        crc += v ^ unsigned(y);
        at += row;
    }
    return hex(crc, 8) + "#" + std::to_string(fmts[format]) + "#" + std::to_string(sizes[format]);
}
Json read_manifest(const std::filesystem::path& root, bool validate_members = true) {
    const auto path = root / "dump.json";
    if (!std::filesystem::exists(path)) {
        if (std::filesystem::exists(root) && !std::filesystem::is_empty(root))
            throw Error("This folder is not a DKR-R texture dump. Choose another destination.");
        return {{"schema", 1},
                {"kind", "dkr-r-texture-dump"},
                {"hash_version", 5},
                {"images", Json::array()},
                {"issues", Json::array()}};
    }
    if (link(root) || link(path))
        throw Error("The dump folder or manifest is a link.");
    auto bytes = read_file(path, 64 * MiB);
    auto j = Json::parse(bytes);
    if (j.value("kind", "") != "dkr-r-texture-dump" || j.value("schema", 0) != 1 || j.value("hash_version", 0) != 5 ||
        !j.at("images").is_array() || !j.at("issues").is_array())
        throw Error("This dump uses an unsupported manifest or texture hash version.");
    if (validate_members)
        for (const auto& image : j["images"]) {
            member(root, image.at("original").get<std::string>());
            member(root, image.at("working").get<std::string>());
        }
    return j;
}
bool same_source_format(const Json& source, unsigned fmt, unsigned siz, unsigned tlut) {
    if (source.value("runtime", false) || source.value("source_artwork", false) || !source.contains("format") || tlut)
        return false;
    static constexpr unsigned fmts[]{0, 0, 4, 4, 3, 3, 3}, sizes[]{3, 2, 1, 0, 2, 1, 0};
    const unsigned format = source.at("format");
    return format < 7 && fmts[format] == fmt && sizes[format] == siz;
}
struct Workspace {
    std::filesystem::path root;
    Json doc;
    std::map<std::string, std::size_t> rows;
    std::map<std::string, std::vector<std::size_t>> pixels;
    std::map<std::string, std::string> originals;
    std::set<std::string> verified_originals;
    unsigned changes = 0;
    explicit Workspace(std::filesystem::path p) : root(private_storage_path(p)), doc(read_manifest(root)) {
        directory(root);
        for (std::size_t i = 0; i < doc["images"].size(); ++i) {
            const auto& row = doc["images"][i];
            rows.emplace(row.at("id").get<std::string>(), i);
            const auto key = row.at("pixels").get<std::string>();
            pixels[key].push_back(i);
            originals.emplace(key, row.at("original").get<std::string>());
        }
    }
    void flush() {
        atomic_json(root / "dump.json", doc);
        changes = 0;
    }
    void issue(const std::string& source, const std::string& message) {
        Json item = {{"source", source}, {"message", message}};
        if (std::find(doc["issues"].begin(), doc["issues"].end(), item) == doc["issues"].end())
            doc["issues"].push_back(std::move(item));
        if (++changes >= 32)
            flush();
    }
    bool image(std::string id, std::string owner, std::string name, const Picture& picture, Json meta) {
        auto found = rows.find(id);
        const auto key = pixel_key(picture);
        bool reused = found != rows.end();
        std::string original, working;
        if (reused) {
            auto& row = doc["images"][found->second];
            if (row.at("pixels") != key)
                throw Error("A source identity changed. Export to a new folder.");
            original = row["original"];
            working = row["working"];
        } else {
            const auto stem = slug(owner) + "/" + slug(name) + "_" +
                              sha256(Data(reinterpret_cast<const std::uint8_t*>(id.data()), id.size())).substr(0, 16);
            auto same = originals.find(key);
            original = same == originals.end()
                           ? "Originals/texture_" + std::to_string(picture.width) + "x" +
                                 std::to_string(picture.height) + "_" + key.substr(key.find(':') + 1) + ".png"
                           : same->second;
            working = "Working/" + stem + ".png";
            originals.emplace(key, original);
        }
        Bytes encoded;
        if (!verified_originals.contains(original)) {
            encoded = png(picture);
            atomic_file(member(root, original), encoded);
            verified_originals.insert(original);
        }
        const auto edit = member(root, working);
        if (!std::filesystem::exists(edit)) {
            if (encoded.empty())
                encoded = read_file(member(root, original), 64 * MiB);
            atomic_file(edit, encoded);
        }
        meta["id"] = id;
        meta["owner"] = owner;
        meta["name"] = name;
        meta["width"] = picture.width;
        meta["height"] = picture.height;
        meta["pixels"] = key;
        meta["original"] = original;
        meta["working"] = working;
        if (!meta.contains("hashes"))
            meta["hashes"] = Json::array();
        // Capturing before exporting must work too. Equal RGBA pixels alone
        // are not provenance: only merge known, palette-free format matches.
        if (auto group = pixels.find(key); group != pixels.end())
            for (auto index : group->second) {
                const auto& runtime = doc["images"][index];
                if (!runtime.value("runtime", false) || !runtime.contains("tile"))
                    continue;
                const auto& tile = runtime["tile"];
                if (same_source_format(meta, tile.at("format"), tile.at("size"), tile.at("tlut")))
                    for (const auto& hash : runtime["hashes"])
                        if (std::find(meta["hashes"].begin(), meta["hashes"].end(), hash) == meta["hashes"].end())
                            meta["hashes"].push_back(hash);
            }
        meta["status"] = meta["hashes"].empty() ? "Needs in-game capture" : "Ready for replacement";
        if (reused) {
            auto& prior = doc["images"][found->second];
            for (const auto& hash : prior.value("hashes", Json::array()))
                if (std::find(meta["hashes"].begin(), meta["hashes"].end(), hash) == meta["hashes"].end())
                    meta["hashes"].push_back(hash);
            meta["status"] = meta["hashes"].empty() ? "Needs in-game capture" : "Ready for replacement";
            prior = std::move(meta);
        } else {
            pixels[key].push_back(doc["images"].size());
            rows.emplace(id, doc["images"].size());
            doc["images"].push_back(std::move(meta));
        }
        if (++changes >= 64)
            flush();
        return reused;
    }
    void associate(const Picture& p, const Json& hashes, unsigned fmt, unsigned siz, unsigned tlut) {
        auto found = pixels.find(pixel_key(p));
        if (found == pixels.end())
            return;
        for (auto i : found->second) {
            auto& row = doc["images"][i];
            if (!same_source_format(row, fmt, siz, tlut))
                continue;
            for (const auto& hash : hashes)
                if (std::find(row["hashes"].begin(), row["hashes"].end(), hash) == row["hashes"].end())
                    row["hashes"].push_back(hash);
            row["status"] = "Ready for replacement";
        }
    }
    bool source_png(const std::string& source, const std::string& owner, const std::string& name, Data bytes) {
        const auto digest = sha256(bytes);
        auto p = read_png(bytes);
        const auto path = "Source_Artwork/" + slug(owner) + "/" + slug(name) + "_" + digest.substr(0, 16) + ".png";
        atomic_file(member(root, path), bytes);
        return image(source + ":source:" + digest, owner, name, p,
                     {{"source_id", source},
                      {"source_artwork", true},
                      {"source_png", path},
                      {"source_png_sha256", digest},
                      {"frame", 0},
                      {"orientation", "as authored"}});
    }
};
#include "texture_gallery.inl"
struct Packet {
    texture_capture::Tile tile;
    std::array<std::uint8_t, 4096> bytes;
};
struct Service {
    std::mutex mutex;
    std::condition_variable cv;
    View view;
    std::shared_ptr<const View> published;
    bool dirty = true;
    std::deque<std::function<void()>> jobs;
    std::deque<Packet> queue;
    std::unordered_set<std::uint64_t> pending, committed;
    std::unique_ptr<Workspace> live;
    std::atomic<bool> cancelled{false}, armed{false}, pressure{false};
    std::atomic<std::uint64_t> deferred{0};
    bool stopping = false, closing = false;
    // Browser requests are coalesced separately from write/capture jobs. The
    // cache is worker-owned; UI threads only see immutable result snapshots.
    GalleryCache gallery_cache;
    std::optional<GalleryRequest> gallery_request;
    std::atomic<std::uint64_t> gallery_serial{0};
    std::jthread worker;
    Service() : worker([this] { run(); }) {}
    ~Service() {
        texture_capture::observer.store(nullptr, std::memory_order_release);
        armed = false;
        cancelled = true;
        ++gallery_serial;
        {
            std::lock_guard lock(mutex);
            closing = true;
            stopping = true;
            gallery_request.reset();
        }
        cv.notify_all();
    }
    void update(const std::function<void(View&)>& action) {
        std::lock_guard lock(mutex);
        action(view);
        dirty = true;
    }
    void check() {
        if (cancelled)
            throw Error("Export stopped. Completed textures are kept; export again to resume.");
    }
    void progress(const std::string& status) {
        check();
        update([&](auto& v) { v.status = status; });
    }
    bool job(std::function<void()> fn) {
        std::lock_guard lock(mutex);
        if (view.busy || view.capturing || view.gallery_loading)
            return false;
        view.busy = true;
        view.error.clear();
        view.status = "Preparing textures...";
        dirty = true;
        cancelled = false;
        jobs.push_back(std::move(fn));
        cv.notify_one();
        return true;
    }
    bool browse(std::filesystem::path root, std::string search, unsigned page, bool reload) {
        root = private_storage_path(root);
        std::lock_guard lock(mutex);
        if (closing || view.busy || view.capturing)
            return false;
        view.gallery_folder = root;
        if (root != view.folder) {
            view.thumbnails.clear();
            view.gallery_pages = 0;
            view.gallery_matches = 0;
            view.gallery_page = 0;
            ++view.gallery_generation;
        }
        const auto serial = ++gallery_serial;
        gallery_request = GalleryRequest{std::move(root), std::move(search), page, reload, serial};
        view.gallery_loading = true;
        view.gallery_requested_page = page;
        view.gallery_error.clear();
        dirty = true;
        cv.notify_one();
        return true;
    }
    void close_browser() {
        std::lock_guard lock(mutex);
        ++gallery_serial;
        gallery_request.reset();
        view.gallery_loading = false;
        dirty = true;
    }
    void count(bool reused, std::size_t bytes) {
        update([&](auto& v) {
            if (reused)
                ++v.reused;
            else
                ++v.exported;
            v.bytes += bytes;
        });
    }
    void error(const std::string& source, const std::string& e, Workspace& ws) {
        ws.issue(source, e);
        update([&](auto& v) { ++v.failed; });
    }
    bool needs(std::uint64_t hash) noexcept {
        if (!armed.load(std::memory_order_relaxed) || pressure.load(std::memory_order_relaxed))
            return false;
        try {
            std::unique_lock lock(mutex, std::try_to_lock);
            if (!lock) {
                ++deferred;
                return false;
            }
            return !pending.contains(hash) && !committed.contains(hash);
        } catch (...) {
            ++deferred;
            return false;
        }
    }
    void observe(const texture_capture::Tile& tile, const std::uint8_t* bytes) noexcept {
        if (!armed.load(std::memory_order_relaxed) || pressure.load(std::memory_order_relaxed))
            return;
        try {
            std::unique_lock lock(mutex, std::try_to_lock);
            if (!lock) {
                ++deferred;
                return;
            } // Cached tiles are observed again on subsequent draws.
            if (!armed || pending.contains(tile.native_hash) || committed.contains(tile.native_hash))
                return;
            if (queue.size() >= 2048 || pending.size() + committed.size() >= 524288) {
                pressure = true;
                view.paused = true;
                view.status =
                    "Capture paused at its memory budget. Let the writer catch up, then Resume; start a fresh capture if the session index is full. Unrecorded textures need to be seen again.";
                dirty = true;
                return;
            }
            Packet packet;
            packet.tile = tile;
            std::memcpy(packet.bytes.data(), bytes, packet.bytes.size());
            pending.insert(tile.native_hash);
            try {
                queue.push_back(std::move(packet));
            } catch (...) {
                pending.erase(tile.native_hash);
                throw;
            }
            view.queued = queue.size();
            ++view.observed;
            dirty = true;
            cv.notify_one();
        } catch (...) {
            pressure = true;
        }
    }
    void run() {
        for (;;) {
            std::function<void()> job;
            std::optional<Packet> packet;
            std::optional<GalleryRequest> browser;
            bool finish = false;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] {
                    return closing || !jobs.empty() || !queue.empty() || stopping || gallery_request.has_value();
                });
                if (closing && jobs.empty() && queue.empty() && !live)
                    return;
                if (!jobs.empty()) {
                    job = std::move(jobs.front());
                    jobs.pop_front();
                } else if (!queue.empty()) {
                    packet = std::move(queue.front());
                    queue.pop_front();
                    view.queued = queue.size();
                } else if (stopping || closing) {
                    stopping = false;
                    finish = true;
                } else if (gallery_request) {
                    browser = std::move(gallery_request);
                    gallery_request.reset();
                }
            }
            if (browser) {
                try {
                    auto result =
                        gallery_cache.load(*browser, [&] { return gallery_serial.load() == browser->serial; });
                    std::lock_guard lock(mutex);
                    if (result && gallery_serial == browser->serial) {
                        view.folder = std::move(result->folder);
                        view.revision = std::move(result->revision);
                        view.thumbnails = std::move(result->thumbnails);
                        view.gallery_page = result->page;
                        view.gallery_pages = result->pages;
                        view.gallery_matches = result->matches;
                        view.gallery_requested_page = result->page;
                        view.gallery_loading = false;
                        view.gallery_error.clear();
                        ++view.gallery_generation;
                        view.status = "Texture browser ready.";
                        dirty = true;
                    }
                } catch (const std::exception& e) {
                    std::lock_guard lock(mutex);
                    if (gallery_serial == browser->serial) {
                        view.gallery_loading = false;
                        view.gallery_error = e.what();
                        dirty = true;
                    }
                }
                continue;
            }
            try {
                if (job) {
                    job();
                    update([](auto& v) { v.busy = false; });
                } else if (packet && live) {
                    const auto& t = packet->tile;
                    Picture p;
                    try {
                        p = decode_tmem(t, packet->bytes);
                    } catch (const std::exception& e) {
                        live->issue(hex(t.native_hash), e.what());
                        std::lock_guard lock(mutex);
                        pending.erase(t.native_hash);
                        committed.insert(t.native_hash);
                        ++view.failed;
                        dirty = true;
                        continue;
                    }
                    Json hashes = Json::array({hex(t.native_hash)});
                    if (t.rice_alias && t.rice_alias != t.native_hash)
                        hashes.push_back(hex(t.rice_alias));
                    Json meta = {{"runtime", true},
                                 {"frame", 0},
                                 {"hashes", hashes},
                                 {"rice", t.rice_name},
                                 {"tile",
                                  {{"format", t.fmt},
                                   {"size", t.siz},
                                   {"line", t.line},
                                   {"tmem", t.tmem},
                                   {"tlut", t.tlut},
                                   {"palette", t.palette},
                                   {"cms", t.cms},
                                   {"cmt", t.cmt},
                                   {"masks", t.masks},
                                   {"maskt", t.maskt},
                                   {"shifts", t.shifts},
                                   {"shiftt", t.shiftt},
                                   {"uls", t.uls},
                                   {"ult", t.ult},
                                   {"lrs", t.lrs},
                                   {"lrt", t.lrt}}}};
                    count(
                        live->image("runtime:" + hex(t.native_hash), "Runtime", hex(t.native_hash), p, std::move(meta)),
                        p.rgba.size());
                    live->associate(p, hashes, t.fmt, t.siz, t.tlut);
                    {
                        std::lock_guard lock(mutex);
                        pending.erase(t.native_hash);
                        committed.insert(t.native_hash);
                    }
                } else if (finish && live) {
                    live->doc["capture_coverage"] =
                        "Observed runtime textures only; unseen dynamic variants are not claimed complete.";
                    live->doc["deferred_observations"] = deferred.load();
                    live->flush();
                    live.reset();
                    update([](auto& v) {
                        v.capturing = false;
                        v.paused = false;
                        v.status =
                            "Capture saved. You can resume it in this folder; unseen/deferred variants may need another visit.";
                    });
                }
            } catch (const std::exception& e) {
                if (packet) {
                    std::lock_guard lock(mutex);
                    pending.clear();
                    view.failed += queue.size();
                    queue.clear();
                    view.queued = 0;
                    view.paused = true;
                    view.status =
                        "Capture paused after a write error. Fix the folder or disk space, then Resume and revisit unsaved textures.";
                    pressure = true;
                    dirty = true;
                } else
                    armed = false;
                update([&](auto& v) {
                    v.error = e.what();
                    v.busy = false;
                    ++v.failed;
                });
                if (job && live) {
                    try {
                        live->flush();
                    } catch (...) {
                    }
                    live.reset();
                    update([](auto& v) { v.capturing = false; });
                }
                if (finish && live) {
                    live.reset();
                    update([](auto& v) {
                        v.capturing = false;
                        v.paused = false;
                        v.status = "Capture stopped with a write error. Completed PNGs are kept.";
                    });
                }
            }
        }
    }
};
Service& service() {
    static Service instance;
    return instance;
}
void observe(const texture_capture::Tile& t, const std::uint8_t* p) noexcept {
    service().observe(t, p);
}
bool needs(std::uint64_t hash) noexcept {
    return service().needs(hash);
}
bool wanted(const Options& o, const Source& s) {
    return (o.include_disabled || s.enabled) && (!o.only_selected || o.selected.contains(s.id)) &&
           ((s.kind == "Game" && o.game) || (s.kind == "Characters" && o.characters) ||
            (s.kind == "Tracks" && o.tracks) || (s.kind == "Vehicles" && o.vehicles));
}
struct Catalog {
    std::shared_ptr<const AssetBank> stock;
    std::vector<TrackCatalogItem> characters, legacy;
    std::vector<custom_tracks::Track> modern;
    std::vector<car_assets::Item> cars;
    std::vector<Source> sources;
    std::vector<std::pair<std::string, std::string>> issues;
};
Catalog catalog(const Options& o, bool scan_library = true) {
    auto rom = o.rom;
    if (rom.empty() && std::filesystem::is_regular_file(o.config / "last-rom.txt")) {
        auto bytes = read_file(o.config / "last-rom.txt", 8192);
        std::string name(bytes.begin(), bytes.end());
        while (!name.empty() && (name.back() == '\r' || name.back() == '\n'))
            name.pop_back();
        rom = utf8_path(name);
    }
    if (rom.empty())
        throw Error("Select your Game Pak in Play first.");
    Catalog c;
    c.stock = AssetBank::stock(read_file(rom, 16 * MiB));
    c.sources.push_back(
        {"game:" + c.stock->revision(), "Diddy Kong Racing (" + c.stock->revision() + ")", "Game", true});
    if (!scan_library)
        return c;
    auto root = o.config / "mods/legacy";
    if (std::filesystem::exists(root)) {
        try {
            c.characters = TrackCatalog::installed_items(root, c.stock->revision(), TrackCatalog::Kind::Character);
        } catch (const std::exception& e) {
            c.issues.emplace_back("Characters", e.what());
        }
        try {
            c.legacy = TrackCatalog::installed_items(root, c.stock->revision(), TrackCatalog::Kind::Track);
        } catch (const std::exception& e) {
            c.issues.emplace_back("Legacy tracks", e.what());
        }
    }
    for (const auto& i : c.characters)
        c.sources.push_back({"character:" + i.id, i.name, "Characters", i.enabled});
    for (const auto& i : c.legacy)
        c.sources.push_back({"legacy:" + i.id, i.name, "Tracks", i.enabled});
    c.modern = custom_tracks::tracks();
    for (const auto& i : c.modern)
        c.sources.push_back({"track:" + i.id, i.name, "Tracks", i.enabled});
    car_assets::Library library(o.config / "mods/vehicles");
    if (std::filesystem::exists(library.root()))
        try {
            c.cars = library.scan();
        } catch (const std::exception& e) {
            c.issues.emplace_back("Vehicles", e.what());
        }
    for (const auto& i : c.cars)
        c.sources.push_back({"car:" + i.digest, i.name, "Vehicles", i.enabled});
    return c;
}
std::filesystem::path output(const Options& o, const Catalog& c) {
    if (o.destination.empty())
        throw Error("Choose a destination folder.");
    const auto destination = private_storage_path(o.destination);
    directory(destination);
    if (std::filesystem::exists(destination / "dump.json")) {
        auto doc = read_manifest(destination);
        if (doc.value("rom_revision", "") != c.stock->revision())
            throw Error("This dump belongs to a different Game Pak revision. Choose another folder.");
        return destination;
    }
    return private_storage_path(std::filesystem::weakly_canonical(destination) /
                                ("DKR-Texture-Dump-" + c.stock->revision()));
}
void begin(Service& s, Workspace& w, const Catalog& c) {
    w.doc["rom_revision"] = c.stock->revision();
    w.doc["rom_sha256"] = c.stock->base_fingerprint();
    w.doc["application_version"] = DKR_RELEASE_VERSION;
    w.doc["coverage_note"] =
        "Stored texture records and source artwork are distinct from runtime replacement variants. Only known or observed identities are mapped.";
    w.flush();
    s.update([&](auto& v) {
        v.sources = c.sources;
        v.revision = c.stock->revision();
        v.folder = w.root;
        v.exported = v.reused = v.failed = v.bytes = v.observed = 0;
        v.thumbnails.clear();
        ++v.gallery_generation;
    });
    for (const auto& [source, error] : c.issues)
        s.error(source, error, w);
}
void track_source_artwork(Service& s, Workspace& w, const custom_tracks::Track& track, const std::string& source) {
    const auto owner = "Tracks/" + track.name + "_" + track.id + "/HD_source";
    for (const auto& pack : texture_packs::snapshot(true)) {
        if (pack.origin != texture_packs::Origin::TrackPack || pack.owner_track_id != track.id)
            continue;
        if (!track.hd_pack_digest.empty() && pack.texture_digest != track.hd_pack_digest) {
            s.error(
                source,
                "The installed HD pack belongs to a different track export. Native textures were exported; the mismatched source artwork was skipped.",
                w);
            continue;
        }
        try {
            if (std::filesystem::is_directory(pack.path)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(pack.path)) {
                    s.check();
                    if (link(entry.path()))
                        throw Error("A source-artwork path is a link.");
                    if (!entry.is_regular_file())
                        continue;
                    auto ext = entry.path().extension().string();
                    for (auto& ch : ext)
                        ch = char(std::tolower(static_cast<unsigned char>(ch)));
                    if (ext == ".png") {
                        auto data = read_file(entry.path(), 64 * MiB);
                        s.count(w.source_png(source, owner, entry.path().stem().string(), data), data.size());
                    }
                }
            } else {
                auto input = read_file(pack.path, 256 * MiB);
                mz_zip_archive zip{};
                if (!mz_zip_reader_init_mem(&zip, input.data(), input.size(), 0))
                    throw Error("The installed HD pack cannot be opened for source export.");
                struct End {
                    mz_zip_archive& z;
                    ~End() {
                        mz_zip_reader_end(&z);
                    }
                } end{zip};
                for (unsigned i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
                    s.check();
                    mz_zip_archive_file_stat st{};
                    if (!mz_zip_reader_file_stat(&zip, i, &st))
                        throw Error("Unreadable HD source entry.");
                    std::string name = st.m_filename;
                    auto ext = std::filesystem::path(name).extension().string();
                    for (auto& ch : ext)
                        ch = char(std::tolower(static_cast<unsigned char>(ch)));
                    if (ext != ".png" || mz_zip_reader_is_file_a_directory(&zip, i))
                        continue;
                    if (st.m_uncomp_size > 64 * MiB)
                        throw Error("HD source PNG exceeds the workspace budget.");
                    Bytes pixels(std::size_t(st.m_uncomp_size));
                    if (!mz_zip_reader_extract_to_mem(&zip, i, pixels.data(), pixels.size(), 0))
                        throw Error("HD source PNG could not be decoded from the archive.");
                    s.count(w.source_png(source, owner, std::filesystem::path(name).stem().string(), pixels),
                            pixels.size());
                }
            }
        } catch (const std::exception& e) {
            s.check();
            s.error(source, e.what(), w);
        }
    }
}
void record(Service& s, Workspace& w, Data bytes, Data palettes, std::string owner, std::string id, unsigned section,
            unsigned number) {
    try {
        slice(bytes, 0, 32);
        const unsigned count = bytes[0x12];
        if (!count)
            throw Error("Texture has no stored frames.");
        const auto palette_offset = be16(bytes, 8);
        auto inflated = bytes[0x1d] ? inflate_asset(bytes.subspan(32), 64 * MiB) : Bytes{};
        const auto frames = inflated.empty() ? bytes : Data(inflated);
        std::size_t offset = 0;
        for (unsigned frame = 0; frame < count; ++frame) {
            s.check();
            const auto header = slice(frames, offset, 32);
            const unsigned width = header[0], height = header[1], format = header[2] & 15, flags = be16(header, 6);
            static constexpr unsigned bits[]{32, 16, 8, 4, 16, 8, 4, 4, 8};
            if (format >= 9)
                throw Error("Unknown stored texture format.");
            const auto size = std::size_t((width * bits[format] + 7) / 8) * height;
            const auto texels = slice(frames, offset + 32, size);
            Data palette;
            if (format >= 7)
                palette = slice(palettes, palette_offset, format == 7 ? 32 : 128);
            auto picture = decode_asset(texels, width, height, format, (flags & 0x400) != 0, palette);
            const auto identity = source_rice(texels, width, height, format);
            Json hashes = Json::array();
            if (!identity.empty())
                hashes.push_back(hex(rice_alias(identity)));
            Json meta = {
                {"source_id", id},
                {"section", section},
                {"record", number},
                {"frame", frame},
                {"frames", count},
                {"format", format},
                {"flags", flags},
                {"palette_offset", palette_offset},
                {"delay_ticks", be16(header, 0x14)},
                {"runtime", false},
                {"hashes", hashes},
                {"rice", identity},
                {"identity_recipe", identity.empty() ? "needs runtime observation" : "DKR standard aligned LoadBlock"},
                {"orientation", "runtime texel order; no authoring vertical flip"}};
            const auto name =
                (section == 2 ? "3D_" : "2D_") + std::to_string(number) + "_frame_" + std::to_string(frame);
            meta["payload_sha256"] = sha256(bytes);
            meta["compressed"] = bytes[0x1d] != 0;
            s.count(w.image(id + ":" + sha256(bytes) + ":" + std::to_string(section) + ":" + std::to_string(number) +
                                ":" + std::to_string(frame),
                            owner, name, picture, std::move(meta)),
                    picture.rgba.size());
            const auto stride = be16(header, 0x16);
            if (frame + 1 < count && stride < 32 + size)
                throw Error("Animated texture frame stride is truncated.");
            offset += stride;
        }
    } catch (const std::exception& e) {
        s.check();
        s.error(id + ":" + std::to_string(section) + ":" + std::to_string(number), e.what(), w);
    }
}
void bank(Service& s, Workspace& w, const AssetBank& a, const std::string& owner, const std::string& id,
          const AssetBank* changed = nullptr) {
    for (unsigned section : {2U, 4U})
        for (unsigned i = 0; i < a.record_count(section); ++i) {
            s.check();
            auto bytes = a.record(section, i);
            if (changed) {
                auto old = changed->record(section, i);
                if (std::ranges::equal(bytes, old))
                    continue;
            }
            if (bytes.empty())
                continue;
            record(s, w, bytes, a.stock_section(14), owner, id, section, i);
        }
}
void export_catalog(Service& s, Workspace& w, const Catalog& c, const Options& o) {
    if (wanted(o, c.sources.front())) {
        s.progress("Exporting original game textures...");
        bank(s, w, *c.stock, "Game", c.sources.front().id);
    }
    const auto root = o.config / "mods/legacy";
    for (const auto& item : c.characters) {
        Source source{"character:" + item.id, item.name, "Characters", item.enabled};
        if (!wanted(o, source))
            continue;
        s.progress("Exporting " + item.name + "...");
        try {
            auto character = read_character_artifact(root / "prepared-characters" / item.group / item.storage,
                                                     item.artifact, *c.stock);
            if (character.root.content_id != item.id)
                throw Error("Character metadata changed; refresh the library.");
            for (const auto& [key, bytes] : character.records)
                if (key.first == 2 || key.first == 4)
                    record(s, w, bytes, c.stock->stock_section(14),
                           "Characters/" + item.name + "_" + item.id.substr(0, 8), source.id, key.first, key.second);
        } catch (const std::exception& e) {
            s.check();
            s.error(source.id, e.what(), w);
        }
    }
    for (const auto& item : c.legacy) {
        Source source{"legacy:" + item.id, item.name, "Tracks", item.enabled};
        if (!wanted(o, source))
            continue;
        s.progress("Exporting " + item.name + "...");
        try {
            auto track = read_track_artifact(root / "prepared" / item.group / item.storage, item.artifact, c.stock);
            if (track.root.content_id != item.id)
                throw Error("Track metadata changed; refresh the library.");
            bank(s, w, *track.bank, "Tracks/" + item.name + "_" + item.id.substr(0, 8), source.id, c.stock.get());
        } catch (const std::exception& e) {
            s.check();
            s.error(source.id, e.what(), w);
        }
    }
    for (const auto& item : c.modern) {
        Source source{"track:" + item.id, item.name, "Tracks", item.enabled};
        if (!wanted(o, source))
            continue;
        s.progress("Exporting " + item.name + "...");
        unsigned n2 = 0, n3 = 0;
        for (const auto& e : item.entries) {
            if (e.section == custom_tracks::Section::Textures3D)
                record(s, w, e.bytes, {}, "Tracks/" + item.name + "_" + item.id, source.id, 2, n3++);
            else if (e.section == custom_tracks::Section::Textures2D)
                record(s, w, e.bytes, {}, "Tracks/" + item.name + "_" + item.id, source.id, 4, n2++);
        }
        if (o.source_artwork)
            track_source_artwork(s, w, item, source.id);
    }
    car_assets::Library library(o.config / "mods/vehicles");
    for (const auto& item : c.cars) {
        Source source{"car:" + item.digest, item.name, "Vehicles", item.enabled};
        if (!wanted(o, source))
            continue;
        s.progress("Exporting " + item.name + "...");
        try {
            auto car = library.package(item.digest);
            const auto owner = "Vehicles/" + car->name + "_" + car->digest.substr(0, 8);
            auto geometry = Json::parse(zip_member(car->source, "geometry.json"));
            for (unsigned i = 0; i < car->textures.size(); ++i) {
                const auto& t = car->textures[i];
                Picture p{t.image.width, t.image.height, t.image.rgba};
                const auto bank_row = (std::size_t(t.image.width) * 2 + 7) & ~std::size_t(7);
                const bool rgba32 = bank_row * t.image.height <= 2048;
                if (!rgba32) {
                    const auto native = car_assets::native_texture(t.image);
                    p = decode_asset(Data(native).subspan(32), t.image.width, t.image.height, 1, false);
                }
                Json meta = {{"source_id", source.id},
                             {"frame", 0},
                             {"material", i},
                             {"repeat_s", t.repeat_s},
                             {"repeat_t", t.repeat_t},
                             {"source_artwork", false},
                             {"format", rgba32 ? 0 : 1},
                             {"orientation", "runtime material orientation"},
                             {"package_digest", car->digest}};
                meta["authored_flipped"] = geometry.at("textures").at(i).value("flipped", false);
                meta["uses"] = Json::array();
                for (unsigned lod = 0; lod < car->lods.size(); ++lod)
                    for (const auto& mesh : car->lods[lod].meshes)
                        if (mesh.texture == int(i))
                            meta["uses"].push_back({{"lod", lod}, {"mesh", mesh.id}, {"name", mesh.name}});
                s.count(w.image(source.id + ":material:" + std::to_string(i), owner, "material_" + std::to_string(i), p,
                                std::move(meta)),
                        p.rgba.size());
            }
            // The picker really uses two RGBA16 tiles, not the full source portrait.
            car_assets::Image thumb{64, 64, Bytes(64 * 64 * 4, 0)};
            const auto& src = car->portrait;
            const float factor = std::min(64.F / src.width, 64.F / src.height);
            const auto tw = std::max(1U, unsigned(src.width * factor)),
                       th = std::max(1U, unsigned(src.height * factor));
            for (unsigned y = 0; y < th; ++y)
                for (unsigned x = 0; x < tw; ++x) {
                    const auto sx = std::min(src.width - 1, unsigned(float(x) / factor)),
                               sy = std::min(src.height - 1, unsigned(float(y) / factor));
                    std::copy_n(src.rgba.begin() + (sy * src.width + sx) * 4, 4,
                                thumb.rgba.begin() + (((64 - th) / 2 + y) * 64 + (64 - tw) / 2 + x) * 4);
                }
            for (unsigned half = 0; half < 2; ++half) {
                car_assets::Image tile{64, 32, Bytes(64 * 32 * 4)};
                std::copy_n(thumb.rgba.begin() + half * tile.rgba.size(), tile.rgba.size(), tile.rgba.begin());
                auto native = car_assets::native_texture(tile);
                record(s, w, native, {}, owner, source.id + ":portrait", 4, half);
            }
            if (o.source_artwork) {
                unsigned material = 0;
                for (const auto& t : geometry.at("textures")) {
                    auto bytes = embedded_png(t.at("image").get<std::string>());
                    s.count(w.source_png(source.id, owner, "source_material_" + std::to_string(material++), bytes),
                            bytes.size());
                }
                auto portrait_png = zip_member(car->source, "portrait.png");
                s.count(w.source_png(source.id, owner, "source_portrait", portrait_png), portrait_png.size());
            } else {
                Picture portrait{car->portrait.width, car->portrait.height, car->portrait.rgba};
                s.count(w.image(source.id + ":portrait", owner, "portrait", portrait,
                                {{"source_id", source.id}, {"frame", 0}, {"source_artwork", true}}),
                        portrait.rgba.size());
            }
        } catch (const std::exception& e) {
            s.check();
            s.error(source.id, e.what(), w);
        }
    }
    w.doc["source_export_finished"] = true;
    w.flush();
}
void make_starter(Workspace& w) {
    Json textures = Json::array();
    struct Candidate {
        std::string path, pixels;
        bool edited = false;
    };
    std::map<std::string, Candidate> mapped;
    std::set<std::string> ambiguous;
    for (const auto& row : w.doc["images"]) {
        if (row.at("hashes").empty())
            continue;
        const auto path = row.at("working").get<std::string>();
        auto bytes = read_file(member(w.root, path), 64 * MiB);
        auto picture = read_png(bytes);
        const auto key = pixel_key(picture);
        const bool edited = key != row.at("pixels").get<std::string>();
        for (const auto& h : row.at("hashes")) {
            auto hash = h.get<std::string>();
            if (hash.size() != 16 || hash.find_first_not_of("0123456789abcdef") != hash.npos)
                throw Error("Invalid replacement identity in the dump manifest.");
            auto prior = mapped.find(hash);
            if (prior == mapped.end())
                mapped.emplace(hash, Candidate{path, key, edited});
            else if (prior->second.pixels != key) {
                if (edited && prior->second.edited)
                    throw Error(
                        "A shared texture has conflicting edits. Keep one edited version for that replacement identity.");
                if (edited)
                    prior->second = {path, key, true};
                else if (!prior->second.edited)
                    ambiguous.insert(hash);
            }
        }
    }
    for (const auto& [hash, item] : mapped) {
        if (ambiguous.contains(hash) && !item.edited) {
            w.issue(
                hash,
                "Different images share this identity. It was omitted from the starter; use an exact in-game capture.");
            continue;
        }
        textures.push_back({{"hashes", {{"rt64", hash}}}, {"path", item.path}});
    }
    if (textures.empty())
        throw Error(
            "No verified replacement identities yet. Capture textures in game, stop capture, then create the starter pack.");
    Json database = {{"configuration",
                      {{"configurationVersion", 3},
                       {"hashVersion", 5},
                       {"autoPath", "rt64"},
                       {"defaultOperation", "stream"},
                       {"defaultShift", "half"}}},
                     {"textures", textures}};
    // Root paths point into Working; originals and reports are never loaded.
    atomic_json(w.root / "rt64.json", database);
    // Stream a conventional ZIP for the launcher's existing Import Texture
    // Pack action. Never include Originals, the ROM, or source/debug reports.
    const auto archive = member(w.root, "Texture-Starter.zip");
    auto temp = archive;
    temp += ".writing-" + std::to_string(Clock::now().time_since_epoch().count());
    if (link(temp))
        throw Error("The starter archive path is a link.");
#ifdef _WIN32
    FILE* file = _wfopen(temp.c_str(), L"wb");
#else
    FILE* file = std::fopen(temp.c_str(), "wb");
#endif
    if (!file)
        throw Error("Could not create the starter ZIP.");
    try {
        mz_zip_archive zip{};
        if (!mz_zip_writer_init_cfile(&zip, file, MZ_ZIP_FLAG_WRITE_ZIP64))
            throw Error("Could not initialize the starter ZIP.");
        struct End {
            mz_zip_archive& z;
            ~End() {
                mz_zip_writer_end(&z);
            }
        } end{zip};
        const auto config = database.dump(2);
        if (!mz_zip_writer_add_mem(&zip, "rt64.json", config.data(), config.size(), 6))
            throw Error("Could not write the starter database.");
        std::set<std::string> added;
        for (const auto& row : textures) {
            const auto path = row.at("path").get<std::string>();
            if (!added.insert(path).second)
                continue;
            auto bytes = read_file(member(w.root, path), 64 * MiB);
            if (!mz_zip_writer_add_mem(&zip, path.c_str(), bytes.data(), bytes.size(), 6))
                throw Error("Could not write a starter image; check free disk space.");
        }
        if (!mz_zip_writer_finalize_archive(&zip))
            throw Error("Could not finish the starter ZIP; check free disk space.");
    } catch (...) {
        std::fclose(file);
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
    if (std::fclose(file))
        throw Error("Could not commit the starter ZIP.");
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), archive.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw Error("Could not replace the starter ZIP.");
#else
    std::filesystem::rename(temp, archive);
#endif
    const std::string notice =
        "DKR-R texture workspace\n\nEdit the PNGs in Working; Originals are kept unchanged. Create Starter Pack again after editing, then import Texture-Starter.zip through Import Texture Pack. Only verified source/runtime identities are mapped. Unseen runtime variants require Capture next game. Shared hashes replace every use, not only the folder's named owner. Export again to resume without overwriting edits. This starter is a private authoring copy. Distribute your replacement artwork, not the ROM dump.\n";
    atomic_file(w.root / "TEXTURE-WORKSPACE.txt",
                Data(reinterpret_cast<const std::uint8_t*>(notice.data()), notice.size()), true);
    w.doc["starter_pack_entries"] = textures.size();
    w.flush();
}
} // namespace

std::shared_ptr<const View> snapshot() {
    auto& s = service();
    std::lock_guard lock(s.mutex);
    if (s.pressure && !s.view.paused && s.view.capturing) {
        s.view.paused = true;
        s.view.status = "Capture paused at its memory budget. Resume when ready, then revisit unsaved textures.";
        s.dirty = true;
    }
    if (s.dirty || !s.published) {
        s.published = std::make_shared<View>(s.view);
        s.dirty = false;
    }
    return s.published;
}
bool refresh(Options o) {
    auto& s = service();
    return s.job([&s, o = std::move(o)] {
        auto c = catalog(o);
        s.update([&](auto& v) {
            v.sources = c.sources;
            v.revision = c.stock->revision();
            v.status = "Choose what to export.";
        });
    });
}
bool export_installed(Options o) {
    auto& s = service();
    return s.job([&s, o = std::move(o)] {
        auto c = catalog(o);
        if (!std::ranges::any_of(c.sources, [&](const auto& source) { return wanted(o, source); }))
            throw Error("Choose at least one texture source to export.");
        Workspace w(output(o, c));
        begin(s, w, c);
        try {
            export_catalog(s, w, c, o);
        } catch (...) {
            w.flush();
            throw;
        }
        s.update([&](auto& v) {
            v.status = v.failed ? "Export finished with skipped items. See dump.json for reasons."
                                : "Textures exported. Create a starter pack when you are ready to edit.";
        });
    });
}
bool capture(Options o) {
    auto& s = service();
    return s.job([&s, o = std::move(o)] {
        auto c = catalog(o, false);
        s.live = std::make_unique<Workspace>(output(o, c));
        begin(s, *s.live, c);
        {
            std::lock_guard lock(s.mutex);
            if (s.cancelled)
                throw Error("Capture cancelled.");
            s.pending.clear();
            s.committed.clear();
            s.queue.clear();
            s.stopping = false;
            s.view.capturing = true;
            s.view.paused = false;
            s.view.status = "Capture is on. Play to collect original textures; stop when finished.";
            s.dirty = true;
        }
        s.deferred = 0;
        s.pressure = false;
        s.armed = true;
        texture_capture::gate.store(needs, std::memory_order_release);
        texture_capture::observer.store(observe, std::memory_order_release);
        std::fprintf(
            stderr,
            "[texture-dump] presentation-only PNG capture enabled; original TMEM, no GPU readback, bounded asynchronous writer\n");
    });
}
void stop_capture() {
    auto& s = service();
    s.armed = false;
    texture_capture::observer.store(nullptr, std::memory_order_release);
    {
        std::lock_guard lock(s.mutex);
        if (!s.view.capturing)
            return;
        s.stopping = true;
        s.view.status = "Saving captured textures...";
        s.dirty = true;
    }
    s.cv.notify_one();
}
void resume_capture() {
    auto& s = service();
    std::lock_guard lock(s.mutex);
    if (!s.view.capturing || s.stopping || s.queue.size() >= 2048)
        return;
    s.pressure = false;
    s.view.paused = false;
    s.view.error.clear();
    s.view.status = "Capture resumed. Revisit textures missed while paused.";
    s.dirty = true;
}
void cancel() {
    service().cancelled = true;
    stop_capture();
}
bool starter_pack(std::filesystem::path root) {
    auto& s = service();
    return s.job([&s, root = std::move(root)] {
        Workspace w(root);
        make_starter(w);
        s.update([](auto& v) {
            v.status =
                "Texture-Starter.zip created. Import it through Import Texture Pack. Recreate it after editing Working.";
        });
    });
}
bool gallery(std::filesystem::path root, std::string search, unsigned page, bool reload) {
    return service().browse(std::move(root), std::move(search), page, reload);
}
void close_gallery() {
    service().close_browser();
}
} // namespace dkr::runtime::texture_dump
