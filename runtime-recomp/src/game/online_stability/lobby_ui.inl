void DrawOlSessionLinks(float width) {
    namespace watch = dkr::runtime::spectators;
    namespace links = dkr::runtime::netplay::links;
    auto* session = &dkr::runtime::netplay::session();
    auto hub = watch::party_link(session);
    struct Snapshot {
        links::Scope scope;
        std::map<links::Pair, links::Sample> samples;
        std::map<links::Id, links::Capacity> capacity;
        links::Handover handover;
        std::set<links::Id> introduced;
        std::string status;
        bool finished = false, directory_public = false;
    };
    Snapshot state;
    watch::PartySnapshot roster;
    bool host = false, waiting = false;
    std::uint64_t local = 0;
    {
        std::scoped_lock lock(hub->mutex);
        if (!hub->enabled)
            return;
        state.scope = hub->links.scope;
        state.samples = hub->links.samples;
        state.capacity = hub->links.capacity;
        state.handover = hub->links.handover;
        state.status = hub->links.status;
        state.finished = hub->links.finished;
        state.directory_public = hub->links.directory_public;
        for (const auto& [id, intro] : hub->links.introduced)
            state.introduced.insert(id);
        roster = hub->party.snapshot();
        host = hub->host;
        local = hub->local;
        waiting = !hub->launch && !hub->running && !hub->stopping;
    }
    auto name = [&](std::uint64_t id) {
        const auto at =
            std::find_if(roster.members.begin(), roster.members.end(), [&](const auto& m) { return m.id == id; });
        return at == roster.members.end() ? std::string("Player") : at->name;
    };
    DrawOlFeature(
        width, "Session networking", "Direct racer input links keep spectators outside the racing input path.",
        [&](float inner) {
            const auto now = links::Clock::now();
            unsigned active = 0;
            for (const auto& [pair, sample] : state.samples)
                if (pair.first == local && links::input_pair(state.scope, pair.first, pair.second) &&
                    sample.observations && now - sample.measured < std::chrono::seconds(2))
                    ++active;
            OlParagraph(std::to_string(active) + " measured direct racer connections", 14.F, kOlSoft, inner);
            if (const auto best = links::recommend(state.scope, state.samples, state.capacity, now)) {
                OlParagraph("Suggested host: " + name(best->member), 16.F, kOlCream, inner, true);
                OlParagraph(best->reason, 14.F, kOlSoft, inner);
            }
            const auto phase = state.handover.phase();
            const bool active_transfer = phase == links::Phase::Offered || phase == links::Phase::Preparing ||
                                         phase == links::Phase::Committing ||
                                         (phase == links::Phase::Committed && !state.finished);
            if (!state.status.empty())
                OlParagraph(state.status, 14.F, kOlSoft, inner);
            if (active_transfer) {
                OlParagraph(state.handover.status(), 15.F, kOlCream, inner, true);
                const auto& proposal = state.handover.proposal();
                if (proposal && proposal->successor == local && phase == links::Phase::Offered) {
                    const bool hidden = state.directory_public && dkr::online::view()->hidden;
                    if (hidden)
                        OlParagraph("Turn off Appear offline before accepting this public lobby.", 14.F, kOlSoft,
                                    inner);
                    {
                        const OlDisabled disabled(hidden);
                        if (OlButton("Accept hosting", OlTone::Go)) {
                            std::scoped_lock lock(hub->mutex);
                            hub->links.consent = true;
                        }
                    }
                    PaddockGap(6.F);
                    if (OlButton("Decline")) {
                        std::scoped_lock lock(hub->mutex);
                        hub->links.consent = false;
                    }
                }
                if (host && phase != links::Phase::Committing && OlButton("Cancel host transfer")) {
                    std::scoped_lock lock(hub->mutex);
                    hub->links.cancel = true;
                }
            } else if (host && waiting) {
                const bool supported = state.scope.members.size() == roster.members.size() &&
                                       std::all_of(state.scope.members.begin(), state.scope.members.end(),
                                                   [](const auto& m) { return m.capable; });
                if (!supported)
                    OlParagraph("Waiting for every player to confirm host-transfer support.", 14.F, kOlSoft, inner);
                for (const auto& member : roster.members)
                    if (member.id != local) {
                        ImGui::PushID(std::to_string(member.id).c_str());
                        const OlDisabled disabled(!supported);
                        if (OlButton(("Transfer host to " + member.name).c_str())) {
                            bool owns_save = false;
                            {
                                std::scoped_lock lock(hub->mutex);
                                owns_save = hub->links.save_owner == local;
                            }
                            std::vector<std::uint8_t> paks, records;
                            std::string error;
                            if (owns_save) {
                                std::vector<std::uint8_t> seed;
                                std::filesystem::path owner;
                                if (!dkr::runtime::saves::read_online_adventure(true, state.scope.room, seed, owner,
                                                                                error) ||
                                    !dkr::runtime::saves::read_experimental_online_paks(true, state.scope.room, paks,
                                                                                        error)) {
                                    OlNotify(error);
                                    ImGui::PopID();
                                    continue;
                                }
                                const auto path = owner.parent_path() / "custom-records-v1.bin";
                                std::error_code ec;
                                if (std::filesystem::exists(path, ec)) {
                                    const auto size = std::filesystem::file_size(path, ec);
                                    if (ec || size > 20 + 8192 * 76)
                                        error = "The online records could not be prepared for transfer.";
                                    else {
                                        std::ifstream input(path, std::ios::binary);
                                        records.resize(std::size_t(size));
                                        if (!input.read(reinterpret_cast<char*>(records.data()),
                                                        std::streamsize(records.size())) ||
                                            !dkr::runtime::saves::records::valid(records))
                                            error = "The online records need recovery before transferring host.";
                                    }
                                }
                            }
                            if (!error.empty())
                                OlNotify(error);
                            else {
                                std::scoped_lock lock(hub->mutex);
                                if (owns_save) {
                                    hub->links.save_paks = std::move(paks);
                                    hub->links.save_records = std::move(records);
                                }
                                hub->links.request = member.id;
                            }
                        }
                        ImGui::PopID();
                        PaddockGap(4.F);
                    }
            } else if (!waiting)
                OlParagraph("Host transfers are available after returning to the lobby.", 14.F, kOlSoft, inner);
        });
    PaddockGap(12.F);
}
void DrawOlJoiningProgress(float width, const OnlineFrame& frame) {
    DrawOlSessionLinks(width);
    if (frame.view.host)
        return; // The host retains the existing approval cards.
    const auto rows = dkr::online_stability::control(&dkr::runtime::netplay::session())->joins();
    if (rows.empty())
        return;
    DrawOlFeature(width, "Joining players", "Session preparation is approved by the host.", [&](float inner) {
        for (const auto& row : rows) {
            ImGui::PushID(std::to_string(row.request).c_str());
            OlParagraph(row.name, 16.F, kOlCream, inner, true);
            OlParagraph(dkr::online_stability::stage_text(row.stage), 14.F, kOlSoft, inner);
            if (row.total && (row.stage == dkr::online_stability::Stage::Downloading ||
                              row.stage == dkr::online_stability::Stage::Queued)) {
                char label[96]{};
                std::snprintf(label, sizeof(label), "%.1f / %.1f MiB", double(row.received) / (1024 * 1024),
                              double(row.total) / (1024 * 1024));
                ImGui::ProgressBar(std::clamp(float(row.received) / float(row.total), 0.F, 1.F), {inner, 24.F}, label);
            }
            PaddockGap(8.F);
            ImGui::PopID();
        }
    });
    PaddockGap(16.F);
}
void DrawOlLiveDelay(float width) {
    const auto view = dkr::online_stability::control(&dkr::runtime::netplay::session())->delay();
    if (!view.requested)
        return;
    const auto text = std::string("Dynamic Input Delay: ") + std::to_string(view.frames) + " frames (" +
                      std::to_string((view.frames * 1000 + 15) / 30) + " ms added) - " + view.reason;
    OlParagraph(text, 14.F, kOlSoft, width);
}
