// Compact rows shared by player and staff pages. One scroll surface belongs to
// the launcher; selecting a row opens a dedicated, restorable detail screen.
bool StoreRow(const std::string& id, const StoreJson& item, const std::string& title, const std::string& meta,
              const char* action, float width, bool selected = false, bool* choice = nullptr) {
    ImGui::PushID(id.c_str());
    StorePanel panel(width, selected);
    const float inner = panel.inner();
    bool pressed = false;
    if (inner >= 530.F && ImGui::BeginTable("row", 3, ImGuiTableFlags_NoSavedSettings, {inner, 0})) {
        ImGui::TableSetupColumn("Art", ImGuiTableColumnFlags_WidthFixed, 72.F);
        ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed,
                                std::max(130.F, PaddockButtonWidth(action, PaddockButtonKind::Plain)));
        ImGui::TableNextColumn();
        StoreCover(item, 64.F, 52.F);
        ImGui::TableNextColumn();
        StoreHeading(title, ImGui::GetContentRegionAvail().x, 18.F);
        StoreNote(meta, ImGui::GetContentRegionAvail().x);
        ImGui::TableNextColumn();
        if (choice)
            StoreCheck("Select", *choice, ImGui::GetContentRegionAvail().x);
        pressed = StoreButton(action, ImGui::GetContentRegionAvail().x);
        g_mod_store.last_row_action = ImGui::GetItemID();
        ImGui::EndTable();
    } else {
        StoreHeading(title, inner, 18.F);
        StoreNote(meta, inner);
        if (choice)
            StoreCheck("Select", *choice, inner);
        pressed = StoreButton(action);
        g_mod_store.last_row_action = ImGui::GetItemID();
    }
    ImGui::PopID();
    return pressed;
}
const StoreJson* StoreBy(const StoreJson& rows, const char* field, const std::string& value) {
    if (rows.is_array())
        for (const auto& row : rows)
            if (StoreText(row, field, 1024) == value)
                return &row;
    return nullptr;
}
int StoreLocalCategory(const StoreJson& local) {
    const auto kind = StoreText(local, "kind", 80);
    return kind == "track" || kind == "legacy-track" ? 1
           : kind == "legacy-character"              ? 2
           : kind == "code"                          ? 4
           : kind == "texture"                       ? 5
                                                     : 3;
}
void StoreInstalledPage(float width, const dkr::store::Snapshot& store, bool locked) {
    const auto view = dkr::store::installations().view();
    width = std::min(width, 1280.F);
    if (!g_mod_store.installed_selected.empty()) {
        const StoreJson* record = nullptr;
        for (const auto& row : view->records)
            if (StoreText(row, "instance", 32) == g_mod_store.installed_selected) {
                record = &row;
                break;
            }
        if (StoreBack("Back to installed mods", [] { g_mod_store.installed_selected.clear(); }))
            return;
        if (!record) {
            StoreNotice("Installation changed", "Return to Installed to choose another mod.", width);
            return;
        }
        const auto& row = *record;
        const auto instance = StoreText(row, "instance", 32);
        const auto* item = StoreFind(StoreItems(store), StoreText(row, "item", 160));
        StoreHeading(StoreText(row, "title", 200), width);
        StoreBadge(StoreText(row, "state", 80) + "  |  " + StoreText(row, "version", 80), width);
        if (row.value("anotherTrackCopyActive", false))
            StoreNotice("Another copy is active",
                        "This copy is installed separately. Your other copy has not been changed.", width);
        for (const auto& warning : row.value("warnings", StoreJson::array()))
            if (warning.is_string())
                StoreNotice("Needs attention", warning.get<std::string>(), width, true);
        ImGui::BeginDisabled(locked || view->busy);
        StorePanel settings(std::min(width, 820.F));
        bool follow = row.value("follow", true);
        if (StoreCheck("Notify me about updates", follow, settings.inner()))
            dkr::store::installations().policy(instance, follow);
        std::vector<std::pair<std::string, std::function<void()>>> actions;
        if (row.value("stored", false) && row.contains("file"))
            actions.push_back({"Use this edition", [instance] {
                                   dkr::store::installations().activate(instance, false, StoreOriginalRoms());
                               }});
        if (item)
            if (const auto* release = StoreInstalledRelease(*item, row)) {
                const auto state = StoreText(row, "state", 80);
                actions.push_back({state == "Missing" || state == "Needs repair" ? "Repair installation" : "Reinstall",
                                   [item = *item, release = *release, instance] {
                                       StoreQueueRelease(item, release, "reinstall", instance);
                                   }});
            }
        if (row.contains("previous") && row["previous"].contains("file"))
            actions.push_back({"Restore previous version", [instance] {
                                   dkr::store::installations().activate(instance, true, StoreOriginalRoms());
                               }});
        StoreActionLine(actions, settings.inner());
        PaddockSeparatorText("Remove or unlink", settings.inner());
        StoreNote("Remove this copy, or keep its files and stop linking it to the Store.", settings.inner());
        if (StoreButton("Remove or unlink")) {
            g_mod_store.remove_instance = instance;
            g_mod_store.remove_title = StoreText(row, "title", 200);
            g_mod_store.remove_requested = true;
        }
        ImGui::EndDisabled();
        return;
    }
    StoreHeading("Installed mods", width);
    StoreField("installed-search", "Find a mod", std::min(width, 760.F), 200);
    const bool filters =
        width >= 800.F && ImGui::BeginTable("installed-filters", 3, ImGuiTableFlags_NoSavedSettings, {width, 0});
    if (filters)
        ImGui::TableNextColumn();
    g_mod_store.installed_category = StoreCombo(
        "Category", g_mod_store.installed_category,
        {"All categories", "Tracks", "Characters", "Vehicles", "Custom Magic Codes", "Texture packs"},
        std::min(width, 340.F));
    if (filters)
        ImGui::TableNextColumn();
    g_mod_store.installed_state =
        StoreCombo("Show", g_mod_store.installed_state,
                   {"All installations", "Installed", "Saved editions", "Needs attention", "Not linked to Store"},
                   std::min(width, 340.F));
    if (filters)
        ImGui::TableNextColumn();
    g_mod_store.installed_sort =
        StoreCombo("Sort", g_mod_store.installed_sort, {"Name A-Z", "Name Z-A", "Status"}, std::min(width, 340.F));
    if (filters)
        ImGui::EndTable();
    const auto query = g_mod_store.fields["installed-search"];
    std::vector<const StoreJson*> rows;
    for (const auto& record : view->records) {
        if (g_mod_store.installed_state == 4 || !StoreMatches(StoreText(record, "title", 200), query))
            continue;
        const auto* item = StoreFind(StoreItems(store), StoreText(record, "item", 160));
        if (g_mod_store.installed_category &&
            (item ? StoreText(*item, "category", 32) : StoreText(record, "category", 32)) !=
                kStoreCategoryIds[g_mod_store.installed_category])
            continue;
        const auto state = StoreText(record, "state", 80);
        if (g_mod_store.installed_state == 1 && (state != "Installed" || record.value("stored", false)))
            continue;
        if (g_mod_store.installed_state == 2 && !record.value("stored", false))
            continue;
        if (g_mod_store.installed_state == 3 && state != "Needs repair" && state != "Missing" &&
            record.value("warnings", StoreJson::array()).empty())
            continue;
        rows.push_back(&record);
    }
    std::stable_sort(rows.begin(), rows.end(), [](auto a, auto b) {
        if (g_mod_store.installed_sort == 2 && StoreText(*a, "state", 80) != StoreText(*b, "state", 80))
            return StoreText(*a, "state", 80) < StoreText(*b, "state", 80);
        const auto left = StoreLower(StoreText(*a, "title", 200)), right = StoreLower(StoreText(*b, "title", 200));
        return g_mod_store.installed_sort == 1 ? left > right : left < right;
    });
    int shown = 0;
    for (const auto* record : rows) {
        if (++shown > StoreLimit("installed"))
            break;
        const auto* item = StoreFind(StoreItems(store), StoreText(*record, "item", 160));
        if (StoreRow(StoreText(*record, "instance", 32), item ? *item : StoreJson::object(),
                     StoreText(*record, "title", 200),
                     StoreText(*record, "version", 80) + "  |  " + StoreText(*record, "state", 80), "Manage", width))
            g_mod_store.installed_selected = StoreText(*record, "instance", 32);
    }
    StoreMore("installed", rows.size(), width);
    if (rows.empty() && g_mod_store.installed_state != 4)
        StoreNote("No linked installations match these filters.", width);
    if (g_mod_store.installed_state != 0 && g_mod_store.installed_state != 4)
        return;
    StoreHeading("Other installed mods", width, 20);
    std::vector<const StoreJson*> unlinked;
    for (const auto& local : view->unlinked)
        if (StoreMatches(StoreText(local, "name", 200), query) &&
            (!g_mod_store.installed_category || StoreLocalCategory(local) == g_mod_store.installed_category))
            unlinked.push_back(&local);
    shown = 0;
    for (const auto* local : unlinked) {
        if (++shown > StoreLimit("unlinked"))
            break;
        ImGui::BeginDisabled(locked || view->busy);
        if (StoreRow(StoreText(*local, "key", 512), StoreJson::object(), StoreText(*local, "name", 200),
                     "Installed locally  |  Not linked to Store updates", "Link to Store", width)) {
            g_mod_store.link_key = StoreText(*local, "key", 512);
            g_mod_store.link_requested = true;
        }
        ImGui::EndDisabled();
    }
    StoreMore("unlinked", unlinked.size(), width);
    if (unlinked.empty())
        StoreNote("No unlinked mods match these filters.", width);
}
void StoreUpdatesPage(float width, const dkr::store::Snapshot& store, bool locked) {
    const auto view = dkr::store::installations().view();
    width = std::min(width, 1280.F);
    StoreHeading("Mod updates", width);
    if (!g_mod_store.update_detail.empty() && StoreBack("Back to updates", [] { g_mod_store.update_detail.clear(); }))
        return;
    ImGui::BeginDisabled(store.loading);
    if (StoreButton(store.loading ? "Checking..." : "Check for updates"))
        dkr::store::service().refresh();
    ImGui::EndDisabled();
    if (view->updates.empty()) {
        StoreNotice(store.loading ? "Checking the library" : "You're up to date",
                    "Updates for linked mods appear here.", width);
        return;
    }
    if (!g_mod_store.update_detail.empty() &&
        std::none_of(view->updates.begin(), view->updates.end(),
                     [](const auto& row) { return StoreText(row, "instance", 32) == g_mod_store.update_detail; })) {
        StoreNotice("This update is no longer pending", "Return to Updates to review the current list.", width);
        return;
    }
    std::set<std::string> available;
    for (const auto& update : view->updates)
        if (!update.value("downgrade", false))
            available.insert(StoreText(update, "instance", 32));
    std::erase_if(g_mod_store.selected_updates, [&](const auto& id) { return !available.contains(id); });
    ImGui::BeginDisabled(locked || view->busy);
    if (g_mod_store.update_detail.empty()) {
        StoreActionLine({{"Select all", [available] { g_mod_store.selected_updates = available; }},
                         {"Clear selection", [] { g_mod_store.selected_updates.clear(); }}},
                        width);
        const auto count = g_mod_store.selected_updates.size();
        ImGui::BeginDisabled(count == 0);
        if (StoreButton("Update selected (" + std::to_string(count) + ")", 0, true))
            for (const auto& update : view->updates)
                if (!update.value("downgrade", false) &&
                    g_mod_store.selected_updates.contains(StoreText(update, "instance", 32)))
                    StoreQueueRelease(update["item"], update["release"], "update", StoreText(update, "instance", 32));
        ImGui::EndDisabled();
        if (StoreButton("Update all"))
            for (const auto& update : view->updates)
                if (!update.value("downgrade", false))
                    StoreQueueRelease(update["item"], update["release"], "update", StoreText(update, "instance", 32));
    }
    int shown = 0;
    for (const auto& update : view->updates) {
        if (++shown > StoreLimit("updates") && g_mod_store.update_detail.empty())
            break;
        const auto instance = StoreText(update, "instance", 32);
        if (g_mod_store.update_detail.empty()) {
            bool selected = g_mod_store.selected_updates.contains(instance);
            const bool downgrade = update.value("downgrade", false);
            if (StoreRow(instance, update["item"], StoreText(update, "title", 200),
                         StoreText(update, "current", 80) + " -> " + StoreText(update["release"], "version", 80) +
                             "  |  " +
                             (downgrade ? "Older publisher release"
                                        : StoreSize(update["release"].value("size", std::uint64_t(0)))),
                         "Review update", width, selected, downgrade ? nullptr : &selected))
                g_mod_store.update_detail = instance;
            if (selected && !downgrade)
                g_mod_store.selected_updates.insert(instance);
            else
                g_mod_store.selected_updates.erase(instance);
            continue;
        }
        if (instance != g_mod_store.update_detail)
            continue;
        ImGui::PushID(instance.c_str());
        StorePanel panel(width);
        const float inner = panel.inner();
        StoreHeading(StoreText(update, "title", 200), inner, 18);
        const auto release = update["release"];
        StoreNote(StoreText(update, "current", 80) + "  ->  " + StoreText(release, "version", 80) + "  |  " +
                      StoreSize(release.value("size", std::uint64_t(0))),
                  inner);
        const bool downgrade = update.value("downgrade", false);
        if (!downgrade) {
            bool selected = g_mod_store.selected_updates.contains(instance);
            if (StoreCheck("Select update", selected, inner)) {
                if (selected)
                    g_mod_store.selected_updates.insert(instance);
                else
                    g_mod_store.selected_updates.erase(instance);
            }
        } else
            StoreNote("Publisher selected an older release. Bulk updates will skip this mod.", inner, false);
        if (update.value("codePermission", false))
            StoreNote("New code needs your approval before it can run.", inner);
        StoreActionLine({{"Release notes",
                          [release, update] {
                              g_mod_store.release_details = release;
                              g_mod_store.release_title = StoreText(update, "title", 200);
                              StoreOpenPopup("Release information");
                          }},
                         {downgrade ? "Review older release" : "Update",
                          [update, instance, downgrade] {
                              if (downgrade) {
                                  g_mod_store.release_details = update["release"];
                                  g_mod_store.update_selected = instance;
                                  StoreOpenPopup("Use older mod release?");
                              } else
                                  StoreQueueRelease(update["item"], update["release"], "update", instance);
                          }},
                         {"Skip release",
                          [instance, release] {
                              dkr::store::installations().policy(instance, true, StoreText(release, "id", 160));
                          }},
                         {"Keep my version", [instance] { dkr::store::installations().policy(instance, false); }}},
                        inner);
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (g_mod_store.update_detail.empty())
        StoreMore("updates", view->updates.size(), width);
}
void StoreDuplicatesPage(float width, const dkr::store::Snapshot&, bool locked) {
    const auto view = dkr::store::installations().view();
    width = std::min(width, 1280.F);
    StoreHeading("Duplicates", width);
    StoreNote("Compare copies before removing one. Matching names do not prove matching content.", width);
    StoreField("duplicates-search", "Find a duplicate", std::min(width, 760.F), 200);
    std::size_t matching = 0;
    for (const auto& group : view->duplicates) {
        bool match = false;
        for (const auto& row : group["entries"])
            if (StoreMatches(StoreText(row, "name", 200), g_mod_store.fields["duplicates-search"]))
                match = true;
        if (!match)
            continue;
        if (++matching > std::size_t(StoreLimit("duplicates")))
            continue;
        ImGui::PushID(StoreText(group, "identity", 1024).c_str());
        StorePanel panel(width);
        const float inner = panel.inner();
        StoreHeading(group.value("exact", false) ? "Matching content" : "Similar names - compare carefully", inner, 18);
        for (const auto& entry : group["entries"]) {
            ImGui::PushID(StoreText(entry, "key", 512).c_str());
            StoreHeading(StoreText(entry, "name", 200), inner, 17);
            StoreNote(std::string(entry.value("enabled", false) ? "Enabled" : "Disabled") + "  |  " +
                          kStoreCategoryNames[StoreLocalCategory(entry)] +
                          (entry.contains("version") ? "  |  " + StoreText(entry, "version", 80) : ""),
                      inner);
            const auto path = StoreText(entry, "path", 4096);
            if (!path.empty()) {
                StoreNote(path, inner);
                if (StoreButton("Copy location"))
                    SDL_SetClipboardText(path.c_str());
            }
            if (entry.contains("priority"))
                StoreNote("Texture priority: " + std::to_string(entry.value("priority", 0)), inner);
            ImGui::BeginDisabled(locked || view->busy);
            if (StoreButton("Remove this copy")) {
                g_mod_store.remove_instance = "local:" + StoreText(entry, "key", 512);
                g_mod_store.remove_title = StoreText(entry, "name", 200) + "\n" + path;
                g_mod_store.remove_requested = true;
            }
            ImGui::EndDisabled();
            PaddockGap(10.F);
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if (!matching)
        StoreNotice("No duplicates found", "No matching copies in the current library inventory.", width);
    StoreMore("duplicates", matching, width);
}
