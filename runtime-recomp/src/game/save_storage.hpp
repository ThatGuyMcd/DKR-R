#pragma once

// Authored save-storage policy. Never use this for renderer/configuration files.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cwctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace dkr::runtime::saves::storage {
using Bytes = std::vector<std::uint8_t>;
using Validator = bool (*)(const std::filesystem::path&, Bytes&);
inline std::atomic<std::uint64_t> serial{0};
inline std::mutex protection_mutex;
inline std::filesystem::path protected_root;
inline bool online_protection = false;

inline std::filesystem::path absolute(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal();
}
inline std::filesystem::path key(const std::filesystem::path& path) {
    auto result = storage::absolute(path);
#if defined(_WIN32)
    auto text = result.native();
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return std::towlower(c); });
    result = text;
#endif
    return result;
}
inline void protect_offline(const std::filesystem::path& root, bool enabled) {
    std::scoped_lock lock(protection_mutex);
    protected_root = storage::absolute(root); online_protection = enabled;
}
inline bool writable_path(const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    const auto target = storage::absolute(path);
    {
        std::scoped_lock lock(protection_mutex);
        if (online_protection) {
            const auto relative = key(target).lexically_relative(key(protected_root)).generic_string();
            if (relative == "saves/dkr.us.v77.bin" ||
                relative.starts_with("saves/dkr.us.v77.bin.") ||
                relative.starts_with("controller-pak-") || relative.starts_with("save-backups/") ||
                relative.starts_with("save-history/")) {
                error = "Offline saves are protected while an online session is active.";
                return false;
            }
        }
    }
    for (auto at = target; !at.empty();) {
        const auto state = std::filesystem::symlink_status(at, ec);
        if (!ec && std::filesystem::is_symlink(state)) {
            error = "Save storage cannot use symbolic links."; return false;
        }
#if defined(_WIN32)
        const auto attributes = GetFileAttributesW(at.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            error = "Save storage cannot use junctions or reparse points."; return false;
        }
#endif
        if (ec && ec != std::errc::no_such_file_or_directory) {
            error = "Cannot inspect save storage: " + ec.message(); return false;
        }
        ec.clear();
        const auto parent = at.parent_path(); if (parent == at) break; at = parent;
    }
    if (std::filesystem::exists(target, ec)) {
        if (!std::filesystem::is_regular_file(target, ec) ||
            std::filesystem::hard_link_count(target, ec) != 1 || ec) {
            error = "A save must be an ordinary file with no hard-link aliases."; return false;
        }
    }
    return !ec;
}

struct Lease {
    std::mutex transaction;
#if defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~Lease() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
    int handle = -1;
    ~Lease() { if (handle >= 0) { flock(handle, LOCK_UN); close(handle); } }
#endif
};
inline std::mutex leases_mutex;
inline std::map<std::filesystem::path, std::weak_ptr<Lease>> leases;
inline std::shared_ptr<Lease> acquire(const std::filesystem::path& path, std::string& error) {
    if (!writable_path(path, error)) return {};
    const auto target = storage::absolute(path);
    std::scoped_lock lock(leases_mutex);
    if (leases.size() > 256) {
        for (auto it = leases.begin(); it != leases.end();) {
            if (it->second.expired()) it = leases.erase(it); else ++it;
        }
    }
    const auto identity = key(target);
    if (auto prior = leases[identity].lock()) return prior;
    std::error_code ec; std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) { error = "Cannot create save storage: " + ec.message(); return {}; }
    const auto lock_path = std::filesystem::path(target.native() + std::filesystem::path(".lock").native());
    if (!writable_path(lock_path, error)) return {};
    auto lease = std::make_shared<Lease>();
#if defined(_WIN32)
    lease->handle = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lease->handle == INVALID_HANDLE_VALUE) {
#else
    lease->handle = open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lease->handle < 0 || flock(lease->handle, LOCK_EX | LOCK_NB) != 0) {
#endif
        error = "Save storage is locked or unavailable. Close another DKR-R instance using this save.";
        return {};
    }
    leases[identity] = lease; return lease;
}
inline std::string content_id(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (auto byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
    char value[32]; std::snprintf(value, sizeof(value), "%016llx", static_cast<unsigned long long>(hash));
    return value;
}
inline bool read_exact(const std::filesystem::path& path, std::size_t size, Bytes& bytes) {
    std::error_code ec; if (std::filesystem::file_size(path, ec) != size || ec) return false;
    std::ifstream file(path, std::ios::binary); bytes.resize(size);
    return bool(file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)));
}
inline bool durable_replace(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                            Validator validator, std::string& error) {
    if (!writable_path(path, error)) return false;
    const auto temporary = std::filesystem::path(path.native() + std::filesystem::path(
        ".pending-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(++serial)).native());
    bool good = false;
#if defined(_WIN32)
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        good = bytes.size() <= MAXDWORD && WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
            written == bytes.size() && FlushFileBuffers(file);
        if (!CloseHandle(file)) good = false;
    }
#else
    int file = open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file >= 0) {
        std::size_t written = 0;
        while (written < bytes.size()) {
            const auto n = write(file, bytes.data() + written, bytes.size() - written);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break; written += static_cast<std::size_t>(n);
        }
        good = written == bytes.size() && fsync(file) == 0;
        if (close(file) != 0) good = false;
    }
#endif
    Bytes check;
    good = good && (validator ? validator(temporary, check) : read_exact(temporary, bytes.size(), check)) &&
        check.size() == bytes.size() && std::equal(check.begin(), check.end(), bytes.begin());
    std::error_code ec;
    if (good) {
#if defined(_WIN32)
        good = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::filesystem::rename(temporary, path, ec); good = !ec;
        if (good) {
            int directory = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            const int synced = directory >= 0 ? fsync(directory) : -1;
            const int failure = errno;
            if (directory >= 0) close(directory);
            // Some SD-card filesystems do not implement directory fsync. Do
            // not reject those; do report real I/O/permission durability errors.
            if (synced != 0 && failure != EINVAL && failure != ENOTSUP) {
                error = "The validated save was committed, but directory durability could not be confirmed. Preserve its backup.";
                return false;
            }
        }
#endif
    }
    if (!good) {
        std::filesystem::remove(temporary, ec);
        error = "The save could not be fully validated and atomically committed. Existing progress was not replaced.";
    }
    return good;
}
inline bool write_atomic(const std::filesystem::path& path, const Bytes& bytes,
                         Validator validator, std::string& error) {
    auto lease = acquire(path, error); if (!lease) return false;
    std::scoped_lock lock(lease->transaction);
    Bytes previous;
    if (validator && validator(path, previous)) {
        if (previous == bytes) { error.clear(); return true; }
        const auto backup = std::filesystem::path(path.native() + std::filesystem::path(".bak").native());
        if (!durable_replace(backup, previous, validator, error)) return false;
        const auto history = path.parent_path() / "save-history" / path.filename();
        // Inspect the directory chain before either writing or pruning. An
        // existing history directory must never redirect pruning through a link.
        if (!writable_path(history / ".directory-check", error)) return false;
        std::error_code ec; std::filesystem::create_directories(history, ec);
        if (ec) { error = "Cannot preserve save history: " + ec.message(); return false; }
        auto prior = history / (content_id(previous) + ".bin");
        Bytes existing;
        if (std::filesystem::exists(prior, ec) && (!validator(prior, existing) || existing != previous))
            prior = history / (content_id(previous) + "-" + std::to_string(++serial) + ".bin");
        if (!std::filesystem::exists(prior, ec) && !durable_replace(prior, previous, validator, error)) return false;
        std::vector<std::filesystem::directory_entry> entries;
        for (std::filesystem::directory_iterator it(history, ec), end; !ec && it != end; it.increment(ec)) {
            std::string checked;
            if (it->is_regular_file(ec) && it->path().extension() == ".bin" &&
                writable_path(it->path(), checked)) entries.push_back(*it);
        }
        if (entries.size() > 8) {
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                std::error_code x, y; return a.last_write_time(x) < b.last_write_time(y);
            });
            for (std::size_t i = 0; i < entries.size() - 8; ++i) std::filesystem::remove(entries[i].path(), ec);
        }
    } else {
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) {
            const auto size = std::filesystem::file_size(path, ec); Bytes raw;
            if (ec || size > 1024 * 1024 || !read_exact(path, static_cast<std::size_t>(size), raw)) {
                error = "Cannot preserve the existing invalid save; it was left untouched."; return false;
            }
            auto preserved = std::filesystem::path(path.native() + std::filesystem::path(".invalid-" + content_id(raw)).native());
            Bytes retained;
            if (std::filesystem::exists(preserved, ec) &&
                (!read_exact(preserved, raw.size(), retained) || retained != raw))
                preserved = std::filesystem::path(preserved.native() + std::filesystem::path("-" + std::to_string(++serial)).native());
            if (!std::filesystem::exists(preserved, ec) && !durable_replace(preserved, raw, nullptr, error)) return false;
        }
    }
    return durable_replace(path, bytes, validator, error);
}
} // namespace dkr::runtime::saves::storage
