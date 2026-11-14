// UI-only helpers. The installer, ownership receipts and server authority remain
// the sole source of truth for writes; these helpers only arrange their controls.
bool StoreBeginManagerModal(const char*, float, float);
bool StoreModalBack();
void StoreModalBody(const char*, float, std::initializer_list<const char*>);
void StoreOpenPopup(const char* name) {
    g_mod_store.popup_request = name;
}
void StoreConfirm(std::string title, std::string message, std::string label, std::function<void()> action) {
    g_mod_store.confirm_title = std::move(title);
    g_mod_store.confirm_message = std::move(message);
    g_mod_store.confirm_label = std::move(label);
    g_mod_store.confirm_identity = g_mod_store.identity;
    g_mod_store.confirmation = std::move(action);
    StoreOpenPopup("Confirm Store action");
}
std::string StoreRoute() {
    return std::to_string(g_mod_store.page) + ":" + std::to_string(g_mod_store.installed_section) + ":" +
           std::to_string(g_mod_store.staff_tab) + ":" + std::to_string(g_mod_store.detail_tab) + ":" +
           std::to_string(g_mod_store.detail) + ":" + std::to_string(g_mod_store.editing) + ":" +
           std::to_string(g_mod_store.editor_step) + ":" + std::to_string(g_mod_store.preview) + ":" +
           std::to_string(g_mod_store.related_open) + ":" + std::to_string(g_mod_store.request_page) + ":" +
           std::to_string(g_mod_store.account_page) + ":" + g_mod_store.installed_selected + ":" +
           std::to_string(g_mod_store.request_picker) + ":" + std::to_string(g_mod_store.review_editing) + ":" +
           std::to_string(g_mod_store.download_section) + ":" + g_mod_store.update_detail + ":" +
           g_mod_store.my_request + ":" + g_mod_store.staff_report + ":" +
           (g_mod_store.detail ? g_mod_store.selected : "") + ":" + g_mod_store.request + ":" +
           g_mod_store.upload_selection;
}
void StoreNavigate(std::function<void()> action) {
    if (g_mod_store.editing && g_mod_store.dirty) {
        g_mod_store.pending_navigation = std::move(action);
        StoreOpenPopup("Leave unsaved listing?");
    } else
        action();
}
bool StoreBack(const char* label, const std::function<void()>& action, bool protect = false) {
    const bool back = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                      !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && !ImGui::GetIO().WantTextInput &&
                      (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape));
    if (StoreButton(label) || back) {
        if (protect)
            StoreNavigate(action);
        else
            action();
        return true;
    }
    PaddockGap(8.F);
    return false;
}
void StoreAction(const std::string& name, StoreJson input) {
    g_mod_store.message_page = std::to_string(g_mod_store.page);
    if (name.starts_with("catalogue.") || name == "request.publish")
        g_mod_store.pending_editor_generation = g_mod_store.editor_generation;
    dkr::store::service().action(name, std::move(input));
}
void StoreSyncIdentity(const dkr::store::Snapshot& store) {
    const auto identity = StoreText(store.user, "id", 160) + ":" + StoreText(store.user, "role", 32);
    if (identity == g_mod_store.identity)
        return;
    const auto search = g_mod_store.fields["search"];
    g_mod_store.fields.clear();
    g_mod_store.fields["search"] = search;
    g_mod_store.fields["profile-name"] = StoreText(store.user, "displayName", 60);
    g_mod_store.identity = identity;
    g_mod_store.request.clear();
    g_mod_store.editor.clear();
    g_mod_store.report.clear();
    g_mod_store.my_request.clear();
    g_mod_store.staff_report.clear();
    g_mod_store.confirmation = {};
    g_mod_store.pending_navigation = {};
    g_mod_store.popup_request.clear();
    g_mod_store.draft = StoreJson::object();
    g_mod_store.editing = false;
    g_mod_store.dirty = false;
    g_mod_store.previewed = false;
    g_mod_store.related_open = false;
    g_mod_store.picker_upload = false;
    g_mod_store.request_picker = false;
    g_mod_store.review_editing = false;
    g_mod_store.request_credit = false;
    g_mod_store.request_target.clear();
    g_mod_store.install_after_download.clear();
    g_mod_store.positions.clear();
    g_mod_store.nav_screen.clear();
    ++g_mod_store.editor_generation;
    if (g_mod_store.page >= 7 && !dkr::store::staff(store.user))
        g_mod_store.page = 3;
}
bool StoreBusyAction(const dkr::store::Snapshot& store) {
    return store.writing || store.loading;
}
void StoreFooter(const std::string& primary, bool disabled, const std::function<void()>& action, float width) {
    const bool paired = PaddockButtonWidth(primary.c_str(), PaddockButtonKind::Primary) +
                            PaddockButtonWidth("Cancel", PaddockButtonKind::Plain) + 8.F <=
                        width;
    ImGui::BeginDisabled(disabled);
    if (StoreButton(primary, 0, true))
        action();
    ImGui::EndDisabled();
    if (paired)
        ImGui::SameLine(0, 8.F);
    if (StoreButton("Cancel") || StoreModalBack()) {
        g_mod_store.confirmation = {};
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsWindowAppearing())
        ImGui::SetItemDefaultFocus();
}
bool StoreMatches(const std::string& value, const std::string& query) {
    const auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    };
    return query.empty() || lower(value).find(lower(query)) != std::string::npos;
}
std::string StoreSize(std::uint64_t bytes) {
    if (!bytes)
        return "Size not supplied";
    char value[80];
    std::snprintf(value, sizeof(value), bytes >= 1048576 ? "%.1f MiB" : "%.0f KiB",
                  double(bytes) / (bytes >= 1048576 ? 1048576. : 1024.));
    return value;
}
bool StoreTransferActive(const dkr::store::Download& job) {
    return job.state == "Queued" || job.state == "Preparing download" || job.state == "Downloading" ||
           job.state == "Verifying" || job.state == "Installing";
}
int& StoreLimit(const std::string& key) {
    auto [it, inserted] = g_mod_store.limits.try_emplace(key, 24);
    return it->second;
}
void StoreMore(const std::string& key, std::size_t total, float width) {
    auto& limit = StoreLimit(key);
    StoreNote(std::to_string(std::min<std::size_t>(total, limit)) + " of " + std::to_string(total), width);
    const auto previous_row = g_mod_store.last_row_action;
    if (total > std::size_t(limit) && StoreButton("Show more##" + key)) {
        limit += 24;
        if (previous_row) {
            ImGui::SetFocusID(previous_row, ImGui::GetCurrentWindow());
            g_mod_store.page_focus = previous_row;
        }
    }
}
void StoreBeginBodyNavigation() {
    g_mod_store.body_navigation = true;
    g_mod_store.last_row_action = 0;
    const auto screen = StoreRoute();
    if (screen != g_mod_store.nav_screen) {
        g_mod_store.nav_screen = screen;
        const auto saved = g_mod_store.positions[screen];
        ImGui::SetScrollY(saved.scroll);
        g_mod_store.restore_focus = saved.focus;
        g_mod_store.focus_first = saved.focus == 0;
        g_mod_store.restoring = true;
        g_mod_store.page_focus = saved.focus;
    }
}
void StoreEndBodyNavigation() {
    g_mod_store.body_navigation = false;
    if (StoreRoute() == g_mod_store.nav_screen) {
        auto& saved = g_mod_store.positions[g_mod_store.nav_screen];
        if (!g_mod_store.restoring)
            saved.scroll = ImGui::GetScrollY();
        if (g_mod_store.page_focus)
            saved.focus = g_mod_store.page_focus;
    }
    // If a previously focused row was removed, focus the first available control
    // on the next frame instead of trapping navigation on a missing item ID.
    if (g_mod_store.restore_focus) {
        g_mod_store.restore_focus = 0;
        g_mod_store.focus_first = true;
    }
    g_mod_store.restoring = false;
}
void StoreMessage(const std::string& title, const std::string& text, float width) {
    if (text.empty())
        return;
    StorePanel panel(std::min(width, 1000.F), true);
    StoreHeading(title, panel.inner(), 18);
    StoreNote(text, panel.inner(), false);
    StoreActionLine({{"Copy message", [text] { SDL_SetClipboardText(text.c_str()); }},
                     {"Dismiss", [] { dkr::store::service().clear_messages(); }}},
                    panel.inner());
}
