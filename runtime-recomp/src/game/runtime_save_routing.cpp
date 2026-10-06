#include "runtime_save_routing.hpp"

#include "netplay/netplay_types.hpp"
#include "save_manager.hpp"
#include "save_storage.hpp"

#include "ultramodern/ultramodern.hpp"

#include <mutex>
#include <string_view>
#include <vector>

namespace {

std::mutex g_runtime_save_mutex;
dkr::runtime::saves::RuntimeOnlineSaveStatus g_runtime_save_status;
struct SaveContext {
    std::filesystem::path path, paks;
    std::shared_ptr<dkr::runtime::saves::storage::Lease> lease;
    bool online = false, host = false, loaded = false, incomplete = false;
    std::uint64_t match = 0, hash = 0;
    std::string failure;
};
SaveContext g_context;

bool ValidRuntimeImage(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
    return dkr::runtime::saves::storage::read_exact(path, 512, bytes) &&
        dkr::runtime::saves::codec::validate(bytes);
}

std::uint64_t HashSave(const std::vector<std::uint8_t>& bytes) {
    return dkr::runtime::netplay::stable_hash(std::string_view(
        reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

} // namespace

void dkr::runtime::saves::reset_runtime_online_save_status() {
    std::scoped_lock lock(g_runtime_save_mutex);
    g_runtime_save_status = {};
}

bool dkr::runtime::saves::activate_online_save_for_runtime(
    bool host, std::uint64_t match_id, std::uint64_t expected_hash,
    std::string& error) {
    reset_runtime_online_save_status();
    {
        std::scoped_lock lock(g_runtime_save_mutex);
        if (!g_context.online || g_context.host != host || g_context.match != match_id ||
            g_context.hash != expected_hash || !g_context.loaded || !g_context.failure.empty()) {
            error = "The online save does not belong to the frozen launch context.";
            return false;
        }
    }
    if (match_id == 0U || expected_hash == 0U) {
        error = "The synchronized online save identity is incomplete.";
        return false;
    }

    std::vector<std::uint8_t> bytes;
    std::filesystem::path expected_path;
    if (!read_online_adventure(host, match_id, bytes, expected_path, error)) {
        return false;
    }
    const std::uint64_t readback_hash = HashSave(bytes);
    if (readback_hash != expected_hash) {
        error = "The online save changed after lobby verification.";
        return false;
    }

    const std::filesystem::path subfolder =
        online_adventure_subfolder(host, match_id);
    ultramodern::change_save_file(subfolder.generic_u8string(),
                                  u8"dkr.us.v77");
    const std::filesystem::path active_path =
        ultramodern::get_save_file_path().lexically_normal();
    if (active_path != expected_path.lexically_normal()) {
        error = "The runtime did not activate the isolated online save path.";
        return false;
    }

    std::vector<std::uint8_t> activated_bytes;
    std::filesystem::path activated_path;
    if (!read_online_adventure(host, match_id, activated_bytes,
                               activated_path, error) ||
        activated_path.lexically_normal() != active_path ||
        HashSave(activated_bytes) != expected_hash) {
        error = "The active online save failed its final read-back check.";
        return false;
    }

    {
        std::scoped_lock lock(g_runtime_save_mutex);
        g_runtime_save_status.active = true;
        g_runtime_save_status.verified = true;
        g_runtime_save_status.hash = expected_hash;
        g_runtime_save_status.path = active_path;
    }
    error.clear();
    return true;
}

dkr::runtime::saves::RuntimeOnlineSaveStatus
dkr::runtime::saves::runtime_online_save_status() {
    std::scoped_lock lock(g_runtime_save_mutex);
    return g_runtime_save_status;
}

bool dkr::runtime::saves::prepare_runtime_save_context(const std::filesystem::path& root,
    const std::filesystem::path& subfolder, const std::filesystem::path& paks,
    bool online, bool host, std::uint64_t match, std::uint64_t hash, std::string& error) {
    std::scoped_lock lock(g_runtime_save_mutex);
    if (g_context.lease) { error = "The previous save owner has not retired."; return false; }
    g_context = {};
    storage::protect_offline(root, online);
    if (online && (!match || !hash)) { error = "The online save identity is incomplete."; return false; }
    const auto folder = online ? online_adventure_subfolder(host, match) : subfolder;
    const auto path = storage::absolute(root / "saves" / folder / "dkr.us.v77.bin");
    if (online) {
        std::vector<std::uint8_t> bytes; std::filesystem::path verified;
        if (!read_online_adventure(host, match, bytes, verified, error) || HashSave(bytes) != hash ||
            storage::absolute(verified) != path) {
            if (error.empty()) error = "The online seed differs from the authenticated lobby save.";
            return false;
        }
    }
    auto lease = storage::acquire(path, error); if (!lease) return false;
    g_context.path = path; g_context.paks = online ? path.parent_path() / "paks" :
        (paks.empty() ? storage::absolute(root) : storage::absolute(paks));
    g_context.online = online; g_context.host = host; g_context.match = match; g_context.hash = hash;
    g_context.lease = std::move(lease);
    std::fprintf(stderr, "[save][scope] online=%d host=%d lease=held immutable-route=1\n", int(online), int(host));
    error.clear(); return true;
}
void dkr::runtime::saves::retire_runtime_save_context() {
    std::scoped_lock lock(g_runtime_save_mutex);
    g_context = {}; g_runtime_save_status = {};
    // Called only after both legacy and owned save producers have joined.
}
bool dkr::runtime::saves::runtime_save_context_ready() {
    std::scoped_lock lock(g_runtime_save_mutex);
    return g_context.lease && g_context.loaded && g_context.failure.empty();
}
bool dkr::runtime::saves::runtime_save_writes_allowed() {
    std::scoped_lock lock(g_runtime_save_mutex); return g_context.lease && g_context.loaded;
}
std::filesystem::path dkr::runtime::saves::runtime_save_path() {
    std::scoped_lock lock(g_runtime_save_mutex); return g_context.path;
}
std::filesystem::path dkr::runtime::saves::runtime_pak_directory() {
    std::scoped_lock lock(g_runtime_save_mutex); return g_context.paks;
}
std::string dkr::runtime::saves::runtime_save_failure() {
    std::scoped_lock lock(g_runtime_save_mutex);
    if (!g_context.failure.empty()) return g_context.failure;
    return g_context.incomplete ? "An incomplete EEPROM update was not committed. The previous valid save was retained." : std::string{};
}
dkr::runtime::saves::RuntimeSaveSelection dkr::runtime::saves::runtime_save_selection() {
    std::scoped_lock lock(g_runtime_save_mutex);
    return {g_context.online, g_context.host, g_context.match, g_context.hash};
}
bool dkr::runtime::saves::load_runtime_save(const std::filesystem::path& path, std::vector<char>& buffer) {
    std::scoped_lock lock(g_runtime_save_mutex);
    std::vector<std::uint8_t> bytes;
    if (!g_context.lease || storage::absolute(path) != g_context.path) {
        g_context.failure = "The runtime tried to load a save outside its launch scope."; return false;
    }
    if (!ValidRuntimeImage(path, bytes)) {
        std::error_code ec; const bool exists = std::filesystem::exists(path, ec);
        if (!g_context.online && ValidRuntimeImage(path.string() + ".bak", bytes)) {
            std::fprintf(stderr, "[save][recovery] valid offline backup loaded; original retained\n");
        } else if (!g_context.online && !exists && !ec &&
            !std::filesystem::exists(path.string() + ".bak", ec) && !ec) bytes = codec::blank_bytes();
        else { g_context.failure = "The active save is unreadable or invalid. It was not replaced. Use Save Manager recovery."; return false; }
    }
    if (g_context.online && HashSave(bytes) != g_context.hash) {
        g_context.failure = "The active online save changed after verification."; return false;
    }
    buffer.assign(bytes.begin(), bytes.end()); g_context.loaded = true; return true;
}
bool dkr::runtime::saves::commit_runtime_save(const std::filesystem::path& path, const std::vector<char>& buffer) {
    std::scoped_lock lock(g_runtime_save_mutex);
    if (!g_context.lease || !g_context.loaded || storage::absolute(path) != g_context.path) {
        g_context.failure = "A stale save write was refused outside its launch scope."; return false;
    }
    const std::vector<std::uint8_t> bytes(buffer.begin(), buffer.end());
    // Retail EEPROM writes are multi-part; never commit a transient invalid image.
    if (!codec::validate(bytes)) {
        if (!g_context.incomplete) std::fprintf(stderr, "[save][commit] deferred incomplete EEPROM transaction\n");
        g_context.incomplete = true; return false;
    }
    std::string error;
    if (!storage::write_atomic(path, bytes, ValidRuntimeImage, error)) {
        g_context.failure = error; std::fprintf(stderr, "[save][commit] %s\n", error.c_str()); return false;
    }
    g_context.incomplete = false; g_context.failure.clear();
    return true;
}
