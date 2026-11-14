// Timing limits change owner-local prediction/sampling, not the launch
// identity or any already-authored input. Gameplay/save rules stay frozen.
bool set_live_timing(std::string_view host_peer_id, bool automatic, std::uint8_t delay, std::uint8_t window,
                     std::string& error) {
    const auto* host = find(host_peer_id);
    auto next = room_.rules;
    next.automatic_input_delay = automatic;
    next.manual_input_delay = delay;
    next.rollback_window = window;
    if (!host || !host->host || !valid_rules(next) ||
        (room_.phase != RoomPhase::Waiting && room_.phase != RoomPhase::Running)) {
        error = "Only the host can change timing in a waiting lobby or running game.";
        return false;
    }
    if (room_.phase == RoomPhase::Waiting)
        return set_rules(host_peer_id, next, error);
    if (next == room_.rules) {
        error.clear();
        return true;
    }
    if (room_.generation == UINT64_MAX) {
        error = "Lobby settings generation exhausted.";
        return false;
    }
    room_.rules = next;
    ++room_.generation;
    error.clear();
    return true;
}
