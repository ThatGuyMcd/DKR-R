#include "save_storage.hpp"
#include "dkr_save_codec.hpp"
#include <cassert>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace storage = dkr::runtime::saves::storage;
namespace codec = dkr::runtime::saves::codec;
bool valid(const std::filesystem::path& path, storage::Bytes& bytes) {
    return storage::read_exact(path, 512, bytes) && codec::validate(bytes);
}
int main() {
    // Isolated deterministic fixture only. Never opens the user's profile.
    const auto root =
        std::filesystem::temp_directory_path() /
        ("dkr-save-policy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto offline = root / "saves" / "dkr.us.v77.bin";
    const auto online = root / "saves" / "online" / "sessions" / "0000000000000001" / "dkr.us.v77.bin";
    const auto original = codec::blank_bytes();
    std::string error;
    assert(storage::write_atomic(offline, original, valid, error));
#if !defined(_WIN32)
    // The temp root may sit behind OS links (macOS /var -> /private/var),
    // which are allowed; a link the user could have planted is not. As root
    // the planted link would itself be root-owned, so only check unprivileged.
    if (geteuid() != 0) {
        std::filesystem::create_directory_symlink(root / "saves", root / "planted");
        assert(!storage::write_atomic(root / "planted" / "dkr.us.v77.bin", original, valid, error));
        assert(error == "Save storage cannot use symbolic links.");
        std::filesystem::remove(root / "planted");
    }
#endif
    storage::protect_offline(root, true);
    // Compare the canonical root even when protection was installed using an
    // alias. This deliberately tests identity independently of link ownership:
    // a root-owned OS alias is legal for writes but not for offline save access.
    const auto root_alias = root.parent_path() / (root.filename().string() + "-alias");
    std::error_code alias_error;
    std::filesystem::create_directory_symlink(root, root_alias, alias_error);
    if (!alias_error) {
        storage::protect_offline(root_alias, true);
        assert(!storage::writable_path(offline, error));
        assert(error == "Offline saves are protected while an online session is active.");
        storage::protect_offline(root, true);
        assert(!storage::writable_path(root_alias / "saves" / "dkr.us.v77.bin", error));
        assert(error == "Offline saves are protected while an online session is active.");
        std::filesystem::remove(root_alias);
    }
    assert(!storage::write_atomic(offline, original, valid, error));
    assert(storage::write_atomic(online, original, valid, error));
    storage::Bytes retained;
    assert(valid(offline, retained) && retained == original);
    storage::protect_offline(root, false);
    auto changed_image = codec::blank_image();
    changed_image.slots[0].name = "NET";
    const auto changed = codec::encode(changed_image);
    assert(storage::write_atomic(online, changed, valid, error));
    assert(valid(online.string() + ".bak", retained) && retained == original);
    auto incomplete = changed;
    incomplete[12] ^= 1;
    assert(!storage::write_atomic(online, incomplete, valid, error));
    assert(valid(online, retained) && retained == changed);
    std::error_code ec;
    const auto alias = root / "saves" / "online" / "alias.bin";
    std::filesystem::create_hard_link(offline, alias, ec);
    if (!ec)
        assert(!storage::write_atomic(alias, changed, valid, error));
    std::filesystem::remove_all(root);
}
