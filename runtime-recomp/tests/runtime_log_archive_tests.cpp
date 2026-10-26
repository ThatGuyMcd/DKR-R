#include "runtime_log_archive.hpp"
#include "mods/online_mod_cache_policy.hpp"
#include <iostream>
#include <stdexcept>
namespace fs = std::filesystem;
void check(bool value) {
    if (!value)
        throw std::runtime_error("Log/cache policy regression");
}
int main() {
    const auto root = fs::temp_directory_path() /
                      ("dkr-log-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        for (unsigned i = 0; i < 25; ++i) {
            check(dkr::runtime::support::history::rotate(root));
            std::ofstream(root / "runtime.log") << i;
        }
        auto retained = dkr::runtime::support::history::logs(root / "history");
        check(retained.size() == 20 && fs::exists(root / "runtime-previous.log"));
        std::ofstream(root / "history" / "my-important-notes.log") << "preserve";
        dkr::runtime::support::history::prune(root / "history");
        check(fs::exists(root / "history" / "my-important-notes.log"));
        std::ofstream large(root / "large.log", std::ios::binary);
        large << std::string(3 * 1024 * 1024, 'x') << "last diagnostic";
        large.close();
        dkr::runtime::support::history::snapshot(root / "large.log", root / "snapshot.log");
        check(fs::file_size(root / "large.log") > 3 * 1024 * 1024 &&
              fs::file_size(root / "snapshot.log") < 2 * 1024 * 1024 + 100);
        constexpr std::uint64_t gib = 1024ULL * 1024 * 1024;
        using dkr::mods::online::resource_cache_budget;
        check(resource_cache_budget(32 * gib, 16 * gib, false) == gib / 2);
        check(resource_cache_budget(4 * gib, gib, false) == gib / 4);
        check(resource_cache_budget(32 * gib, 16 * gib, true) == gib / 4);
        check(resource_cache_budget(32 * gib, gib / 4, false) == gib / 16);
        check(resource_cache_budget(0, gib, false) == 0);
    } catch (...) {
        fs::remove_all(root);
        throw;
    }
    fs::remove_all(root);
    std::cout << "Log retention and memory-aware reuse policy passed\n";
}
