#pragma once
#ifndef _WIN32
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dkr::profile::local_device {
struct Fd {
    int value = -1;
    explicit Fd(int fd) : value(fd) {}
    ~Fd() {
        if (value >= 0)
            close(value);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
inline bool token_valid(const std::string& value) {
    return value.size() == 43 &&
           value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == value.npos;
}
inline int directory(const std::filesystem::path& root, bool create) {
    std::error_code error;
    if (create)
        std::filesystem::create_directories(root, error);
    if (error)
        return -1;
    Fd parent(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (parent.value < 0)
        return -1;
    if (create && mkdirat(parent.value, "device-session", 0700) != 0 && errno != EEXIST)
        return -1;
    int fd = openat(parent.value, "device-session", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat info {};
    if (fd >= 0 && (fstat(fd, &info) || info.st_uid != geteuid() || (info.st_mode & 0077))) {
        close(fd);
        return -1;
    }
    return fd;
}
inline bool store(const std::filesystem::path& root, const std::string& value) {
    if (!token_valid(value))
        return false;
    Fd dir(directory(root, true));
    if (dir.value < 0)
        return false;
    static std::atomic<unsigned> sequence{0};
    const auto temporary = "new-" + std::to_string(getpid()) + "-" + std::to_string(++sequence);
    Fd fd(openat(dir.value, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (fd.value < 0)
        return false;
    bool ok = true;
    std::size_t written = 0;
    while (written < value.size()) {
        const auto count = write(fd.value, value.data() + written, value.size() - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            ok = false;
            break;
        }
        written += static_cast<std::size_t>(count);
    }
    ok = ok && fsync(fd.value) == 0 && renameat(dir.value, temporary.c_str(), dir.value, "device.token") == 0;
    if (ok)
        ok = fsync(dir.value) == 0;
    if (!ok)
        unlinkat(dir.value, temporary.c_str(), 0);
    return ok;
}
inline std::string load(const std::filesystem::path& root) {
    Fd dir(directory(root, false));
    if (dir.value < 0)
        return {};
    Fd fd(openat(dir.value, "device.token", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
    struct stat info {};
    if (fd.value < 0 || fstat(fd.value, &info) || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & 0077) || info.st_nlink != 1 || info.st_size != 43)
        return {};
    std::string result(43, '\0');
    std::size_t received = 0;
    while (received < result.size()) {
        const auto count = read(fd.value, result.data() + received, result.size() - received);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return {};
        received += static_cast<std::size_t>(count);
    }
    return token_valid(result) ? result : std::string{};
}
inline void clear(const std::filesystem::path& root) {
    Fd dir(directory(root, false));
    if (dir.value >= 0) {
        unlinkat(dir.value, "device.token", 0);
        fsync(dir.value);
    }
}
} // namespace dkr::profile::local_device

// Used on the existing account worker. UI reads never touch disk or a keyring.
#ifdef DKR_PROFILE_WITH_KEYRING
namespace dkr::profile::devices {
inline bool store(const std::string& root, const std::string& value) {
    if (dkr::online::keyring::store(root, value)) {
        local_device::clear(root);
        return true;
    }
    return local_device::store(root, value);
}
inline std::string load(const std::string& root) {
    auto value = dkr::online::keyring::load(root);
    return local_device::token_valid(value) ? value : local_device::load(root);
}
inline void clear(const std::string& root) {
    local_device::clear(root);
    dkr::online::keyring::clear(root);
}
} // namespace dkr::profile::devices
#endif
#endif
