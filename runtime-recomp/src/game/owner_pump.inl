// Inside DirectSession::pump_locked, before receiving packets. This is the only
// place directory intents are applied to native host state.
{
    namespace directory = dkr::online;
    namespace watch = dkr::runtime::spectators;
    auto owner = directory::native::owner(this);
    const auto online = directory::view();
    const auto now = std::chrono::steady_clock::now();
    const bool hosting = is_host_ && method_ == ConnectionMethod::QuickJoin && state_ != ConnectionState::Offline &&
                         state_ != ConnectionState::Failed;
    if (!hosting) {
        if (!owner->epoch.empty())
            directory::native::reset(this);
        directory::publish(this, directory::Json::object());
    } else {
        auto party = watch::party_link(this);
        std::scoped_lock party_lock(party->mutex);
        const auto epoch = hex64(match_id_);
        if (owner->epoch != epoch) {
            directory::native::reset(this);
            owner->epoch = epoch;
            owner->guarded = online->feature_enabled || online->public_desired;
        }
        if (owner->channel != online->connection_generation) {
            owner->channel = online->connection_generation;
            for (auto& a : owner->admissions)
                if (!a.consumed)
                    crypto_wipe(a.capability.data(), 32);
            std::erase_if(owner->admissions, [](const auto& a) { return !a.consumed; });
        }
        const bool hidden = online->hidden;
        const bool make_public = online->public_desired && !hidden;
        if (owner->public_room != make_public) {
            owner->public_room = make_public;
            ++owner->privacy;
            for (auto& a : owner->admissions)
                if (!a.consumed)
                    crypto_wipe(a.capability.data(), 32);
            std::erase_if(owner->admissions, [](const auto& a) { return !a.consumed; });
        }
        auto gate = dkr::runtime::party_start::gate(this);
        const bool starting = launch_countdown_active_ || launch_stage_ != LaunchStage::Idle ||
                              gate->phase == dkr::runtime::party_start::Phase::Countdown ||
                              gate->phase == dkr::runtime::party_start::Phase::Committing;
        const bool testing = connection_test_active_ || dkr::runtime::rollback_check::active(this);
        const bool returning = party->stopping || (owned_match_end_ && !owned_match_end_->resumed);
        const bool live = party->running || state_ == ConnectionState::Running;
        const bool loading = state_ == ConnectionState::Loading || bool(party->launch);
        const std::string phase = returning  ? "returning"
                                  : testing  ? "testing"
                                  : starting ? "starting"
                                  : live     ? "racing"
                                  : loading  ? "loading"
                                             : "waiting";
        unsigned racers = 0, spectators = 0, pending_racers = 0, pending_spectators = 0;
        std::uint64_t roster = room_view_.generation;
        if (party->enabled) {
            const auto snapshot = party->party.snapshot();
            roster = snapshot.generation;
            for (const auto& member : snapshot.members)
                if (member.connected) {
                    if (member.role == watch::Role::Spectator)
                        ++spectators;
                    else
                        ++racers;
                }
        } else
            racers = unsigned(occupied_players());
        for (const auto& p : pending_joins_)
            if (p.active) {
                if (party->enabled && party->pending_watch.contains(p.sender_id) && party->pending_watch[p.sender_id])
                    ++pending_spectators;
                else
                    ++pending_racers;
            }
        const auto authorization = phase + ":" + std::to_string(lobby_locked_) + ":" + std::to_string(make_public) +
                                   ":" + invite_ + ":" + secure::encode_key(host_key_pair_.public_key) + ":" +
                                   std::to_string(room_view_.rules.maximum_players);
        const auto policy =
            authorization + ":" + std::to_string(roster) + ":" + std::to_string(online->account_generation);
        if (owner->policy != policy) {
            const bool same_scope = owner->authorization == authorization;
            owner->authorization = authorization;
            owner->policy = policy;
            ++owner->privacy;
            for (auto& pending : pending_joins_)
                if (pending.active) {
                    auto* binding = directory::native::binding(*owner, pending.sender_id);
                    if (binding && binding->privacy != owner->privacy) {
                        // Host-approved immutable content preparation belongs to
                        // the authenticated native session, not a directory lease.
                        if (pending.mods_approved && same_scope) {
                            binding->privacy = owner->privacy;
                            continue;
                        }
                        send_with_key(pending.address, pending.key, protocol::MessageType::HelloAck,
                                      protocol::encode_hello_ack(
                                          {false, 0, "The host changed the lobby. Please request to join again."}));
                        party->pending_watch.erase(pending.sender_id);
                        clear_pending_mod_route_locked(pending);
                        pending = {};
                    }
                }
            for (auto& admission : owner->admissions)
                if (!admission.consumed)
                    crypto_wipe(admission.capability.data(), 32);
            std::erase_if(owner->admissions, [](const auto& a) { return !a.consumed; });
        }
        std::erase_if(owner->admissions, [&](auto& a) {
            auto* pending = pending_by_sender(a.sender);
            const bool member = party->enabled ? party->peer(a.sender) != nullptr : peer_by_sender(a.sender) != nullptr;
            const bool expired = directory::contract::expired_admission(a.expires <= now, a.consumed,
                                                                        pending && pending->mods_approved, member);
            const bool finished = a.consumed && !pending && !member;
            if (expired && pending) {
                send_with_key(pending->address, pending->key, protocol::MessageType::HelloAck,
                              protocol::encode_hello_ack(
                                  {false, 0, "The authenticated join request expired. Please request to join again."}));
                party->pending_watch.erase(pending->sender_id);
                clear_pending_mod_route_locked(*pending);
                *pending = {};
            }
            if (expired || finished)
                crypto_wipe(a.capability.data(), 32);
            return expired || finished;
        });
        for (auto& command : directory::commands(this)) {
            const auto type = command.value("type", std::string{});
            if (type == "revoke") {
                const auto id = command.value("id", std::string{});
                for (auto& a : owner->admissions)
                    if (a.id == id && !a.consumed)
                        crypto_wipe(a.capability.data(), 32);
                std::erase_if(owner->admissions, [&](const auto& a) { return a.id == id && !a.consumed; });
                continue;
            }
            if (type != "intent" || !command.contains("join"))
                continue;
            const auto& intent = command["join"];
            directory::native::Admission a;
            a.id = intent.value("id", std::string{});
            a.account = intent.value("account", std::string{});
            a.epoch = intent.value("epoch", std::string{});
            a.watching = intent.value("role", std::string{}) == "spectator";
            a.allow_running = intent.value("running", false) && online->running_spectators;
            a.privacy = owner->privacy;
            a.expires = now + std::chrono::seconds(30);
            const bool running_watch = directory::contract::running_spectator(
                a.watching, a.allow_running, party->enabled, party->running, party->stopping, starting);
            bool allowed = owner->guarded && online->connected && online->signed_in &&
                           (phase == "waiting" || phase == "racing" && running_watch) && !lobby_locked_ &&
                           (!intent.value("public", true) || owner->public_room) && a.epoch == owner->epoch &&
                           !owner->blocked.contains(a.account) && owner->admissions.size() < 16 &&
                           secure::decode_key(intent.value("key", std::string{}), a.client) &&
                           (a.watching ? party->enabled && spectators + pending_spectators < 8
                                       : racers + pending_racers < room_view_.rules.maximum_players);
            const auto friend_token = intent.value("friendAdmission", std::string{});
            if (allowed && !friend_token.empty()) {
                // A current friend capability preserves invited auto-admission.
                // Otherwise possession of the resolved code grants only the
                // ordinary host-approved request, never automatic entry.
                secure::Key proof{};
                a.automatic = secure::decode_key(friend_token, proof) && consume_friend_admission_locked(proof);
            }
            if (allowed) {
                a.capability = secure::generate_key();
                owner->admissions.push_back(a);
            }
            directory::respond(this, {{"type", "grant"},
                                      {"id", a.id},
                                      {"allowed", allowed},
                                      {"admission", allowed ? secure::encode_key(a.capability) : std::string{}}});
        }
        if (owner->guarded && now - owner->published >= std::chrono::milliseconds(250)) {
            owner->published = now;
            directory::publish(this, {{"host", true},
                                      {"title", room_view_.name},
                                      {"epoch", epoch},
                                      {"compatibility", manifest_.release_version},
                                      {"visibility", make_public ? "public" : "private"},
                                      {"phase", phase},
                                      {"racers", racers},
                                      {"maxRacers", unsigned(room_view_.rules.maximum_players)},
                                      {"spectators", spectators},
                                      {"maxSpectators", party->enabled ? 8 : 0},
                                      {"reservedRacers", pending_racers},
                                      {"reservedSpectators", pending_spectators},
                                      {"canSpectateRunning", party->enabled && online->running_spectators},
                                      {"locked", lobby_locked_},
                                      {"hidden", online->hidden},
                                      {"roster", std::to_string(roster)},
                                      {"code", invite_},
                                      {"bootstrap", invite_}});
        } else if (!owner->guarded && now - owner->published >= std::chrono::milliseconds(250)) {
            owner->published = now;
            directory::publish(this, {{"legacyHost", true}});
        }
    }
}
