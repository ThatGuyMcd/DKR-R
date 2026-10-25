void StoreDownloads(float width, bool locked, const dkr::store::Snapshot& store) {
    width = std::min(width, 1280.F);
    StoreHeading("Downloads", width);
    g_mod_store.download_section = StoreTabLine({"In progress", "Needs attention", "History"},
                                                g_mod_store.download_section, width, "download-sections");
    StoreField("download-search", "Find a download", std::min(width, 760.F), 200);
    if (g_mod_store.download_section == 2) {
        if (StoreButton("Clear completed history"))
            dkr::store::service().clear_completed_history();
        StoreNote("Clears completed entries only. Installed mods and cached packages are kept.", width);
    }
    const auto manager = dkr::store::installations().view();
    std::size_t matching = 0;
    for (auto it = store.downloads.rbegin(); it != store.downloads.rend(); ++it) {
        const auto& job = *it;
        const int section =
            job.state == "Installed"                                                                            ? 2
            : StoreTransferActive(job) || job.state == "Ready to install" || job.state == "Paused for gameplay" ? 0
                                                                                                                : 1;
        if (section != g_mod_store.download_section || !StoreMatches(job.title, g_mod_store.fields["download-search"]))
            continue;
        if (++matching > std::size_t(StoreLimit("downloads-" + std::to_string(section))))
            continue;
        ImGui::PushID(job.key.c_str());
        StorePanel card(width);
        const float inner = card.inner();
        StoreHeading(job.title, inner, 18);
        StoreBadge(job.state, inner,
                   section == 1   ? PaddockTone::Warning
                   : section == 2 ? PaddockTone::On
                                  : PaddockTone::Neutral);
        if (job.state == "Downloading" || job.state == "Verifying") {
            if (job.total)
                ImGui::ProgressBar(std::clamp(float(double(job.received) / double(job.total)), 0.F, 1.F), {inner, 12.F},
                                   "");
            StoreNote(StoreSize(job.received) + " / " + StoreSize(job.total), inner);
        } else if (job.state == "Installing") {
            if (g_mod_store.pending_key == job.key && manager->busy) {
                StoreNote(manager->stage, inner);
                ImGui::ProgressBar(manager->progress, {inner, 12.F}, "");
            } else
                StoreNote("Waiting for the installer.", inner);
        }
        if (!job.error.empty()) {
            StoreNote(job.error, inner, false);
            if (StoreButton("Copy error"))
                SDL_SetClipboardText(job.error.c_str());
        }
        if (job.state == "Queued" || job.state == "Downloading" || job.state == "Preparing download") {
            if (StoreButton("Pause"))
                dkr::store::service().pause_download(job.key);
        } else if (job.state == "Failed" || job.state == "Paused") {
            if (StoreButton("Retry / resume", 0, true)) {
                g_mod_store.install_after_download.insert(job.key);
                dkr::store::service().resume_download(job.key);
            }
        } else if (job.state == "Ready to install" || job.state == "Install failed") {
            ImGui::BeginDisabled(locked || manager->busy);
            if (StoreButton(job.state == "Install failed" ? "Retry installation" : "Install", 0, true))
                StoreInstall(job, locked);
            ImGui::EndDisabled();
            if (locked)
                StoreNote("Leave the online lobby before installing.", inner);
        } else if (job.state == "Installed") {
            if (StoreButton("Manage installed mods")) {
                g_mod_store.page = 4;
                g_mod_store.installed_section = 0;
            }
        }
        if (const auto* item = StoreFind(StoreItems(store), job.item)) {
            if (StoreButton("View mod")) {
                StoreOpenMod(*item, true);
                g_mod_store.page = 0;
            }
            if (section == 1 && item->contains("releases"))
                for (const auto& release : (*item)["releases"])
                    if (StoreText(release, "id", 160) == job.release) {
                        const auto url = StoreText(release, "fileUrl", 2048);
                        if (dkr::store::trusted_url(url) && StoreButton("Open download in browser"))
                            SDL_OpenURL(url.c_str());
                        break;
                    }
        }
        ImGui::PopID();
    }
    if (!matching) {
        StoreNotice("Nothing here yet",
                    g_mod_store.download_section == 0   ? "New downloads and installations appear here."
                    : g_mod_store.download_section == 1 ? "No paused or failed downloads match your search."
                                                        : "Completed installations appear here.",
                    width);
        if (StoreButton("Browse mods", 0, true))
            g_mod_store.page = 0;
    }
    StoreMore("downloads-" + std::to_string(g_mod_store.download_section), matching, width);
}
void StoreRequestForm(float width, const dkr::store::Snapshot& store) {
    StoreHeading("Requests", width);
    if (store.user.is_null()) {
        StoreNotice("Share a mod with the library", "Sign in with Discord to send a request or follow its progress.",
                    width);
        if (StoreButton("Sign in with Discord", 0, true))
            dkr::store::service().login();
        return;
    }
    if (g_mod_store.request_picker) {
        if (StoreBack("Back to request", [] { g_mod_store.request_picker = false; }))
            return;
        StoreHeading("Choose an existing listing", width, 20);
        StoreField("request-listing-search", "Find a mod", std::min(width, 760.F), 200);
        std::size_t matches = 0;
        for (const auto& row : StoreItems(store)) {
            if (StoreText(row, "category", 32) != kStoreCategoryIds[g_mod_store.request_category] ||
                !StoreMatches(StoreText(row, "title", 200), g_mod_store.fields["request-listing-search"]))
                continue;
            if (++matches > std::size_t(StoreLimit("request-listings")))
                continue;
            if (StoreRow(StoreText(row, "id", 160), row, StoreText(row, "title", 200), StoreText(row, "author", 200),
                         "Choose", width)) {
                g_mod_store.request_target = StoreText(row, "id", 160);
                g_mod_store.request_picker = false;
            }
        }
        if (!matches)
            StoreNote("No listings match this category and search.", width);
        StoreMore("request-listings", matches, width);
        return;
    }
    g_mod_store.request_page =
        StoreTabLine({"Request a mod", "My requests"}, g_mod_store.request_page, width, "request-pages");
    if (g_mod_store.request_page == 0) {
        StoreFormLayout layout("request-layout", width, 760.F);
        layout.sidebar([&](float side) {
            StorePanel preview(side);
            StoreHeading("Your submission", preview.inner(), 20);
            if (!g_mod_store.fields["request-screenshot"].empty())
                StoreCover({{"image", g_mod_store.fields["request-screenshot"]}}, preview.inner());
            StoreHeading(g_mod_store.fields["request-title"].empty() ? "Mod title"
                                                                     : g_mod_store.fields["request-title"],
                         preview.inner(), 18);
            StoreNote(g_mod_store.fields["request-author"].empty() ? "Creator credit"
                                                                   : "by " + g_mod_store.fields["request-author"],
                      preview.inner());
            StoreNote("Private request -> Staff review -> Library listing", preview.inner());
        });
        StorePanel form(layout.width);
        const float inner = form.inner();
        ImGui::BeginDisabled(StoreBusyAction(store));
        PaddockSeparatorText("Mod details", inner);
        g_mod_store.request_kind =
            StoreCombo("I'm sharing", g_mod_store.request_kind, {"An existing mod", "A mod I created"}, inner);
        StoreFieldPair("request-title", "Mod title", "request-author", "Original creator", inner);
        const int category =
            StoreCombo("Category", g_mod_store.request_category - 1,
                       {"Tracks", "Characters", "Vehicles", "Custom Magic Codes", "Texture packs"}, inner) +
            1;
        if (category != g_mod_store.request_category) {
            g_mod_store.request_category = category;
            g_mod_store.request_target.clear();
        }
        g_mod_store.request_relation = StoreCombo("Request type", g_mod_store.request_relation,
                                                  {"New library listing", "Update an existing listing"}, inner);
        if (g_mod_store.request_relation) {
            const auto* target = StoreFind(StoreItems(store), g_mod_store.request_target);
            StoreNote(target ? StoreText(*target, "title", 200) : "Choose the listing to update.", inner, false);
            if (StoreButton("Choose listing"))
                g_mod_store.request_picker = true;
        }
        StoreField("request-summary", "Description and creator credits", inner, 4000, true);
        PaddockSeparatorText("Links & requirements", inner);
        StoreField("request-source", "Original source link (required)", inner, 1500);
        StoreField("request-download", "Download link (optional)", inner, 1500);
        StoreFieldPair("request-tested", "DKR-R version played", "request-screenshot", "Screenshot link (optional)",
                       inner, 80, 1500);
        StoreField("request-requirements", "Requirements (optional)", inner, 2000, true);
        StoreCheck("I have credited the original creator", g_mod_store.request_credit, inner);
        const bool valid = !g_mod_store.fields["request-title"].empty() &&
                           !g_mod_store.fields["request-author"].empty() &&
                           !g_mod_store.fields["request-summary"].empty() &&
                           g_mod_store.fields["request-source"].starts_with("https://") && g_mod_store.request_credit &&
                           (!g_mod_store.request_relation || !g_mod_store.request_target.empty());
        if (!valid)
            StoreNote(
                "Add a title, creator, description and HTTPS source link. Confirm the creator credit before sending.",
                inner);
        ImGui::BeginDisabled(!valid);
        if (StoreButton("Send for review", 0, true)) {
            StoreJson input = {{"title", g_mod_store.fields["request-title"]},
                               {"author", g_mod_store.fields["request-author"]},
                               {"category", kStoreCategoryIds[g_mod_store.request_category]},
                               {"sourceUrl", g_mod_store.fields["request-source"]},
                               {"downloadUrl", g_mod_store.fields["request-download"]},
                               {"summary", g_mod_store.fields["request-summary"]},
                               {"requirements", g_mod_store.fields["request-requirements"]},
                               {"testedVersion", g_mod_store.fields["request-tested"]},
                               {"screenshotUrl", g_mod_store.fields["request-screenshot"]},
                               {"kind", g_mod_store.request_kind ? "creator" : "suggestion"},
                               {"relation", g_mod_store.request_relation ? "update" : "new"}};
            if (g_mod_store.request_relation)
                input["targetId"] = g_mod_store.request_target;
            StoreAction("request.submit", std::move(input));
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        return;
    }
    const auto requests = store.personal.value("requests", StoreJson::array());
    if (!g_mod_store.my_request.empty()) {
        if (StoreBack("Back to my requests", [] { g_mod_store.my_request.clear(); }))
            return;
        const auto* row = StoreFind(requests, g_mod_store.my_request);
        if (!row) {
            StoreNote("This request is no longer available.", width);
            return;
        }
        const auto& request = *row;
        StoreHeading(StoreText(request, "title", 200), width, 22);
        StoreBadge(StoreText(request, "status", 50), width);
        StoreNote(StoreText(request, "decisionMessage", 2400), width, false);
        StoreNote(StoreText(request, "summary", 4000), width);
        StoreHeading("Request history", width, 18);
        if (request.contains("events"))
            for (const auto& event : request["events"]) {
                StorePanel entry(std::min(width, 960.F));
                StoreHeading(StoreText(event, "label", 200), entry.inner(), 17);
                StoreNote(StoreText(event, "message", 3000), entry.inner());
                StoreNote(StoreDate(event, "createdAt"), entry.inner());
            }
        if (StoreText(request, "status", 40) == "changes") {
            const auto prefix = "resubmit-" + g_mod_store.my_request + "-";
            for (const char* key : {"sourceUrl", "downloadUrl", "screenshotUrl", "requirements", "testedVersion"})
                if (!g_mod_store.fields.contains(prefix + key))
                    g_mod_store.fields[prefix + key] = StoreText(request, key, 2000);
            StorePanel reply(std::min(width, 820.F));
            ImGui::BeginDisabled(StoreBusyAction(store));
            StoreField((prefix + "reply").c_str(), "Reply to the library team", reply.inner(), 2400, true);
            StoreField((prefix + "sourceUrl").c_str(), "Updated source link", reply.inner(), 1500);
            StoreField((prefix + "downloadUrl").c_str(), "Updated download link", reply.inner(), 1500);
            StoreField((prefix + "requirements").c_str(), "Requirements", reply.inner(), 2000, true);
            StoreFieldPair((prefix + "testedVersion").c_str(), "Version played", (prefix + "screenshotUrl").c_str(),
                           "Screenshot link", reply.inner(), 80, 1500);
            if (StoreButton("Send updated request", 0, true))
                StoreAction("request.resubmit", {{"id", request["id"]},
                                                 {"expectedRevision", StoreInt(request, "revision")},
                                                 {"message", g_mod_store.fields[prefix + "reply"]},
                                                 {"sourceUrl", g_mod_store.fields[prefix + "sourceUrl"]},
                                                 {"downloadUrl", g_mod_store.fields[prefix + "downloadUrl"]},
                                                 {"requirements", g_mod_store.fields[prefix + "requirements"]},
                                                 {"testedVersion", g_mod_store.fields[prefix + "testedVersion"]},
                                                 {"screenshotUrl", g_mod_store.fields[prefix + "screenshotUrl"]}});
            ImGui::EndDisabled();
        }
        return;
    }
    StoreField("my-request-search", "Find a request", std::min(width, 760.F), 200);
    std::size_t matching = 0;
    for (const auto& row : requests) {
        if (!StoreMatches(StoreText(row, "title", 200), g_mod_store.fields["my-request-search"]))
            continue;
        if (++matching > std::size_t(StoreLimit("my-requests")))
            continue;
        if (StoreRow(StoreText(row, "id", 160), StoreJson::object(), StoreText(row, "title", 200),
                     StoreText(row, "status", 40) + "  |  " + StoreDate(row), "Open request", width))
            g_mod_store.my_request = StoreText(row, "id", 160);
    }
    if (!matching)
        StoreNotice("No requests yet", "Use Request a mod to share something with the library team.", width);
    StoreMore("my-requests", matching, width);
}
