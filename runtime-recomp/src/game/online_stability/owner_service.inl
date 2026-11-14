// Runs only on the authenticated DirectSession owner, with its mutex held.
{
    namespace stability = dkr::online_stability;
    namespace watch = dkr::runtime::spectators;
    auto extension = stability::control(this);
    const auto now = std::chrono::steady_clock::now();
    auto party = watch::party_link(this);
    std::scoped_lock party_lock(party->mutex);
#include "netplay/peer_relay/owner_service.inl"
    stability::Identity identity;
    identity.room = match_id_;
    identity.local = sender_id_;
    identity.host = is_host_ ? sender_id_ : host_sender_id_;
    identity.authority = is_host_;
    identity.dynamic = room_view_.rules.automatic_input_delay;
    identity.manual_delay = room_view_.rules.manual_input_delay;
    identity.prediction_window = room_view_.rules.rollback_window;
    identity.local_racer = !party->enabled || !party->local_watching();
    identity.active = match_id_ && sender_id_ && identity.host &&
                      (state_ == ConnectionState::Hosting || state_ == ConnectionState::Lobby ||
                       state_ == ConnectionState::Loading || state_ == ConnectionState::Running);
    if (is_host_) {
        if (party->enabled) {
            for (const auto& peer : party->peers)
                if (const auto* member = party->party.find(peer.id)) {
                    unsigned recent = 0;
                    for (unsigned i = 0; i < std::min(4U, peer.rtt_count); ++i)
                        recent = std::max(recent, unsigned(peer.rtts[(peer.rtt_index - 1 - i) % peer.rtts.size()]));
                    identity.routes.push_back(
                        {peer.id, recent, member->role == watch::Role::Racer, member->port.value_or(255)});
                }
        } else
            for (const auto& peer : peers_)
                if (peer.active) {
                    double worst = peer.rtt_ms;
                    for (std::size_t i = 0; i < peer.rtt_sample_count; ++i)
                        if (now - peer.rtt_sample_times[i] <= std::chrono::seconds(10))
                            worst = std::max(worst, peer.rtt_samples[i]);
                    identity.routes.push_back(
                        {peer.sender_id, unsigned(std::clamp(worst, 0., 5000.)), true, peer.slot});
                }
    } else {
        const auto* host_member = party->enabled ? party->party.find(host_sender_id_) : nullptr;
        identity.routes.push_back({host_sender_id_,
                                   party->enabled ? unsigned(party->rtt_ms) : unsigned(network_rtt_ms_),
                                   !party->enabled || (host_member && host_member->role == watch::Role::Racer),
                                   host_member ? unsigned(host_member->port.value_or(255)) : 0U});
    }
    for (auto& route : identity.routes)
        if (const auto measured = dkr::relay::rtt(this, route.id))
            route.rtt_ms = measured;
    static thread_local std::chrono::steady_clock::time_point transport_report_at{};
    if (now >= transport_report_at) {
        transport_report_at = now + std::chrono::seconds(5);
        unsigned rtt = 0;
        for (const auto& route : identity.routes)
            if (route.racer)
                rtt = std::max(rtt, route.rtt_ms);
        std::fprintf(
            stderr,
            "[online][transport] role=%s maximum-route-rtt-ms=%u realtime-buffered=%zu authority-buffered=%zu control-buffered=%zu checkpoint-buffered=%zu\n",
            is_host_ ? "host" : "client", rtt, transport_->buffered_bytes(TransportTrafficClass::Realtime),
            transport_->buffered_bytes(TransportTrafficClass::Authoritative),
            transport_->buffered_bytes(TransportTrafficClass::Control),
            transport_->buffered_bytes(TransportTrafficClass::Checkpoint));
    }
    const auto missing = extension->identity().room == identity.room ? extension->delay().stalled_owners : 0;
    for (const auto& route : identity.routes)
        if (route.owner < 4 && (missing & (1U << route.owner)))
            dkr::relay::recover(this, route.id);
    extension->configure(std::move(identity));
#include "host_settings_service.inl"
    if (is_host_) {
        std::vector<stability::Join> pending;
        for (const auto& request : pending_joins_)
            if (request.active) {
                const auto attempt = std::uint64_t(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(request.first_seen.time_since_epoch())
                        .count());
                pending.push_back({request.sender_id, std::max(std::uint64_t(1), attempt), 0, 0, request.display_name,
                                   !request.mods_approved  ? stability::Stage::Approval
                                   : request.mods_verified ? stability::Stage::Entering
                                                           : stability::Stage::Review});
            }
        extension->pending(std::move(pending), now);
        if (auto publication = extension->publication(now))
            extension->send(0, std::move(*publication));
    }
    for (auto& packet : extension->outgoing()) {
        // Delay negotiation/reports already repeat and reject stale serials.
        // Don't retain old racing timing reports in SCTP's reliable queue.
        if (party->enabled && packet.bytes.size() >= 6 && packet.bytes[5] == stability::DelayPayloadKind) {
            (void)party->enqueue({packet.target, watch::LinkKind(stability::ExtensionKind),
                                  TransportTrafficClass::Realtime, std::move(packet.bytes)});
            continue;
        }
        auto payload = watch::link_encode(watch::LinkKind(stability::ExtensionKind), packet.bytes);
        auto send = [&](const auto& address, const auto& key) {
            send_with_key(address, key, protocol::MessageType(watch::PartyMessageType), payload);
        };
        if (is_host_) {
            if (party->enabled) {
                for (const auto& peer : party->peers)
                    if (!packet.target || packet.target == peer.id)
                        send(peer.address, peer.key);
            } else
                for (const auto& peer : peers_)
                    if (peer.active && (!packet.target || packet.target == peer.sender_id))
                        send(peer.address, peer.key);
        } else if (!packet.target || packet.target == host_sender_id_)
            send(host_address_, key_);
    }
#include "netplay/session_chat/owner_service.inl"
}
