// Shared by launcher and in-game store. ImGui text is always unformatted;
// network work stays on the existing account worker.
struct StoreDiscussionUi {
    std::string account, item, editing, report, operation, signature;
    std::string compose_draft, edit_draft, error;
    std::vector<std::string> cursors{std::string{}};
    std::size_t page = 0;
    int reason = 0;
    std::uint64_t mutation = 0;
} g_store_discussion;
std::map<std::string, StoreDiscussionUi> g_store_discussion_drafts;
void StoreCommunityCounts(const StoreJson& row, float width) {
    StoreNote(
        dkr::localisation::format("library.counts", {{"downloads", dkr::store::community::count(row, "downloadCount")},
                                                     {"comments", dkr::store::community::count(row, "commentCount")}}),
        width);
}
void StoreCommentLoad(const std::string& id, const std::string& cursor = {}) {
    dkr::store::service().action("community.load", {{"itemId", id}, {"cursor", cursor}});
}
void StoreCommentAction(const std::string& action, StoreJson input) {
    input["itemId"] = g_store_discussion.item;
    const auto signature = action + ":" + input.dump();
    if (signature != g_store_discussion.signature) {
        try {
            g_store_discussion.operation = dkr::store::community::operation_id();
        } catch (const std::exception&) {
            g_store_discussion.error = "The comment form could not start securely. Try again.";
            return;
        }
        g_store_discussion.signature = signature;
        g_store_discussion.error.clear();
    }
    input["operationId"] = g_store_discussion.operation;
    dkr::store::service().action(action, std::move(input));
}
void StoreComments(const StoreJson& item, float width, const dkr::store::Snapshot& store) {
    const auto id = StoreText(item, "id", 96), account = StoreText(store.user, "id", 160);
    auto& ui = g_store_discussion;
    if (ui.account != account || ui.item != id) {
        if (ui.account != account)
            g_store_discussion_drafts.clear();
        else if (!ui.item.empty()) {
            ui.compose_draft = g_mod_store.fields["comment-compose"];
            ui.edit_draft = g_mod_store.fields["comment-edit"];
            if (g_store_discussion_drafts.size() >= 32 && !g_store_discussion_drafts.contains(ui.item))
                g_store_discussion_drafts.erase(g_store_discussion_drafts.begin());
            g_store_discussion_drafts[ui.item] = ui;
        }
        const auto prior = g_store_discussion_drafts.find(id);
        if (prior != g_store_discussion_drafts.end())
            ui = prior->second;
        else {
            ui = {};
            ui.account = account;
            ui.item = id;
            ui.mutation = store.mutation;
        }
        g_mod_store.fields["comment-compose"] = ui.compose_draft;
        g_mod_store.fields["comment-edit"] = ui.edit_draft;
        StoreCommentLoad(id, ui.cursors[ui.page]);
    }
    if (ui.mutation != store.mutation) {
        ui.mutation = store.mutation;
        if (store.last_action.starts_with("community.") &&
            StoreText(store.last_result, "operationId", 32) == ui.operation &&
            StoreText(store.last_result, "itemId", 96) == id) {
            if (store.last_action == "community.create" &&
                g_mod_store.fields["comment-compose"] == StoreText(store.last_result, "submittedText", 8000))
                g_mod_store.fields["comment-compose"].clear();
            ui.editing.clear();
            ui.report.clear();
            ui.operation.clear();
            ui.signature.clear();
            ui.cursors = {std::string{}};
            ui.page = 0;
        }
    }
    const StoreJson* page = nullptr;
    if (store.reviews.contains(dkr::store::community::SnapshotKey)) {
        const auto& value = store.reviews[dkr::store::community::SnapshotKey];
        if (value.is_object() && StoreText(value, "itemId", 96) == id)
            page = &value;
    }
    const bool loading = page && page->value("loading", false);
    StoreHeading("Comments", width, 21);
    if (!ui.error.empty())
        StoreNote(ui.error, width, false);
    StoreNote("Talk about this mod. Star ratings and reviews are kept separate.", width);
    ImGui::BeginDisabled(loading || store.writing);
    if (StoreButton("Refresh comments")) {
        ui.cursors = {std::string{}};
        ui.page = 0;
        StoreCommentLoad(id);
    }
    ImGui::EndDisabled();
    if (page) {
        const auto error = StoreText(*page, "error", 1000);
        if (!error.empty())
            StoreNote(error, width, false);
        if (page->contains("total"))
            StoreNote(dkr::store::community::count(*page, "total") + " comments", width);
        if (page->contains("comments") && (*page)["comments"].is_array()) {
            if ((*page)["comments"].empty() && !loading && error.empty())
                StoreNote("No comments yet. Be the first!", width);
            for (const auto& comment : (*page)["comments"]) {
                const auto comment_id = StoreText(comment, "id", 96);
                ImGui::PushID(comment_id.c_str());
                {
                    StorePanel card(width);
                    const auto inner = card.inner();
                    StoreNote(StoreText(comment, "displayName", 400) + "  |  " +
                                  StoreText(comment, "createdAt", 40).substr(0, 10),
                              inner, false);
                    StoreNote(StoreText(comment, "text", 8000), inner, false);
                    ImGui::BeginDisabled(loading || store.writing);
                    if (comment.value("isOwn", false)) {
                        if (StoreButton("Edit")) {
                            ui.editing = comment_id;
                            g_mod_store.fields["comment-edit"] = StoreText(comment, "text", 8000);
                        }
                        ImGui::SameLine();
                        if (StoreButton("Delete"))
                            ui.editing = "delete:" + comment_id;
                    } else if (!store.user.is_null() && StoreButton("Report"))
                        ui.report = comment_id;
                    if (ui.editing == comment_id) {
                        StoreField("comment-edit", "Edit comment", inner, 2000, true);
                        if (StoreButton("Save changes", inner, true))
                            StoreCommentAction("community.edit", {{"commentId", comment_id},
                                                                  {"text", g_mod_store.fields["comment-edit"]},
                                                                  {"expectedRevision", comment["revision"]}});
                        if (StoreButton("Cancel edit", inner))
                            ui.editing.clear();
                    }
                    if (ui.editing == "delete:" + comment_id) {
                        StoreNote("Delete this comment?", inner, false);
                        if (StoreButton("Delete comment", inner, true))
                            StoreCommentAction("community.delete",
                                               {{"commentId", comment_id}, {"expectedRevision", comment["revision"]}});
                        if (StoreButton("Keep comment", inner))
                            ui.editing.clear();
                    }
                    if (ui.report == comment_id) {
                        const int reason = StoreCombo("Report reason", ui.reason,
                                                      {"Spam or advertising", "Harassment or abusive language",
                                                       "Off-topic or misleading content"},
                                                      inner);
                        ui.reason = reason;
                        constexpr const char* reasons[] = {"Spam or advertising", "Harassment or abusive language",
                                                           "Off-topic or misleading content"};
                        if (StoreButton("Send report", inner, true))
                            StoreCommentAction("community.report", {{"commentId", comment_id},
                                                                    {"reason", reasons[std::clamp(reason, 0, 2)]}});
                        if (StoreButton("Cancel report", inner))
                            ui.report.clear();
                    }
                    ImGui::EndDisabled();
                }
                ImGui::PopID();
                PaddockGap(9.F);
            }
        }
        ImGui::BeginDisabled(loading || store.writing);
        if (ui.page && StoreButton("Previous comments")) {
            --ui.page;
            StoreCommentLoad(id, ui.cursors[ui.page]);
        }
        const auto next = StoreText(*page, "nextCursor", 220);
        if (!next.empty() && ui.page < 200 && StoreText(*page, "cursor", 220) == ui.cursors[ui.page] &&
            StoreButton("Next comments")) {
            ui.cursors.resize(ui.page + 1);
            ui.cursors.push_back(next);
            ++ui.page;
            StoreCommentLoad(id, next);
        }
        ImGui::EndDisabled();
    }
    if (loading)
        StoreNote("Loading comments...", width);
    PaddockGap(12.F);
    if (store.user.is_null())
        StoreNote("Sign in with Discord in Your account to join the discussion.", width);
    else {
        StoreField("comment-compose", "Your comment", width, 2000, true);
        ImGui::BeginDisabled(loading || store.writing || g_mod_store.fields["comment-compose"].empty());
        if (StoreButton("Post comment", width, true))
            StoreCommentAction("community.create", {{"text", g_mod_store.fields["comment-compose"]}});
        ImGui::EndDisabled();
    }
}
