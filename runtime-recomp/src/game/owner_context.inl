// Inside DirectSession, with mutex_ held. Credentials never enter the sidecar
// or its public snapshot. Lock order remains DirectSession -> Party -> check.
auto rollback_context = [&] {
    namespace check = dkr::runtime::rollback_check;
    namespace watch = dkr::runtime::spectators;
    check::Context c;
    c.room = match_id_;
    c.local = sender_id_;
    c.host_id = is_host_ ? sender_id_ : host_sender_id_;
    c.host = is_host_;
    const auto& room = is_host_ ? lobby_.room() : room_view_;
    const auto& rules = room.rules;
    c.window = rules.rollback_window;
    c.automatic = rules.automatic_input_delay;
    c.delay = c.automatic ? input_delay_ : rules.manual_input_delay;
    c.idle = (state_ == ConnectionState::Hosting || state_ == ConnectionState::Lobby) && !pending_invitation_ &&
             launch_stage_ == LaunchStage::Idle && !launch_countdown_active_ &&
             rules.synchronization == SynchronizationMode::ExperimentalRollback;
    auto party = watch::party_link(this);
    std::scoped_lock party_lock(party->mutex);
    c.party = party->enabled;
    if (c.party) {
        const auto roster = party->party.snapshot();
        c.roster = roster.generation;
        c.local_watching = party->local_watching();
        c.idle = c.idle && party->admitted && !party->launch && !party->running && !party->stopping &&
                 !party->start_requested && !roster.handover;
        for (const auto& m : roster.members) {
            if (m.id == sender_id_ || !m.connected || (!is_host_ && m.id != host_sender_id_))
                continue;
            const bool observer = m.role == watch::Role::Spectator;
            const auto* peer = party->peer(m.id);
            const auto address = is_host_ && peer ? peer->address : host_address_;
            bool ready = bool(address) && transport_->traffic_ready(address, TransportTrafficClass::Control);
            ready = ready && transport_->traffic_ready(address, observer ? TransportTrafficClass::Replica
                                                                         : TransportTrafficClass::Control);
            if (observer)
                ready = ready && transport_->traffic_ready(address, TransportTrafficClass::Checkpoint);
            c.peers.push_back({m.id, m.name, observer, ready});
        }
        c.queued = party->outgoing_bytes;
    } else if (is_host_) {
        for (const auto& peer : peers_)
            if (peer.active) {
                bool ready = true;
                for (auto lane : {TransportTrafficClass::Control, TransportTrafficClass::Realtime,
                                  TransportTrafficClass::Authoritative})
                    ready = ready && transport_->traffic_ready(peer.address, lane);
                c.peers.push_back({peer.sender_id, room.players[peer.slot].display_name, false, ready});
            }
    } else if (host_address_) {
        std::string name = "Host";
        for (const auto& player : room_view_.players)
            if (player.host)
                name = player.display_name;
        c.peers.push_back({host_sender_id_, name, false, true});
    }
    // Report identities are authenticated party IDs; ordinary four-port
    // lobbies use generation-bound port tokens because their Room codec does
    // not expose other clients' authentication IDs. No port is assigned here.
    if (c.party) {
        c.local_member = sender_id_;
        c.host_member = c.host_id;
        for (const auto& m : party->party.snapshot().members)
            if (m.connected)
                c.members.push_back({m.id, m.name, m.role == watch::Role::Spectator, true, m.id});
        for (auto& p : c.peers)
            p.member = p.id;
    } else {
        c.roster = room.generation;
        c.local_member = local_slot_ < 4 ? std::uint64_t(local_slot_) + 1 : 0;
        c.host_member = 1;
        for (unsigned i = 0; i < room.players.size(); ++i)
            if (room.players[i].occupied)
                c.members.push_back(
                    {std::uint64_t(i) + 1, room.players[i].display_name, false, true, std::uint64_t(i) + 1});
        for (auto& p : c.peers) {
            if (is_host_) {
                if (const auto* peer = peer_by_sender(p.id))
                    p.member = std::uint64_t(peer->slot) + 1;
            } else
                p.member = 1;
        }
    }
    std::sort(c.members.begin(), c.members.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(c.peers.begin(), c.peers.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (const auto* queue :
         {&critical_outbound_, &repair_outbound_, &authority_outbound_, &normal_priority_outbound_, &bulk_outbound_})
        for (const auto& packet : *queue)
            c.queued += packet.bytes.size();
    for (auto lane :
         {TransportTrafficClass::Control, TransportTrafficClass::Authoritative, TransportTrafficClass::Realtime,
          TransportTrafficClass::Replica, TransportTrafficClass::Checkpoint})
        c.queued += transport_->buffered_bytes(lane);
    return c;
};
