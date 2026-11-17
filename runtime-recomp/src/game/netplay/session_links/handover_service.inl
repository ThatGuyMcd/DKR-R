// Native host-transfer coordinator. All calls run under the session owner lock.
auto digest_transfer = [](std::span<const std::uint8_t> bytes) {
    direct::Digest result{};
    crypto_blake2b(result.data(), result.size(), bytes.data(), bytes.size());
    return result;
};
const bool waiting = !party->launch && !party->running && !party->stopping && !launch_countdown_active_ &&
                     launch_stage_ == LaunchStage::Idle && !connection_test_active_ &&
                     pending_invitation_ == std::nullopt &&
                     std::none_of(pending_joins_.begin(), pending_joins_.end(), [](const auto& p) { return p.active; });
auto transfer_send = [&](direct::Id target, direct::Kind kind, std::vector<std::uint8_t> bytes = {}) {
    const auto& p = links.handover.proposal();
    if (!p)
        return false;
    return party->enqueue({target, watch::LinkKind::Extension, TransportTrafficClass::Control,
                           direct::encode({kind, match_id_, p->before.generation, std::move(bytes)})});
};
auto transfer_all = [&](direct::Kind kind, const std::vector<std::uint8_t>& bytes) {
    if (const auto& p = links.handover.proposal())
        for (const auto& m : p->before.members)
            if (m.id != sender_id_)
                transfer_send(m.id, kind, bytes);
};
auto transfer_identity = [&] {
    direct::Write out;
    if (const auto& p = links.handover.proposal())
        out.number(p->serial).data(p->state);
    return out.bytes;
};
auto transfer_identity_ok = [&](direct::Read& r) {
    const auto digest = r.data(32);
    const auto& p = links.handover.proposal();
    return p && digest.size() == 32 && std::equal(digest.begin(), digest.end(), p->state.begin());
};
auto reset_transfer = [&] {
    links.registration_started = links.registration_ready = links.prepared_state = links.released = links.finished =
        false;
    links.successor_endpoint.clear();
    links.successor_invite.clear();
    links.prepared_bytes.clear();
    links.assembly = {};
    links.expected_transfer = 0;
    links.activated.clear();
    links.transfer_offsets.clear();
    links.transfer_send = {};
    links.local_consent.reset();
    links.inventory_assembly = {};
    links.inventory_bytes.clear();
    links.pending_keys.clear();
    links.inventory_hash = {};
    links.inventory_ready = false;
    links.directory_required = links.directory_public = links.directory_accepted = links.directory_committed = false;
    links.directory_code.clear();
    links.directory_send = {};
    links.mod_requested = links.mod_ready = false;
};
auto parse_transfer = [&](bool install) -> bool {
    const auto& p = links.handover.proposal();
    if (!p || links.prepared_bytes.empty() || digest_transfer(links.prepared_bytes) != p->state)
        return false;
    direct::Read r{links.prepared_bytes};
    if (r.number() != p->serial || r.number() != p->successor || r.number() != p->save_owner)
        return false;
    const auto pub = r.data(32), cap = r.data(32);
    const auto endpoint = r.text(), invite = r.text(256);
    const auto compatibility = r.number(), save_hash = r.number(), save_generation = r.number(4);
    const auto inventory_hash = r.data(32);
    const auto manifest_bytes = r.number(4);
    const auto roster_bytes = r.data(r.number(4)), room_bytes = r.data(r.number(4)), seed = r.data(512);
    const auto paks = r.data(r.number(4)), records = r.data(r.number(4));
    const auto maximum = r.number(1), spectators = r.number(1), launch_serial = r.number();
    std::map<direct::Id, std::string> architectures;
    const auto architecture_count = r.number(1);
    if (architecture_count != party->party.snapshot().members.size())
        return false;
    for (unsigned i = 0; i < architecture_count; ++i) {
        const auto id = r.number();
        const auto architecture = r.text(128);
        if (architecture.empty() || !party->party.find(id) || !architectures.emplace(id, architecture).second)
            return false;
    }
    const auto own_architecture = architectures.find(sender_id_);
    if (own_architecture == architectures.end() || own_architecture->second != manifest_.architecture)
        return false;
    if (!r.done() || pub.size() != 32 || cap.size() != 32 || inventory_hash.size() != 32 || seed.size() != 512 ||
        (!paks.empty() && paks.size() != 131072) || records.size() > 20 + 8192 * 76 || maximum < 1 || maximum > 4 ||
        spectators > 1)
        return false;
    watch::PartySnapshot prepared;
    protocol::LobbyStatePayload room;
    std::string error;
    if (!watch::wire::roster(roster_bytes, prepared, error) || !protocol::decode_lobby_state(room_bytes, room, error) ||
        prepared.host != p->before.host || prepared.generation != p->before.generation || prepared.handover ||
        prepared.members != party->party.snapshot().members || prepared.controls != party->party.snapshot().controls ||
        compatibility != manifest_class_hash(room_view_.manifest) || !save_generation ||
        stable_hash(std::string_view(reinterpret_cast<const char*>(seed.data()), seed.size())) != save_hash)
        return false;
    if (install) {
        std::copy(pub.begin(), pub.end(), links.successor_public.begin());
        std::copy(cap.begin(), cap.end(), links.successor_capability.begin());
        links.successor_endpoint = endpoint;
        links.successor_invite = invite;
        links.save_owner = p->save_owner;
        std::copy(inventory_hash.begin(), inventory_hash.end(), links.inventory_hash.begin());
        links.save_paks = paks;
        links.save_records = records;
        party->seed = seed;
        party->maximum_racers = unsigned(maximum);
        mod_manifest_bytes_ = std::uint32_t(manifest_bytes);
        party->allow_spectators = spectators != 0;
        party->launch_serial = launch_serial;
        party->save_generation = std::uint32_t(save_generation);
        party->save_hash = save_hash;
        session_save_ = seed;
        session_save_generation_ = party->save_generation;
        const auto local_architecture = manifest_.architecture;
        manifest_ = room_view_.manifest;
        manifest_.architecture = local_architecture;
        links.architectures = std::move(architectures);
        other_architecture_senders_.clear();
        for (const auto& [id, architecture] : links.architectures)
            if (id != sender_id_ && architecture != local_architecture)
                other_architecture_senders_.insert(id);
        manifest_.session_save_hash = save_hash;
        room_view_.manifest = manifest_;
        room_view_.name = room.room_name;
        room_view_.visibility = room.visibility;
        room_view_.rules = room.rules;
        input_delay_ = room.input_delay_frames;
    }
    return true;
};
auto apply_transfer = [&]() -> bool {
    const auto& p = links.handover.proposal();
    if (!p || links.last_applied == p->serial)
        return bool(p);
    if (!links.prepared_state || !parse_transfer(true))
        return false;
    std::string error;
    const bool successor = sender_id_ == p->successor;
    // Preserve the existing key's outbound nonce domain before adding a host
    // route to PartyLink. Membership changes never reset replay or AEAD state.
    if (!is_host_ && !party->peer(host_sender_id_)) {
        watch::LinkPeer old;
        old.id = host_sender_id_;
        old.address = host_address_;
        old.key = key_;
        old.sequence = outbound_sequence(host_address_, key_);
        old.heard = now;
        party->peers.push_back(old);
    }
    auto* next = successor ? nullptr : party->peer(p->successor);
    if (!successor && !next)
        return false;
    if (!direct::apply_host_role(*transport_, successor, links.successor_endpoint, error)) {
        links.status = error;
        return false;
    }
    if (!party->party.transfer_host(p->before.host, p->successor, p->before.generation + 1, error)) {
        links.status = error;
        return false;
    }
    for (const auto& peer : party->peers) {
        direct::install(this, match_id_, peer.id, peer.address, peer.key);
        links.introduced.try_emplace(
            peer.id, direct::Introduction{peer.id, peer.key, direct::peer_name(*transport_, peer.address), now});
    }
    if (successor) {
        // Routing roles changed. Prove fresh realtime round trips in the new
        // host role before publishing Finished or admitting the first Start.
        for (auto& peer : party->peers) {
            peer.rtt_count = peer.rtt_index = 0;
            peer.rtts = {};
            peer.rtt_at = peer.ping_sent = {};
            peer.ping_window = {};
        }
        links.keys = links.pending_keys;
        host_key_pair_ = links.successor_keys;
        invitation_capability_ = links.successor_capability;
        invite_ = transport_->quick_join() ? links.successor_invite
                                           : make_invite(advertised_host_.empty() ? "127.0.0.1" : advertised_host_);
        const auto room = room_view_;
        const auto* me = party->party.find(sender_id_);
        if (!lobby_.create(hex64(match_id_), "dkr-r/" + invite_, room.name, room.visibility, std::to_string(sender_id_),
                           me ? me->name : local_name_, manifest_, room.rules, error)) {
            links.status = error;
            return false;
        }
        room_view_ = lobby_.room();
        room_view_.generation = party->party.snapshot().generation;
        // Existing waiting members are projected from Party, not reassigned by
        // the four-slot legacy lobby. Their controller leases stay unchanged.
        peers_ = {};
        state_ = ConnectionState::Hosting;
    } else {
        host_address_ = next->address;
        key_ = next->key;
        host_public_ = links.successor_public;
        invitation_capability_ = links.successor_capability;
        invite_ = links.successor_invite;
        state_ = ConnectionState::Lobby;
        peers_ = {};
    }
    is_host_ = successor;
    host_sender_id_ = p->successor;
    party->host = successor;
    party->authority = p->successor;
    party->ready.clear();
    party->advertised = {};
    last_admission_response_ = now;
    last_client_request_ = now;
    links.last_applied = p->serial;
    links.released = true;
    links.status = "Host transferred. Confirming every connection.";
    return true;
};
auto handle_transfer = [&](direct::Id source, const direct::Packet& packet) {
    std::string error;
    direct::Read r{packet.body};
    if (packet.kind == direct::Kind::Offer) {
        if (source != host_sender_id_ || is_host_ || !waiting)
            return;
        direct::Proposal p;
        p.before = links.scope;
        p.serial = r.number();
        p.successor = r.number();
        p.save_owner = r.number();
        const auto digest = r.data(32);
        const auto directory = r.number(1), public_room = r.number(1);
        const auto code = r.text();
        if (!r.done() || digest.size() != 32 || directory > 1 || public_room > 1 ||
            (directory && !valid_quick_join_code(code)) || (!directory && !code.empty()))
            return;
        std::copy(digest.begin(), digest.end(), p.state.begin());
        for (auto& m : p.before.members)
            m.capable = true;
        if (links.handover.offer(source, p, waiting, now, error)) {
            if (links.serial != p.serial) {
                reset_transfer();
                links.serial = p.serial;
            }
            links.directory_required = directory != 0;
            links.directory_public = public_room != 0;
            links.directory_code = code;
            for (const auto& m : p.before.members)
                links.capable.insert(m.id);
            links.scope = p.before;
        } else
            links.status = error;
        return;
    }
    const auto proposal = links.handover.proposal();
    if (!proposal || packet.generation != proposal->before.generation)
        return;
    const auto serial = r.number();
    if (serial != proposal->serial)
        return;
    if (packet.kind == direct::Kind::Accept) {
        const auto accepted = r.number(1);
        if (!r.done() || accepted > 1 || (source != proposal->successor && source != proposal->before.host))
            return;
        // Relayed consent is sufficient for observers, but the candidate
        // must have accepted locally before opening a new host registration.
        if (sender_id_ == proposal->successor && accepted && !links.local_consent.value_or(false))
            return;
        if (links.handover.consent(proposal->successor, serial, accepted != 0, now, error) &&
            sender_id_ == proposal->before.host) {
            direct::Write consent;
            consent.number(serial).number(accepted, 1);
            transfer_all(direct::Kind::Accept, consent.bytes);
        }
    } else if (packet.kind == direct::Kind::Registration) {
        if (sender_id_ != proposal->before.host || source != proposal->successor ||
            links.handover.phase() != direct::Phase::Preparing || !links.prepared_bytes.empty())
            return;
        const auto pub = r.data(32), cap = r.data(32);
        const auto endpoint = r.text(), invite = r.text(256);
        if (!r.done() || pub.size() != 32 || cap.size() != 32 || endpoint.empty() || invite.empty())
            return;
        auto* peer = party->peer(source);
        if (!peer || !direct::alias_peer(*transport_, peer->address, endpoint, error))
            return;
        std::copy(pub.begin(), pub.end(), links.successor_public.begin());
        std::copy(cap.begin(), cap.end(), links.successor_capability.begin());
        links.successor_endpoint = endpoint;
        links.successor_invite = invite;
        links.registration_ready = true;
    } else if (packet.kind == direct::Kind::Prepare) {
        if (source != proposal->before.host || links.handover.phase() != direct::Phase::Preparing)
            return;
        const auto digest = r.data(32);
        const auto total = r.number(4);
        const auto endpoint = r.text();
        if (!r.done() || digest.size() != 32 || !total || total > direct::MaximumTransferBytes)
            return;
        direct::Digest hash;
        std::copy(digest.begin(), digest.end(), hash.begin());
        if (links.expected_transfer) {
            if (links.expected_transfer != total || proposal->state != hash)
                return;
        } else if (!links.handover.bind(source, serial, hash, error))
            return;
        if (auto* peer = party->peer(proposal->successor); peer && links.successor_endpoint != endpoint)
            if (!direct::alias_peer(*transport_, peer->address, endpoint, error))
                return;
        links.successor_endpoint = endpoint;
        links.expected_transfer = std::size_t(total);
    } else if (packet.kind == direct::Kind::SaveChunk) {
        if (source != proposal->before.host || !links.expected_transfer ||
            links.handover.phase() != direct::Phase::Preparing)
            return;
        const auto offset = r.number(4);
        const auto data = r.data(r.bytes.size() - std::min(r.at, r.bytes.size()));
        if (!r.done() || !links.assembly.accept(serial, links.expected_transfer, std::size_t(offset), data))
            return;
        std::size_t through = 0;
        for (bool v : links.assembly.received) {
            if (!v)
                break;
            through += direct::TransferChunk;
        }
        through = std::min(through, links.expected_transfer);
        direct::Write receipt;
        receipt.number(serial).number(through, 4);
        transfer_send(source, direct::Kind::SaveReceipt, std::move(receipt.bytes));
        if (links.assembly.complete() && !links.prepared_state) {
            links.prepared_bytes = links.assembly.bytes;
            links.prepared_state = parse_transfer(true);
        }
    } else if (packet.kind == direct::Kind::Inventory) {
        if (source != proposal->before.host || sender_id_ != proposal->successor || !links.prepared_state ||
            links.handover.phase() != direct::Phase::Preparing)
            return;
        const auto total = r.number(4), offset = r.number(4);
        const auto data = r.data(r.bytes.size() - std::min(r.at, r.bytes.size()));
        if (!r.done() || total > 1 + 66 * 48 ||
            !links.inventory_assembly.accept(serial, std::size_t(total), std::size_t(offset), data))
            return;
        if (links.inventory_assembly.complete() &&
            digest_transfer(links.inventory_assembly.bytes) == links.inventory_hash) {
            direct::Read inventory{links.inventory_assembly.bytes};
            const auto count = inventory.number(1);
            std::map<direct::Pair, secure::Key> keys;
            if (count > 66)
                return;
            for (unsigned i = 0; i < count; ++i) {
                const auto a = inventory.number(), b = inventory.number();
                const auto bytes = inventory.data(32);
                if (a >= b || !party->party.find(a) || !party->party.find(b) || bytes.size() != 32 ||
                    keys.contains({a, b}))
                    return;
                secure::Key key;
                std::copy(bytes.begin(), bytes.end(), key.begin());
                keys.emplace(direct::Pair{a, b}, key);
            }
            if (inventory.done()) {
                links.pending_keys = std::move(keys);
                links.inventory_ready = true;
            }
        }
    } else if (packet.kind == direct::Kind::SaveReceipt) {
        if (sender_id_ != proposal->before.host)
            return;
        const auto offset = r.number(4);
        if (!r.done() || offset > links.prepared_bytes.size() ||
            (offset != links.prepared_bytes.size() && offset % direct::TransferChunk))
            return;
        links.transfer_offsets[source] = std::max(links.transfer_offsets[source], std::size_t(offset));
    } else if (packet.kind == direct::Kind::Prepared) {
        if (sender_id_ != proposal->before.host || !transfer_identity_ok(r) || !r.done())
            return;
        links.handover.prepared(source, serial, proposal->state, true, now, error);
    } else if (packet.kind == direct::Kind::Commit) {
        if (source != proposal->before.host || !links.prepared_state || !links.handover.ready(sender_id_) ||
            !transfer_identity_ok(r) || !r.done())
            return;
        if (links.handover.phase() == direct::Phase::Preparing)
            for (const auto& member : proposal->before.members)
                links.handover.prepared(member.id, serial, proposal->state, true, now, error);
        if (links.handover.commit(source, serial, proposal->state, error))
            transfer_send(source, direct::Kind::Acknowledge, transfer_identity());
    } else if (packet.kind == direct::Kind::Acknowledge) {
        if (sender_id_ == proposal->before.host && transfer_identity_ok(r) && r.done())
            links.handover.acknowledge(source, serial, proposal->state, error);
    } else if (packet.kind == direct::Kind::Release) {
        if (source != proposal->before.host || !transfer_identity_ok(r) || !r.done() ||
            (links.handover.phase() != direct::Phase::Committing && links.handover.phase() != direct::Phase::Committed))
            return;
        if (apply_transfer() && sender_id_ != proposal->successor)
            transfer_send(source, direct::Kind::Activated, transfer_identity());
    } else if (packet.kind == direct::Kind::Activated) {
        if (sender_id_ == proposal->before.host && links.released && transfer_identity_ok(r) && r.done())
            links.activated.insert(source);
    } else if (packet.kind == direct::Kind::Finished) {
        if (source == proposal->before.host && links.released && transfer_identity_ok(r) && r.done()) {
            for (const auto& member : proposal->before.members)
                links.handover.acknowledge(member.id, serial, proposal->state, error);
            links.finished = true;
            links.status = "Host transferred. Ready to race.";
        }
    } else if (packet.kind == direct::Kind::Cancel) {
        if (!r.done() || (source != proposal->before.host && source != proposal->successor))
            return;
        if (links.handover.cancel(source, serial, "Host transfer cancelled. The original host is still in charge.",
                                  error) &&
            sender_id_ == proposal->before.host)
            transfer_all(direct::Kind::Cancel, packet.body);
    }
};
