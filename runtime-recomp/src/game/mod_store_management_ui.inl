// Shared native-library inventory. Called only when a native catalogue changes
// or a bounded idle refresh is due; no file scan runs in the frame loop.
std::string StoreLower(std::string value) {
    for (auto& c : value)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
std::vector<std::filesystem::path> StoreOriginalRoms() {
    std::vector<std::filesystem::path> result;
    for (const auto& entry : LoadRomCatalog()) {
        result.push_back(entry.path);
        if (result.size() == 8)
            break;
    }
    return result;
}
StoreJson StoreNativeInventory() {
    StoreJson result = StoreJson::array();
    const auto relative = [](const std::filesystem::path& path) {
        // The track reader returns \\?\ paths on Windows. Compare both sides
        // in that native representation before deriving the portable receipt.
        const auto value = dkr::store::store_io_path(path)
                               .lexically_relative(dkr::store::store_io_path(g_config_directory))
                               .generic_u8string();
        return std::string(value.begin(), value.end());
    };
    const auto legacy = g_legacy_imports.snapshot();
    for (const bool characters : {false, true}) {
        const auto catalog = characters ? legacy.characters : legacy.tracks;
        if (!catalog)
            continue;
        for (const auto& item : catalog->tracks)
            result.push_back({{"kind", characters ? "legacy-character" : "legacy-track"},
                              {"key", std::string(characters ? "legacy-character:" : "legacy-track:") + item.id + ":" +
                                          item.revision},
                              {"id", item.id},
                              {"name", item.name},
                              {"revision", item.revision},
                              {"digest", item.patch},
                              {"group", item.group},
                              {"slot", characters ? item.base_character : item.carrier},
                              {"enabled", item.enabled},
                              {"hidden", item.hidden},
                              {"path", std::string("mods/legacy/") +
                                           (characters ? "prepared-characters/" : "prepared/") + item.group},
                              {"bytes", item.managed_bytes}});
    }
    for (const auto& car : StoreVehicleInventory()) result.push_back(car);
    for (const auto& item : g_code_library.view()->items)
        result.push_back({{"kind", "code"},
                          {"key", "code:" + item.digest},
                          {"id", item.id},
                          {"name", item.name},
                          {"digest", item.digest},
                          {"path", "mods/custom-magic-codes/packages/" + item.digest + ".dkrcode"},
                          {"version", item.version},
                          {"enabled", item.enabled},
                          {"selected", item.selected},
                          {"approved", item.approved},
                          {"settings", item.settings},
                          {"data", item.data}});
    for (const auto& pack : dkr::runtime::texture_packs::snapshot(true))
        if (pack.compatible)
            result.push_back({{"kind", "texture"},
                              {"key", "texture:" + pack.id},
                              {"id", pack.id},
                              {"name", pack.name},
                              {"path", relative(pack.path)},
                              {"enabled", pack.enabled},
                              {"hidden", pack.hidden},
                              {"priority", pack.priority},
                              {"owner", pack.owner_track_id},
                              {"bytes", pack.managed_size_bytes}});
    for (const auto& track : dkr::runtime::custom_tracks::tracks())
        if (dkr::runtime::custom_tracks::is_installed(track))
            result.push_back({{"kind", "track"},
                              {"key", "track:" + track.id},
                              {"id", track.id},
                              {"name", track.name},
                              {"path", relative(track.source)},
                              {"enabled", track.enabled}});
    return result;
}
void StoreManagerInstall(const dkr::store::Download& job, bool locked) {
    if (locked || StoreVehiclesBusy() || g_legacy_imports.snapshot().busy || g_code_library.view()->busy || TexturePackImportRunning() ||
        DialogJobRunning())
        return;
    if (dkr::store::installations().propose(job, StoreOriginalRoms())) {
        dkr::store::service().begin_install(job.key);
        g_mod_store.pending_key = job.key;
    }
}
void StoreQueueRelease(const StoreJson& item, const StoreJson& release, std::string intent = {},
                       std::string instance = {}) {
    const auto key = dkr::store::download_key(StoreText(item, "id", 160), StoreText(release, "id", 160), instance);
    g_mod_store.install_after_download.insert(key);
    dkr::store::service().download(item, release, std::move(intent), std::move(instance));
}
void StoreManagerPump() {
    auto& manager = dkr::store::installations();
    const auto view = manager.view();
    const bool safe = !dkr::runtime::netplay::session().active() && !g_online_mod_host_request.has_value() &&
                      !g_mod_launch.snapshot().modal && !g_legacy_imports.snapshot().busy &&
                      !g_code_library.view()->busy && !StoreVehiclesBusy() && !TexturePackImportRunning() && !DialogJobRunning();
    manager.safe_to_write(safe);
    static std::uint64_t completion = 0, last_code = UINT64_MAX, last_texture = UINT64_MAX, last_names = UINT64_MAX;
    static std::shared_ptr<const dkr::mods::TrackCatalogView> last_tracks, last_characters;
    static const void* last_items = nullptr;
    static std::shared_ptr<const void> last_vehicles;
    static bool refresh_pending = false;
    static auto last_refresh = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (view->completion != completion) {
        completion = view->completion;
        if (!view->completed_key.empty())
            dkr::store::service().installed(view->completed_key, view->succeeded,
                                            view->succeeded       ? view->result
                                            : view->error.empty() ? view->result
                                                                  : view->error);
        g_mod_store.pending_key.clear();
        refresh_pending = true;
        last_refresh = std::chrono::steady_clock::time_point{};
    }
    if (refresh_pending && safe && !view->busy) {
        g_legacy_imports.refresh();
        g_code_library.refresh();
        StoreRefreshVehicles();
        refresh_pending = false;
        return; // Wait for fresh native snapshots before reconciling receipts.
    }
    const auto store = dkr::store::service().view();
    const auto legacy = g_legacy_imports.snapshot();
    const auto vehicles = StoreVehiclesSnapshot();
    const auto code = g_code_library.view()->generation, texture = dkr::runtime::texture_packs::generation(),
               names = dkr::store::library_names_generation();
    if (safe && view->ready && !view->busy &&
        (vehicles != last_vehicles || legacy.tracks != last_tracks || legacy.characters != last_characters || last_code != code ||
         last_texture != texture || last_names != names || last_items != store->items.get() ||
         now - last_refresh > std::chrono::seconds(30))) {
        last_vehicles = vehicles;
        last_tracks = legacy.tracks;
        last_characters = legacy.characters;
        last_code = code;
        last_texture = texture;
        last_names = names;
        last_items = store->items.get();
        last_refresh = now;
        manager.refresh(*store->items, StoreNativeInventory());
    }
    static auto next_check = now + std::chrono::minutes(30);
    if (safe && !view->busy && now >= next_check) {
        next_check = now + std::chrono::minutes(30);
        dkr::store::service().refresh();
    }
}
bool StoreBeginManagerModal(const char* name, float width, float height) {
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 size{std::min(width, std::max(1.F, viewport->WorkSize.x - 24.F)),
                      std::min(height, std::max(1.F, viewport->WorkSize.y - 24.F))};
    ImGui::SetNextWindowPos(
        {viewport->WorkPos.x + viewport->WorkSize.x * .5F, viewport->WorkPos.y + viewport->WorkSize.y * .5F},
        ImGuiCond_Always, {.5F, .5F});
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    // Do not route through BeginPaddedModal: its desktop fallback replaces the
    // caller's minimum-width constraint with zero. Wrapped/custom-drawn content
    // then feeds a near-zero available width back into AlwaysAutoResize.
    return BeginPaddockModalWindow(name,
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoScrollWithMouse,
                                   true);
}
bool StoreModalBack() {
    return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
           (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape));
}
void StoreModalBody(const char* id, float width, std::initializer_list<const char*> footer) {
    // Reserve the actual wrapped footer height, including on smaller displays.
    const auto look = PaddockLook(PaddockButtonKind::Plain);
    const auto type = PaddockReading(look.font_px, true, look.line);
    float x = 0, y = 0, row = 0;
    for (const auto* label : footer) {
        const float w = std::min(width, std::max({90.F, PaddockButtonWidth(label, PaddockButtonKind::Plain),
                                                  PaddockButtonWidth(label, PaddockButtonKind::Primary)}));
        if (x > 0 && x + w > width) {
            x = 0;
            y += row + 8.F;
            row = 0;
        }
        const auto lines = PaddockWrap(type, label, std::max(1.F, w - look.padding_x * 2.F - 2.F));
        const auto primary = PaddockLook(PaddockButtonKind::Primary);
        const auto primary_type = PaddockReading(primary.font_px, true, primary.line);
        const auto primary_lines = PaddockWrap(primary_type, label, std::max(1.F, w - primary.padding_x * 2.F - 2.F));
        row = std::max({row, 44.F, type.line * lines.size() + look.padding_y * 2.F + 2.F,
                        primary_type.line * primary_lines.size() + primary.padding_y * 2.F + 2.F});
        x += w + 8.F;
    }
    const float reserved = y + row + 9.F + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild(id, {0, std::max(1.F, ImGui::GetContentRegionAvail().y - reserved)}, false);
}
void StoreManagerModals() {
    PaddockFlatScope flat;
    const auto view = dkr::store::installations().view();
    static std::string choice_job;
    static int selected = 0;
    if (view->choosing) {
        const auto job = StoreText(view->proposal, "job", 512);
        if (job != choice_job || !ImGui::IsPopupOpen("Mod already installed")) {
            choice_job = job;
            selected = 0;
            ImGui::OpenPopup("Mod already installed");
        }
    }
    if (StoreBeginManagerModal("Mod already installed", 760.F, 580.F)) {
        if (!view->choosing) {
            // A worker failure/cancellation must not leave a stale modal above
            // the launcher with no actionable proposal.
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        } else {
            const float width = std::max(1.F, ImGui::GetContentRegionAvail().x);
            const auto candidates = view->proposal.value("candidates", StoreJson::array());
            const bool valid = candidates.is_array() && !candidates.empty();
            StoreModalBody("Duplicate details", width, {"Replace & reinstall", "Add as new", "Cancel"});
            const float body_width = std::max(1.F, ImGui::GetContentRegionAvail().x);
            StoreHeading(StoreText(view->proposal, "title", 200), body_width);
            if (valid) {
                selected = std::clamp(selected, 0, int(candidates.size() - 1));
                StoreNote(candidates[selected].value("exact", false)
                              ? "An existing installation matches this mod."
                              : "Possible duplicate: the names match, but the content is not confirmed identical.",
                          body_width);
                for (int i = 0; i < int(candidates.size()); ++i) {
                    const auto& item = candidates[i];
                    if (StoreButton(std::string(i == selected ? "> " : "") + StoreText(item, "title", 200) + " - " +
                                        StoreText(item, "version", 80) + "##candidate" + std::to_string(i),
                                    body_width))
                        selected = i;
                }
                StoreNote("Incoming version: " + StoreText(view->proposal, "version", 80), body_width);
                if (view->proposal.value("modified", false))
                    StoreNote(
                        "The installed files have changed locally. Replace only if you want to discard those edits. "
                        "A protected copy will be retained.",
                        body_width);
                if (view->proposal.value("code", false))
                    StoreNote(
                        "Changed code requires your approval before it can run. Compatible settings and user data "
                        "are retained.",
                        body_width);
                if (!view->proposal.value("canRunCopies", false))
                    StoreNote("Add as new keeps a separate edition. Only one edition of the same game identity can be "
                              "in use.",
                              body_width);
                if (view->proposal.value("identityChanged", false))
                    StoreNote("The publisher changed this mod's internal identity. Replace only if intended; cups or "
                              "settings using the old identity may need attention. The old edition is retained.",
                              body_width);
            } else {
                StoreNote("This installation choice is no longer available. Cancel and retry the download.",
                          body_width);
            }
            ImGui::EndChild();
            // Cancel is outside the scroll area and never disabled by missing data.
            std::vector<std::pair<std::string, std::function<void()>>> actions;
            if (valid) {
                actions = {
                    {"Replace & reinstall",
                     [&] {
                         dkr::store::installations().decide("replace",
                                                            StoreText(candidates[selected], "instance", 512));
                         ImGui::CloseCurrentPopup();
                     }},
                    {"Add as new",
                     [] {
                         dkr::store::installations().decide("add");
                         ImGui::CloseCurrentPopup();
                     }},
                };
            }
            actions.push_back({"Cancel", [] {
                                   dkr::store::installations().decide("cancel");
                                   ImGui::CloseCurrentPopup();
                               }});
            StoreActionLine(actions, width);
            if (StoreModalBack()) {
                dkr::store::installations().decide("cancel");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    static std::uint64_t offered = 0;
    if (!view->busy && !view->choosing && !view->updates.empty() && view->updates_revision != offered &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && g_mod_store.popup_request.empty() &&
        !g_mod_store.remove_requested && !g_mod_store.link_requested) {
        offered = view->updates_revision;
        ImGui::OpenPopup("Mod updates available");
    }
    if (StoreBeginManagerModal("Mod updates available", 760.F, 340.F)) {
        const float width = ImGui::GetContentRegionAvail().x;
        StoreModalBody("Update details", width, {"Review updates", "Later"});
        const float body_width = std::max(1.F, ImGui::GetContentRegionAvail().x);
        StoreHeading("Updates for your installed mods", body_width);
        StoreNote(std::to_string(view->updates.size()) +
                      " update(s) available. Nothing changes until you choose an update.",
                  body_width);
        StoreNote("Code updates need approval before running. Your working versions are kept if preparation fails.",
                  body_width);
        ImGui::EndChild();
        StoreActionLine({{"Review updates",
                          [] {
                              g_mods_page.entry_section = 4;
                              g_mods_page.section = 4;
                              g_page_navigation_request = kPageModsHacks;
                              g_mod_store.page = 5;
                              ImGui::CloseCurrentPopup();
                          }},
                         {"Later", [] { ImGui::CloseCurrentPopup(); }}},
                        width);
        if (StoreModalBack())
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g_mod_store.remove_requested) {
        g_mod_store.remove_requested = false;
        ImGui::OpenPopup("Remove installed mod?");
    }
    if (StoreBeginManagerModal("Remove installed mod?", 680.F, 380.F)) {
        const float width = ImGui::GetContentRegionAvail().x;
        StoreModalBody("Removal details", width, {"Remove installation", "Keep files, unlink Store", "Cancel"});
        const float body_width = std::max(1.F, ImGui::GetContentRegionAvail().x);
        StoreHeading("Remove this installation?", body_width);
        StoreNote(g_mod_store.remove_title, body_width);
        StoreNote("Only this local installation is removed. Saves, original downloads and copies owned by other "
                  "packages are kept. A protected copy is retained for recovery.",
                  body_width);
        ImGui::EndChild();
        StoreActionLine({{"Remove installation",
                          [] {
                              if (!dkr::store::installations().view()->busy) {
                                  dkr::store::installations().forget(g_mod_store.remove_instance, true);
                                  ImGui::CloseCurrentPopup();
                              }
                          }},
                         {"Keep files, unlink Store",
                          [] {
                              if (!dkr::store::installations().view()->busy) {
                                  dkr::store::installations().forget(g_mod_store.remove_instance, false);
                                  ImGui::CloseCurrentPopup();
                              }
                          }},
                         {"Cancel", [] { ImGui::CloseCurrentPopup(); }}},
                        width);
        if (StoreModalBack())
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g_mod_store.link_requested) {
        g_mod_store.link_requested = false;
        g_mod_store.fields["link-search"].clear();
        ImGui::OpenPopup("Link installed mod");
    }
    if (StoreBeginManagerModal("Link installed mod", 760.F, 640.F)) {
        const float width = ImGui::GetContentRegionAvail().x;
        StoreModalBody("Store listings", width, {"Cancel"});
        const float body_width = std::max(1.F, ImGui::GetContentRegionAvail().x);
        StoreHeading("Choose its Store listing", body_width);
        StoreNote("This links update notifications. It does not claim your local files match a published release, or "
                  "replace anything now.",
                  body_width);
        StoreField("link-search", "Find a Store mod", body_width, 200);
        const auto query = StoreLower(g_mod_store.fields["link-search"]);
        unsigned shown = 0, matching = 0;
        for (const auto& item : *dkr::store::service().view()->items) {
            if (!query.empty() && StoreLower(StoreText(item, "title", 200)).find(query) == std::string::npos)
                continue;
            ++matching;
            if (++shown > unsigned(StoreLimit("link-list")))
                continue;
            if (StoreButton(StoreText(item, "title", 200) + "##link" + StoreText(item, "id", 160), body_width)) {
                dkr::store::installations().link(g_mod_store.link_key, StoreText(item, "id", 160));
                ImGui::CloseCurrentPopup();
            }
        }
        StoreMore("link-list", matching, body_width);
        ImGui::EndChild();
        if (StoreButton("Cancel"))
            ImGui::CloseCurrentPopup();
        if (StoreModalBack())
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
const StoreJson* StoreInstalledRelease(const StoreJson& item, const StoreJson& record) {
    if (item.contains("releases"))
        for (const auto& release : item["releases"])
            if (StoreText(release, "id", 160) == StoreText(record, "release", 160))
                return &release;
    return nullptr;
}
#include "store_player_library.inl"
