void DrawStorageSettings(float width) {
    namespace storage = dkr::storage;
    DetailHeading("Storage", width);
    bool enabled = storage::automatic();
    if (SettingsCheck("##automatic-storage-cleanup", "Automatically clear disposable files once per launch", &enabled,
                      width))
        storage::automatic(enabled);
    DetailText("Runs once after startup and continues at low priority while playing. Active installs and online files "
               "are protected.",
               width, kSetMuted);
    const auto state = storage::view();
    const auto bytes = [](std::uint64_t n) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(1) << double(n) / (1024.0 * 1024.0) << " MiB";
        return s.str();
    };
    for (const auto& row : state.rows)
        DetailText(row.name + ": " + bytes(row.bytes), width, kSetMuted, 0.F);
    DetailText(state.status, width, kSetMuted, 0.F);
    if (state.removed_files)
        DetailText(std::string(state.preview ? "Can clear " : "Cleared ") + bytes(state.removed_bytes) + " in " +
                       std::to_string(state.removed_files) + " files.",
                   width, kSetMuted, 0.F);
    ImGui::BeginDisabled(state.busy);
    if (DetailRaceButton("CHECK STORAGE", width))
        storage::request(storage::Action::Preview);
    if (state.preview) {
        if (DetailRaceButton("CLEAR DISPOSABLE FILES", width))
            storage::request(storage::Action::Clean);
    }
    if (DetailRaceButton("CLEAR CACHED ARTWORK AND UNUSED ONLINE FILES", width))
        storage::request(storage::Action::ClearCaches);
    static bool history_confirm = false, dumps_confirm = false;
    SettingsCheck("##storage-history-confirm", "Also remove old, unreferenced mod recovery history", &history_confirm,
                  width);
    if (history_confirm && DetailRaceButton("REMOVE OLD MOD HISTORY", width)) {
        storage::request(storage::Action::TrimHistory);
        history_confirm = false;
    }
    SettingsCheck("##storage-dumps-confirm", "Remove original crash dumps older than 30 days", &dumps_confirm, width);
    if (dumps_confirm && DetailRaceButton("REMOVE OLD CRASH DUMPS", width)) {
        storage::request(storage::Action::OldDumps);
        dumps_confirm = false;
    }
    ImGui::EndDisabled();
    DetailText(
        "Recordings, texture dumps, development files and support exports are kept. Manage those in their own folders.",
        width, kSetMuted);
    PaddockGap(16.F);
}
