void StorePolishModals() {
    if (!g_mod_store.popup_request.empty()) {
        ImGui::OpenPopup(g_mod_store.popup_request.c_str());
        g_mod_store.popup_request.clear();
    }
    if (StoreBeginManagerModal("Report a review", 720.F, 440.F)) {
        const float width = ImGui::GetContentRegionAvail().x;
        StoreModalBody("Report form", width, {"Send report", "Cancel"});
        StoreHeading("Report this review", ImGui::GetContentRegionAvail().x, 20);
        const auto key = "report-" + g_mod_store.report;
        StoreField(key.c_str(), "Reason", ImGui::GetContentRegionAvail().x, 1600, true);
        ImGui::EndChild();
        const auto store = dkr::store::service().view();
        StoreFooter(
            "Send report",
            store->user.is_null() || StoreBusyAction(*store) || g_mod_store.report.empty() ||
                g_mod_store.fields[key].empty(),
            [&] {
                StoreAction("review.report", {{"reviewId", g_mod_store.report}, {"reason", g_mod_store.fields[key]}});
                ImGui::CloseCurrentPopup();
            },
            width);
        ImGui::EndPopup();
    }
    if (StoreBeginManagerModal("Delete your library account?", 680.F, 440.F)) {
        const float width = ImGui::GetContentRegionAvail().x;
        StoreModalBody("Account deletion", width, {"Delete account", "Cancel"});
        StoreHeading("Delete your library account?", ImGui::GetContentRegionAvail().x, 20);
        StoreNote("Your library account and personal data will be deleted. Installed mods and your Discord account are "
                  "kept. Type DELETE to confirm.",
                  ImGui::GetContentRegionAvail().x);
        StoreField("account-delete", "Confirmation", ImGui::GetContentRegionAvail().x, 6);
        ImGui::EndChild();
        const auto store = dkr::store::service().view();
        StoreFooter(
            "Delete account",
            store->user.is_null() || StoreBusyAction(*store) || g_mod_store.fields["account-delete"] != "DELETE",
            [] {
                StoreAction("account.delete", {{"confirmation", "DELETE"}});
                ImGui::CloseCurrentPopup();
            },
            width);
        ImGui::EndPopup();
    }
    if (StoreBeginManagerModal("Confirm Store action", 680.F, 380.F)) {
        const float width = ImGui::GetContentRegionAvail().x;
        StoreModalBody("Action summary", width, {g_mod_store.confirm_label.c_str(), "Cancel"});
        StoreHeading(g_mod_store.confirm_title, ImGui::GetContentRegionAvail().x, 21);
        StoreNote(g_mod_store.confirm_message, ImGui::GetContentRegionAvail().x, false);
        ImGui::EndChild();
        StoreFooter(
            g_mod_store.confirm_label.empty() ? "Confirm" : g_mod_store.confirm_label,
            !g_mod_store.confirmation || g_mod_store.confirm_identity != g_mod_store.identity ||
                StoreBusyAction(*dkr::store::service().view()),
            [] {
                auto action = std::move(g_mod_store.confirmation);
                ImGui::CloseCurrentPopup();
                if (action)
                    action();
            },
            width);
        ImGui::EndPopup();
    }
    if (StoreBeginManagerModal("Use older mod release?", 700.F, 380.F)) {
        const float width = ImGui::GetContentRegionAvail().x;
        StoreModalBody("Older release", width, {"Use this release", "Cancel"});
        StoreHeading("Use the publisher's older release?", ImGui::GetContentRegionAvail().x, 20);
        StoreNote("This replaces your selected installation with the older release. Bulk updates never do this "
                  "automatically.",
                  ImGui::GetContentRegionAvail().x);
        ImGui::EndChild();
        const auto view = dkr::store::installations().view();
        const StoreJson* update = nullptr;
        for (const auto& row : view->updates)
            if (StoreText(row, "instance", 32) == g_mod_store.update_selected &&
                row["release"] == g_mod_store.release_details)
                update = &row;
        StoreFooter(
            "Use this release", !update || view->busy || dkr::runtime::netplay::session().active(),
            [&] {
                StoreQueueRelease((*update)["item"], (*update)["release"], "update", g_mod_store.update_selected);
                ImGui::CloseCurrentPopup();
            },
            width);
        ImGui::EndPopup();
    }
    if (StoreBeginManagerModal("Leave unsaved listing?", 640.F, 300.F)) {
        const float width = ImGui::GetContentRegionAvail().x;
        StoreModalBody("Unsaved listing", width, {"Keep editing", "Discard changes"});
        StoreHeading("Keep your changes?", ImGui::GetContentRegionAvail().x);
        StoreNote("This listing has unsaved changes. Keep editing, or discard them before leaving.",
                  ImGui::GetContentRegionAvail().x);
        ImGui::EndChild();
        StoreActionLine({{"Keep editing",
                          [] {
                              g_mod_store.pending_navigation = {};
                              ImGui::CloseCurrentPopup();
                          }},
                         {"Discard changes",
                          [] {
                              auto action = std::move(g_mod_store.pending_navigation);
                              g_mod_store.dirty = false;
                              g_mod_store.editing = false;
                              ++g_mod_store.editor_generation;
                              ImGui::CloseCurrentPopup();
                              if (action)
                                  action();
                          }}},
                        width);
        if (StoreModalBack()) {
            g_mod_store.pending_navigation = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (StoreBeginManagerModal("Mod gallery", 980.F, 680.F)) {
        const float width = ImGui::GetContentRegionAvail().x;
        StoreModalBody("Gallery image", width, {"Previous", "Next", "Close"});
        StoreHeading(g_mod_store.gallery_title, ImGui::GetContentRegionAvail().x, 20);
        const int count = int(g_mod_store.gallery.size());
        if (count) {
            g_mod_store.gallery_index = std::clamp(g_mod_store.gallery_index, 0, count - 1);
            const float inner = ImGui::GetContentRegionAvail().x;
            StoreCover({{"image", g_mod_store.gallery[g_mod_store.gallery_index]}}, inner,
                       std::max(80.F, std::min(inner * .56F, ImGui::GetContentRegionAvail().y - 45.F)), true);
            StoreNote(std::to_string(g_mod_store.gallery_index + 1) + " / " + std::to_string(count), inner);
        }
        ImGui::EndChild();
        StoreActionLine({{"Previous",
                          [count] {
                              if (count)
                                  g_mod_store.gallery_index = (g_mod_store.gallery_index + count - 1) % count;
                          }},
                         {"Next",
                          [count] {
                              if (count)
                                  g_mod_store.gallery_index = (g_mod_store.gallery_index + 1) % count;
                          }},
                         {"Close", [] { ImGui::CloseCurrentPopup(); }}},
                        width);
        if (StoreModalBack())
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (StoreBeginManagerModal("Release information", 800.F, 560.F)) {
        const float width = ImGui::GetContentRegionAvail().x;
        StoreModalBody("Release text", width, {"Close"});
        const float inner = ImGui::GetContentRegionAvail().x;
        StoreHeading(g_mod_store.release_title, inner, 21);
        StoreNote("Version " + StoreText(g_mod_store.release_details, "version", 80), inner, false);
        const auto notes = StoreText(g_mod_store.release_details, "notes", 8192);
        StoreNote(notes.empty() ? "No release notes supplied." : notes, inner);
        const auto requirements = StoreText(g_mod_store.release_details, "requirements", 4096);
        if (!requirements.empty()) {
            StoreHeading("Requirements", inner, 18);
            StoreNote(requirements, inner);
        }
        ImGui::EndChild();
        if (StoreButton("Close") || StoreModalBack())
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
