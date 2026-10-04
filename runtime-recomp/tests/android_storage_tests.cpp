#include "../src/android/android_storage.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    using dkr::runtime::android::prepare_renderer_directory;
    fs::path result;
    std::string error;
    if (prepare_renderer_directory(nullptr, result, error) || error.empty()) return 1;
    if (prepare_renderer_directory("", result, error) || error.empty()) return 2;
    if (prepare_renderer_directory("relative", result, error) || error.empty()) return 3;
    const auto root = fs::temp_directory_path() / ("dkr-android-storage-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(root)) return 4;
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    const auto private_files = root / "private files";
    if (!prepare_renderer_directory(private_files.string().c_str(), result, error) ||
        result != private_files / "config" / "rt64" || !fs::is_directory(result)) return 5;
    // Repeated starts must retain existing renderer files, not recreate storage.
    { std::ofstream marker(result / "marker"); marker << "retain"; }
    if (!prepare_renderer_directory(private_files.string().c_str(), result, error) ||
        !fs::exists(result / "marker")) return 6;
    const auto blocked = root / "blocked";
    { std::ofstream file(blocked); file << "not a directory"; }
    if (prepare_renderer_directory(blocked.string().c_str(), result, error) ||
        !result.empty() || error.empty()) return 7;
    std::cout << "Android renderer storage: private, repeatable, fail-closed\n";
}
