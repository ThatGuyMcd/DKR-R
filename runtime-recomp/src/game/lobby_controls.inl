// Presentation only: retain the original lobby panel, code plate and cards.
// Actions below use authenticated party APIs, never edit controller leases.
void DrawOlLobbyToolbar(float width, const OnlineFrame& frame) {
    const auto origin = ImGui::GetCursorScreenPos();
    float x = 0, y = 0;
    const std::array<const char*, 2> labels{"Check Rollback connection", "Invite friends"};
    for (unsigned i = 0; i < labels.size(); ++i) {
        const float w = std::min(width, OlButtonWidth(labels[i]));
        if (x > 0 && x + w > width) {
            x = 0;
            y += 48.F;
        }
        ImGui::SetCursorScreenPos({origin.x + x, origin.y + y});
        {
            const OlDisabled disabled(i == 0 ? !OlCanTestConnection(frame) : !frame.has_space);
            if (OlButton(labels[i], OlTone::Plain, w)) {
                if (i == 0)
                    OlRequestConnectionTest();
                else
                    g_online_page.section = OlSection::Friends;
            }
        }
        x += w + 8.F;
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width, y + 40.F});
}
bool OlPartyChanging(const OnlineFrame& frame) {
    return g_ol_party.link.stopping || g_ol_rollback_check.active || bool(g_ol_party.link.party.handover) ||
           frame.view.launch_countdown_active || (g_ol_party.launching && !g_ol_party.link.running);
}
bool OlPartyRole(lobby_party::MemberId id, bool racer) {
    auto& online = dkr::runtime::netplay::session();
    const auto link = lobby_party::party_link(&online);
    std::string error;
    bool ok = false;
    {
        std::scoped_lock lock(link->mutex);
        if (link->room_id != g_ol_party.room || link->party.snapshot().generation != g_ol_party.link.party.generation)
            error = "The lobby changed. Choose the player again.";
        else
            ok = lobby_party::party_waiting_role(&online, id, racer, error);
    }
    OlNotify(ok ? (racer ? "Racing place assigned. Racers must ready again."
                         : "Moved to spectators. Racers must ready again.")
                : error);
    return ok;
}
bool OlPartySwap(unsigned port, lobby_party::MemberId outgoing, lobby_party::MemberId incoming, bool approve) {
    auto& online = dkr::runtime::netplay::session();
    const auto link = lobby_party::party_link(&online);
    bool valid = false;
    {
        std::scoped_lock lock(link->mutex);
        valid = link->enabled && link->room_id == g_ol_party.room && port < 4 &&
                link->party.snapshot().generation == g_ol_party.link.party.generation &&
                link->party.snapshot().controls[port].member == outgoing && !link->party.snapshot().handover;
        if (valid) {
            if (approve)
                lobby_party::party_approve_swap(&online, port, incoming);
            else
                lobby_party::party_request_swap(&online, port, incoming);
        }
    }
    OlNotify(valid ? (approve ? "Player swap approval sent. Waiting for the safe handover."
                              : "Racing-place request sent for host approval.")
                   : "The racing place changed. Choose it again.");
    return valid;
}
void DrawOlPartySpectators(float width, const OnlineFrame& frame) {
    if (!OlPartyActive())
        return;
    PaddockGap(18.F);
    const auto rule = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(rule, {rule.x + width, rule.y + 1.F}, PaddockCol(0x2F6A86));
    PaddockGap(14.F);
    const auto at = ImGui::GetCursorScreenPos();
    const char* action = OlPartyWatching() ? (OlPartyLive() ? "Request a place" : "View players") : "Manage players";
    const float button = OlButtonWidth(action);
    const bool pair = width - button - 12.F >= 230.F;
    OlParagraph("SPECTATORS " + std::to_string(OlPartySpectators()) + " / 8", 17.F, kOlCream,
                pair ? width - button - 12.F : width, true);
    const float label_height = ImGui::GetItemRectSize().y;
    ImGui::SetCursorScreenPos(pair ? ImVec2{at.x + width - button, at.y} : ImVec2{at.x, at.y + label_height + 8.F});
    {
        const OlDisabled disabled(OlPartyChanging(frame));
        if (OlButton(action))
            g_ol_manage_open = true;
    }
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, pair ? std::max(40.F, label_height) : label_height + 48.F});
    PaddockGap(10.F);
    const auto chips = ImGui::GetCursorScreenPos();
    float x = 0, y = 0;
    unsigned count = 0;
    for (const auto& m : g_ol_party.link.party.members)
        if (m.role == lobby_party::Role::Spectator) {
            ++count;
            ImGui::PushID(std::to_string(m.id).c_str());
            const std::string name =
                m.name + (m.id == g_ol_party.link.local ? " (you)" : "") + (m.host ? " (host)" : "");
            const std::string shown =
                PaddockEllipsize(OlRead(15.F, true), name, std::max(20.F, std::min(width, 300.F) - 32.F));
            const float w = std::min(width, OlButtonWidth(shown.c_str()));
            if (x > 0 && x + w > width) {
                x = 0;
                y += 48.F;
            }
            ImGui::SetCursorScreenPos({chips.x + x, chips.y + y});
            if (OlButton(shown.c_str(), OlTone::Plain, w, name.c_str()))
                OlPartyOpenMember(m.id);
            x += w + 8.F;
            ImGui::PopID();
        }
    ImGui::SetCursorScreenPos(chips);
    if (count)
        ImGui::Dummy({width, y + 40.F});
    else
        OlParagraph("No spectators yet. Friends can join with Watch Race.", 14.F, kOlSoft, width);
    if (OlPartyWatching()) {
        PaddockGap(14.F);
        static const std::vector<std::string> modes{"All splits", "Follow player", "T.T.-style director"};
        int camera = int(g_ol_party.link.camera);
        if (OlSelect("spectator-camera", "Watch camera", &camera, modes, width))
            lobby_party::party_camera(&dkr::runtime::netplay::session(), lobby_party::WatchCamera(camera),
                                      g_ol_party.link.target);
        if (camera == int(lobby_party::WatchCamera::Follow)) {
            std::vector<std::string> names;
            std::vector<unsigned> ports;
            int selected = 0;
            for (unsigned p = 0; p < 4; ++p)
                if (const auto* m = OlPartyMember(g_ol_party.link.party.controls[p].member)) {
                    if (p == g_ol_party.link.target)
                        selected = int(ports.size());
                    ports.push_back(p);
                    names.push_back("P" + std::to_string(p + 1) + " - " + m->name);
                }
            PaddockGap(10.F);
            if (!ports.empty() && OlSelect("spectator-target", "Follow racer", &selected, names, width))
                lobby_party::party_camera(&dkr::runtime::netplay::session(), lobby_party::WatchCamera::Follow,
                                          ports[std::size_t(selected)]);
        }
        PaddockGap(8.F);
        OlParagraph("D-pad left/right: racer. C-up: camera. No controller port is assigned to a spectator.", 14.F,
                    kOlSoft, width);
    }
    if (g_ol_party.link.party.handover) {
        PaddockGap(12.F);
        OlParagraph("Preparing player swap. Waiting for the confirmed handover boundary.", 15.F, kOlAmber, width, true);
    }
    if (frame.view.host && OlPartyLive() && !g_ol_party.link.swaps.empty()) {
        PaddockGap(14.F);
        OlPanelLook look;
        look.fill = 0x3A2C14;
        look.ring = 0xC08A2C;
        look.ring_width = 1.F;
        DrawOlCard(width, look, [&](float inner) {
            OlParagraph("PLAYER SWAP REQUESTS", 17.F, kOlAmber, inner, true);
            for (const auto& [port, incoming] : g_ol_party.link.swaps) {
                const auto* m = OlPartyMember(incoming);
                if (!m || port >= 4)
                    continue;
                const auto outgoing = g_ol_party.link.party.controls[port].member;
                PaddockGap(10.F);
                OlParagraph(m->name + " wants to take P" + std::to_string(port + 1) + ".", 15.F, kOlCream, inner, true);
                OlParagraph("Same racer, vehicle and race progress. The host approves the safe handover.", 14.F,
                            kOlSoft, inner);
                ImGui::PushID(int(port));
                ImGui::PushID(std::to_string(incoming).c_str());
                {
                    const OlDisabled disabled(OlPartyChanging(frame));
                    if (OlButton("Approve swap", OlTone::Go))
                        OlPartySwap(port, outgoing, incoming, true);
                }
                ImGui::PopID();
                ImGui::PopID();
            }
            PaddockGap(10.F);
            if (OlButton("Cancel pending swap"))
                lobby_party::party_cancel_swap(&dkr::runtime::netplay::session());
        });
    }
    if (!g_ol_party.link.status.empty()) {
        PaddockGap(10.F);
        OlParagraph(g_ol_party.link.status, 14.F, kOlSoft, width);
    }
}
void DrawOlPartyStart(float width, const OnlineFrame& frame) {
    const bool live = OlPartyLive(), watcher = OlPartyWatching(), host = frame.view.host;
    OlPanelLook look;
    look.fill = 0x061D2C;
    look.ring = 0xFFFFFF;
    look.ring_alpha = 18U;
    look.ring_width = 1.F;
    look.radii = PaddockRound(10.F);
    DrawOlCard(width, look, [&](float inner) {
        if (g_ol_party.link.stopping)
            OlParagraph("Returning safely to the lobby. Please wait for saving and game retirement.", 16.F, kOlCream,
                        inner, true);
        else
            OlParagraph(watcher                   ? "You are watching. No racing controller is assigned to you."
                        : g_ol_party.link.running ? "Race running. Racing-place swaps keep the racer, car and progress."
                                                  : "Loading the race. Waiting for the confirmed game state.",
                        16.F, kOlCream, inner, true);
        PaddockGap(12.F);
        const bool pair = (host || watcher) && inner >= 390.F;
        const float button = pair ? (inner - 10.F) * .5F : inner;
        const auto at = ImGui::GetCursorScreenPos();
        {
            const OlDisabled disabled(frame.launcher || !g_ol_party.link.running || g_ol_party.link.stopping);
            if (OlCta(watcher ? "BACK TO WATCHING" : "BACK TO MY RACE", OlCtaColour::Green, button,
                      frame.launcher || !g_ol_party.link.running || g_ol_party.link.stopping))
                dkr::runtime::ui::toggle_overlay();
        }
        if (host || watcher) {
            ImGui::SetCursorScreenPos(pair ? ImVec2{at.x + button + 10.F, at.y} : ImVec2{at.x, at.y + 62.F});
            const bool disabled = OlPartyChanging(frame) || (!host && watcher && !live);
            const bool waiting_ready = g_ol_party.save_ready &&
                                       std::count_if(frame.view.room.players.begin(), frame.view.room.players.end(),
                                                     [](const auto& p) { return p.occupied; }) >= 2 &&
                                       std::none_of(frame.view.room.players.begin(), frame.view.room.players.end(),
                                                    [](const auto& p) { return p.occupied && !p.ready; });
            if (OlCta(host ? (live ? "RETURN TO LOBBY" : "START RACE")
                           : (live ? "REQUEST A PLACE" : "WAITING FOR HOST"),
                      host ? OlCtaColour::Red : OlCtaColour::Blue, button, disabled || (!live && !waiting_ready))) {
                if (host) {
                    if (live)
                        g_ol_return_open = true;
                    else
                        lobby_party::party_start(&dkr::runtime::netplay::session());
                } else
                    g_ol_manage_open = true;
            }
        }
    });
}
void DrawOlPartyDialogs(const OnlineFrame& frame) {
    constexpr const char* member_popup = "Player options###restored-lobby-member";
    constexpr const char* manage_popup = "Players and spectators###restored-lobby-manage";
    constexpr const char* return_popup = "Return everyone to the lobby?###restored-lobby-return";
    constexpr const char* remove_popup = "Remove this player?###restored-lobby-remove";
    static std::uint64_t dialog_room = 0;
    if (!OlPartyActive() || (dialog_room && dialog_room != g_ol_party.room)) {
        g_ol_member_open = g_ol_manage_open = g_ol_return_open = g_ol_remove_open = false;
        // A disconnected/replaced room must not leave an invisible modal
        // blocking the launcher or allow a confirmation to target a new room.
        for (const auto* name : {member_popup, manage_popup, return_popup, remove_popup})
            if (BeginOlDialog(name)) {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        dialog_room = 0;
        return;
    }
    dialog_room = g_ol_party.room;
    if (std::exchange(g_ol_member_open, false))
        ImGui::OpenPopup(member_popup);
    if (std::exchange(g_ol_manage_open, false))
        ImGui::OpenPopup(manage_popup);
    if (std::exchange(g_ol_return_open, false))
        ImGui::OpenPopup(return_popup);
    if (std::exchange(g_ol_remove_open, false))
        ImGui::OpenPopup(remove_popup);
    if (BeginOlDialog(member_popup)) {
        const float width = std::max(1.F, ImGui::GetContentRegionAvail().x);
        const auto* m = OlPartyMember(g_ol_member_id);
        const bool same =
            m && g_ol_member_room == g_ol_party.room && g_ol_member_generation == g_ol_party.link.party.generation;
        if (!same)
            OlParagraph("The lobby changed. Close this menu and choose the player again.", 16.F, kOlSoft, width);
        else {
            OlParagraph(m->name, 20.F, kOlCream, width, true);
            PaddockGap(12.F);
            OlParagraph(m->role == lobby_party::Role::Spectator
                            ? "Spectator - no racing controller port."
                            : "Racer - controller ownership follows the confirmed lobby roster.",
                        15.F, kOlSoft, width);
            PaddockGap(12.F);
            {
                const OlDisabled disabled(OlPartyChanging(frame));
                if (!OlPartyLive() && frame.view.host) {
                    if (OlButton(m->role == lobby_party::Role::Spectator ? "Make racer" : "Make spectator"))
                        if (OlPartyRole(m->id, m->role == lobby_party::Role::Spectator))
                            ImGui::CloseCurrentPopup();
                }
                if (OlPartyLive() && OlPartyWatching() && m->role == lobby_party::Role::Racer && m->port) {
                    if (OlButton("Request this racing place"))
                        if (OlPartySwap(*m->port, m->id, g_ol_party.link.local, false))
                            ImGui::CloseCurrentPopup();
                }
                if (OlPartyLive() && !OlPartyWatching() && m->role == lobby_party::Role::Spectator) {
                    if (const auto* me = OlPartyMember(g_ol_party.link.local); me && me->port)
                        if (OlButton("Offer my place to this spectator"))
                            if (OlPartySwap(*me->port, me->id, m->id, false))
                                ImGui::CloseCurrentPopup();
                }
                if (frame.view.host && m->id != g_ol_party.link.local) {
                    PaddockGap(8.F);
                    if (OlButton("Remove from lobby", OlTone::Danger)) {
                        g_ol_remove_open = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
        }
        PaddockGap(14.F);
        if (OlButton("Back") || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        if (ImGui::IsWindowAppearing())
            ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }
    if (BeginOlDialog(manage_popup)) {
        const float width = std::max(1.F, ImGui::GetContentRegionAvail().x);
        OlParagraph(OlPartyWatching() && OlPartyLive() ? "Choose a racer to request their racing place."
                                                       : "Choose a player for role and racing-place controls.",
                    15.F, kOlSoft, width);
        PaddockGap(12.F);
        const float height = std::min(62.F * g_ol_party.link.party.members.size(),
                                      std::max(100.F, ImGui::GetIO().DisplaySize.y * .6F - 140.F));
        ImGui::BeginChild("##restored-party-members", {width, height}, false);
        for (const auto& m : g_ol_party.link.party.members) {
            ImGui::PushID(std::to_string(m.id).c_str());
            const float inner = std::max(1.F, ImGui::GetContentRegionAvail().x);
            const std::string label = m.name + (m.role == lobby_party::Role::Spectator
                                                    ? " (spectator)"
                                                    : " (P" + std::to_string(m.port.value_or(0) + 1) + ")");
            const std::string shown = PaddockEllipsize(OlRead(16.F, true), label, std::max(20.F, inner - 32.F));
            if (OlButton(shown.c_str(), OlTone::Plain, inner, label.c_str())) {
                OlPartyOpenMember(m.id);
                ImGui::CloseCurrentPopup();
            }
            PaddockGap(10.F);
            ImGui::PopID();
        }
        ImGui::EndChild();
        PaddockGap(14.F);
        if (OlButton("Back") || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        if (ImGui::IsWindowAppearing())
            ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }
    if (BeginOlDialog(return_popup)) {
        ImGui::TextWrapped("The race will stop safely and everyone will return to this lobby. The lobby stays open.");
        const int action = DrawOlDialogActions({{"KEEP RACING"}, {"RETURN TO LOBBY", true}});
        if (action == 1 && frame.view.host)
            lobby_party::party_stop(&dkr::runtime::netplay::session(), "The host returned everyone to the lobby.");
        if (action >= 0 || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (BeginOlDialog(remove_popup)) {
        const auto* m = OlPartyMember(g_ol_member_id);
        ImGui::TextWrapped(
            "Remove %s from this lobby? Removing a racer during a race returns the remaining players safely to the lobby.",
            m ? m->name.c_str() : "this player");
        const int action = DrawOlDialogActions({{"CANCEL"}, {"REMOVE PLAYER", true}});
        if (action == 1) {
            std::string error;
            if (!m || g_ol_member_room != g_ol_party.room || g_ol_member_generation != g_ol_party.link.party.generation)
                OlNotify("The lobby changed. Choose the player again.");
            else if (!lobby_ui::request_removal(&dkr::runtime::netplay::session(), g_ol_party, m->id, error))
                OlNotify(error);
        }
        if (action >= 0 || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
