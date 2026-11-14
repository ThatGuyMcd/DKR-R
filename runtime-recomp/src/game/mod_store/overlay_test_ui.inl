// Armed only by the existing CLI-owned check after empty-profile validation.
// A normal launch never reads the environment switch.
bool g_store_overlay_test = false;
std::chrono::steady_clock::time_point g_store_overlay_test_after;
void StoreOverlayTestFrame() {
    if (!g_store_overlay_test)
        return;
    static const auto pixels = [] {
        std::array<unsigned char, 32 * 32 * 4> value{};
        for (unsigned i = 0; i < 32 * 32; ++i) {
            value[i * 4] = static_cast<unsigned char>(i % 256);
            value[i * 4 + 1] = 180;
            value[i * 4 + 2] = 70;
            value[i * 4 + 3] = 255;
        }
        return value;
    }();
    dkr::store::overlay::image("https://positron-portal.com/dkr-r/assets/pattern.png", 32, 32, pixels);
    // Exercise eviction against real queued draws, not a guessed frame count.
    static unsigned rotation = 0;
    auto view = dkr::store::overlay::image("isolated-preview-" + std::to_string(rotation++ % 48), 32, 32, pixels);
    if (view.id)
        ImGui::GetForegroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(view.id), {8, 8}, {40, 40});
}
std::shared_ptr<const dkr::store::Snapshot> StoreOverlayFixture(const dkr::store::Snapshot& original) {
    auto value = std::make_shared<dkr::store::Snapshot>(original);
    value->user = nullptr;
    value->error.clear();
    value->status.clear();
    value->writing = value->loading = false;
    value->items = std::make_shared<const StoreJson>(StoreJson::array({{{"id", "overlay_mod"},
                                                                        {"title", "Mod Store Overlay Check"},
                                                                        {"author", "Isolated fixture"},
                                                                        {"category", "tracks"},
                                                                        {"version", "1"},
                                                                        {"summary", "Shared counts and comments"},
                                                                        {"image", "pattern"},
                                                                        {"downloadCount", 17U},
                                                                        {"commentCount", 3U}}}));
    value->reviews[dkr::store::community::SnapshotKey] = {
        {"itemId", "overlay_mod"},
        {"comments", StoreJson::array({{{"id", "comment_fixture"},
                                        {"itemId", "overlay_mod"},
                                        {"text", "The same discussion appears in the launcher and overlay."},
                                        {"displayName", "Test player"},
                                        {"createdAt", "2026-10-30"},
                                        {"revision", 1U},
                                        {"isOwn", false}}})},
        {"total", 3U},
        {"nextCursor", ""},
        {"cursor", ""},
        {"loading", false},
        {"error", ""}};
    g_mod_store.opened = true;
    g_mod_store.detail = true;
    g_mod_store.selected = "overlay_mod";
    g_mod_store.detail_tab = 4;
    g_store_discussion.account.clear();
    g_store_discussion.item = "overlay_mod";
    static bool logged = false;
    if (!logged) {
        logged = true;
        std::fprintf(stderr,
                     "[mod-store][isolated-check] overlay tab, downloadCount=17 commentCount=3, discussion visible\n");
    }
    return value;
}
