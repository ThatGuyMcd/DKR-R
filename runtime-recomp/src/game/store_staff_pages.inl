// Staff presentation uses the existing authenticated actions and revision
// fences. No role is inferred from UI state and no publication is automatic.
void StoreUseAsset(const StoreJson& asset) {
    StoreEnsureEditor();
    g_mod_store.dirty = true;
    g_mod_store.previewed = false;
    const bool image = StoreText(asset, "kind", 32) == "image";
    if (image)
        g_mod_store.fields["draft-image"] = StoreText(asset, "url", 1500);
    else {
        g_mod_store.fields["draft-fileUrl"] = StoreText(asset, "url", 1500);
        g_mod_store.fields["draft-fileName"] = StoreText(asset, "fileName", 180);
        g_mod_store.fields["draft-format"] = StoreText(asset, "format", 40);
    }
    g_mod_store.picker_upload = false;
    g_mod_store.editor_step = image ? 1 : 2;
    g_mod_store.staff_tab = 2;
}
void StoreUploads(float width, const dkr::store::Snapshot& store) {
    if (g_mod_store.picker_upload && StoreBack("Back to listing", [] {
            g_mod_store.picker_upload = false;
            g_mod_store.staff_tab = 2;
        }))
        return;
    StoreHeading(g_mod_store.picker_upload ? "Choose a listing file" : "Uploads & hosted files", width, 22);
    int category = 0;
    for (int i = 1; i < 6; ++i)
        if (g_mod_store.upload_category == kStoreCategoryIds[i])
            category = i - 1;
    category =
        StoreCombo("Category", category, {"Tracks", "Characters", "Vehicles", "Custom Magic Codes", "Texture packs"},
                   std::min(width, 350.F));
    g_mod_store.upload_category = kStoreCategoryIds[category + 1];
    StoreActionLine({{"Upload package", [] { StoreChooseUpload("download"); }},
                     {"Upload image", [] { StoreChooseUpload("image"); }},
                     {"Refresh files", [] { dkr::store::service().load_staff_files(); }}},
                    width);
    StoreField("hosted-search", "Find a file", std::min(width, 760.F), 160);
    g_mod_store.upload_selection = g_mod_store.upload_selection.empty() ? "hosted" : g_mod_store.upload_selection;
    const int section = StoreTabLine({"Hosted files", "Upload history"},
                                     g_mod_store.upload_selection == "hosted" ? 0 : 1, width, "file-section");
    g_mod_store.upload_selection = section ? "history" : "hosted";
    if (!store.upload.empty() && (store.uploading || StoreText(store.upload, "state", 40) != "completed")) {
        StorePanel active(width);
        StoreHeading(StoreText(store.upload, "fileName", 180), active.inner(), 18);
        StoreBadge(StoreText(store.upload, "state", 40), active.inner());
        if (store.upload.contains("offset") && store.upload.contains("size") && store.upload["offset"].is_number() &&
            store.upload["size"].is_number()) {
            const auto total = store.upload["size"].get<double>(), offset = store.upload["offset"].get<double>();
            if (total > 0)
                ImGui::ProgressBar(std::clamp(float(offset / total), 0.F, 1.F), {active.inner(), 12.F}, "");
            StoreNote(StoreSize(std::uint64_t(std::max(0., offset))) + " / " +
                          StoreSize(std::uint64_t(std::max(0., total))),
                      active.inner());
        }
        if (store.uploading && StoreButton("Pause upload"))
            dkr::store::service().pause_upload();
    }
    if (!store.upload.empty() && StoreText(store.upload, "state", 40) == "completed" &&
        store.upload.contains("asset")) {
        StorePanel completed(width);
        StoreHeading("Upload ready", completed.inner(), 18);
        StoreNote(StoreText(store.upload, "fileName", 180), completed.inner());
        if (StoreButton("Use this file", 0, true))
            StoreUseAsset(store.upload["asset"]);
    }
    std::size_t matching = 0;
    const auto& rows = section ? store.uploads : store.assets;
    if (rows.is_array())
        for (const auto& row : rows) {
            if (StoreText(row, "category", 32) != g_mod_store.upload_category ||
                !StoreMatches(StoreText(row, "fileName", 180), g_mod_store.fields["hosted-search"]))
                continue;
            if (g_mod_store.picker_upload && !g_mod_store.upload_kind.empty() &&
                StoreText(row, "kind", 32) != g_mod_store.upload_kind)
                continue;
            if (++matching > std::size_t(StoreLimit(section ? "upload-history" : "hosted-files")))
                continue;
            ImGui::PushID((StoreText(row, "id", 160) + StoreText(row, "url", 1500)).c_str());
            StorePanel card(width);
            const float inner = card.inner();
            StoreHeading(StoreText(row, "fileName", 180), inner, 18);
            const auto state = StoreText(row, "state", 40);
            StoreNote((section ? state : "Hosted file") + "  |  " + StoreText(row, "format", 40) + "  |  " +
                          StoreSize(row.value("size", std::uint64_t(0))),
                      inner);
            if (!section && StoreText(row, "kind", 32) == "image")
                StoreCover({{"image", StoreText(row, "url", 1500)}}, std::min(inner, 220.F), 110.F);
            if (!section) {
                if (StoreButton("Use in listing"))
                    StoreUseAsset(row);
            } else if (state == "open" || state == "completing") {
                if (StoreButton("Resume with original file"))
                    StoreChooseUpload(StoreText(row, "kind", 32), StoreText(row, "id", 160));
                if (StoreButton("Cancel unfinished upload")) {
                    const auto id = StoreText(row, "id", 160);
                    StoreConfirm("Cancel upload?", StoreText(row, "fileName", 180), "Cancel upload",
                                 [id] { dkr::store::service().cancel_upload(id); });
                }
            } else if (state == "completed") {
                if (row.contains("asset") && StoreButton("Use in listing"))
                    StoreUseAsset(row["asset"]);
                if (StoreButton("Recheck using original file"))
                    StoreChooseUpload(StoreText(row, "kind", 32), StoreText(row, "id", 160));
            }
            ImGui::PopID();
        }
    if (!matching)
        StoreNotice("No matching files", "Choose another category, change your search, or upload a file.", width);
    StoreMore(section ? "upload-history" : "hosted-files", matching, width);
}
void StoreSaveDraft() {
    StoreJson input = {{"draft", StoreDraft()}};
    if (!g_mod_store.editor.empty()) {
        input["id"] = g_mod_store.editor;
        input["expectedRevision"] = StoreInt(g_mod_store.draft, "revision");
    }
    StoreAction("catalogue.save", std::move(input));
}
void StoreEditor(float width, const dkr::store::Snapshot& store) {
    if (!dkr::store::staff(store.user))
        return;
    if (g_mod_store.related_open) {
        if (StoreBack("Back to listing", [] { g_mod_store.related_open = false; }))
            return;
        StoreHeading("Companion downloads", width, 22);
        StoreNote("Choose up to 10. Players install companions separately.", width);
        StoreField("related-search", "Find a mod", std::min(width, 760.F), 200);
        auto& related = g_mod_store.draft["related"];
        if (!related.is_array())
            related = StoreJson::array();
        std::size_t matching = 0;
        for (const auto& item : StoreItems(store)) {
            const auto id = StoreText(item, "id", 160);
            if (id == StoreText(g_mod_store.draft, "targetId", 160) || id == StoreText(g_mod_store.draft, "id", 160) ||
                !StoreMatches(StoreText(item, "title", 200), g_mod_store.fields["related-search"]))
                continue;
            if (++matching > std::size_t(StoreLimit("companions")))
                continue;
            bool selected = std::find(related.begin(), related.end(), StoreJson(id)) != related.end();
            ImGui::BeginDisabled(!selected && related.size() >= 10);
            if (StoreCheck(StoreText(item, "title", 200) + "###companion-" + id, selected, width)) {
                if (selected)
                    related.push_back(id);
                else
                    related.erase(std::remove(related.begin(), related.end(), StoreJson(id)), related.end());
                g_mod_store.dirty = true;
                g_mod_store.previewed = false;
            }
            ImGui::EndDisabled();
        }
        StoreMore("companions", matching, width);
        if (StoreButton("Done", 0, true))
            g_mod_store.related_open = false;
        return;
    }
    StoreHeading(g_mod_store.metadata ? "Edit listing details" : "Listing editor", width, 22);
    const std::vector<std::string> steps =
        g_mod_store.metadata ? std::vector<std::string>{"Details", "Artwork", "Review"}
                             : std::vector<std::string>{"Details", "Artwork", "Release files", "Preview & publish"};
    g_mod_store.editor_step = StoreTabLine(steps, g_mod_store.editor_step, width, "editor-steps");
    const bool preview = g_mod_store.editor_step == int(steps.size()) - 1;
    const auto draft = StoreDraft();
    StoreFormLayout layout("editor-layout", width, 780.F);
    layout.sidebar([&](float side) {
        StorePanel summary(side);
        StoreHeading("Listing preview", summary.inner(), 19);
        StoreCover(draft, summary.inner(), 150.F);
        StoreHeading(StoreText(draft, "title", 100), summary.inner(), 18);
        StoreNote("by " + StoreText(draft, "author", 100), summary.inner());
        StoreBadge(g_mod_store.dirty ? "Unsaved changes" : "Saved", summary.inner(),
                   g_mod_store.dirty ? PaddockTone::Warning : PaddockTone::On);
    });
    StorePanel form(layout.width);
    const float inner = form.inner();
    ImGui::BeginDisabled(StoreBusyAction(store));
    if (g_mod_store.editor_step == 0) {
        StoreFieldPair("draft-title", "Title", "draft-author", "Creator credit", inner);
        int category = 0;
        for (int i = 1; i < 6; ++i)
            if (g_mod_store.fields["draft-category"] == kStoreCategoryIds[i])
                category = i - 1;
        ImGui::BeginDisabled(g_mod_store.metadata);
        const int choice = StoreCombo("Category", category,
                                      {"Tracks", "Characters", "Vehicles", "Custom Magic Codes", "Texture packs"}, inner);
        ImGui::EndDisabled();
        if (choice != category) {
            g_mod_store.fields["draft-category"] = kStoreCategoryIds[choice + 1];
            g_mod_store.dirty = true;
            g_mod_store.previewed = false;
        }
        StoreField("draft-summary", "Description", inner, 4000, true);
        StoreField("draft-tags", "Tags, separated by commas", inner, 500);
        if (StoreCheck("Feature this listing", g_mod_store.featured, inner)) {
            g_mod_store.dirty = true;
            g_mod_store.previewed = false;
        }
    } else if (g_mod_store.editor_step == 1) {
        if (StoreButton("Choose or upload artwork", 0, true)) {
            g_mod_store.upload_category = g_mod_store.fields["draft-category"];
            g_mod_store.upload_kind = "image";
            g_mod_store.picker_upload = true;
            g_mod_store.staff_tab = 3;
            dkr::store::service().load_staff_files();
        }
        StoreField("draft-image", "Cover image link", inner, 1500);
        if (!g_mod_store.fields["draft-image"].empty())
            StoreCover(draft, std::min(inner, 460.F), 210.F);
        StoreNote(std::to_string(draft.value("related", StoreJson::array()).size()) + " companion downloads selected",
                  inner);
        if (StoreButton("Choose companion downloads"))
            g_mod_store.related_open = true;
    } else if (!preview) {
        if (StoreButton("Choose or upload package", 0, true)) {
            g_mod_store.upload_category = g_mod_store.fields["draft-category"];
            g_mod_store.upload_kind = "download";
            g_mod_store.picker_upload = true;
            g_mod_store.staff_tab = 3;
            dkr::store::service().load_staff_files();
        }
        StoreFieldPair("draft-version", "Release version", "draft-format", "Package format", inner, 80, 40);
        StoreField("draft-fileName", "Package filename", inner, 180);
        StoreField("draft-fileUrl", "Package download link", inner, 1500);
        StoreField("draft-requirements", "Requirements", inner, 2000, true);
        StoreField("draft-install", "Installation instructions", inner, 6000, true);
    } else {
        StoreHeading(StoreText(draft, "title", 100), inner, 23);
        StoreNote("by " + StoreText(draft, "author", 100), inner);
        StoreNote(StoreText(draft, "summary", 4000), inner, false);
        if (!g_mod_store.metadata) {
            StoreHeading("Release", inner, 18);
            StoreNote(StoreText(draft, "version", 80) + "  |  " + StoreText(draft, "fileName", 180), inner);
            StoreNote(StoreText(draft, "requirements", 2000), inner);
            StoreNote(StoreText(draft, "install", 6000), inner);
        }
        if (!g_mod_store.dirty)
            g_mod_store.previewed = true;
    }
    PaddockGap(12.F);
    if (g_mod_store.metadata) {
        if (StoreButton("Save listing details", 0, true)) {
            auto input = StoreDraft();
            input["id"] = g_mod_store.draft["id"];
            input["expectedRevision"] = StoreInt(g_mod_store.draft, "revision");
            StoreAction("catalogue.update", std::move(input));
        }
    } else {
        if (StoreButton("Save draft", 0, true))
            StoreSaveDraft();
        if (preview) {
            StoreNote(g_mod_store.dirty ? "Save the draft before publishing."
                                        : "Check the listing above before publishing.",
                      inner);
            const auto requests = store.staff.value("requests", StoreJson::array());
            const auto* request = StoreFind(requests, g_mod_store.request);
            const bool allowed =
                !g_mod_store.dirty && g_mod_store.previewed &&
                (g_mod_store.request.empty() ? !g_mod_store.editor.empty()
                                             : request && StoreText(*request, "status", 32) == "approved");
            ImGui::BeginDisabled(!allowed);
            if (StoreButton("Publish to library", 0, true)) {
                if (request) {
                    const auto input = StoreJson{{"id", g_mod_store.request},
                                                 {"expectedRevision", StoreInt(*request, "revision")},
                                                 {"draft", StoreDraft()}};
                    StoreConfirm("Publish this mod?",
                                 StoreText(draft, "title", 100) + " will become available to everyone.", "Publish",
                                 [input] { StoreAction("request.publish", input); });
                } else {
                    const auto input = StoreJson{{"id", g_mod_store.editor},
                                                 {"expectedRevision", StoreInt(g_mod_store.draft, "revision")}};
                    StoreConfirm("Publish this listing?",
                                 StoreText(draft, "title", 100) + " will become available to everyone.", "Publish",
                                 [input] { StoreAction("catalogue.publish", input); });
                }
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::EndDisabled();
    StoreActionLine(
        {{"Previous section", [steps] { g_mod_store.editor_step = std::max(0, g_mod_store.editor_step - 1); }},
         {"Next section",
          [steps] { g_mod_store.editor_step = std::min(int(steps.size()) - 1, g_mod_store.editor_step + 1); }}},
        inner);
}
void StoreStaffRequest(const StoreJson& row, float width, const dkr::store::Snapshot& store) {
    if (StoreBack("Back to request queue", [] { g_mod_store.request.clear(); }))
        return;
    const auto id = StoreText(row, "id", 160), prefix = "staff-" + id + "-";
    StoreHeading(StoreText(row, "title", 200), width, 23);
    StoreBadge(StoreText(row, "status", 40), width);
    StoreHeading("Submitted material", width, 18);
    StoreNote("Creator: " + StoreText(row, "author", 100), width);
    for (const char* field : {"summary", "sourceUrl", "downloadUrl", "requirements", "testedVersion", "screenshotUrl"})
        if (!StoreText(row, field, 4000).empty())
            StoreNote(StoreText(row, field, 4000), width, false);
    StoreHeading("History", width, 18);
    if (row.contains("events"))
        for (const auto& event : row["events"]) {
            StoreNote(StoreText(event, "label", 200) + "  |  " + StoreDate(event, "createdAt"), width, false);
            StoreNote(StoreText(event, "message", 3000), width);
        }
    {
        StorePanel notes(std::min(width, 860.F));
        const float inner = notes.inner();
        ImGui::BeginDisabled(StoreBusyAction(store));
        StoreHeading("Private staff notes", inner, 18);
        StoreNote("Only the library team can see these notes.", inner);
        if (row.contains("notes"))
            for (const auto& note : row["notes"]) {
                StoreNote(StoreText(note, "actorName", 100) + "  |  " + StoreDate(note, "createdAt"), inner, false);
                StoreNote(StoreText(note, "text", 3000).empty() ? StoreText(note, "message", 3000)
                                                                : StoreText(note, "text", 3000),
                          inner);
            }
        StoreField((prefix + "private").c_str(), "Add a private note", inner, 1600, true);
        ImGui::BeginDisabled(g_mod_store.fields[prefix + "private"].empty());
        if (StoreButton("Save private note"))
            StoreAction("request.note", {{"id", id},
                                         {"expectedRevision", StoreInt(row, "revision")},
                                         {"value", g_mod_store.fields[prefix + "private"]}});
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    StorePanel decision(std::min(width, 860.F));
    const float inner = decision.inner();
    StoreHeading("Review decision", inner, 18);
    ImGui::BeginDisabled(StoreBusyAction(store));
    const auto state = StoreText(row, "status", 40);
    if (state == "pending") {
        if (StoreButton("Start review", 0, true))
            StoreAction("request.start", {{"id", id}, {"expectedRevision", StoreInt(row, "revision")}});
    } else if (state == "review") {
        StoreHeading("Review checks", inner, 18);
        bool all = true;
        StoreJson checks = StoreJson::object();
        const char* keys[] = {"source", "files", "requirements", "credits"};
        const char* labels[] = {"Source and creator checked", "Package contents checked", "Requirements checked",
                                "Credits checked"};
        for (int i = 0; i < 4; ++i) {
            const auto key = prefix + keys[i];
            if (!g_mod_store.fields.contains(key)) {
                const auto found = row.find("checks");
                g_mod_store.fields[key] =
                    found != row.end() && found->is_object() && found->value(keys[i], false) ? "1" : "0";
            }
            bool checked = g_mod_store.fields[key] == "1";
            if (StoreCheck(labels[i], checked, inner))
                g_mod_store.fields[key] = checked ? "1" : "0";
            checks[keys[i]] = checked;
            all = all && checked;
        }
        StoreField((prefix + "decision").c_str(), "Message to requester (not private)", inner, 2400, true);
        const auto message = g_mod_store.fields[prefix + "decision"];
        const auto decide = [id, row, checks, message](std::string decision) {
            const StoreJson input{{"id", id},
                                  {"expectedRevision", StoreInt(row, "revision")},
                                  {"checks", checks},
                                  {"decision", decision},
                                  {"message", message}};
            StoreConfirm("Confirm request decision", StoreText(row, "title", 200) + "\n" + message, "Send decision",
                         [input] { StoreAction("request.decide", input); });
        };
        ImGui::BeginDisabled(!all);
        if (StoreButton("Approve", 0, true))
            decide("approve");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(message.empty());
        StoreActionLine(
            {{"Ask for changes", [decide] { decide("changes"); }}, {"Reject", [decide] { decide("reject"); }}}, inner);
        ImGui::EndDisabled();
        if (message.empty())
            StoreNote("Add a message before asking for changes or rejecting.", inner);
    } else if (state == "approved" && StoreButton("Prepare listing", 0, true)) {
        StoreLoadDraft(row);
        g_mod_store.request = id;
        g_mod_store.fields["draft-fileUrl"] = StoreText(row, "downloadUrl", 1500);
        g_mod_store.fields["draft-image"] = StoreText(row, "screenshotUrl", 1500);
        if (StoreText(row, "relation", 32) == "update") {
            const auto target = StoreText(row, "targetId", 160);
            g_mod_store.draft["target"] = "existing";
            g_mod_store.draft["targetId"] = target;
            if (const auto* item = StoreFind(StoreItems(store), target))
                g_mod_store.draft["targetRevision"] = StoreInt(*item, "revision");
        }
        g_mod_store.staff_tab = 2;
    }
    ImGui::EndDisabled();
}
void StoreManagement(float width, const dkr::store::Snapshot& store) {
    if (!dkr::store::staff(store.user))
        return;
    StoreHeading("Manage library", width);
    std::vector<std::string> tabs{"Requests", "Reports", "Listings", "Uploads"};
    if (dkr::store::admin(store.user)) {
        tabs.push_back("Activity");
        tabs.push_back("Staff access");
    }
    const auto chosen = StoreTabLine(tabs, g_mod_store.staff_tab, width, "staff-tabs");
    if (chosen != g_mod_store.staff_tab)
        StoreNavigate([chosen] {
            g_mod_store.staff_tab = chosen;
            if (chosen == 3) {
                g_mod_store.picker_upload = false;
                dkr::store::service().load_staff_files();
            }
        });
    if (g_mod_store.staff_tab == 0) {
        const auto requests = store.staff.value("requests", StoreJson::array());
        if (!g_mod_store.request.empty()) {
            if (const auto* row = StoreFind(requests, g_mod_store.request))
                StoreStaffRequest(*row, width, store);
            else {
                StoreNote("This request is no longer available.", width);
                if (StoreButton("Back to queue"))
                    g_mod_store.request.clear();
            }
            return;
        }
        StoreField("staff-request-search", "Find a request", std::min(width, 760.F), 200);
        g_mod_store.staff_filter =
            StoreCombo("Status", g_mod_store.staff_filter,
                       {"All", "Pending", "In review", "Changes requested", "Approved", "Rejected", "Published"},
                       std::min(width, 350.F));
        const char* states[] = {"", "pending", "review", "changes", "approved", "rejected", "published"};
        std::size_t matching = 0;
        for (const auto& row : requests) {
            if (!StoreMatches(StoreText(row, "title", 200), g_mod_store.fields["staff-request-search"]) ||
                (g_mod_store.staff_filter && StoreText(row, "status", 40) != states[g_mod_store.staff_filter]))
                continue;
            if (++matching > std::size_t(StoreLimit("staff-requests")))
                continue;
            if (StoreRow(StoreText(row, "id", 160), StoreJson::object(), StoreText(row, "title", 200),
                         StoreText(row, "status", 40) + "  |  " + StoreText(row, "author", 100), "Review", width))
                g_mod_store.request = StoreText(row, "id", 160);
        }
        if (!matching)
            StoreNotice("No matching requests", "New submissions appear in this queue.", width);
        StoreMore("staff-requests", matching, width);
    } else if (g_mod_store.staff_tab == 1) {
        const auto reports = store.staff.value("reports", StoreJson::array());
        if (!g_mod_store.staff_report.empty()) {
            if (StoreBack("Back to reports", [] { g_mod_store.staff_report.clear(); }))
                return;
            const auto* report = StoreFind(reports, g_mod_store.staff_report);
            if (!report) {
                StoreNote("This report is no longer available.", width);
                return;
            }
            // Own the review value before drawing or capturing a decision.
            StoreJson review_copy;
            const auto review_rows = store.staff.value("reviews", StoreJson::array());
            const auto* review = StoreFind(review_rows, StoreText(*report, "reviewId", 160));
            if (review)
                review_copy = *review;
            StoreHeading("Reported review", width, 21);
            StoreNote(StoreText(*report, "reason", 1600), width, false);
            if (!review_copy.is_null()) {
                StoreNote(StoreText(review_copy, "displayName", 100), width);
                StoreNote(StoreText(review_copy, "text", 4000), width, false);
            }
            const auto key = "report-decision-" + g_mod_store.staff_report;
            ImGui::BeginDisabled(StoreBusyAction(store));
            StoreField(key.c_str(), "Decision reason", std::min(width, 820.F), 1600, true);
            for (const auto& [label, action] : std::vector<std::pair<std::string, std::string>>{
                     {"Keep review", "keep"}, {"Hide review", "hide"}, {"Restore review", "restore"}}) {
                if (StoreButton(label)) {
                    const StoreJson input{{"reportId", (*report)["id"]},
                                          {"expectedRevision", StoreInt(*report, "revision")},
                                          {"expectedReviewRevision", StoreInt(review_copy, "revision")},
                                          {"action", action},
                                          {"reason", g_mod_store.fields[key]}};
                    StoreConfirm(label + "?",
                                 StoreText(review_copy, "displayName", 100) + "\n" + g_mod_store.fields[key], label,
                                 [input] { StoreAction("report.resolve", input); });
                }
            }
            ImGui::EndDisabled();
            return;
        }
        StoreField("staff-report-search", "Find a report", std::min(width, 760.F), 200);
        std::size_t matching = 0;
        for (const auto& row : reports) {
            if (!StoreMatches(StoreText(row, "reason", 1600), g_mod_store.fields["staff-report-search"]))
                continue;
            if (++matching > std::size_t(StoreLimit("reports")))
                continue;
            if (StoreRow(StoreText(row, "id", 160), StoreJson::object(), "Reported review",
                         StoreText(row, "reason", 1600), "Review report", width))
                g_mod_store.staff_report = StoreText(row, "id", 160);
        }
        if (!matching)
            StoreNote("No reports match your search.", width);
        StoreMore("reports", matching, width);
    } else if (g_mod_store.staff_tab == 2) {
        if (g_mod_store.editing) {
            if (StoreBack(
                    "Back to listings",
                    [] {
                        g_mod_store.editing = false;
                        g_mod_store.request.clear();
                    },
                    true))
                return;
            StoreEditor(width, store);
            return;
        }
        if (StoreButton("New listing", 0, true)) {
            g_mod_store.request.clear();
            StoreLoadDraft(StoreJson::object());
        }
        StoreField("staff-listing-search", "Find a listing or draft", std::min(width, 760.F), 200);
        const auto query = g_mod_store.fields["staff-listing-search"];
        const auto drafts = store.admin.value("drafts", StoreJson::array());
        std::size_t draft_count = 0;
        for (const auto& row : drafts) {
            if (StoreText(row, "status", 40) != "draft" || !StoreMatches(StoreText(row, "title", 200), query))
                continue;
            if (++draft_count > std::size_t(StoreLimit("staff-drafts")))
                continue;
            if (StoreRow(StoreText(row, "id", 160), row, StoreText(row, "title", 200), "Unpublished draft", "Continue",
                         width)) {
                g_mod_store.request.clear();
                StoreLoadDraft(row.contains("draft") ? row["draft"] : row);
                g_mod_store.editor = StoreText(row, "id", 160);
                g_mod_store.draft["revision"] = StoreInt(row, "revision");
            }
        }
        if (draft_count)
            StoreMore("staff-drafts", draft_count, width);
        auto listings = store.admin.value("items", StoreJson::array());
        if (listings.empty())
            listings = StoreItems(store);
        std::size_t matching = 0;
        for (const auto& row : listings) {
            if (!StoreMatches(StoreText(row, "title", 200), query))
                continue;
            if (++matching > std::size_t(StoreLimit("staff-listings")))
                continue;
            ImGui::PushID(StoreText(row, "id", 160).c_str());
            StorePanel panel(width);
            StoreHeading(StoreText(row, "title", 200), panel.inner(), 18);
            StoreNote(StoreCategory(row) + "  |  " + StoreText(row, "version", 80) + "  |  " +
                          (row.value("archived", false) ? "Archived" : "Published"),
                      panel.inner());
            StoreActionLine({{"Edit details",
                              [row] {
                                  g_mod_store.request.clear();
                                  StoreLoadDraft(row);
                                  g_mod_store.editor.clear();
                                  g_mod_store.metadata = true;
                              }},
                             {"Add release",
                              [row] {
                                  g_mod_store.request.clear();
                                  StoreLoadDraft(row);
                                  g_mod_store.editor.clear();
                                  g_mod_store.draft["target"] = "existing";
                                  g_mod_store.draft["targetId"] = row["id"];
                                  g_mod_store.draft["targetRevision"] = StoreInt(row, "revision");
                              }}},
                            panel.inner());
            if (dkr::store::admin(store.user) &&
                StoreButton(row.value("archived", false) ? "Restore listing" : "Archive listing")) {
                const bool archived = row.value("archived", false);
                StoreConfirm(archived ? "Restore this listing?" : "Archive this listing?",
                             StoreText(row, "title", 200) + "\nInstalled copies are kept.",
                             archived ? "Restore" : "Archive", [row, archived] {
                                 StoreAction("catalogue.archive", {{"id", row["id"]},
                                                                   {"archived", !archived},
                                                                   {"expectedRevision", StoreInt(row, "revision")}});
                             });
            }
            ImGui::PopID();
        }
        StoreMore("staff-listings", matching, width);
        if (!matching && !draft_count)
            StoreNote("No listings or drafts match your search.", width);
    } else if (g_mod_store.staff_tab == 3)
        StoreUploads(width, store);
    else if (g_mod_store.staff_tab == 4 && dkr::store::admin(store.user)) {
        StoreField("audit-search", "Find activity", std::min(width, 760.F), 200);
        auto rows = store.admin.value("audit", StoreJson::array());
        std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            return StoreText(a, "createdAt", 80) > StoreText(b, "createdAt", 80);
        });
        std::size_t matching = 0;
        for (const auto& row : rows) {
            if (!StoreMatches(StoreText(row, "action", 200) + " " + StoreText(row, "message", 2000),
                              g_mod_store.fields["audit-search"]))
                continue;
            if (++matching > std::size_t(StoreLimit("audit")))
                continue;
            StorePanel card(width);
            StoreHeading(StoreText(row, "action", 200), card.inner(), 18);
            StoreNote(StoreText(row, "message", 2000), card.inner());
            StoreNote(StoreDate(row, "createdAt") + "  |  " + StoreText(row, "actorName", 100), card.inner());
        }
        if (!matching)
            StoreNote("No activity matches your search.", width);
        StoreMore("audit", matching, width);
    } else if (dkr::store::admin(store.user)) {
        StoreField("staff-access-search", "Find an account", std::min(width, 760.F), 200);
        std::size_t matching = 0;
        for (const auto& row : store.admin.value("accounts", StoreJson::array())) {
            if (!StoreMatches(StoreText(row, "displayName", 100), g_mod_store.fields["staff-access-search"]))
                continue;
            if (++matching > std::size_t(StoreLimit("staff-access")))
                continue;
            const auto id = StoreText(row, "id", 160), key = "staff-role-" + id;
            ImGui::PushID(id.c_str());
            StorePanel panel(width);
            StoreHeading(StoreText(row, "displayName", 100), panel.inner(), 18);
            const auto current = StoreText(row, "role", 40);
            StoreNote("Current role: " + current, panel.inner());
            if (!g_mod_store.fields.contains(key))
                g_mod_store.fields[key] = current;
            int role = g_mod_store.fields[key] == "admin" ? 2 : g_mod_store.fields[key] == "moderator" ? 1 : 0;
            role = StoreCombo("Role", role, {"Player", "Moderator", "Administrator"}, std::min(panel.inner(), 340.F));
            const char* roles[] = {"player", "moderator", "admin"};
            g_mod_store.fields[key] = roles[role];
            ImGui::BeginDisabled(StoreBusyAction(store) || current == roles[role]);
            if (StoreButton("Save role")) {
                const std::string new_role = roles[role];
                StoreConfirm("Change staff access?",
                             StoreText(row, "displayName", 100) + "\n" + current + " -> " + new_role, "Change role",
                             [id, new_role] { StoreAction("admin.role", {{"accountId", id}, {"role", new_role}}); });
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (!matching)
            StoreNote("No accounts match your search.", width);
        StoreMore("staff-access", matching, width);
    }
}
