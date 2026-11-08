#include "store_vehicle_adapter.hpp"
#include "vehicle_library.hpp"
#include "mods/online_mod_cache.hpp"
#include <algorithm>
#include <set>

namespace dkr::store {
namespace {
namespace cars = dkr::mods::vehicles;
namespace fs = std::filesystem;
using Bytes = dkr::mods::Bytes;
void running(std::stop_token stop) {
    if (stop.stop_requested()) throw dkr::mods::Error("Vehicle Store operation cancelled before publication.");
}
std::string folded(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return value;
}
std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string(); return {bytes.begin(), bytes.end()};
}
Json member(const cars::Item& item) {
    return {{"kind", "vehicle"}, {"key", "vehicle:" + item.digest}, {"id", item.id},
            {"digest", item.digest}, {"name", item.name}, {"type", unsigned(item.type)},
            {"path", "mods/vehicles/packages/" + item.digest + cars::extension(item.type)},
            {"enabled", item.enabled}};
}
bool owned_elsewhere(const std::string& digest, const Json& owners) {
    return std::any_of(owners.begin(), owners.end(), [&](const Json& m) {
        return text(m, "kind", 40) == "vehicle" && text(m, "digest", 64) == digest;
    });
}
}

bool vehicle_store_category(std::string_view value) { return value == "legacy" || value == "vehicles"; }
bool vehicle_store_extension(std::string_view value) {
    return value == ".dkrkart" || value == ".dkrhover" || value == ".dkrplane";
}
StorePrepared prepare_store_vehicle(const fs::path& source, const fs::path& stage, std::stop_token stop) {
    running(stop);
    // The normal importer validates every inner export, seat table, texture,
    // portrait and LOD. Nothing is installed into the live library here.
    cars::Library staging(stage / "library");
    const auto batch = staging.prepare_import(source);
    running(stop);
    staging.import_prepared(batch, cars::ImportPolicy::KeepBoth);
    StorePrepared result;
    result.native = {{"vehicle", true}, {"members", Json::array()}};
    for (const auto& item : staging.scan(stop)) {
        running(stop);
        result.native["members"].push_back(member(item));
        result.outputs.push_back({{"kind", "vehicle"}, {"digest", item.digest},
            {"name", item.source_name}, {"type", unsigned(item.type)},
            {"source", path_text(staging.root() / "packages" / (item.digest + cars::extension(item.type)))},
            {"target", "mods/vehicles/packages/" + item.digest + cars::extension(item.type)}});
        result.outputs.push_back({{"kind", "vehicle-portrait"}, {"digest", item.digest},
            {"source", path_text(staging.root() / "portraits" / (item.digest + ".rgba"))},
            {"target", "mods/vehicles/portraits/" + item.digest + ".rgba"}});
    }
    if (result.native["members"].empty()) throw dkr::mods::Error("This download contains no supported vehicles.");
    return result;
}
Json publish_store_vehicles(const fs::path& config, const StorePrepared& prepared,
                           const Json& previous, const Json& other_owners, std::stop_token stop) {
    cars::Library library(config / "mods/vehicles");
    const auto before = library.scan(stop);
    cars::ImportBatch batch;
    for (const auto& output : prepared.outputs) if (text(output, "kind", 40) == "vehicle") {
        running(stop);
        const auto type = cars::Type(output.at("type").get<unsigned>());
        auto p = cars::inspect(dkr::mods::read_file(fs::u8path(text(output, "source", 4096)), cars::MaxPackageBytes),
                               text(output, "name", 240), type);
        if (p->digest != text(output, "digest", 64))
            throw dkr::mods::Error("The prepared vehicle changed before publication.");
        batch.packages.push_back(std::move(p));
    }
    running(stop);
    // Store duplicate choices identify exact owners. KeepBoth imports the same
    // validated exports without deleting every unrelated same-name local car.
    library.import_prepared(batch, cars::ImportPolicy::KeepBoth);
    std::set<std::string> incoming;
    for (const auto& p : batch.packages) incoming.insert(p->digest);
    for (const auto& old : previous) {
        const auto digest = text(old, "digest", 64);
        if (text(old, "kind", 40) == "vehicle" && cars::valid_digest(digest) && !incoming.contains(digest) &&
            !owned_elsewhere(digest, other_owners) &&
            std::any_of(before.begin(), before.end(), [&](const auto& item) { return item.digest == digest; })) {
            running(stop); library.remove(digest); // Immutable files/history remain recoverable.
        }
    }
    for (const auto& p : batch.packages) {
        // Identical already-installed exports retain their current activation.
        if (std::any_of(before.begin(), before.end(), [&](const auto& i) { return i.digest == p->digest; })) continue;
        const Json* old = nullptr;
        for (const auto& m : previous) if (text(m, "kind", 40) == "vehicle" &&
            m.value("type", 0U) == unsigned(p->type) && folded(text(m, "name", 240)) == folded(p->name)) { old = &m; break; }
        if (!old && batch.packages.size() == 1 && previous.size() == 1 && text(previous.front(), "kind", 40) == "vehicle")
            old = &previous.front();
        if (old) {
            bool enabled = old->value("enabled", true);
            for (const auto& item : before) if (item.digest == text(*old, "digest", 64)) enabled = item.enabled;
            running(stop); library.enable(p->digest, enabled);
        }
    }
    Json members = Json::array();
    for (const auto& item : library.scan(stop)) if (incoming.contains(item.digest)) members.push_back(member(item));
    if (members.size() != batch.packages.size()) throw dkr::mods::Error("The vehicle library did not publish every prepared car.");
    return members;
}
void remove_store_vehicle(const fs::path& config, const Json& m, std::stop_token stop) {
    const auto digest = text(m, "digest", 64);
    if (!cars::valid_digest(digest)) throw dkr::mods::Error("Invalid vehicle installation receipt.");
    cars::Library library(config / "mods/vehicles");
    for (const auto& item : library.scan(stop)) if (item.digest == digest) {
        running(stop); library.remove(digest); return;
    }
}

fs::path prepare_vehicle_store_upload(const fs::path& source, const fs::path& cache, std::stop_token stop) {
    const auto ext = folded(path_text(source.extension()));
    if (!vehicle_store_extension(ext)) return source;
    running(stop);
    const auto inputs = cars::read_inputs(source);
    if (inputs.size() != 1) throw dkr::mods::Error("Choose one standalone vehicle export to upload.");
    const auto package = cars::inspect(inputs.front().bytes, inputs.front().name, inputs.front().type);
    // The deployed API already accepts ZIP uploads. A deterministic stored ZIP
    // preserves the exact inner .dkrkart bytes and keeps resumable uploads stable.
    const std::string leaf = std::string("vehicle") + cars::extension(package->type);
    const auto& payload = package->source;
    std::uint32_t crc = 0xffffffffU;
    for (auto byte : payload) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    crc ^= 0xffffffffU;
    Bytes zip;
    auto u16 = [&](std::uint16_t n) { zip.push_back(std::uint8_t(n)); zip.push_back(std::uint8_t(n >> 8)); };
    auto u32 = [&](std::uint32_t n) { u16(std::uint16_t(n)); u16(std::uint16_t(n >> 16)); };
    const auto size = std::uint32_t(payload.size());
    u32(0x04034b50);u16(20);u16(0);u16(0);u16(0);u16(33);u32(crc);u32(size);u32(size);u16(std::uint16_t(leaf.size()));u16(0);
    zip.insert(zip.end(), leaf.begin(), leaf.end());zip.insert(zip.end(), payload.begin(), payload.end());
    const auto directory = std::uint32_t(zip.size());
    u32(0x02014b50);u16(20);u16(20);u16(0);u16(0);u16(0);u16(33);u32(crc);u32(size);u32(size);
    u16(std::uint16_t(leaf.size()));u16(0);u16(0);u16(0);u16(0);u32(0);u32(0);zip.insert(zip.end(), leaf.begin(), leaf.end());
    const auto directory_size = std::uint32_t(zip.size()) - directory;
    u32(0x06054b50);u16(0);u16(0);u16(1);u16(1);u32(directory_size);u32(directory);u16(0);
    running(stop);
    if(zip.size()>cars::MaxPackageBytes)
        throw dkr::mods::Error("This export is too large for a vehicle ZIP. Reduce the vehicle package size before uploading.");
    dkr::mods::online::check_storage(cache.parent_path(), true);
    fs::create_directories(cache);dkr::mods::online::check_storage(cache, true);
    const auto folder = cache / dkr::mods::sha256(zip);
    fs::create_directory(folder);dkr::mods::online::check_storage(folder, true);
    const auto target = folder / fs::u8path(package_filename(package->name, "vehicle.zip"));
    if (!fs::exists(target)) dkr::mods::write_new_file(dkr::mods::private_storage_path(target), zip);
    else {
        dkr::mods::online::check_storage(target, false);
        if (dkr::mods::read_file(dkr::mods::private_storage_path(target), cars::MaxPackageBytes + 4096) != zip)
            throw dkr::mods::Error("The prepared vehicle upload cache has changed.");
    }
    return target;
}
}
