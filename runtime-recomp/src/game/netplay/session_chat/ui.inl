// Drawn by the existing launcher/RT64 overlay owner. Network work stays in Channel.
struct SessionChatUi {
    dkr::chat::Id room = 0, read = 0;
    bool collapsed = false, editor = false, focus = false;
    std::set<dkr::chat::Id> muted;
    char draft[257]{};
    std::string error;
};
SessionChatUi g_session_chat;
void DrawOlGrid(float, const OnlineFrame&);

void PumpSessionChatUi() {
    const auto view = dkr::chat::channel(&dkr::runtime::netplay::session())->view(dkr::chat::Clock::now(), 0);
    if (view.room != g_session_chat.room || !view.active) {
        if (g_session_chat.room || g_session_chat.editor) {
            g_session_chat = {};
            dkr::chat::typing = false;
            dkr::chat::on_screen_keyboard = false;
            dkr::chat::open_requested = false;
            dkr::chat::close_requested = false;
        }
        g_session_chat.room = view.room;
    }
    if (dkr::chat::close_requested.exchange(false)) {
        g_session_chat.editor = false;
        dkr::chat::typing = false;
    }
    if (dkr::chat::open_requested.exchange(false) && view.active) {
        g_session_chat.editor = true;
        g_session_chat.focus = true;
        dkr::chat::typing = true;
        g_overlay_visible = false;
    }
    if (g_text_entry_target == TextEntryTarget::None)
        dkr::chat::on_screen_keyboard = false;
    if (dkr::chat::settings_changed.exchange(false))
        SaveSettings();
}
void SendSessionChat() {
    if (dkr::chat::channel(&dkr::runtime::netplay::session())->send(g_session_chat.draft, g_session_chat.error))
        g_session_chat.draft[0] = '\0';
}
void SessionChatKeyboard() {
    dkr::chat::on_screen_keyboard = true;
    const auto room = g_session_chat.room;
    RequestCustomTextKeyboard(
        "LOBBY CHAT", g_session_chat.draft, sizeof(g_session_chat.draft),
        [room](const std::string& text) {
            if (room != g_session_chat.room)
                return;
            const auto length = std::min(text.size(), sizeof(g_session_chat.draft) - 1);
            std::memcpy(g_session_chat.draft, text.data(), length);
            g_session_chat.draft[length] = '\0';
            dkr::chat::on_screen_keyboard = false;
            SendSessionChat();
        },
        false);
}
void SessionChatOptions(const dkr::chat::View& view) {
    if (OlButton("...##chat-options", OlTone::Plain, 38.F, "Chat options"))
        ImGui::OpenPopup("session-chat-options");
    if (ImGui::BeginPopup("session-chat-options")) {
        if (view.host) {
            bool allow = dkr::chat::host_enabled.load();
            if (ImGui::Checkbox("Allow lobby chat", &allow)) {
                dkr::chat::host_enabled = allow;
                SaveSettings();
            }
        }
        bool feed = dkr::chat::show_feed.load();
        if (ImGui::Checkbox("Show messages during races", &feed)) {
            dkr::chat::show_feed = feed;
            SaveSettings();
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Mute on this device");
        for (const auto& m : view.members)
            if (m.id != view.local) {
                ImGui::PushID(std::to_string(m.id).c_str());
                bool muted = g_session_chat.muted.contains(m.id);
                if (ImGui::Checkbox(m.name.c_str(), &muted)) {
                    if (muted)
                        g_session_chat.muted.insert(m.id);
                    else
                        g_session_chat.muted.erase(m.id);
                }
                ImGui::PopID();
            }
        ImGui::EndPopup();
    }
}
void DrawSessionChatPanel(float width, bool game = false) {
    const auto view = dkr::chat::channel(&dkr::runtime::netplay::session())->view();
    ImGui::PushID(game ? "session-chat-game" : "session-chat-lobby");
    OlPanelLook look;
    look.fill = 0x061D2C;
    look.ring = 0x28556B;
    look.ring_width = 1.F;
    DrawOlCard(width, look, [&](float inner) {
        const float controls = game ? 94.F : 100.F;
        const auto at = ImGui::GetCursorScreenPos();
        OlParagraph("LOBBY CHAT", 18.F, kOlCream, std::max(40.F, inner - controls), true);
        OlParagraph(view.host ? "You are hosting" : "Racers + spectators", 12.F, kOlSoft,
                    std::max(40.F, inner - controls));
        const float title_height = ImGui::GetCursorScreenPos().y - at.y;
        ImGui::SetCursorScreenPos({at.x + inner - controls, at.y});
        SessionChatOptions(view);
        ImGui::SameLine(0, 6.F);
        if (OlButton(game ? "Close" : g_session_chat.collapsed ? "Show" : "Hide", OlTone::Plain, controls - 44.F)) {
            if (game)
                dkr::chat::request_close();
            else
                g_session_chat.collapsed = !g_session_chat.collapsed;
        }
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({inner, std::max(title_height, 36.F)});
        if (!game && g_session_chat.collapsed) {
            unsigned unread = 0;
            for (const auto& m : view.messages)
                if (m.id > g_session_chat.read && m.author != view.local && !g_session_chat.muted.contains(m.author))
                    ++unread;
            if (unread)
                OlParagraph(dkr::localisation::plural("chat.unread", unread), 12.F, kOlGoText, inner);
            return;
        }
        PaddockGap(10.F);
        ImGui::BeginChild("messages", {inner, game ? 190.F : 180.F}, false, ImGuiWindowFlags_NoSavedSettings);
        const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.F;
        const float content = std::max(20.F, ImGui::GetContentRegionAvail().x);
        unsigned shown = 0;
        for (const auto& m : view.messages)
            if (!g_session_chat.muted.contains(m.author)) {
                ++shown;
                ImGui::PushID((std::to_string(m.author) + ":" + std::to_string(m.request)).c_str());
                OlParagraph(m.name, 13.F, m.author == view.local ? kOlGoText : kOlCream, content, true);
                OlParagraph(m.text, 14.F, kOlInk, content);
                if (m.delivery == dkr::chat::Delivery::Waiting)
                    OlParagraph("Sending...", 12.F, kOlSoft, content);
                else if (m.delivery == dkr::chat::Delivery::Failed) {
                    OlParagraph("Not delivered", 12.F, kOlAmber, content);
                    if (OlButton("Retry"))
                        dkr::chat::channel(&dkr::runtime::netplay::session())->retry(m.request, g_session_chat.error);
                }
                PaddockGap(10.F);
                ImGui::PopID();
                if (m.id)
                    g_session_chat.read = std::max(g_session_chat.read, m.id);
            }
        if (!shown)
            OlParagraph("Chat with everyone in this lobby.", 13.F, kOlSoft, content);
        if (at_bottom)
            ImGui::SetScrollHereY(1.F);
        ImGui::EndChild();
        PaddockGap(8.F);
        const bool disabled = !view.active || !view.known || !view.enabled;
        OlInputOptions options;
        options.hint = "Say something...";
        options.disabled = disabled;
        if (game && g_session_chat.focus) {
            ImGui::SetKeyboardFocusHere();
            g_session_chat.focus = false;
        }
        const auto input =
            OlInput("##message", nullptr, g_session_chat.draft, sizeof(g_session_chat.draft), inner, options);
        if (input == OlInputResult::Gamepad)
            SessionChatKeyboard();
        if (input == OlInputResult::Entered && !disabled)
            SendSessionChat();
        PaddockGap(8.F);
        {
            const OlDisabled unavailable(disabled || g_session_chat.draft[0] == '\0');
            if (OlButton("Send", OlTone::Strong, inner))
                SendSessionChat();
        }
        PaddockGap(4.F);
        OlParagraph(std::to_string(std::strlen(g_session_chat.draft)) + " / 256", 11.F, kOlSoft, inner);
        if (!view.status.empty())
            OlParagraph(view.status, 12.F, kOlSoft, inner);
        if (!g_session_chat.error.empty())
            OlParagraph(g_session_chat.error, 12.F, kOlAmber, inner);
    });
    ImGui::PopID();
}
void DrawSessionChatLobby(float width, const OnlineFrame& frame) {
    const bool side = width >= 820.F;
    const float chat_width = side ? std::clamp(width * .30F, 270.F, 360.F) : width;
    const float racers = side ? width - chat_width - 16.F : width;
    const auto at = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    DrawOlGrid(racers, frame);
    ImGui::EndGroup();
    const float height = ImGui::GetItemRectSize().y;
    if (side)
        ImGui::SetCursorScreenPos({at.x + racers + 16.F, at.y});
    else
        PaddockGap(14.F);
    ImGui::BeginGroup();
    DrawSessionChatPanel(chat_width);
    ImGui::EndGroup();
    const float chat_height = ImGui::GetItemRectSize().y;
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, side ? std::max(height, chat_height) : height + 14.F + chat_height});
}
void DrawSessionChatPreferences(float width) {
    DrawOlFeature(width, "Lobby chat", "Text chat for your current online session.", [&](float inner) {
        bool feed = dkr::chat::show_feed.load();
        if (OlSwitchRow("##chat-feed", "Show messages during races", "Recent messages fade after a few seconds.", &feed,
                        inner, false)) {
            dkr::chat::show_feed = feed;
            SaveSettings();
        }
        PaddockGap(10.F);
        const auto key = dkr::chat::shortcut_key.load();
        const std::string label = dkr::chat::capture_key.load()
                                      ? "Press a key... Esc cancels"
                                      : "Chat key: " + std::string(SDL_GetScancodeName(SDL_Scancode(key)));
        if (OlButton(label.c_str(), OlTone::Plain, inner))
            dkr::chat::capture_key = true;
        PaddockGap(10.F);
        static const std::vector<std::string> corners{"Bottom left", "Bottom right", "Top left", "Top right"};
        int corner = dkr::chat::feed_corner.load();
        if (OlSelect("chat-corner", "In-game position", &corner, corners, inner)) {
            dkr::chat::feed_corner = corner;
            SaveSettings();
        }
        if (dkr::runtime::netplay::session().presentation_active()) {
            PaddockGap(10.F);
            if (OlButton("Open lobby chat", OlTone::Plain, inner))
                dkr::chat::request_open();
        }
    });
    PaddockGap(16.F);
}
void DrawSessionChatOverlay(bool other_overlay) {
    const auto view = dkr::chat::channel(&dkr::runtime::netplay::session())->view(dkr::chat::Clock::now(), 40);
    if (!view.active || !dkr::runtime::netplay::session().presentation_active() || other_overlay)
        return;
    if (!g_session_chat.editor && !dkr::chat::show_feed.load())
        return;
    const auto feed = dkr::chat::recent_feed(view, g_session_chat.muted, dkr::chat::Clock::now());
    if (!g_session_chat.editor && feed.empty())
        return;
    ImGui::SetNextWindowBgAlpha(g_session_chat.editor ? 1.F : .88F);
    const auto* viewport = ImGui::GetMainViewport();
    const auto size = viewport->WorkSize;
    const int corner = dkr::chat::feed_corner.load();
    const bool right = (corner & 1) != 0, top = (corner & 2) != 0;
    const ImVec2 anchor{viewport->WorkPos.x + (right ? size.x - 24.F : 24.F),
                        viewport->WorkPos.y + (top ? 100.F : size.y - 130.F)};
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, {right ? 1.F : 0.F, top ? 0.F : 1.F});
    const float width = std::min(g_session_chat.editor ? 420.F : 380.F, std::max(180.F, size.x - 48.F));
    ImGui::SetNextWindowSize({width, 0}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.F, 10.F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, PaddockRgb(0x061D2C));
    if (g_session_chat.editor && g_session_chat.focus)
        ImGui::SetNextWindowFocus();
    ImGui::Begin("##session-chat-overlay", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove |
                     (g_session_chat.editor ? 0
                                            : ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus |
                                                  ImGuiWindowFlags_NoInputs));
    const float inner = std::max(20.F, ImGui::GetContentRegionAvail().x);
    if (g_session_chat.editor)
        DrawSessionChatPanel(inner, true);
    else {
        OlParagraph("LOBBY CHAT", 12.F, kOlGoText, inner);
        PaddockGap(5.F);
        for (const auto* m : feed) {
            OlParagraph(m->name + ": " + m->text, 14.F, m->author == view.local ? kOlGoText : kOlCream, inner);
            PaddockGap(5.F);
        }
        const std::string key = SDL_GetScancodeName(SDL_Scancode(dkr::chat::shortcut_key.load()));
        OlParagraph(key + " to chat", 11.F, kOlSoft, inner);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
bool HandleSessionChatEvent(SDL_Event* event) {
    const bool keydown = event->type == SDL_KEYDOWN, keyup = event->type == SDL_KEYUP;
    const bool text = event->type == SDL_TEXTINPUT || event->type == SDL_TEXTEDITING;
    if (dkr::chat::capture_key.load() && keydown && !event->key.repeat) {
        const int key = event->key.keysym.scancode;
        if (dkr::chat::valid_shortcut(key)) {
            dkr::chat::shortcut_key = key;
            dkr::chat::settings_changed = true;
        }
        dkr::chat::capture_key = false;
        return true;
    }
    if (dkr::chat::typing.load() && !dkr::chat::on_screen_keyboard.load() && event->type == SDL_CONTROLLERBUTTONDOWN &&
        event->cbutton.button == SDL_CONTROLLER_BUTTON_B) {
        dkr::chat::request_close();
        return true;
    }
    const bool can_open = dkr::runtime::netplay::session().presentation_active() && !g_overlay_visible.load() &&
                          g_text_entry_target == TextEntryTarget::None && !g_online_code_keyboard_visible.load() &&
                          !dkr::runtime::hud::editor::active();
    const auto kind = keydown ? dkr::chat::InputKind::KeyDown
                      : keyup ? dkr::chat::InputKind::KeyUp
                      : text  ? dkr::chat::InputKind::Text
                              : dkr::chat::InputKind::Other;
    const int key = keydown || keyup ? int(event->key.keysym.scancode) : 0;
    const auto action =
        dkr::chat::on_screen_keyboard.load() && dkr::chat::typing.load() && (keydown || keyup || text)
            ? dkr::chat::InputAction::Forward
            : dkr::chat::input_action(kind, key, keydown && event->key.repeat, can_open, dkr::chat::shortcut_key.load(),
                                      keydown && key == SDL_SCANCODE_ESCAPE);
    if (action == dkr::chat::InputAction::Pass)
        return false;
    if (action == dkr::chat::InputAction::Forward) {
        std::scoped_lock guard(g_inspector_guard);
        if (g_inspector) {
            std::scoped_lock frame_lock(g_inspector->frameMutex);
            g_inspector->handleSdlEvent(event);
        }
    }
    return true;
}
