void StoreReviewForm(const StoreJson& row, float width, const dkr::store::Snapshot& store) {
    const auto item = StoreText(row, "id", 160), prefix = "review-" + item;
    if (store.user.is_null()) {
        StoreNote("Sign in with Discord to leave a review.", width);
        if (StoreButton("Sign in with Discord"))
            dkr::store::service().login();
        return;
    }
    if (!g_mod_store.fields.contains(prefix + "-text")) {
        if (StoreBusyAction(store) && !store.personal.contains("reviews")) {
            StoreNote("Loading your review...", width);
            return;
        }
        g_mod_store.fields[prefix + "-text"] = "";
        g_mod_store.fields[prefix + "-version"] = StoreText(row, "version", 80);
        g_mod_store.fields[prefix + "-rating"] = "5";
        for (const auto& own : store.personal.value("reviews", StoreJson::array()))
            if (StoreText(own, "itemId", 160) == item) {
                g_mod_store.fields[prefix + "-text"] = StoreText(own, "text", 4000);
                g_mod_store.fields[prefix + "-version"] = StoreText(own, "version", 80);
                g_mod_store.fields[prefix + "-rating"] = std::to_string(std::clamp(StoreInt(own, "score", 5), 1, 5));
            }
    }
    StorePanel form(std::min(width, 820.F));
    const auto inner = form.inner();
    StoreHeading("Your review", inner, 20);
    ImGui::PushID(prefix.c_str());
    ImGui::BeginDisabled(StoreBusyAction(store));
    auto& rating = g_mod_store.fields[prefix + "-rating"];
    StoreNote("Rating", inner);
    const int selected = rating.empty() ? 4 : std::clamp(rating[0] - '1', 0, 4);
    rating = std::to_string(
        StoreTabLine({"1 star", "2 stars", "3 stars", "4 stars", "5 stars"}, selected, inner, "review-rating") + 1);
    StoreField((prefix + "-text").c_str(), "Review", inner, 4000, true, true);
    StoreField((prefix + "-version").c_str(), "Version played", std::min(inner, 340.F), 80, false, true);
    if (StoreButton("Save review", 0, true)) {
        StoreAction("review.save", {{"itemId", item},
                                    {"score", std::clamp(rating[0] - '0', 1, 5)},
                                    {"text", g_mod_store.fields[prefix + "-text"]},
                                    {"version", g_mod_store.fields[prefix + "-version"]}});
        g_mod_store.review_editing = false;
    }
    ImGui::EndDisabled();
    if (StoreButton("Keep for later"))
        g_mod_store.review_editing = false;
    ImGui::PopID();
}

void StoreDetails(const StoreJson& row, float width, const dkr::store::Snapshot& store, bool inspect) {
    if (!inspect && StoreBack("Back to browse", [] { g_mod_store.detail = false; }))
        return;
    const auto id = StoreText(row, "id", 160);
    const float outer = width;
    const bool hero =
        !inspect && width >= 840.F && ImGui::BeginTable("detail-hero", 2, ImGuiTableFlags_NoSavedSettings, {width, 0});
    if (hero) {
        ImGui::TableSetupColumn("Art", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableSetupColumn("Release", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        StoreCover(row, ImGui::GetContentRegionAvail().x, 220.F);
        ImGui::TableNextColumn();
    } else
        StoreCover(row, width, std::min(190.F, width * .45F));
    {
        StorePanel summary(hero ? ImGui::GetContentRegionAvail().x : width);
        const float inner = summary.inner();
        StoreHeading(StoreText(row, "title", 200), inner, 23);
        StoreNote("by " + StoreText(row, "author", 200) + "  |  " + StoreCategory(row), inner);
        StoreNote(StoreRating(row), inner);
        StoreNote("Version " + StoreText(row, "version", 80) + "  |  " + StoreDate(row), inner);
        StoreDownloadButton(row, StoreCurrentRelease(row), inner);
    }
    if (hero)
        ImGui::EndTable();
    width = std::min(1120.F, outer);
    g_mod_store.detail_tab =
        StoreTabLine({"Overview", "Reviews", "Install help", "Releases"}, g_mod_store.detail_tab, width, "mod-details");
    if (g_mod_store.detail_tab == 0) {
        const auto description = StoreText(row, "description", 16000);
        StoreNote(description.empty() ? StoreText(row, "summary", 4000) : description, width, false);
        const auto requirements = StoreText(row, "requirements", 2000);
        if (!requirements.empty()) {
            StoreHeading("Requirements", width, 18);
            StoreNote(requirements, width);
        }
        if (row.contains("gallery") && row["gallery"].is_array() && !row["gallery"].empty()) {
            StoreHeading("Screenshots", width, 18);
            const int columns = std::max(1, std::min(4, int(width / 180.F)));
            const float thumb = std::min(230.F, (width - (columns - 1) * 12.F) / columns);
            if (ImGui::BeginTable("gallery-thumbnails", columns, ImGuiTableFlags_NoSavedSettings, {width, 0})) {
                int shown = 0;
                for (std::size_t i = 0; i < row["gallery"].size() && shown < columns; ++i) {
                    if (!row["gallery"][i].is_string())
                        continue;
                    ++shown;
                    ImGui::TableNextColumn();
                    ImGui::PushID(int(i));
                    StoreCover({{"image", row["gallery"][i]}}, thumb, thumb * .56F);
                    if (StoreButton("View image", thumb)) {
                        g_mod_store.gallery = StoreJson::array();
                        for (const auto& image : row["gallery"])
                            if (image.is_string())
                                g_mod_store.gallery.push_back(image);
                        g_mod_store.gallery_index = shown - 1;
                        g_mod_store.gallery_title = StoreText(row, "title", 200);
                        StoreOpenPopup("Mod gallery");
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            StoreNote(std::to_string(row["gallery"].size()) + " screenshots - open the gallery to see all.", width);
        }
        if (row.contains("related") && row["related"].is_array())
            for (const auto& related : row["related"])
                if (related.is_string())
                    if (const auto* companion = StoreFind(StoreItems(store), related.get<std::string>()))
                        if (StoreRow(StoreText(*companion, "id", 160), *companion, StoreText(*companion, "title", 200),
                                     "Companion download - installed separately", "View mod", width))
                            StoreOpenMod(*companion, !inspect);
    } else if (g_mod_store.detail_tab == 1) {
        StoreActionLine({{"Refresh reviews", [id] { dkr::store::service().reviews(id); }},
                         {store.user.is_null() ? "Sign in to review" : "Write / edit review",
                          [&store] {
                              if (store.user.is_null())
                                  dkr::store::service().login();
                              else
                                  g_mod_store.review_editing = true;
                          }}},
                        width);
        if (g_mod_store.review_editing) {
            StoreReviewForm(row, width, store);
            return;
        }
        const auto reviews = store.reviews.contains(id) ? store.reviews[id] : StoreJson::array();
        if (reviews.empty())
            StoreNote("No reviews yet.", width);
        int shown = 0;
        for (const auto& review : reviews) {
            if (++shown > StoreLimit("reviews-" + id))
                break;
            ImGui::PushID(StoreText(review, "id", 160).c_str());
            StorePanel panel(width);
            StoreHeading(StoreText(review, "displayName", 100) + "  |  " + std::to_string(StoreInt(review, "score")) +
                             " / 5",
                         panel.inner(), 18);
            StoreNote(StoreText(review, "text", 4000), panel.inner(), false);
            StoreNote(StoreDate(review, "createdAt") + "  |  Version " + StoreText(review, "version", 80),
                      panel.inner());
            if (!store.user.is_null() && StoreButton("Report")) {
                g_mod_store.report = StoreText(review, "id", 160);
                StoreOpenPopup("Report a review");
            }
            ImGui::PopID();
        }
        StoreMore("reviews-" + id, reviews.size(), width);
    } else if (g_mod_store.detail_tab == 2) {
        const auto install = StoreText(row, "install", 6000);
        StoreNote(install.empty() ? "Choose Download & install. DKR-R handles the package for you." : install, width);
        StoreNote("Code mods still need your permission. Patch imports may need your original ROM.", width);
    } else if (row.contains("releases") && row["releases"].is_array()) {
        int shown = 0;
        const auto current = StoreText(StoreCurrentRelease(row), "id", 160);
        for (const auto& release : row["releases"]) {
            if (++shown > StoreLimit("releases-" + id))
                break;
            ImGui::PushID(StoreText(release, "id", 160).c_str());
            StorePanel panel(width);
            StoreHeading("Version " + StoreText(release, "version", 80), panel.inner(), 18);
            StoreBadge(StoreText(release, "id", 160) == current ? "Current release" : "Older / alternate release",
                       panel.inner());
            StoreNote(StoreDate(release, "createdAt") + "  |  " + StoreText(release, "format", 40) + "  |  " +
                          StoreSize(release.value("size", std::uint64_t(0))),
                      panel.inner());
            if (StoreButton("Release information")) {
                g_mod_store.release_details = release;
                g_mod_store.release_title = StoreText(row, "title", 200);
                StoreOpenPopup("Release information");
            }
            StoreDownloadButton(row, release, std::min(panel.inner(), 300.F));
            ImGui::PopID();
        }
        StoreMore("releases-" + id, row["releases"].size(), width);
    }
}
