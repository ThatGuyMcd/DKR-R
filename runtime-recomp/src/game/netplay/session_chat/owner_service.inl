// Runs after essential lobby/delay messages, on DirectSession's owner thread.
{
    namespace chat = dkr::chat;
    chat::Context context;
    context.room = match_id_;
    context.local = sender_id_;
    context.host = is_host_ ? sender_id_ : host_sender_id_;
    context.active = match_id_ && sender_id_ && context.host &&
                     (state_ == ConnectionState::Hosting || state_ == ConnectionState::Lobby ||
                      state_ == ConnectionState::Loading || state_ == ConnectionState::Running);
    context.allow = chat::host_enabled.load();
    context.members.push_back({sender_id_, local_name_});
    if (party->enabled) {
        for (const auto& m : party->party.snapshot().members)
            if (m.id != sender_id_)
                context.members.push_back({m.id, m.name});
    } else if (is_host_) {
        for (const auto& peer : peers_)
            if (peer.active)
                context.members.push_back({peer.sender_id, peer.slot < room_view_.players.size()
                                                               ? room_view_.players[peer.slot].display_name
                                                               : std::string{"Racer"}});
    } else {
        // Classic lobby payloads carry slot names but no peer identities.
        // Reuse the host-authenticated live roster already used by route cards.
        for (const auto& member : dkr::relay::view(this).lobby)
            if (member.endpoint != sender_id_)
                context.members.push_back({member.endpoint, member.name});
        for (const auto& player : room_view_.players)
            if (player.occupied) {
                const auto id = chat::peer_id(player.peer_id);
                if (id && id != sender_id_ &&
                    std::none_of(context.members.begin(), context.members.end(),
                                 [&](const auto& m) { return m.id == id; }))
                    context.members.push_back({id, player.display_name});
            }
    }
    auto messages = chat::channel(this);
    messages->configure(std::move(context));
    // Backpressure in the existing control channel takes precedence over chat.
    if (transport_->buffered_bytes(TransportTrafficClass::Control) < 16 * 1024)
        for (auto& packet : messages->outgoing()) {
            auto payload = watch::link_encode(watch::LinkKind::Extension, packet.bytes);
            if (is_host_) {
                if (party->enabled) {
                    if (const auto* peer = party->peer(packet.target))
                        send_with_key(peer->address, peer->key, protocol::MessageType(watch::PartyMessageType),
                                      payload);
                } else if (const auto* peer = peer_by_sender(packet.target))
                    send_with_key(peer->address, peer->key, protocol::MessageType(watch::PartyMessageType), payload);
            } else if (packet.target == host_sender_id_)
                send_with_key(host_address_, key_, protocol::MessageType(watch::PartyMessageType), payload);
        }
}
