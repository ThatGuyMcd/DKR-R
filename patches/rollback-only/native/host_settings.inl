void DrawOlHostSettings(float width) {
    DrawOlFeature(width, "Host settings", "Applies to the next lobby you create.", [&](float inner) {
        bool changed = false;
        changed |= OlSwitchRow("##spectators", "Spectator-capable lobby",
            "Allow up to eight spectators.", &g_online_allow_spectators, inner, false);
        static const std::vector<std::string> ownership{
            "Host until character select", "Host for shared menus", "All players"};
        static const std::vector<std::string> racers{"2", "3", "4"};
        PaddockGap(16.F);
        changed |= OlSelect("menu-ownership", "Who controls the menus?", &g_online_host_control, ownership, inner);
        PaddockGap(16.F);
        int racer_option = g_online_maximum_players - 2;
        if (OlSelect("maximum-racers", "Maximum players", &racer_option, racers, inner)) {
            g_online_maximum_players = racer_option + 2;
            changed = true;
        }
        PaddockGap(16.F);
        OlParagraph("Adventure supports 2 players. Races and minigames support up to 4.", 15.F, kOlSoft, inner);
        PaddockGap(16.F);
        OlParagraph("Online mode: Rollback", 15.F, kOlCream, inner);
        PaddockGap(16.F);
        changed |= OlRange("experimental-window", "Prediction window (frames)", &g_online_rollback_window, 2, 20, inner);
        OlParagraph("How many frames Rollback can predict while waiting for late inputs.", 14.F, kOlSoft, inner, true);
        PaddockGap(16.F);
        changed |= OlSwitchRow("##automatic-delay", "Automatic input delay",
            "Adjusts input delay for the lobby's connection.", &g_online_automatic_delay, inner, false);
        PaddockGap(16.F);
        changed |= OlRange("input-delay", "Input delay (frames)", &g_online_manual_delay, 0, 9, inner, g_online_automatic_delay);
        PaddockGap(16.F);
        changed |= OlSwitchRow("##record-replay", "Record game replay",
            "Records game inputs for replay tools, not a video.", &g_online_record_replay, inner, false);
        PaddockGap(16.F);
        changed |= OlSwitchRow("##development-lobby", "Development lobby",
            "Allows host tool commands. All racers must agree. Progress is temporary; achievements are paused.",
            &g_online_development, inner, false);
        if (changed) SaveSettings();
    });
}
