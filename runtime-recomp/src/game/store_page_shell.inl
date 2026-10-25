void DrawModStore(float width, bool locked) {
    auto& service = dkr::store::service();
    if (!g_mod_store.opened) {
        g_mod_store.opened = true;
        service.refresh();
    }
    auto store = service.view();
    StoreSyncIdentity(*store);
    if (!store->writing && store->mutation != g_mod_store.mutation) {
        g_mod_store.mutation = store->mutation;
        if (g_mod_store.pending_editor_generation == g_mod_store.editor_generation) {
            if (store->last_action == "catalogue.save" && store->last_result.is_string()) {
                const auto id = store->last_result.get<std::string>();
                const auto drafts = store->admin.value("drafts", StoreJson::array());
                if (const auto* saved = StoreFind(drafts, id)) {
                    const int step = g_mod_store.editor_step;
                    StoreLoadDraft(saved->contains("draft") ? (*saved)["draft"] : *saved);
                    g_mod_store.editor = id;
                    g_mod_store.last_saved = id;
                    g_mod_store.draft["revision"] = StoreInt(*saved, "revision");
                    g_mod_store.editor_step = step;
                    g_mod_store.dirty = false;
                }
            } else if (store->last_action == "catalogue.update")
                g_mod_store.dirty = false;
            else if (store->last_action == "catalogue.publish" || store->last_action == "request.publish") {
                g_mod_store.dirty = false;
                g_mod_store.editing = false;
                g_mod_store.request.clear();
            }
        }
        if (store->last_action == "request.submit") {
            for (const char* key : {"request-title", "request-author", "request-source", "request-download",
                                    "request-summary", "request-requirements", "request-tested", "request-screenshot"})
                g_mod_store.fields[key].clear();
            g_mod_store.request_credit = false;
            if (g_mod_store.page == 1)
                g_mod_store.request_page = 1;
        }
    }
    if (auto url = service.take_browser_url(); !url.empty())
        SDL_OpenURL(url.c_str());
    StorePumpInstall();
    StoreStartQueuedInstall(locked || store->writing);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.F);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10.F, 8.F});
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {.5F, .5F});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10.F, 14.F});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, PaddockCol(0x102F43));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PaddockCol(0x1C4C66));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, PaddockCol(0x205774));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockCol(0x496579));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, PaddockCol(0xFFC15A));
    const bool header =
        width >= 700.F && ImGui::BeginTable("store-header", 2, ImGuiTableFlags_NoSavedSettings, {width, 0});
    if (header) {
        ImGui::TableSetupColumn("Store", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Tools", ImGuiTableColumnFlags_WidthFixed, 300.F);
        ImGui::TableNextColumn();
    }
    StoreHeading("MOD STORE", ImGui::GetContentRegionAvail().x, 27);
    StoreNote(store->user.is_null() ? "Community mods for DKR-R"
                                    : "Signed in as " + StoreText(store->user, "displayName", 100),
              ImGui::GetContentRegionAvail().x);
    if (header)
        ImGui::TableNextColumn();
    ImGui::BeginDisabled(store->loading);
    StoreActionLine({{store->writing   ? "Saving..."
                      : store->loading ? "Refreshing..."
                                       : "Refresh",
                      [&] {
                          g_mod_store.message_page = std::to_string(g_mod_store.page);
                          service.refresh();
                      }},
                     {"Website", [] { SDL_OpenURL("https://positron-portal.com/dkr-r/index.html"); }}},
                    ImGui::GetContentRegionAvail().x);
    ImGui::EndDisabled();
    if (header)
        ImGui::EndTable();
    if (store->cached && !store->loading)
        StoreNote("Showing the saved library. Refresh to try connecting again.", width);
    const auto active = std::count_if(store->downloads.begin(), store->downloads.end(), StoreTransferActive);
    const auto manager = dkr::store::installations().view();
    const int current = g_mod_store.page == 0                                                     ? 0
                        : g_mod_store.page == 4 || g_mod_store.page == 5 || g_mod_store.page == 6 ? 1
                        : g_mod_store.page == 2                                                   ? 2
                        : g_mod_store.page == 1                                                   ? 3
                        : g_mod_store.page == 3                                                   ? 4
                                                                                                  : 5;
    std::vector<std::string> tabs{"Browse", "Installed", "Downloads (" + std::to_string(active) + ")", "Requests",
                                  store->user.is_null() ? "Sign in" : "Account"};
    if (dkr::store::staff(store->user))
        tabs.push_back("Manage library");
    const int chosen = StoreTabLine(tabs, current, width, "store-pages");
    if (chosen != current)
        StoreNavigate([chosen] {
            const int pages[] = {0, 4, 2, 1, 3, 7};
            g_mod_store.page = pages[chosen];
            if (chosen != 5)
                g_mod_store.picker_upload = false;
        });
    if (g_mod_store.page == 5 || g_mod_store.page == 6) {
        g_mod_store.installed_section = g_mod_store.page == 5 ? 1 : 2;
        g_mod_store.page = 4;
    }
    if (g_mod_store.page == 4)
        g_mod_store.installed_section =
            StoreTabLine({"Installed mods", "Updates (" + std::to_string(manager->updates.size()) + ")", "Duplicates"},
                         g_mod_store.installed_section, width, "installed-sections");
    if (g_mod_store.last_error != store->error || g_mod_store.last_status != store->status) {
        if (!store->error.empty() && g_mod_store.message_page.empty())
            g_mod_store.message_page = std::to_string(g_mod_store.page);
        g_mod_store.last_error = store->error;
        g_mod_store.last_status = store->status;
    }
    if (!store->error.empty() && g_mod_store.message_page == std::to_string(g_mod_store.page))
        StoreMessage("Couldn't complete that action", store->error, width);
    else if (!store->status.empty() && g_mod_store.message_page == std::to_string(g_mod_store.page) &&
             (store->status == "Your change was saved." || store->status.starts_with("Library account deleted"))) {
        StoreNote(store->status, width, false);
        if (StoreButton("Dismiss message"))
            service.clear_messages();
    }
    if (!manager->ready && !manager->busy) {
        StoreNotice("Installation recovery needs attention",
                    manager->error.empty() ? "Finish recovery before changing mods or starting a game."
                                           : manager->error,
                    width, true);
        if (!manager->error.empty() && StoreButton("Copy recovery error"))
            SDL_SetClipboardText(manager->error.c_str());
        if (StoreButton("Retry recovery", 0, true))
            dkr::store::installations().retry_recovery();
    }
    if (manager->busy && g_mod_store.page != 2) {
        StoreNote(manager->stage, width);
        ImGui::ProgressBar(manager->progress, {width, 10.F}, "");
        if (StoreButton("View installation")) {
            g_mod_store.page = 2;
            g_mod_store.download_section = 0;
        }
    }
    const auto attention = std::count_if(store->downloads.begin(), store->downloads.end(), [](const auto& job) {
        return job.state == "Failed" || job.state == "Install failed";
    });
    if (attention && g_mod_store.page != 2) {
        if (StoreButton(std::to_string(attention) + " download(s) need attention")) {
            g_mod_store.page = 2;
            g_mod_store.download_section = 1;
        }
    }
    StoreBeginBodyNavigation();
    if (g_mod_store.page == 0)
        StoreBrowser(width, *store);
    else if (g_mod_store.page == 1)
        StoreRequestForm(width, *store);
    else if (g_mod_store.page == 2)
        StoreDownloads(width, locked, *store);
    else if (g_mod_store.page == 3)
        StoreAccount(width, *store);
    else if (g_mod_store.page == 4) {
        if (g_mod_store.installed_section == 0)
            StoreInstalledPage(width, *store, locked);
        else if (g_mod_store.installed_section == 1)
            StoreUpdatesPage(width, *store, locked);
        else
            StoreDuplicatesPage(width, *store, locked);
    } else
        StoreManagement(width, *store);
    StoreEndBodyNavigation();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);
}
