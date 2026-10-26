// Connection panel only. The accepted lobby cards, code plate, invitations,
// moderation and spectator/role controls remain composed by the prior layer.
void OlRequestConnectionTest() {
    std::string error;
    if (!dkr::runtime::netplay::session().request_connection_test(error))
        OlNotify(error);
    else
        OlNotify("Checking Rollback input timing. Please wait about 10 seconds.");
}
bool OlCanTestConnection(const OnlineFrame& frame) {
    using dkr::runtime::netplay::ConnectionState;
    return frame.view.host && !frame.busy && !OlPartyLive() &&
           (frame.view.state == ConnectionState::Hosting || frame.view.state == ConnectionState::Lobby);
}
bool OlHasTestResults(const OnlineFrame&) {
    return g_ol_rollback_check.complete;
}
std::string OlTestCountdown(const OnlineFrame& frame) {
    return std::to_string((frame.view.connection_test_remaining_ms + 999U) / 1000U) + " seconds";
}
std::string OlRollbackMs(double value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f ms", value);
    return text;
}
void DrawOlRollbackResults(const OnlineFrame&);
void DrawOlConnection(float width, const OnlineFrame& frame) {
    using namespace dkr::runtime::netplay;
    DrawOlFeature(width, "Rollback connection", "", [&](float inner) {
        PaddockGap(12.F);
        OlParagraph(OlRulesLine(frame.view.room.rules), 16.F, kOlCream, inner, true);
        OlParagraph(
            "Checks the active Rollback transport at the game's 30 Hz input cadence. It does not run a race or change your settings.",
            14.F, kOlSoft, inner);
        if (!g_ol_rollback_check.status.empty()) {
            PaddockGap(12.F);
            OlParagraph(g_ol_rollback_check.status, 15.F, kOlAmber, inner);
        }
        if (OlPartyActive()) {
            for (const auto& m : g_ol_party.link.party.members) {
                std::string timing = m.id == g_ol_party.link.local ? "This PC" : "Measuring lobby RTT...";
                for (const auto& metric : g_ol_party.metrics)
                    if (metric.member == m.id && metric.count)
                        timing = std::to_string(metric.rtts[(metric.index - 1) % 16]) + " ms lobby RTT";
                if (!frame.view.host && m.host && g_ol_party.host_rtt)
                    timing = std::to_string(g_ol_party.host_rtt) + " ms to host";
                if (!frame.view.host && !m.host && m.id != g_ol_party.link.local)
                    timing = "Measured by the host";
                PaddockGap(8.F);
                OlParagraph(m.name + (m.role == lobby_party::Role::Spectator ? " (spectator)" : " (racer)") + " - " +
                                timing,
                            14.F, kOlSoft, inner);
            }
        } else if (frame.view.network_rtt_ms)
            OlParagraph("Lobby RTT: " + std::to_string(frame.view.network_rtt_ms) +
                            " ms. Run the Rollback check for input-channel timings.",
                        14.F, kOlSoft, inner);
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        const auto live = experimental::runtime_view();
        if (live.active) {
            PaddockGap(12.F);
            OlParagraph("Live Rollback: frame " + std::to_string(live.frame) + ", confirmed " +
                            std::to_string(live.confirmed) + ", corrections " + std::to_string(live.corrections) +
                            ", replayed " + std::to_string(live.replayed) + ".",
                        14.F, kOlInk, inner);
            if (!live.status.empty())
                OlParagraph(live.status, 14.F, kOlSoft, inner);
        } else
#endif
            if (frame.view.state == ConnectionState::Running)
            OlParagraph("The race is running. Preflight checks are disabled until everyone returns to the lobby.", 14.F,
                        kOlSoft, inner);
        if (frame.view.host) {
            PaddockGap(14.F);
            const OlDisabled disabled(!OlCanTestConnection(frame));
            if (OlButton("Check Rollback connection", OlTone::Plain))
                OlRequestConnectionTest();
        } else
            OlParagraph(
                "The host can run a shared Rollback check while everyone is in the lobby. All participants need the updated build.",
                14.F, kOlSoft, inner);
        if (g_ol_rollback_check.active) {
            PaddockGap(10.F);
            OlParagraph("Checking... " + OlTestCountdown(frame), 15.F, kOlAmber, inner);
        }
        if (g_ol_rollback_check.complete) {
            PaddockGap(12.F);
            if (OlButton("View Rollback report"))
                g_online_page.open_results = true;
        }
    });
}
void DrawOlPartyConnection(float width, const OnlineFrame& frame) {
    DrawOlConnection(width, frame);
}
void DrawOlPartyResults(const OnlineFrame& frame) {
    DrawOlRollbackResults(frame);
}
