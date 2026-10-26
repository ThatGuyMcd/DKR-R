#include "update_core.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <json/json.hpp>
#include <map>
#include <mbedtls/sha256.h>
#include <miniz/miniz.h>
#include <monocypher-ed25519.h>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dkr::updates {
using Json = nlohmann::json;
namespace {
[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}
std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
bool digits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::uint32_t number(std::string_view s) {
    std::uint32_t n{};
    auto result = std::from_chars(s.data(), s.data() + s.size(), n);
    if (!digits(s) || (s.size() > 1 && s.front() == '0') || result.ec != std::errc{} ||
        result.ptr != s.data() + s.size())
        fail("Invalid release version.");
    return n;
}
bool identifier(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    });
}
std::vector<std::string> split(std::string_view s, char delimiter) {
    std::vector<std::string> out;
    for (;;) {
        auto at = s.find(delimiter);
        out.emplace_back(s.substr(0, at));
        if (at == s.npos)
            break;
        s.remove_prefix(at + 1);
    }
    return out;
}
Json parse(std::string_view text) {
    if (text.empty() || text.size() > kMaxMetadataBytes)
        fail("Update metadata is empty or too large.");
    // Reject duplicate JSON keys; the signing tool emits bounded, unambiguous JSON.
    std::vector<std::set<std::string>> keys;
    auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 64)
            fail("Update metadata is too deeply nested.");
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        if (event == Json::parse_event_t::key && (keys.empty() || !keys.back().insert(value.get<std::string>()).second))
            fail("Duplicate update metadata key.");
        if (event == Json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    };
    return Json::parse(text.begin(), text.end(), callback);
}
std::string text(const Json& j, const char* field, std::size_t limit = 256) {
    auto s = j.at(field).get<std::string>();
    if (s.empty() || s.size() > limit || s.find('\0') != s.npos)
        fail(std::string("Invalid update field: ") + field);
    return s;
}
void regular(const std::filesystem::path& p) {
    std::error_code ec;
    auto s = std::filesystem::symlink_status(p, ec);
    if (ec || !std::filesystem::is_regular_file(s) || std::filesystem::is_symlink(s))
        fail("Update file is missing or is a filesystem link: " + p.string());
#ifdef _WIN32
    auto a = GetFileAttributesW(p.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT))
        fail("Update file is a reparse point.");
#endif
    if (std::filesystem::hard_link_count(p, ec) != 1 || ec)
        fail("Update file has another filesystem owner.");
}
bool path_exists(const std::filesystem::path& p) {
    std::error_code ec;
    auto s = std::filesystem::symlink_status(p, ec);
    if (s.type() == std::filesystem::file_type::not_found)
        return false;
    if (ec)
        fail("Could not inspect update path: " + p.string());
    return true;
}
void parent_safe(const std::filesystem::path& root, const std::filesystem::path& relative) {
    auto at = root;
    for (const auto& component : relative.parent_path()) {
        at /= component;
        if (path_exists(at)) {
            require_private_tree(at);
        } else
            std::filesystem::create_directory(at);
    }
}
std::filesystem::path transaction(const std::filesystem::path& root) {
    return root / ".dkr-update-transaction";
}
void sync_directory(const std::filesystem::path& p) {
#ifndef _WIN32
    int fd = open(p.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        fail("Could not sync update directory.");
    auto ok = fsync(fd) == 0;
    close(fd);
    if (!ok)
        fail("Could not durably sync update directory.");
#else
    (void)p; // MoveFileExW WRITE_THROUGH and flushed journal files below.
#endif
}
void sync_file(const std::filesystem::path& target) {
    regular(target);
#ifdef _WIN32
    HANDLE h = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = h != INVALID_HANDLE_VALUE && FlushFileBuffers(h);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
#else
    int h = open(target.c_str(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
    bool ok = h >= 0 && fsync(h) == 0;
    if (h >= 0)
        close(h);
#endif
    if (!ok)
        fail("Could not durably stage the update.");
    sync_directory(target.parent_path());
}
void copy_durable(const std::filesystem::path& source, const std::filesystem::path& target) {
    regular(source);
    if (path_exists(target))
        fail("Update staging destination already exists.");
    std::filesystem::copy_file(source, target);
    sync_file(target);
}
Json journal(const std::filesystem::path& root) {
    auto dir = transaction(root);
    require_private_tree(dir);
    auto j = parse(read_text(dir / "journal.json"));
    if (j.at("schema") != kSchema || !j.at("files").is_array() || j.at("files").size() > 4096)
        fail("Invalid recovery journal.");
    std::set<std::string> unique;
    for (const auto& e : j.at("files")) {
        auto p = text(e, "path", 1024);
        if (!safe_relative(p) || protected_path(p) || !unique.insert(lower(p)).second)
            fail("Unsafe recovery path.");
        if (e.at("old").get<bool>())
            unhex(text(e, "old_hash"), 32);
        if (e.at("new").get<bool>())
            unhex(text(e, "new_hash"), 32);
    }
    return j;
}
} // namespace
Version Version::parse(std::string_view value) {
    if (value.size() > 128)
        fail("Invalid release version.");
    auto plus = value.find('+');
    if (plus != value.npos) {
        for (auto& s : split(value.substr(plus + 1), '.'))
            if (!identifier(s))
                fail("Invalid version metadata.");
        value = value.substr(0, plus);
    }
    auto dash = value.find('-');
    Version v;
    if (dash != value.npos) {
        v.pre = split(value.substr(dash + 1), '.');
        for (auto& s : v.pre)
            if (!identifier(s) || (digits(s) && s.size() > 1 && s[0] == '0'))
                fail("Invalid prerelease version.");
        value = value.substr(0, dash);
    }
    auto core = split(value, '.');
    if (core.size() != 3)
        fail("Invalid release version.");
    v.major = number(core[0]);
    v.minor = number(core[1]);
    v.patch = number(core[2]);
    return v;
}
int Version::compare(const Version& o) const {
    for (auto pair : {std::pair{major, o.major}, std::pair{minor, o.minor}, std::pair{patch, o.patch}})
        if (pair.first != pair.second)
            return pair.first < pair.second ? -1 : 1;
    if (pre.empty() != o.pre.empty())
        return pre.empty() ? 1 : -1;
    for (std::size_t i = 0; i < std::min(pre.size(), o.pre.size()); ++i) {
        if (pre[i] == o.pre[i])
            continue;
        bool a = digits(pre[i]), b = digits(o.pre[i]);
        if (a != b)
            return a ? -1 : 1;
        if (a && pre[i].size() != o.pre[i].size())
            return pre[i].size() < o.pre[i].size() ? -1 : 1;
        return pre[i] < o.pre[i] ? -1 : 1;
    }
    return pre.size() == o.pre.size() ? 0 : (pre.size() < o.pre.size() ? -1 : 1);
}
std::string platform() {
#if defined(__ANDROID__)
    return "android-arm64";
#elif defined(DKR_IOS)
    return "ios-arm64";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos-arm64";
#elif defined(__APPLE__)
    return "macos-x86_64";
#elif defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    return "windows-arm64";
#elif defined(_WIN32)
    return "windows-x64";
#elif defined(__aarch64__)
    return "linux-aarch64";
#else
    return "linux-x86_64";
#endif
}
bool safe_relative(std::string_view path) {
    if (path.empty() || path.size() > 1024 || path.front() == '/' || path.back() == '/')
        return false;
    for (unsigned char c : path)
        if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '\0')
            return false;
    for (const auto& part : split(path, '/')) {
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
            return false;
        auto stem = lower(part.substr(0, part.find('.')));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '0' &&
             stem[3] <= '9'))
            return false;
    }
    return true;
}
bool protected_path(std::string_view path) {
    auto p = lower(path);
    auto top = p.substr(0, p.find('/'));
    if (top == "portable.txt" || top == "dkr-runtime-data" || top == "mods" || top == "online-mods" ||
        top == "texture-packs" || top == "saves" || top == "logs" || top == "crash-dumps" ||
        top.starts_with(".dkr-update"))
        return true;
    auto extension = lower(std::filesystem::path(p).extension().string());
    return extension == ".eep" || extension == ".mpk" || extension == ".z64" || extension == ".v64" ||
           extension == ".n64" || extension == ".sra" || extension == ".fla";
}
bool allowed_https_url(std::string_view url) {
    if (url.size() > 4096 || !url.starts_with("https://") || url.find_first_of("\\\r\n\t ") != url.npos)
        return false;
    auto end = url.find('/', 8);
    auto host = url.substr(8, end == url.npos ? url.size() - 8 : end - 8);
    return host == "api.github.com" || host == "github.com" || host == "release-assets.githubusercontent.com" ||
           host == "objects.githubusercontent.com";
}
std::string artifact_url(const Manifest& m, const Artifact& a) {
    return "https://github.com/ThatGuyMcd/DKR-R/releases/download/" + m.tag + "/" + a.name;
}
std::vector<std::uint8_t> unhex(std::string_view input, std::size_t size) {
    if (input.size() != size * 2)
        fail("Invalid update cryptographic identity.");
    std::vector<std::uint8_t> out(size);
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < size; ++i) {
        int a = digit(input[i * 2]), b = digit(input[i * 2 + 1]);
        if (a < 0 || b < 0)
            fail("Invalid update signature encoding.");
        out[i] = std::uint8_t(a * 16 + b);
    }
    return out;
}
std::string hex(std::span<const std::uint8_t> bytes) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto b : bytes) {
        out += alphabet[b >> 4];
        out += alphabet[b & 15];
    }
    return out;
}
std::string hash(std::span<const std::uint8_t> bytes) {
    std::array<unsigned char, 32> out{};
    if (mbedtls_sha256(bytes.data(), bytes.size(), out.data(), 0) != 0)
        fail("Could not hash the update.");
    return hex(out);
}
std::string hash_file(const std::filesystem::path& path, std::uint64_t limit) {
    regular(path);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        fail("Could not read update file.");
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    struct Free {
        mbedtls_sha256_context& c;
        ~Free() {
            mbedtls_sha256_free(&c);
        }
    } free{context};
    if (mbedtls_sha256_starts(&context, 0) != 0)
        fail("Could not initialize update hash.");
    std::array<char, 65536> buffer{};
    std::uint64_t total = 0;
    while (input) {
        input.read(buffer.data(), buffer.size());
        auto n = input.gcount();
        total += std::uint64_t(n);
        if (total > limit)
            fail("Update file exceeds its size limit.");
        if (n && mbedtls_sha256_update(&context, reinterpret_cast<unsigned char*>(buffer.data()), std::size_t(n)))
            fail("Update hashing failed.");
    }
    if (!input.eof())
        fail("Update file could not be read completely.");
    std::array<unsigned char, 32> out{};
    if (mbedtls_sha256_finish(&context, out.data()))
        fail("Update hashing failed.");
    return hex(out);
}
Manifest verify_manifest(std::string_view bytes, std::string_view signature, std::string_view key) {
    if (bytes.empty() || bytes.size() > kMaxMetadataBytes)
        fail("Update manifest exceeds its size limit.");
    auto sig = unhex(signature, 64), pub = unhex(key, 32);
    if (crypto_ed25519_check(sig.data(), pub.data(), reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()))
        fail("The update manifest signature is not trusted. Nothing was installed.");
    auto j = parse(bytes);
    if (j.at("schema") != kSchema)
        fail("Unsupported updater manifest schema.");
    Manifest m;
    m.version = text(j, "version", 128);
    auto version = Version::parse(m.version);
    m.tag = text(j, "tag", 128);
    m.channel = text(j, "channel", 16);
    if (!std::all_of(m.tag.begin(), m.tag.end(),
                     [](char c) { return identifier(std::string_view(&c, 1)) || c == '.'; }) ||
        (m.channel != "stable" && m.channel != "beta") || (m.channel == "stable" && !version.pre.empty()))
        fail("Invalid release channel or tag.");
    m.notes = text(j, "notes", 1024);
    if (!m.notes.starts_with("https://github.com/ThatGuyMcd/DKR-R/releases/"))
        fail("Update notes must refer to the official repository.");
    m.commit = text(j, "commit", 40);
    unhex(m.commit, 20);
    m.minimum_updater = j.at("minimum_updater").get<unsigned>();
    if (m.minimum_updater > kSchema)
        fail("This release needs a newer updater. Open the releases page to update manually.");
    if (!j.at("artifacts").is_array() || j.at("artifacts").empty() || j.at("artifacts").size() > 16)
        fail("Invalid update platform table.");
    std::set<std::string> platforms;
    for (auto& item : j.at("artifacts")) {
        Artifact a;
        a.platform = text(item, "platform", 32);
        a.name = text(item, "name", 180);
        a.kind = text(item, "kind", 16);
        a.minimum_os = item.value("minimum_os", std::string{});
        a.minimum_glibc = item.value("minimum_glibc", std::string{});
        for (const auto& requirement : {a.minimum_os, a.minimum_glibc}) {
            if (requirement.size() > 32 || requirement.find_first_not_of("0123456789.") != requirement.npos)
                fail("Invalid update operating-system requirement.");
        }
        a.sha256 = lower(text(item, "sha256", 64));
        unhex(a.sha256, 32);
        if (!platforms.insert(a.platform).second || !safe_relative(a.name) || a.name.find('/') != a.name.npos ||
            a.name.find('%') != a.name.npos || a.name.find('?') != a.name.npos || a.name.find('#') != a.name.npos)
            fail("Invalid update asset name.");
        a.bytes = item.at("bytes").get<std::uint64_t>();
        if (!a.bytes || a.bytes > kMaxPackageBytes)
            fail("Update package exceeds its size limit.");
        if (a.kind != "zip" && a.kind != "appimage" && a.kind != "apk" && a.kind != "ipa")
            fail("Unsupported update package kind.");
        const std::map<std::string, std::string> kinds{{"windows-x64", "zip"},       {"windows-arm64", "zip"},
                                                       {"linux-x86_64", "appimage"}, {"linux-aarch64", "appimage"},
                                                       {"macos-arm64", "zip"},       {"macos-x86_64", "zip"},
                                                       {"android-arm64", "apk"},     {"ios-arm64", "ipa"}};
        if (!kinds.contains(a.platform) || kinds.at(a.platform) != a.kind)
            fail("Update platform and package kind disagree.");
        a.root = item.value("root", std::string{});
        if (!a.root.empty() && !safe_relative(a.root))
            fail("Unsafe update archive root.");
        std::set<std::string> paths;
        std::uint64_t size = 0;
        for (auto& entry : item.at("files")) {
            File f;
            f.path = text(entry, "path", 1024);
            f.sha256 = lower(text(entry, "sha256", 64));
            unhex(f.sha256, 32);
            f.bytes = entry.at("bytes").get<std::uint64_t>();
            f.mode = entry.value("mode", 0644U);
            f.link = entry.value("link", std::string{});
            if (!safe_relative(f.path) || protected_path(f.path) || !paths.insert(lower(f.path)).second ||
                f.bytes > kMaxPackageBytes || f.mode > 0777U)
                fail("Unsafe update file table.");
            if (!f.link.empty() && (a.platform.starts_with("windows") || f.link.front() == '/' ||
                                    f.link.size() > 1024 || f.link.find('\\') != f.link.npos))
                fail("Unsafe update link.");
            size += f.bytes;
            if (size > kMaxExtractedBytes || paths.size() > 4096)
                fail("Update extraction exceeds its limit.");
            a.files.push_back(std::move(f));
        }
        if (a.kind == "zip" && (a.files.empty() || a.root.empty()))
            fail("Update ZIP has no file ownership table.");
        if (a.platform.starts_with("windows")) {
            for (const auto* required :
                 {"DKR-R.exe", "assets/updates/install.json", "libexec/dkr-r/DKR-R-Updater.exe",
                  "libexec/dkr-r/DKR-R-ModWorker.exe", "libexec/dkr-r/DKR-R-InputHost.exe", "libexec/dkr-r/SDL3.dll"})
                if (!paths.contains(lower(required)))
                    fail("Windows update package is missing a required program component.");
            const bool private_runtime = paths.contains("libexec/dkr-r/dkr-r-runtime.exe");
            for (const auto* component : {"DKR-R-Runtime.exe", "SDL2.dll", "dxcompiler.dll", "dxil.dll"}) {
                const auto path = std::string(private_runtime ? "libexec/dkr-r/" : "") + component;
                if (!paths.contains(lower(path)))
                    fail("Windows update is missing a runtime component for its installation "
                         "layout.");
            }
        }
        m.artifacts.push_back(std::move(a));
    }
    return m;
}
const Artifact* Manifest::find(std::string_view p) const {
    for (auto& a : artifacts)
        if (a.platform == p)
            return &a;
    return nullptr;
}
std::string read_text(const std::filesystem::path& p, std::size_t limit) {
    regular(p);
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    if (!file)
        fail("Could not read update metadata.");
    auto n = file.tellg();
    if (n < 0 || std::uint64_t(n) > limit)
        fail("Update metadata exceeds its size limit.");
    std::string s(std::size_t(n), '\0');
    file.seekg(0);
    if (n && !file.read(s.data(), n))
        fail("Update metadata changed during reading.");
    return s;
}
void require_private_tree(const std::filesystem::path& p) {
    std::error_code ec;
    auto s = std::filesystem::symlink_status(p, ec);
    if (ec || !std::filesystem::is_directory(s) || std::filesystem::is_symlink(s))
        fail("Update directory is unavailable or is a link: " + p.string());
#ifdef _WIN32
    auto a = GetFileAttributesW(p.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT))
        fail("Update directory is a reparse point.");
#endif
}
void replace_file(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        fail("Could not replace update file (Windows error " + std::to_string(GetLastError()) + ").");
#else
    std::filesystem::rename(from, to);
    sync_directory(to.parent_path());
#endif
}
void durable_write(const std::filesystem::path& path, std::string_view bytes) {
    require_private_tree(path.parent_path());
    auto temp = path;
    temp += ".new";
    if (path_exists(temp)) {
        regular(temp);
        std::filesystem::remove(temp);
    } // Only this writer's stale ordinary temporary file.
#ifdef _WIN32
    HANDLE fd = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD n = 0;
    bool okay = fd != INVALID_HANDLE_VALUE && bytes.size() <= MAXDWORD &&
                WriteFile(fd, bytes.data(), DWORD(bytes.size()), &n, nullptr) && n == bytes.size() &&
                FlushFileBuffers(fd);
    if (fd != INVALID_HANDLE_VALUE)
        CloseHandle(fd);
#else
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    std::size_t offset = 0;
    bool okay = fd >= 0;
    while (okay && offset < bytes.size()) {
        auto n = write(fd, bytes.data() + offset, bytes.size() - offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            okay = false;
            break;
        }
        offset += std::size_t(n);
    }
    okay = okay && fsync(fd) == 0;
    if (fd >= 0)
        close(fd);
#endif
    if (!okay)
        fail("Could not write durable update metadata.");
    replace_file(temp, path);
}
void extract_verified(const std::filesystem::path& package, const Artifact& artifact,
                      const std::filesystem::path& fresh) {
    if (std::filesystem::file_size(package) != artifact.bytes ||
        hash_file(package, kMaxPackageBytes) != artifact.sha256)
        fail("Update package checksum or size does not match.");
    if (path_exists(fresh))
        fail("Update extraction requires a fresh directory.");
    std::filesystem::create_directory(fresh);
    require_private_tree(fresh);
    mz_zip_archive zip{};
    // Keep filesystem paths native. miniz's char* fopen entrypoints cannot
    // open Unicode Windows profile/install paths reliably.
    struct ArchiveInput {
        std::ifstream stream;
        std::uint64_t size;
    };
    ArchiveInput input{std::ifstream(package, std::ios::binary), artifact.bytes};
    zip.m_pIO_opaque = &input;
    zip.m_pRead = [](void* opaque, mz_uint64 offset, void* buffer, std::size_t count) -> std::size_t {
        auto& source = *static_cast<ArchiveInput*>(opaque);
        if (offset > source.size || count > source.size - offset)
            return 0;
        source.stream.clear();
        source.stream.seekg(std::streamoff(offset));
        source.stream.read(static_cast<char*>(buffer), std::streamsize(count));
        return std::size_t(source.stream.gcount());
    };
    if (!input.stream || !mz_zip_reader_init(&zip, artifact.bytes, 0))
        fail("Could not open update archive.");
    struct End {
        mz_zip_archive& z;
        ~End() {
            mz_zip_reader_end(&z);
        }
    } end{zip};
    auto count = mz_zip_reader_get_num_files(&zip);
    if (count > 8192)
        fail("Update archive has too many entries.");
    std::map<std::string, const File*> wanted;
    for (auto& f : artifact.files)
        wanted.emplace(f.path, &f);
    std::set<std::string> found, all;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            fail("Invalid update archive entry.");
        std::string full = stat.m_filename;
        bool directory = mz_zip_reader_is_file_a_directory(&zip, i);
        if (directory && full.ends_with('/'))
            full.pop_back();
        if (!safe_relative(full) || !all.insert(lower(full)).second || stat.m_is_encrypted)
            fail("Unsafe or duplicate update archive entry.");
        if (full == artifact.root && directory)
            continue;
        auto prefix = artifact.root + "/";
        if (!full.starts_with(prefix))
            fail("Update archive contains an unexpected root.");
        auto name = full.substr(prefix.size());
        if (directory)
            continue;
        auto entry = wanted.find(name);
        if (entry == wanted.end() || !found.insert(name).second)
            fail("Update archive contains an unlisted file.");
        auto& f = *entry->second;
        if (stat.m_uncomp_size != f.bytes || stat.m_uncomp_size > kMaxPackageBytes ||
            (((stat.m_external_attr >> 16) & 0170000) == 0120000) != !f.link.empty())
            fail("Update archive entry does not match its manifest.");
        auto destination = fresh / std::filesystem::u8path(name);
        parent_safe(fresh, std::filesystem::u8path(name));
        if (!f.link.empty()) {
            if (f.bytes > 1024 || f.bytes != f.link.size())
                fail("Update link is too large or has an inconsistent length.");
            std::vector<char> data(std::size_t(f.bytes));
            if (f.bytes > 1024 || !mz_zip_reader_extract_to_mem(&zip, i, data.data(), data.size(), 0) ||
                std::string(data.begin(), data.end()) != f.link)
                fail("Update link does not match manifest.");
            continue;
        }
        struct ArchiveOutput {
            std::ofstream stream;
            std::uint64_t size;
        };
        ArchiveOutput output{std::ofstream(destination, std::ios::binary | std::ios::trunc), f.bytes};
        auto write = [](void* opaque, mz_uint64 offset, const void* buffer, std::size_t count) -> std::size_t {
            auto& target = *static_cast<ArchiveOutput*>(opaque);
            if (offset > target.size || count > target.size - offset)
                return 0;
            target.stream.seekp(std::streamoff(offset));
            target.stream.write(static_cast<const char*>(buffer), std::streamsize(count));
            return target.stream ? count : 0;
        };
        const bool extracted = output.stream && mz_zip_reader_extract_to_callback(&zip, i, write, &output, 0);
        output.stream.close();
        if (!extracted || !output.stream || hash_file(destination) != f.sha256)
            fail("Extracted update file did not verify.");
        sync_file(destination); // Durable before the installed application can be replaced.
#ifndef _WIN32
        std::filesystem::permissions(destination, std::filesystem::perms(f.mode),
                                     std::filesystem::perm_options::replace);
#endif
    }
    if (found.size() != artifact.files.size())
        fail("Update archive is missing required files.");
    for (auto& f : artifact.files)
        if (!f.link.empty()) {
            auto path = fresh / std::filesystem::u8path(f.path);
            auto resolved = (path.parent_path() / std::filesystem::u8path(f.link)).lexically_normal();
            auto relative = resolved.lexically_relative(fresh);
            if (relative.empty() || relative.native().starts_with(std::filesystem::path("..").native()) ||
                hash(std::span(reinterpret_cast<const std::uint8_t*>(f.link.data()), f.link.size())) != f.sha256)
                fail("Update link leaves its bundle.");
            std::filesystem::create_symlink(std::filesystem::u8path(f.link), path);
        }
    for (auto& f : artifact.files)
        if (!f.link.empty()) {
            auto resolved = std::filesystem::canonical(fresh / std::filesystem::u8path(f.path));
            auto relative = resolved.lexically_relative(std::filesystem::canonical(fresh));
            if (relative.empty() || relative.native().starts_with(std::filesystem::path("..").native()))
                fail("Update link resolves outside its bundle.");
        }
}
std::vector<std::string> installed_files(const std::filesystem::path& root) {
    auto j = parse(read_text(root / "assets/updates/install.json"));
    if (j.at("schema") != kSchema || j.at("files").size() > 4096)
        fail("This installation has no supported ownership receipt. Update manually.");
    Version::parse(text(j, "version", 128));
    std::vector<std::string> files;
    std::set<std::string> unique;
    for (auto& e : j.at("files")) {
        auto p = e.get<std::string>();
        if (!safe_relative(p) || protected_path(p) || !unique.insert(lower(p)).second)
            fail("Unsafe installed ownership receipt.");
        files.push_back(p);
    }
    return files;
}
bool pending_files(const std::filesystem::path& root) {
    return path_exists(transaction(root));
}
void install_files(const std::filesystem::path& root, const std::filesystem::path& stage, const Artifact& a) {
    require_private_tree(root);
    require_private_tree(stage);
    if (pending_files(root))
        fail("An earlier update requires recovery first.");
    auto old = installed_files(root);
    std::set<std::string> old_set(old.begin(), old.end());
    std::map<std::string, const File*> files;
    for (auto& f : a.files) {
        if (!f.link.empty())
            fail("File-by-file updates do not accept filesystem links.");
        files.emplace(f.path, &f);
        if (hash_file(stage / std::filesystem::u8path(f.path)) != f.sha256)
            fail("Staged update changed before installation.");
    }
    for (auto& f : a.files)
        if (path_exists(root / std::filesystem::u8path(f.path)) && !old_set.contains(f.path))
            fail("Update would overwrite an unowned user file: " + f.path);
    std::set<std::string> paths(old.begin(), old.end());
    for (auto& f : a.files)
        paths.insert(f.path);
    Json j = {{"schema", kSchema}, {"state", "preparing"}, {"files", Json::array()}};
    auto dir = transaction(root);
    // Write a recoverable record before touching the installed application.
    for (auto& p : paths) {
        auto target = root / std::filesystem::u8path(p);
        parent_safe(root, std::filesystem::u8path(p));
        bool have = path_exists(target);
        Json e = {{"path", p}, {"old", have}, {"new", files.contains(p)}};
        if (have)
            e["old_hash"] = hash_file(target);
        if (files.contains(p))
            e["new_hash"] = files.at(p)->sha256;
        j["files"].push_back(e);
    }
    auto preparing = root / ".dkr-update-preparing";
    if (path_exists(preparing)) {
        require_private_tree(preparing);
        fail("An earlier update preparation directory needs inspection; installed files were left "
             "untouched.");
    }
    std::filesystem::create_directory(preparing);
    durable_write(preparing / "journal.json", j.dump());
    std::filesystem::rename(preparing, dir);
    sync_directory(root);
    std::filesystem::create_directory(dir / "backup");
    for (auto& e : j["files"])
        if (e["old"].get<bool>()) {
            auto p = std::filesystem::u8path(e["path"].get<std::string>());
            parent_safe(dir / "backup", p);
            copy_durable(root / p, dir / "backup" / p);
            if (hash_file(dir / "backup" / p) != e["old_hash"].get<std::string>())
                fail("Old application changed while backing up.");
        }
    j["state"] = "prepared";
    durable_write(dir / "journal.json", j.dump());
    try {
        for (auto& e : j["files"]) {
            auto p = std::filesystem::u8path(e["path"].get<std::string>());
            if (e["new"].get<bool>())
                replace_file(stage / p, root / p);
            else if (e["old"].get<bool>()) {
                std::filesystem::remove(root / p);
                sync_directory((root / p).parent_path());
            }
        }
        j["state"] = "installed";
        durable_write(dir / "journal.json", j.dump());
    } catch (...) {
        recover_files(root);
        throw;
    }
}
void recover_files(const std::filesystem::path& root) {
    if (!pending_files(root))
        return;
    auto j = journal(root);
    auto dir = transaction(root);
    if (j["state"] == "committed")
        return;
    if (j["state"] == "preparing") { // No installed files were modified yet.
        j["state"] = "recovered";
        durable_write(dir / "journal.json", j.dump());
        return;
    }
    for (auto& e : j["files"]) {
        auto p = std::filesystem::u8path(e["path"].get<std::string>());
        auto target = root / p;
        parent_safe(root, p);
        if (path_exists(target)) {
            auto h = hash_file(target);
            if ((!e["new"].get<bool>() || h != e.value("new_hash", std::string{})) &&
                (!e["old"].get<bool>() || h != e.value("old_hash", std::string{})))
                fail("Recovery found a changed user file and left it untouched: " + p.string());
        }
        if (e["old"].get<bool>()) {
            auto backup = dir / "backup" / p;
            if (hash_file(backup) != e["old_hash"].get<std::string>())
                fail("Recovery backup did not verify.");
            auto temp = dir / "restore.tmp";
            if (path_exists(temp))
                regular(temp);
            if (path_exists(temp))
                std::filesystem::remove(temp);
            copy_durable(backup, temp);
            replace_file(temp, target);
        } else if (path_exists(target))
            std::filesystem::remove(target);
    }
    j["state"] = "recovered";
    durable_write(dir / "journal.json", j.dump());
}
void commit_files(const std::filesystem::path& root) {
    auto j = journal(root);
    if (j["state"] != "installed")
        fail("Only a complete installation may be committed.");
    j["state"] = "committed";
    durable_write(transaction(root) / "journal.json", j.dump());
}
std::string transaction_state(const std::filesystem::path& root) {
    return journal(root).at("state").get<std::string>();
}
void archive_transaction(const std::filesystem::path& root) {
    auto state = transaction_state(root);
    if (state != "committed" && state != "recovered")
        fail("Only a completed or recovered update may be archived.");
    auto directory = root / ".dkr-update-backups";
    if (!path_exists(directory))
        std::filesystem::create_directory(directory);
    require_private_tree(directory);
    for (unsigned i = 0; i < 10000; ++i) {
        auto destination = directory / std::to_string(i);
        if (!path_exists(destination)) {
            std::filesystem::rename(transaction(root), destination);
            sync_directory(root);
            return;
        }
    }
    fail("Update backup retention needs manual cleanup before another update.");
}
void authorize_updated_launch(const std::filesystem::path& root, std::string_view token,
                              const std::filesystem::path& data, std::string_view version) {
    unhex(token, 16);
    auto j = journal(root);
    if (j["state"] != "installed")
        fail("Update is not ready for a startup check.");
    j["health_token"] = token;
    auto path = data.u8string();
    j["health_data"] = std::string(reinterpret_cast<const char*>(path.data()), path.size());
    Version::parse(version);
    j["health_version"] = version;
    durable_write(transaction(root) / "journal.json", j.dump());
}
bool updated_launch_authorized(const std::filesystem::path& root, std::string_view token) {
    try {
        unhex(token, 16);
        auto j = journal(root);
        return j["state"] == "installed" && j.value("health_token", std::string{}) == token;
    } catch (...) {
        return false;
    }
}
bool finalize_healthy_update(const std::filesystem::path& root) {
    auto j = journal(root);
    if (j["state"] != "installed" || !j.contains("health_data"))
        return false;
    try {
        auto token = j.at("health_token").get<std::string>();
        unhex(token, 16);
        auto data = std::filesystem::u8path(j.at("health_data").get<std::string>());
        if (!data.is_absolute())
            return false;
        auto proof = parse(read_text(data / "updates" / ("health-" + token + ".json"), 4096));
        if (!proof.value("ready", false) || proof.value("id", std::string{}) != token ||
            proof.value("version", std::string{}) != j.at("health_version").get<std::string>())
            return false;
        commit_files(root);
        archive_transaction(root);
        return true;
    } catch (...) {
        return false;
    }
}
void copy_file_durable(const std::filesystem::path& from, const std::filesystem::path& to) {
    copy_durable(from, to);
}
} // namespace dkr::updates
