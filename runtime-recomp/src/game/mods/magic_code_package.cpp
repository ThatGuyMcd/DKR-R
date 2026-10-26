#include "magic_code_package.hpp"
#include <cmath>
#include "sdk2_schema.hpp"
#include <recompiler/context.h>
#include <miniz/miniz.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

#include "magic_code_execution_policy.hpp"

namespace dkr::mods::code {
namespace {
[[noreturn]] void bad(std::string why) {
    throw Error("Custom Magic Code: " + why);
}
std::string text(const Json& object, const char* key, std::size_t maximum, bool optional = false) {
    if (optional && !object.contains(key))
        return {};
    if (!object.contains(key) || !object[key].is_string())
        bad(std::string("missing ") + key);
    auto value = object[key].get<std::string>();
    if ((!optional && value.empty()) || value.size() > maximum || value.find('\0') != value.npos)
        bad(std::string("invalid ") + key);
    return value;
}
bool path_ok(std::string_view name) {
    if (name.empty() || name.size() > 240 || name.front() == '/' || name.back() == '/' ||
        name.find('\\') != name.npos || name.find(':') != name.npos)
        return false;
    // ASCII archive paths avoid platform-dependent Unicode/case aliases.
    // Display names and resource contents remain UTF-8.
    for (unsigned char c : name)
        if (c < 32 || c >= 127 || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    for (std::size_t at = 0; at < name.size();) {
        const auto end = name.find('/', at);
        auto part = name.substr(at, end == name.npos ? name.size() - at : end - at);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
            return false;
        std::string stem(part.substr(0, part.find('.')));
        std::transform(stem.begin(), stem.end(), stem.begin(),
                       [](unsigned char c) { return char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c); });
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' &&
             stem[3] <= '9'))
            return false;
        at = end == name.npos ? name.size() : end + 1;
    }
    return true;
}
struct Archive {
    mz_zip_archive zip{};
    std::map<std::string, Bytes> files;
    explicit Archive(View bytes) {
        if (bytes.empty() || bytes.size() > MaxPackage)
            bad("package size is outside its limit");
        if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
            bad("invalid package archive");
        try {
            const auto count = mz_zip_reader_get_num_files(&zip);
            if (!count || count > MaxMembers)
                bad("too many package entries");
            std::size_t total = 0;
            std::set<std::string> names;
            for (unsigned i = 0; i < count; ++i) {
                mz_zip_archive_file_stat stat{};
                if (!mz_zip_reader_file_stat(&zip, i, &stat))
                    bad("cannot read package directory");
                const auto length = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
                if (length < 2 || length > 242)
                    bad("invalid archive member name length");
                std::string raw(length, '\0');
                if (mz_zip_reader_get_filename(&zip, i, raw.data(), length) != length || raw.back() != '\0' ||
                    raw.find('\0') != length - 1)
                    bad("invalid archive member name");
                raw.pop_back();
                const bool directory = mz_zip_reader_is_file_a_directory(&zip, i) != 0;
                if (directory && raw.back() == '/')
                    raw.pop_back();
                const std::string name = std::move(raw);
                if (!path_ok(name) || ((stat.m_external_attr >> 16) & 0170000U) == 0120000U)
                    bad("unsafe package path");
                std::string folded = name;
                std::transform(folded.begin(), folded.end(), folded.begin(),
                               [](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
                if (!names.insert(folded).second)
                    bad("duplicate or case-colliding package entries");
                if (directory)
                    continue;
                const auto dot = folded.rfind('.');
                const auto ext = dot == folded.npos ? std::string{} : folded.substr(dot);
                if (ext == ".dll" || ext == ".so" || ext == ".exe" || ext == ".dylib" || ext == ".bat" ||
                    ext == ".cmd" || ext == ".ps1" || folded == "patch.bps")
                    bad("native programs and whole-ROM patches are not portable Custom Magic Codes");
                if (stat.m_uncomp_size > MaxExpanded - total || stat.m_uncomp_size > MaxPackage)
                    bad("expanded package exceeds its budget");
                total += std::size_t(stat.m_uncomp_size);
                Bytes content(std::size_t(stat.m_uncomp_size));
                if (!mz_zip_reader_extract_to_mem(&zip, i, content.data(), content.size(), 0))
                    bad("package entry failed its checksum");
                files.emplace(name, std::move(content));
            }
        } catch (...) {
            mz_zip_reader_end(&zip);
            throw;
        }
    }
    ~Archive() {
        mz_zip_reader_end(&zip);
    }
    const Bytes& require(const std::string& name) const {
        const auto it = files.find(name);
        if (it == files.end())
            bad("missing " + name);
        return it->second;
    }
};
// The pinned symbol parser is a trusted tooling parser. Validate its complete
// wire envelope and every index before it allocates or forms string views.
void preflight_symbols(View bytes, View binary) {
    if (bytes.size() < 52 || std::memcmp(bytes.data(), "N64RSYMS", 8))
        bad("invalid code symbol header");
    const auto word = [&](std::size_t at) {
        const auto b = slice(bytes, at, 4);
        return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
    };
    if (word(8) != 1)
        bad("unsupported code symbol version");
    std::array<unsigned, 10> count{};
    for (unsigned i = 0; i < count.size(); ++i) {
        count[i] = word(12 + 4 * i);
        if (count[i] > (i == 9 ? 65536U : MaxFunctions))
            bad("code symbol count exceeds its budget");
    }
    if (!count[0] || count[0] > 256 || count[1] > MaxDependencies || count[2] > 4096 || count[9] % 4)
        bad("invalid code symbol counts");
    std::size_t at = 52;
    const auto strings = slice(bytes, at, count[9]);
    at += count[9];
    const auto name = [&](unsigned start, unsigned size) {
        if (!size || size > 160 || start > strings.size() || size > strings.size() - start)
            bad("invalid code symbol name");
        for (auto c : strings.subspan(start, size))
            if (c < 32 || c > 126)
                bad("invalid code symbol name characters");
    };
    const auto table = [&](unsigned size, unsigned n) {
        const auto begin = at;
        if (n > (bytes.size() - std::min(at, bytes.size())) / size)
            bad("truncated code symbols");
        at += std::size_t(size) * n;
        return begin;
    };
    unsigned functions = 0;
    std::size_t code_bytes = 0, resident_bytes = 0, total_relocs = 0;
    struct SectionBounds {
        unsigned ram, size;
    };
    std::vector<SectionBounds> bounds;
    std::vector<std::pair<unsigned, unsigned>> local_targets;
    for (unsigned section = 0; section < count[0]; ++section) {
        const auto h = table(28, 1);
        const unsigned flags = word(h), offset = word(h + 4), ram = word(h + 8), size = word(h + 12),
                       bss = word(h + 16), funcs = word(h + 20), relocs = word(h + 24);
        if (flags || size > MaxCode || bss > MaxCode - size || offset > binary.size() ||
            size > binary.size() - offset || ram > 0xffffffffU - size - bss || funcs > MaxFunctions - functions ||
            relocs > MaxFunctions * 16 || (offset | ram) & 3U || size + bss > MaxCode - resident_bytes ||
            relocs > MaxFunctions * 16 - total_relocs)
            bad("invalid code section");
        resident_bytes += size + bss;
        total_relocs += relocs;
        for (const auto& b : bounds)
            if (ram < b.ram + b.size && b.ram < ram + size + bss)
                bad("overlapping code sections");
        bounds.push_back({ram, size + bss});
        const auto f = table(8, funcs);
        functions += funcs;
        std::vector<std::pair<unsigned, unsigned>> ranges;
        for (unsigned i = 0; i < funcs; ++i) {
            const unsigned off = word(f + 8 * i), len = word(f + 8 * i + 4);
            if (!len || (off | len | ram) & 3U || off > size || len > size - off || len > MaxCode - code_bytes)
                bad("function extends outside its code budget");
            code_bytes += len;
            ranges.emplace_back(off, off + len);
        }
        std::sort(ranges.begin(), ranges.end());
        for (unsigned i = 1; i < ranges.size(); ++i)
            if (ranges[i].first < ranges[i - 1].second)
                bad("overlapping code functions");
        const auto r = table(16, relocs);
        for (unsigned i = 0; i < relocs; ++i) {
            const unsigned off = word(r + 16 * i), type = word(r + 16 * i + 4), target = word(r + 16 * i + 8),
                           owner = word(r + 16 * i + 12);
            if (off % 4 || off > size || size - off < 4 || type > unsigned(N64Recomp::RelocType::R_MIPS_GPREL16))
                bad("invalid code relocation");
            if (owner == 0xfffffffeU) {
                if (target >= count[2])
                    bad("invalid import relocation");
            } else if (owner == 0xfffffffdU) {
                if (target >= count[7])
                    bad("invalid event relocation");
            } else if (owner & 0x80000000U) {
                if ((owner & 0x7fffffffU) >= count[0])
                    bad("invalid local section relocation");
                local_targets.emplace_back(owner & 0x7fffffffU, target);
            } else if (owner != 0x1000U || target >= 0x800000U)
                bad("invalid base-game relocation");
        }
    }
    for (const auto& [owner, target] : local_targets)
        if (target > bounds[owner].size)
            bad("local relocation is outside its section");
    auto t = table(12, count[1]);
    std::set<std::string> dependencies;
    for (unsigned i = 0; i < count[1]; ++i) {
        const unsigned start = word(t + 12 * i + 4), size = word(t + 12 * i + 8);
        name(start, size);
        if (!dependencies.emplace(reinterpret_cast<const char*>(strings.data() + start), size).second)
            bad("duplicate code dependency");
    }
    for (unsigned group : {2U, 3U}) {
        t = table(12, count[group]);
        std::set<std::pair<unsigned, std::string>> symbols;
        for (unsigned i = 0; i < count[group]; ++i) {
            const auto start = word(t + 12 * i), size = word(t + 12 * i + 4), dep = word(t + 12 * i + 8);
            name(start, size);
            if (dep >= count[1] ||
                !symbols.emplace(dep, std::string(reinterpret_cast<const char*>(strings.data() + start), size)).second)
                bad("invalid or duplicate dependency symbol");
        }
    }
    t = table(16, count[4]);
    for (unsigned i = 0; i < count[4]; ++i)
        if (word(t + 16 * i) >= functions || word(t + 16 * i + 4) != 0x1000U || word(t + 16 * i + 8) % 4 ||
            word(t + 16 * i + 12) > 1)
            bad("invalid function replacement");
    t = table(12, count[5]);
    std::set<unsigned> exported_functions;
    std::set<std::string> exported_names;
    for (unsigned i = 0; i < count[5]; ++i) {
        const auto function = word(t + 12 * i), start = word(t + 12 * i + 4), size = word(t + 12 * i + 8);
        name(start, size);
        if (function >= functions || !exported_functions.insert(function).second ||
            !exported_names.emplace(reinterpret_cast<const char*>(strings.data() + start), size).second)
            bad("invalid or duplicate function export");
    }
    t = table(8, count[6]);
    for (unsigned i = 0; i < count[6]; ++i)
        if (word(t + 8 * i) >= count[3] || word(t + 8 * i + 4) >= functions)
            bad("invalid code callback");
    t = table(8, count[7]);
    std::set<std::string> provided_events;
    for (unsigned i = 0; i < count[7]; ++i) {
        const auto start = word(t + 8 * i), size = word(t + 8 * i + 4);
        name(start, size);
        if (!provided_events.emplace(reinterpret_cast<const char*>(strings.data() + start), size).second)
            bad("duplicate provided event");
    }
    t = table(16, count[8]);
    for (unsigned i = 0; i < count[8]; ++i)
        if (word(t + 16 * i) >= functions || word(t + 16 * i + 4) != 0x1000U || word(t + 16 * i + 8) % 4 ||
            word(t + 16 * i + 12) > 1)
            bad("invalid function hook");
    if (at != bytes.size())
        bad("unexpected trailing code symbol bytes");
}
std::set<std::string> ids(const Json& object, const char* key) {
    std::set<std::string> out;
    if (!object.contains(key))
        return out;
    const auto& a = object[key];
    if (!a.is_array() || a.size() > MaxDependencies)
        bad(std::string("invalid ") + key);
    for (const auto& v : a) {
        if (!v.is_string() || !valid_id(v.get_ref<const std::string&>()) || !out.insert(v.get<std::string>()).second)
            bad(std::string("invalid or duplicate ") + key);
    }
    return out;
}
} // namespace
std::string compatibility_issue(std::string_view digest) {
    if (digest == "86b5b6a8ff1128d76250706b39c6af80a6ca75796413c92d345a97a439dd164c" ||
        digest == "9d3aa5c35ce02e1dbeac7d7daf9ce12783e71cabd9438eae3db236c21f0d54a8")
        return "Update Custom Trophy Races to 1.0.2 in Details before playing. Your saved cups can be kept.";
    return {};
}
bool valid_id(std::string_view id) {
    return !id.empty() && id.size() <= 96 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
Version Version::parse(std::string_view value) {
    Version out;
    unsigned* parts[] = {&out.major, &out.minor, &out.patch};
    if (value.empty() || value.size() > 64)
        bad("invalid semantic version");
    const auto suffix = value.find_first_of("-+");
    if (suffix != value.npos) {
        auto tail = value.substr(suffix + 1);
        const bool prerelease = value[suffix] == '-';
        if (prerelease) {
            const auto build = tail.find('+');
            out.prerelease = tail.substr(0, build);
        }
        for (std::size_t at = 0; at <= tail.size();) {
            const auto end = tail.find_first_of(".+", at);
            const auto part = tail.substr(at, end == tail.npos ? tail.size() - at : end - at);
            if (part.empty() || !std::all_of(part.begin(), part.end(), [](unsigned char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
                }))
                bad("invalid semantic version suffix");
            if (prerelease && at < out.prerelease.size() && part.size() > 1 && part[0] == '0' &&
                std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; }))
                bad("invalid numeric prerelease identifier");
            if (end == tail.npos)
                break;
            if (tail[end] == '+' && (value[suffix] == '+' || tail.find('+', end + 1) != tail.npos))
                bad("invalid build metadata");
            at = end + 1;
        }
        value = value.substr(0, suffix);
    }
    for (unsigned i = 0; i < 3; ++i) {
        const auto end = value.find('.');
        const auto part = value.substr(0, end);
        if (part.empty() || (part.size() > 1 && part[0] == '0'))
            bad("invalid semantic version");
        auto [last, ec] = std::from_chars(part.data(), part.data() + part.size(), *parts[i]);
        if (ec != std::errc{} || last != part.data() + part.size() || *parts[i] > 65535)
            bad("invalid semantic version");
        if (i == 2) {
            if (end != value.npos)
                bad("invalid semantic version");
        } else {
            if (end == value.npos)
                bad("invalid semantic version");
            value.remove_prefix(end + 1);
        }
    }
    return out;
}
std::strong_ordering Version::operator<=>(const Version& b) const {
    if (auto c = major <=> b.major; c != 0)
        return c;
    if (auto c = minor <=> b.minor; c != 0)
        return c;
    if (auto c = patch <=> b.patch; c != 0)
        return c;
    if (prerelease.empty() || b.prerelease.empty())
        return prerelease.empty() == b.prerelease.empty() ? std::strong_ordering::equal
               : prerelease.empty()                       ? std::strong_ordering::greater
                                                          : std::strong_ordering::less;
    std::string_view a_tail = prerelease, b_tail = b.prerelease;
    while (true) {
        const auto ae = a_tail.find('.'), be = b_tail.find('.');
        const auto a = a_tail.substr(0, ae), bb = b_tail.substr(0, be);
        const auto numeric = [](std::string_view s) {
            return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
        };
        const bool an = numeric(a), bn = numeric(bb);
        if (an != bn)
            return an ? std::strong_ordering::less : std::strong_ordering::greater;
        if (an && a.size() != bb.size())
            return a.size() <=> bb.size();
        if (auto c = a <=> bb; c != 0)
            return c;
        if (ae == a_tail.npos || be == b_tail.npos)
            return (ae == a_tail.npos) == (be == b_tail.npos) ? std::strong_ordering::equal
                   : ae == a_tail.npos                        ? std::strong_ordering::less
                                                              : std::strong_ordering::greater;
        a_tail.remove_prefix(ae + 1);
        b_tail.remove_prefix(be + 1);
    }
}
Json parse_document(View bytes, std::size_t limit) {
    if (bytes.empty() || bytes.size() > limit)
        bad("metadata size is outside its limit");
    std::vector<std::set<std::string>> keys;
    auto result = Json::parse(bytes.begin(), bytes.end(), [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 20)
            bad("metadata is nested too deeply");
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        if (event == Json::parse_event_t::key && (!keys.size() || !keys.back().insert(value.get<std::string>()).second))
            bad("duplicate metadata field");
        if (event == Json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
    if (!result.is_object())
        bad("metadata must be an object");
    return result;
}
std::shared_ptr<const Package> inspect(View bytes, std::string_view host_version) {
    Archive archive(bytes);
    auto manifest = parse_document(archive.require("mod.json"));
    auto meta = parse_document(archive.require("dkr.json"));
    auto p = std::make_shared<Package>();
    p->id = text(manifest, "id", 96);
    if (!valid_id(p->id))
        bad("invalid mod ID");
    p->name = text(manifest, "display_name", 160);
    p->version = text(manifest, "version", 64);
    Version::parse(p->version);
    p->description = text(manifest, "description", 32768, true);
    if (text(manifest, "game_id", 32) != "dkr")
        bad("package is for a different game");
    p->minimum_host = Version::parse(text(manifest, "minimum_recomp_version", 64));
    if (p->minimum_host > Version::parse(host_version))
        bad("package requires a newer DKR-R version");
    if (manifest.contains("native_libraries") &&
        (!manifest["native_libraries"].is_array() || !manifest["native_libraries"].empty()))
        bad("native libraries are not supported");
    if (!meta.contains("schema") || !meta["schema"].is_number_unsigned() || meta["schema"] != 1 ||
        !meta.contains("api") || !meta["api"].is_number_unsigned() || (meta["api"] != 1 && meta["api"] != 2))
        bad("unsupported DKR SDK version");
    p->api = meta.at("api").get<unsigned>();
    if (meta.contains("development") && !meta["development"].is_boolean())
        bad("invalid development-session declaration");
    p->development = p->api == 2 && meta.value("development", false);
    if (meta.value("activation", std::string("restart")) != "restart")
        bad("code changes require a new game session");
    const auto mode = meta.value("online", std::string("offline-only"));
    if (mode != "offline-only" && mode != "deterministic")
        bad("unsupported online execution mode");
    p->deterministic = mode == "deterministic";
    if (!meta.contains("revisions") || !meta["revisions"].is_array() || meta["revisions"].empty() ||
        meta["revisions"].size() > 2)
        bad("supported ROM revisions are required");
    for (const auto& r : meta["revisions"]) {
        if (!r.is_string())
            bad("invalid ROM revision");
        const auto v = r.get<std::string>();
        if ((v != "us.v77" && v != "us.v80") || !p->revisions.insert(v).second)
            bad("invalid or duplicate ROM revision");
    }
    p->conflicts = ids(meta, "conflicts");
    p->systems = ids(meta, "systems");
    if (meta.contains("requires")) {
        const auto& modules = meta["requires"];
        if (!modules.is_object() || modules.size() > 32)
            bad("invalid SDK module requirements");
        for (const auto& [name, version] : modules.items()) {
            if (!valid_id(name) || !version.is_number_unsigned() || version.get<std::uint64_t>() < 1 ||
                version.get<std::uint64_t>() > p->api)
                bad("unsupported SDK module version");
            p->modules.emplace(name, version.get<unsigned>());
        }
    }
    if (manifest.contains("dependencies")) {
        if (!manifest["dependencies"].is_array() || manifest["dependencies"].size() > MaxDependencies)
            bad("invalid dependencies");
        std::set<std::string> seen;
        for (const auto& d : manifest["dependencies"]) {
            if (!d.is_string())
                bad("invalid dependency");
            const auto raw = d.get<std::string>();
            const auto split = raw.find(':');
            Dependency dep{raw.substr(0, split), split == raw.npos ? Version{} : Version::parse(raw.substr(split + 1))};
            if (!valid_id(dep.id) || dep.id == p->id || !seen.insert(dep.id).second)
                bad("invalid or duplicate dependency");
            p->dependencies.push_back(std::move(dep));
        }
    }
    if (meta.contains("requires_tracks")) {
        if (!meta["requires_tracks"].is_array() || meta["requires_tracks"].size() > 512)
            bad("invalid course dependencies");
        std::set<std::string> seen;
        for (const auto& v : meta["requires_tracks"]) {
            if (!v.is_string() || v.get_ref<const std::string&>().empty() ||
                v.get_ref<const std::string&>().size() > 160 || !seen.insert(v.get<std::string>()).second)
                bad("invalid course identity");
            p->tracks.push_back(v.get<std::string>());
        }
    }
    if (meta.contains("resources")) {
        const auto& resources = meta["resources"];
        if (!resources.is_object() || resources.size() > 256)
            bad("invalid resources");
        std::set<std::string> paths;
        for (const auto& [name, path] : resources.items()) {
            if (!valid_id(name) || !path.is_string() || !path_ok(path.get_ref<const std::string&>()) ||
                !paths.insert(path.get<std::string>()).second)
                bad("invalid or aliased resource name");
            const auto entry = archive.files.find(path.get<std::string>());
            if (entry == archive.files.end() || entry->first == "mod.json" || entry->first == "dkr.json" ||
                entry->first == "mod_binary.bin" || entry->first == "mod_syms.bin")
                bad("invalid resource entry");
            p->resources.emplace(name, std::move(entry->second));
        }
    }
    if (manifest.contains("config_schema")) {
        const auto& schema = manifest["config_schema"];
        if (!schema.is_object() || !schema.contains("options") || !schema["options"].is_array() ||
            schema["options"].size() > 128)
            bad("invalid settings schema");
        p->options = schema["options"];
    }
    if (p->api == 2 && meta.contains("profile")) {
        if (!meta["profile"].is_string())
            bad("SDK profile must be compact, standard or large");
        p->sdk2_profile = meta["profile"].get<std::string>();
        if (p->sdk2_profile == "compact") {
            p->sdk2_heap = 32768;
            p->sdk2_instructions = 100000;
            p->sdk2_native_calls = 32;
        } else if (p->sdk2_profile == "large") {
            p->sdk2_heap = 262144;
            p->sdk2_instructions = 500000;
            p->sdk2_native_calls = 256;
        } else if (p->sdk2_profile != "standard")
            bad("unknown portable SDK profile");
    }
    if (meta.contains("live_reload")) {
        if (p->api != 2 || !p->development || meta.at("live_reload") != "instruction-only" ||
            !meta.contains("state_schema") || !meta.at("state_schema").is_number_unsigned() ||
            meta.at("state_schema").get<std::uint64_t>() < 1 ||
            meta.at("state_schema").get<std::uint64_t>() > UINT32_MAX)
            bad("Live reload requires API 2 development, instruction-only and a nonzero state_schema");
        p->sdk2_reload = true;
        p->sdk2_state_schema = meta.at("state_schema").get<unsigned>();
    }
    if (meta.contains("live_options")) {
        if (p->api != 2 || !p->modules.contains("live-settings2") || !meta.at("live_options").is_array() ||
            meta.at("live_options").size() > 128)
            bad("Live options require API 2/live-settings2 and a bounded list");
        for (const auto& id : meta.at("live_options")) {
            if (!id.is_string() || !valid_id(id.get<std::string>()) ||
                !p->sdk2_live_options.insert(id.get<std::string>()).second)
                bad("Invalid or duplicate live option ID");
            const auto option = std::find_if(p->options.begin(), p->options.end(),
                                             [&](const auto& item) { return item.at("id") == id; });
            if (option == p->options.end() || (option->at("type") != "Number" && option->at("type") != "Enum"))
                bad("Only explicitly reviewed numeric/enum options can be live");
        }
    }
    sdk2_schema(*p);
    if (p->api == 2 && p->resources.contains("source-map")) {
        const auto map = parse_document(p->resources.at("source-map"), 1024 * 1024);
        if (map.value("schema", 0) != 2 || !map.contains("functions") || !map["functions"].is_array() ||
            map["functions"].size() > 16384)
            bad("invalid SDK 2 source map");
        for (const auto& row : map["functions"]) {
            if (!row.is_object() || !row.contains("name") || !row["name"].is_string() ||
                row["name"].get_ref<const std::string&>().size() > 512 || !row.contains("file") ||
                !row["file"].is_string() || row["file"].get_ref<const std::string&>().size() > 512 ||
                !row.contains("line") || !row["line"].is_number_unsigned() ||
                row["line"].get<std::uint64_t>() > 10000000)
                bad("invalid SDK source-map row");
            const auto file = row["file"].get<std::string>();
            if (file.find("..") != file.npos || file.find(':') != file.npos || file.find('\\') != file.npos ||
                std::any_of(file.begin(), file.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                bad("unsafe source-map filename");
            if (!file.empty() && row["line"].get<unsigned>())
                p->debug_lines.emplace(row["name"].get<std::string>(),
                                       file + ":" + std::to_string(row["line"].get<unsigned>()));
        }
    }
    p->binary = archive.require("mod_binary.bin");
    const auto& symbols = archive.require("mod_syms.bin");
    if (p->binary.empty() || p->binary.size() > MaxCode || symbols.size() > 4 * MiB)
        bad("code image exceeds its budget");
    preflight_symbols(symbols, p->binary);
    auto context = std::make_shared<N64Recomp::Context>();
    const std::unordered_map<std::uint32_t, std::uint16_t> sections{{0x1000, 0}};
    const auto result = N64Recomp::parse_mod_symbols(
        std::span(reinterpret_cast<const char*>(symbols.data()), symbols.size()), p->binary, sections, *context);
    if (result != N64Recomp::ModSymbolsError::Good || context->functions.empty() ||
        context->functions.size() > MaxFunctions || context->sections.size() > 256)
        bad("invalid code symbols or unsupported section layout");
    for (const auto& section : context->sections) {
        if (section.fixed_address || section.globally_loaded || section.rom_addr > p->binary.size() ||
            section.size > p->binary.size() - section.rom_addr || section.bss_size > MaxCode)
            bad("fixed-address or invalid mod sections are not supported");
    }
    for (const auto& import : context->import_symbols) {
        if (import.base.name == "recomp_change_save_file" || import.base.name == "recomp_get_mod_file_path")
            bad("code packages cannot bypass DKR-R save or resource routing");
    }
    p->symbols = std::move(context);
    p->digest = sha256(bytes);
    configuration(*p, Json::object());
    return p;
}
Json configuration(const Package& p, const Json& selected) {
    if (!selected.is_object() || selected.size() > 128)
        bad("invalid mod settings");
    Json out = Json::object();
    std::set<std::string> known;
    for (const auto& option : p.options) {
        if (!option.is_object())
            bad("invalid option");
        const auto id = text(option, "id", 96);
        text(option, "name", 160, true);
        if (!valid_id(id) || !known.insert(id).second)
            bad("invalid or duplicate setting ID");
        const auto type = text(option, "type", 20);
        if (!option.contains("default"))
            bad("setting has no default");
        auto value = selected.contains(id) ? selected[id] : option["default"];
        if (type == "Enum") {
            // RecompModTool emits enum defaults by label. The frozen runtime
            // configuration uses numeric indices, independent of UI locale.
            if (value.is_string() && option.contains("options") && option["options"].is_array()) {
                const auto& choices = option["options"];
                const auto found = std::find(choices.begin(), choices.end(), value);
                if (found == choices.end())
                    bad("unknown enum label " + id);
                value = std::int64_t(std::distance(choices.begin(), found));
            }
            if (!option.contains("options") || !option["options"].is_array() || option["options"].empty() ||
                option["options"].size() > 256 || !value.is_number_integer() || value.get<std::int64_t>() < 0 ||
                std::uint64_t(value.get<std::int64_t>()) >= option["options"].size())
                bad("invalid enum setting " + id);
            for (const auto& choice : option["options"])
                if (!choice.is_string() || choice.get_ref<const std::string&>().size() > 160)
                    bad("invalid enum labels");
        } else if (type == "Number") {
            if (!value.is_number() || !std::isfinite(value.get<double>()) || !option.contains("min") ||
                !option.contains("max") || !option["min"].is_number() || !option["max"].is_number() ||
                !std::isfinite(option["min"].get<double>()) || !std::isfinite(option["max"].get<double>()) ||
                option["min"].get<double>() > option["max"].get<double>() ||
                value.get<double>() < option["min"].get<double>() || value.get<double>() > option["max"].get<double>())
                bad("invalid numeric setting " + id);
        } else if (type == "String") {
            if (!value.is_string() || value.get_ref<const std::string&>().size() > 4096 ||
                value.get_ref<const std::string&>().find('\0') != std::string::npos)
                bad("invalid text setting " + id);
        } else
            bad("unsupported setting type " + type);
        out[id] = std::move(value);
    }
    for (const auto& [key, value] : selected.items())
        if (!known.contains(key))
            bad("unknown setting " + key);
    return out;
}
std::vector<std::shared_ptr<const Package>> resolve(const std::vector<std::shared_ptr<const Package>>& installed,
                                                    const std::set<std::string>& enabled, std::string_view revision,
                                                    bool online,
                                                    const std::map<std::string, unsigned>& available_modules) {
    if (enabled.size() > 128)
        bad("too many enabled code packages");
    std::map<std::string, std::shared_ptr<const Package>> by_id;
    for (const auto& p : installed)
        if (enabled.contains(p->id) && !by_id.emplace(p->id, p).second)
            bad("more than one edition selected for " + p->id);
    std::map<std::string, unsigned> marks;
    std::map<std::string, std::string> owners;
    std::vector<std::shared_ptr<const Package>> out;
    std::function<void(const std::string&)> visit = [&](const auto& id) {
        if (marks[id] == 2)
            return;
        if (marks[id] == 1)
            bad("dependency cycle at " + id);
        const auto found = by_id.find(id);
        if (found == by_id.end())
            bad("enable the required package " + id);
        const auto& p = found->second;
        marks[id] = 1;
        if (!p->revisions.contains(std::string(revision)))
            bad(p->name + " does not support the selected Game Pak");
        if (const auto issue = compatibility_issue(p->digest); !issue.empty())
            bad(issue);
        if (online && !p->deterministic)
            bad(p->name + " is offline-only. Disable it or import an online-compatible edition before hosting.");
        for (const auto& [module, version] : p->modules) {
            const auto found = available_modules.find(module);
            if (found == available_modules.end() || found->second != version)
                bad(p->name + " requires an unavailable SDK module: " + module);
        }
        for (const auto& d : p->dependencies) {
            visit(d.id);
            if (Version::parse(by_id.at(d.id)->version) < d.minimum)
                bad("a newer " + d.id + " is required by " + p->name);
        }
        for (const auto& conflict : p->conflicts)
            if (by_id.contains(conflict))
                bad(p->name + " conflicts with " + by_id.at(conflict)->name);
        for (const auto& system : p->systems)
            if (!owners.emplace(system, p->name).second)
                bad(p->name + " and " + owners.at(system) + " both own " + system);
        marks[id] = 2;
        out.push_back(p);
    };
    for (const auto& id : enabled)
        visit(id);
    return out;
}
Json structured_data(const Json& data) {
    if (!data.is_object() || data.size() > 32)
        bad("invalid structured code configuration");
    for (const auto& [id, value] : data.items())
        if (!valid_id(id) || !value.is_object())
            bad("invalid code configuration document");
    const auto bytes = data.dump();
    if (bytes.size() > 65536)
        bad("structured code configuration exceeds 64 KiB");
    return parse_document(View(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()), 65536);
}
std::string session_identity(const std::vector<std::shared_ptr<const Package>>& packages,
                             const std::map<std::string, Json>& settings, std::string_view revision,
                             const std::map<std::string, Json>& data) {
    const bool sdk2 = std::any_of(packages.begin(), packages.end(), [](const auto& p) { return p->api == 2; });
    Json value = {{"work-policy", std::string(execution::Policy)},
                  {"api", sdk2 ? 2 : ApiVersion},
                  {"execution", sdk2 ? 2 : ExecutionVersion},
                  {"revision", revision},
                  {"packages", Json::array()}};
    for (const auto& p : packages) {
        const auto found = settings.find(p->id);
        const auto options = configuration(*p, found == settings.end() ? Json::object() : found->second);
        const auto document = data.find(p->id);
        value["packages"].push_back(
            {{"id", p->id},
             {"version", p->version},
             {"hash", p->digest},
             {"settings", options},
             {"data", document == data.end() ? Json::object() : structured_data(document->second)}});
    }
    const auto bytes = value.dump();
    return sha256(View(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}
} // namespace dkr::mods::code
