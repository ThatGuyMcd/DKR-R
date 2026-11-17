{
    std::string transfer_error;
    // Capability flags are membership scoped. A stale UI request cannot move
    // a member who left, nor overlap a race, seat swap, invitation or download.
    if (links.request) {
        const auto target = std::exchange(links.request, 0);
        if (is_host_ && waiting && !roster.handover && !direct::busy(links)) {
            const bool directory = method_ == ConnectionMethod::QuickJoin && dkr::online::hosting_mode(this) == 2;
            if (directory && !dkr::online::view()->host_handover) {
                links.status = "This directory has not enabled host handover yet.";
            } else {
                direct::Proposal p{links.scope, ++links.serial, target, links.save_owner, {}};
                p.state = digest_transfer(watch::wire::roster(roster));
                if (links.handover.offer(sender_id_, p, true, now, transfer_error)) {
                    reset_transfer();
                    links.directory_required = directory;
                    links.directory_code = directory ? invite_ : std::string{};
                    if (directory) {
                        const auto online = dkr::online::view();
                        links.directory_public = online->public_desired && !online->hidden;
                    }
                } else
                    links.status = transfer_error;
            }
        }
    }
    if (links.consent) {
        const bool accept = *links.consent;
        links.consent.reset();
        links.local_consent = accept;
        if (const auto& p = links.handover.proposal(); p && p->successor == sender_id_) {
            direct::Write value;
            value.number(p->serial).number(accept, 1);
            transfer_send(p->before.host, direct::Kind::Accept, std::move(value.bytes));
        }
    }
    if (links.cancel) {
        links.cancel = false;
        if (const auto& p = links.handover.proposal();
            p && links.handover.cancel(sender_id_, p->serial, "Host transfer cancelled.", transfer_error)) {
            direct::Write value;
            value.number(p->serial);
            if (sender_id_ == p->before.host)
                transfer_all(direct::Kind::Cancel, value.bytes);
            else
                transfer_send(p->before.host, direct::Kind::Cancel, std::move(value.bytes));
        }
    }
    const auto prior = links.handover.phase();
    links.handover.service(links.scope, waiting && !roster.handover, now);
    if (links.handover.phase() == direct::Phase::Cancelled && prior != direct::Phase::Cancelled) {
        if (const auto& p = links.handover.proposal(); p && sender_id_ == p->before.host) {
            direct::Write v;
            v.number(p->serial);
            transfer_all(direct::Kind::Cancel, v.bytes);
        }
        links.status = links.handover.status();
    }
    const auto proposal = links.handover.proposal();
    if (proposal && links.handover.phase() == direct::Phase::Preparing && sender_id_ == proposal->successor &&
        links.local_consent.value_or(false) && !links.registration_started) {
        links.successor_keys = secure::generate_key_pair();
        links.successor_capability = secure::generate_key();
        const auto bootstrap = "dkr-r-quick-v1\n" + hex64(match_id_) + "\n" + hex64(sender_id_) + "\n" +
                               secure::encode_key(links.successor_keys.public_key) + "\n" +
                               secure::encode_key(links.successor_capability) + "\n" +
                               std::to_string(unix_seconds() + 21600U);
        links.registration_started = direct::prepare_host(*transport_, bootstrap, transfer_error);
        if (!links.registration_started)
            links.status = transfer_error;
    }
    if (proposal && links.handover.phase() == direct::Phase::Preparing && sender_id_ == proposal->successor &&
        links.registration_started &&
        (!transport_->quick_join() || transport_->rekey_status() == QuickJoinRekeyStatus::Committed)) {
        links.registration_ready = true;
        links.successor_public = links.successor_keys.public_key;
        links.successor_endpoint = transport_->quick_join()
                                       ? direct::local_peer_name(*transport_)
                                       : std::string("127.0.0.1:") + std::to_string(transport_->local_port());
        links.successor_invite = transport_->quick_join() ? transport_->quick_join_code() : links.successor_endpoint;
    }
    if (proposal && sender_id_ == proposal->before.host && links.registration_ready && links.prepared_bytes.empty() &&
        links.handover.phase() == direct::Phase::Preparing) {
        auto room = lobby_payload();
        room.generation = roster.generation;
        room.phase = RoomPhase::Waiting;
        const auto snapshot = watch::wire::roster(roster), room_bytes = protocol::encode_lobby_state(room);
        auto keys = links.keys;
        for (const auto& peer : party->peers)
            keys[{std::min(sender_id_, peer.id), std::max(sender_id_, peer.id)}] = peer.key;
        direct::Write inventory;
        inventory.number(keys.size(), 1);
        for (const auto& [pair, key] : keys)
            inventory.number(pair.first).number(pair.second).data(key);
        links.inventory_bytes = std::move(inventory.bytes);
        links.inventory_hash = digest_transfer(links.inventory_bytes);
        direct::Write bundle;
        bundle.number(proposal->serial)
            .number(proposal->successor)
            .number(proposal->save_owner)
            .data(links.successor_public)
            .data(links.successor_capability)
            .text(links.successor_endpoint)
            .text(links.successor_invite)
            .number(manifest_class_hash(room_view_.manifest))
            .number(party->save_hash)
            .number(party->save_generation, 4)
            .data(links.inventory_hash)
            .number(mod_manifest_bytes_, 4)
            .number(snapshot.size(), 4)
            .data(snapshot)
            .number(room_bytes.size(), 4)
            .data(room_bytes)
            .data(party->seed)
            .number(links.save_paks.size(), 4)
            .data(links.save_paks)
            .number(links.save_records.size(), 4)
            .data(links.save_records)
            .number(party->maximum_racers, 1)
            .number(party->allow_spectators, 1)
            .number(party->launch_serial);
        bundle.number(roster.members.size(), 1);
        for (const auto& member : roster.members) {
            const auto architecture = links.architectures.find(member.id);
            if (architecture == links.architectures.end() || architecture->second.empty() ||
                architecture->second.size() > 128) {
                links.status =
                    "A member's platform identity is not available. Please rejoin before transferring the host.";
                bundle.bytes.clear();
                break;
            }
            bundle.number(member.id).text(architecture->second);
        }
        if (!bundle.bytes.empty() && bundle.bytes.size() <= direct::MaximumTransferBytes &&
            links.handover.bind(sender_id_, proposal->serial, digest_transfer(bundle.bytes), transfer_error)) {
            links.prepared_bytes = std::move(bundle.bytes);
            links.expected_transfer = links.prepared_bytes.size();
            links.prepared_state = parse_transfer(true);
        } else
            links.status =
                transfer_error.empty() ? "The transfer exceeds the bounded session save size." : transfer_error;
    }
    if (proposal && links.prepared_state && links.handover.phase() == direct::Phase::Preparing) {
        bool routes = true;
        for (const auto& member : proposal->before.members) {
            if (member.id == sender_id_)
                continue;
            if (sender_id_ != proposal->successor && member.id != proposal->successor)
                continue;
            const auto* peer = party->peer(member.id);
            const auto address = member.id == host_sender_id_ ? host_address_ : peer ? peer->address : PeerAddress{};
            if (!address) {
                routes = false;
                break;
            }
            for (const auto lane :
                 {TransportTrafficClass::Control, TransportTrafficClass::Authoritative, TransportTrafficClass::Realtime,
                  TransportTrafficClass::Replica, TransportTrafficClass::Checkpoint})
                routes = routes && transport_->traffic_ready(address, lane);
            const auto sample = links.samples.find({sender_id_, member.id});
            routes = routes && sample != links.samples.end() && sample->second.observations &&
                     now - sample->second.measured < std::chrono::seconds(5);
        }
        if (routes &&
            (sender_id_ != proposal->successor ||
             (links.inventory_ready && (manifest_.mod_manifest_hash.empty() || links.mod_ready))) &&
            (!links.directory_required || sender_id_ != proposal->successor || links.directory_accepted))
            links.handover.prepared(sender_id_, proposal->serial, links.handover.proposal()->state, true, now,
                                    transfer_error);
    }
    if (proposal && links.directory_required && now >= links.directory_send && links.prepared_state &&
        !links.released) {
        links.directory_send = now + std::chrono::seconds(2);
        const auto current = links.handover.proposal();
        const auto online = dkr::online::view();
        if (online->connected && online->signed_in && online->host_handover &&
            (sender_id_ != current->successor || !links.directory_public || !online->hidden) &&
            ((sender_id_ == current->successor && links.handover.phase() == direct::Phase::Preparing) ||
             (sender_id_ == current->before.host && ((links.handover.complete() && !links.directory_committed) ||
                                                     links.handover.phase() == direct::Phase::Preparing)))) {
            dkr::online::respond(this, {{"type", sender_id_ == current->successor ? "transfer_accept"
                                                 : links.handover.complete()      ? "transfer_commit"
                                                                                  : "transfer_prepare"},
                                        {"serial", std::to_string(current->serial)},
                                        {"epoch", hex64(match_id_)},
                                        {"digest", secure::encode_key(current->state)},
                                        {"code", links.directory_code},
                                        {"nextCode", links.successor_invite},
                                        {"sender", std::to_string(current->successor)}});
        }
    }
    if (now >= links.transfer_send && proposal) {
        links.transfer_send = now + std::chrono::milliseconds(200);
        const auto phase = links.handover.phase();
        const auto current = links.handover.proposal();
        if (sender_id_ == current->successor && links.released && !links.finished) {
            bool realtime_ready = true;
            for (const auto& member : party->party.snapshot().members)
                if (member.port && member.id != sender_id_) {
                    const auto* peer = party->peer(member.id);
                    realtime_ready = realtime_ready && peer && peer->rtt_count &&
                                     now - peer->rtt_at < std::chrono::seconds(5) &&
                                     now - peer->heard < std::chrono::seconds(5);
                }
            if (realtime_ready)
                transfer_send(current->before.host, direct::Kind::Activated, transfer_identity());
        }
        if (sender_id_ == current->successor && phase == direct::Phase::Offered && links.local_consent) {
            direct::Write value;
            value.number(current->serial).number(*links.local_consent, 1);
            transfer_send(current->before.host, direct::Kind::Accept, std::move(value.bytes));
        }
        if (sender_id_ == current->before.host && phase == direct::Phase::Cancelled) {
            direct::Write value;
            value.number(current->serial);
            transfer_all(direct::Kind::Cancel, value.bytes);
        }
        if (sender_id_ == current->before.host && phase == direct::Phase::Offered) {
            direct::Write value;
            value.number(current->serial).number(current->successor).number(current->save_owner).data(current->state);
            value.number(links.directory_required, 1).number(links.directory_public, 1).text(links.directory_code);
            transfer_all(direct::Kind::Offer, value.bytes);
        }
        if (phase == direct::Phase::Preparing) {
            if (sender_id_ == current->successor && links.registration_ready) {
                direct::Write value;
                value.number(current->serial)
                    .data(links.successor_public)
                    .data(links.successor_capability)
                    .text(links.successor_endpoint)
                    .text(links.successor_invite);
                transfer_send(current->before.host, direct::Kind::Registration, std::move(value.bytes));
            }
            if (sender_id_ == current->before.host) {
                direct::Write consent;
                consent.number(current->serial).number(1, 1);
                transfer_all(direct::Kind::Accept, consent.bytes);
                if (links.prepared_state) {
                    direct::Write prepare;
                    prepare.number(current->serial)
                        .data(current->state)
                        .number(links.prepared_bytes.size(), 4)
                        .text(links.successor_endpoint);
                    transfer_all(direct::Kind::Prepare, prepare.bytes);
                    if (!links.handover.ready(current->successor))
                        for (std::size_t at = 0; at < links.inventory_bytes.size(); at += direct::TransferChunk) {
                            direct::Write chunk;
                            chunk.number(current->serial)
                                .number(links.inventory_bytes.size(), 4)
                                .number(at, 4)
                                .data(std::span(links.inventory_bytes)
                                          .subspan(at,
                                                   std::min(direct::TransferChunk, links.inventory_bytes.size() - at)));
                            transfer_send(current->successor, direct::Kind::Inventory, std::move(chunk.bytes));
                        }
                    for (const auto& member : current->before.members)
                        if (member.id != sender_id_ && !links.handover.ready(member.id)) {
                            const auto offset = links.transfer_offsets[member.id];
                            for (unsigned n = 0;
                                 n < 8 && offset + n * direct::TransferChunk < links.prepared_bytes.size(); ++n) {
                                const auto at = offset + n * direct::TransferChunk,
                                           count = std::min(direct::TransferChunk, links.prepared_bytes.size() - at);
                                direct::Write chunk;
                                chunk.number(current->serial)
                                    .number(at, 4)
                                    .data(std::span(links.prepared_bytes).subspan(at, count));
                                transfer_send(member.id, direct::Kind::SaveChunk, std::move(chunk.bytes));
                            }
                        }
                }
            } else if (links.handover.ready(sender_id_))
                transfer_send(current->before.host, direct::Kind::Prepared, transfer_identity());
        }
        if (sender_id_ == current->before.host && links.handover.can_commit()) {
            links.handover.commit(sender_id_, current->serial, current->state, transfer_error);
            links.handover.acknowledge(sender_id_, current->serial, current->state, transfer_error);
        }
        if (sender_id_ == current->before.host && links.handover.phase() == direct::Phase::Committing)
            transfer_all(direct::Kind::Commit, transfer_identity());
        if (sender_id_ == current->before.host && links.handover.complete() &&
            (!links.directory_required || links.directory_committed)) {
            if (!links.released && apply_transfer())
                links.activated.insert(sender_id_);
            if (links.released) {
                transfer_all(direct::Kind::Release, transfer_identity());
                if (links.activated.size() == current->before.members.size()) {
                    if (!links.finished) {
                        links.finished = true;
                        links.finished_until = now + std::chrono::seconds(60);
                        links.status = "Host transferred. Ready to race.";
                    }
                    if (now < links.finished_until)
                        transfer_all(direct::Kind::Finished, transfer_identity());
                }
            }
        }
    }
}
