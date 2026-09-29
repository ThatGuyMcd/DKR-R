#pragma once

#include <filesystem>
#include <string>

namespace dkr::runtime::android {

// Never infer an Android application's writable directory from HOME/getpwuid.
// Bionic's app passwd entry points at /data, not the app's private files folder.
inline bool prepare_renderer_directory(const char* private_files,
                                      std::filesystem::path& result,
                                      std::string& error) {
    result.clear();
    error.clear();
    if (!private_files || !*private_files ||
        !std::filesystem::path(private_files).is_absolute()) {
        error = "Android did not provide an absolute private storage path.";
        return false;
    }
    const auto directory = std::filesystem::path(private_files) / "config" / "rt64";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = "Cannot prepare private renderer storage: " + ec.message();
        return false;
    }
    result = directory;
    return true;
}

} // namespace dkr::runtime::android
