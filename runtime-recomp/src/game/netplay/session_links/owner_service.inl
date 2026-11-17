// Session links' explicit owner boundary. Requires mutex_ and party.mutex.
void DirectSession::service_session_links_locked(dkr::runtime::spectators::PartyLink& value,
                                                 std::chrono::steady_clock::time_point now) {
    namespace watch = dkr::runtime::spectators;
    auto* party = &value;
    namespace direct = dkr::runtime::netplay::links;
    auto& links = party->links;
    const auto& roster = party->party.snapshot();
    if (links.scope.room != match_id_) {
        direct::forget(this);
        links.reset();
        links.save_owner = roster.host;
    }
    std::erase_if(links.capable, [&](auto id) { return !party->party.find(id); });
    std::erase_if(links.capacity, [&](const auto& entry) { return !party->party.find(entry.first); });
    const auto departed = [&](const auto& entry) { return !party->party.find(entry.first); };
    std::erase_if(links.pings, departed);
    std::erase_if(links.rtts, departed);
    std::erase_if(links.last_probe, departed);
    std::erase_if(links.probe_results, departed);
    std::erase_if(links.samples, [&](const auto& entry) {
        return !party->party.find(entry.first.first) || !party->party.find(entry.first.second);
    });
    links.capable.insert(sender_id_);
    links.architectures.insert_or_assign(sender_id_, manifest_.architecture);
    direct::Scope scope{match_id_, roster.host, roster.generation};
    for (const auto& m : roster.members)
        scope.members.push_back({m.id, m.port ? int(*m.port) : -1, links.capable.contains(m.id)});
    links.scope = scope;
    auto emit = [&](std::uint64_t target, direct::Kind kind, std::vector<std::uint8_t> bytes = {}) {
        const auto traffic = kind == direct::Kind::Ping || kind == direct::Kind::Pong ? TransportTrafficClass::Realtime
                                                                                      : TransportTrafficClass::Control;
        return party->enqueue({target, watch::LinkKind::Extension, traffic,
                               direct::encode({kind, match_id_, roster.generation, std::move(bytes)})});
    };
#include "handover_service.inl"
    for (auto it = links.keys.begin(); it != links.keys.end();) {
        if (!party->party.find(it->first.first) || !party->party.find(it->first.second)) {
            crypto_wipe(it->second.data(), it->second.size());
            it = links.keys.erase(it);
        } else
            ++it;
    }
    for (auto it = links.introduced.begin(); it != links.introduced.end();) {
        if (!party->party.find(it->first)) {
            direct::revoke(this, it->first);
            if (!is_host_) {
                if (const auto* peer = party->peer(it->first))
                    transport_->release_peer_route(peer->address);
                std::erase_if(party->peers, [&](const auto& p) { return p.id == it->first; });
            }
            it = links.introduced.erase(it);
        } else
            ++it;
    }
    if (now >= links.hello) {
        links.hello = now + std::chrono::seconds(1);
        direct::Write hello;
        hello.number(direct::ProtocolVersion, 1);
        if (!is_host_)
            emit(host_sender_id_, direct::Kind::Hello, std::move(hello.bytes));
    }
    for (unsigned work = 0; work < 32 && !links.incoming.empty(); ++work) {
        auto incoming = std::move(links.incoming.front());
        links.incoming.pop_front();
        const auto source = incoming.source;
        const auto& packet = incoming.packet;
        const auto& transfer = links.handover.proposal();
        const bool transferring =
            transfer && packet.kind >= direct::Kind::Offer && packet.generation == transfer->before.generation;
        if (packet.room != match_id_ || (!transferring && packet.generation != roster.generation) ||
            !party->party.find(source))
            continue;
        direct::Read read{packet.body};
        if (packet.kind == direct::Kind::Hello) {
            const auto version = read.number(1);
            const auto save_owner = !is_host_ && read.at < read.bytes.size() ? read.number() : 0;
            if (version == direct::ProtocolVersion && read.done() && (is_host_ || source == host_sender_id_)) {
                links.capable.insert(source);
                if (is_host_) {
                    direct::Write hello;
                    hello.number(direct::ProtocolVersion, 1).number(links.save_owner);
                    emit(source, direct::Kind::Hello, std::move(hello.bytes));
                } else if (save_owner)
                    links.save_owner = save_owner;
            }
        } else if (packet.kind == direct::Kind::Introduce) {
            if (is_host_ || source != host_sender_id_)
                continue;
            const auto id = read.number();
            const auto bytes = read.data(32);
            const auto endpoint = read.text();
            const auto* remote = party->party.find(id);
            if (!read.done() || !remote || id == sender_id_ || id == host_sender_id_ || bytes.size() != 32)
                continue;
            const bool racers = remote->role == watch::Role::Racer && !party->local_watching();
            const auto& proposal = links.handover.proposal();
            const bool transfer = proposal && links.handover.phase() == direct::Phase::Preparing &&
                                  (proposal->successor == id || proposal->successor == sender_id_);
            if (!racers && !transfer)
                continue;
            secure::Key key;
            std::copy(bytes.begin(), bytes.end(), key.begin());
            if (const auto old = links.introduced.find(id); old != links.introduced.end() && old->second.key != key)
                continue;
            PeerAddress address;
            std::string error;
            if (const auto old = links.introduced.find(id);
                old != links.introduced.end() && old->second.endpoint != endpoint)
                if (auto* peer = party->peer(id);
                    peer && !direct::alias_peer(*transport_, peer->address, endpoint, error))
                    continue;
            if (!direct::introduce_peer(*transport_, endpoint, sender_id_ < id, address, error))
                continue;
            auto* peer = party->peer(id);
            if (peer && (!(peer->address == address) || peer->key != key))
                continue;
            if (!direct::install(this, match_id_, id, address, key))
                continue;
            if (!peer) {
                watch::LinkPeer value;
                value.id = id;
                value.address = address;
                value.key = key;
                value.heard = now;
                party->peers.push_back(value);
            }
            links.capable.insert(id);
            links.introduced.insert_or_assign(id, direct::Introduction{id, key, endpoint, now});
            transport_->retain_peer_route(address);
        } else if (packet.kind == direct::Kind::Ping) {
            const auto token = read.number();
            if (!read.done() || !token)
                continue;
            if (source != host_sender_id_ && !links.introduced.contains(source) && !is_host_)
                continue;
            direct::Write pong;
            pong.number(token);
            emit(source, direct::Kind::Pong, std::move(pong.bytes));
        } else if (packet.kind == direct::Kind::Pong) {
            const auto token = read.number();
            if (!read.done())
                continue;
            auto pending = links.pings.find(source);
            if (pending == links.pings.end())
                continue;
            const auto sent = pending->second.find(token);
            if (sent == pending->second.end() || now < sent->second)
                continue;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - sent->second).count();
            pending->second.erase(sent);
            if (elapsed > 5000)
                continue;
            auto& values = links.rtts[source];
            values.push_back(unsigned(elapsed));
            if (values.size() > 32)
                values.pop_front();
            std::vector<unsigned> sorted(values.begin(), values.end());
            std::sort(sorted.begin(), sorted.end());
            auto& outcomes = links.probe_results[source];
            outcomes.push_back(true);
            if (outcomes.size() > 32)
                outcomes.pop_front();
            const auto losses = std::count(outcomes.begin(), outcomes.end(), false);
            links.samples[{sender_id_, source}] = {sorted[(sorted.size() - 1) * 95 / 100],
                                                   unsigned(1000U * losses / outcomes.size()), unsigned(values.size()),
                                                   now};
        } else if (packet.kind == direct::Kind::Report) {
            if (!is_host_)
                continue;
            const auto other = read.number(), p95 = read.number(2), loss = read.number(2), samples = read.number(1);
            if (!read.done() || !party->party.find(other) || other == source || p95 > 5000 || loss > 1000 ||
                samples > 32)
                continue;
            links.samples[{source, other}] = {unsigned(p95), unsigned(loss), unsigned(samples), now};
        } else
            handle_transfer(source, packet);
    }
    if (is_host_ && now >= links.introduce) {
        links.introduce = now + std::chrono::seconds(1);
        for (const auto& a : roster.members)
            for (const auto& b : roster.members) {
                if (a.id >= b.id || a.id == sender_id_ || b.id == sender_id_ || !links.capable.contains(a.id) ||
                    !links.capable.contains(b.id))
                    continue;
                const bool racers = a.role == watch::Role::Racer && b.role == watch::Role::Racer;
                const auto& proposal = links.handover.proposal();
                const bool transfer = proposal && links.handover.phase() == direct::Phase::Preparing &&
                                      (proposal->successor == a.id || proposal->successor == b.id);
                if (!racers && !transfer)
                    continue;
                auto* first = party->peer(a.id);
                auto* second = party->peer(b.id);
                if (!first || !second)
                    continue;
                auto key = links.keys.find({a.id, b.id});
                if (key == links.keys.end())
                    key = links.keys.emplace(direct::Pair{a.id, b.id}, secure::generate_key()).first;
                const auto first_name = direct::peer_name(*transport_, first->address),
                           second_name = direct::peer_name(*transport_, second->address);
                if (first_name.empty() || second_name.empty())
                    continue;
                direct::Write for_first;
                for_first.number(b.id).data(key->second).text(second_name);
                direct::Write for_second;
                for_second.number(a.id).data(key->second).text(first_name);
                emit(a.id, direct::Kind::Introduce, std::move(for_first.bytes));
                emit(b.id, direct::Kind::Introduce, std::move(for_second.bytes));
            }
    }
    if (now >= links.probe) {
        links.probe = now + std::chrono::milliseconds(500);
        links.live_routes.clear();
        auto probe = [&](std::uint64_t id, const PeerAddress& address) {
            if (!transport_->traffic_ready(address, TransportTrafficClass::Control))
                return;
            if (party->participant(sender_id_) && party->participant(id) &&
                transport_->traffic_ready(address, TransportTrafficClass::Realtime))
                links.live_routes.insert(id);
            auto& pending = links.pings[id];
            std::erase_if(pending, [&](const auto& p) {
                if (now - p.second <= std::chrono::seconds(5))
                    return false;
                auto& results = links.probe_results[id];
                results.push_back(false);
                if (results.size() > 32)
                    results.pop_front();
                return true;
            });
            if (pending.size() >= 12)
                return;
            const auto token =
                std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
            pending[token] = now;
            direct::Write ping;
            ping.number(token);
            emit(id, direct::Kind::Ping, std::move(ping.bytes));
        };
        if (is_host_) {
            for (const auto& peer : party->peers)
                if (links.capable.contains(peer.id))
                    probe(peer.id, peer.address);
        } else {
            if (links.capable.contains(host_sender_id_))
                probe(host_sender_id_, host_address_);
            for (const auto& [id, intro] : links.introduced)
                if (auto* peer = party->peer(id))
                    probe(id, peer->address);
        }
    }
    if (!is_host_ && now >= links.report) {
        links.report = now + std::chrono::seconds(1);
        for (const auto& [pair, sample] : links.samples)
            if (pair.first == sender_id_ && now - sample.measured < std::chrono::seconds(5)) {
                direct::Write report;
                report.number(pair.second)
                    .number(sample.p95_ms, 2)
                    .number(sample.loss_per_mille, 2)
                    .number(sample.observations, 1);
                emit(host_sender_id_, direct::Kind::Report, std::move(report.bytes));
            }
    }
#include "handover_pump.inl"
}
