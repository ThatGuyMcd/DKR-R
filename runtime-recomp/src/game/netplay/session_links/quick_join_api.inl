std::string local_peer_name() const override {
    std::scoped_lock lock(mutex_);
    return local_peer_id_;
}
std::string peer_name(const PeerAddress& address) const override {
    std::scoped_lock lock(mutex_);
    for (const auto& [id, peer] : peers_)
        if (peer->address == address) {
            const auto alias = peer_aliases_.find(id);
            return alias == peer_aliases_.end() ? peer->remote_id : alias->second;
        }
    return {};
}
std::string signal_name(std::uint64_t id, std::string_view fallback) const {
    std::scoped_lock lock(mutex_);
    const auto alias = peer_aliases_.find(id);
    return alias == peer_aliases_.end() ? std::string(fallback) : alias->second;
}
std::size_t pinned_route_count_locked() const {
    std::set<std::uint64_t> ids;
    for (const auto& [name, route] : pinned_routes_)
        ids.insert(route.first);
    return ids.size();
}
bool introduce_peer(std::string_view name, bool initiate, PeerAddress& address, std::string& error) override {
    if (name.empty() || name.size() > 128 ||
        name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != name.npos) {
        error = "Invalid introduced Quick Join peer.";
        return false;
    }
    bool exists = false;
    {
        std::scoped_lock lock(mutex_);
        if (name == local_peer_id_ ||
            (authorized_remote_.size() >= 12 && !authorized_remote_.contains(std::string(name)))) {
            error = "The introduced peer exceeds this lobby's limits.";
            return false;
        }
        const std::string remote(name);
        authorized_remote_.insert(remote);
        auto pinned = pinned_routes_.find(remote);
        if (pinned == pinned_routes_.end()) {
            const auto id = next_peer_identifier_.fetch_add(1U, std::memory_order_relaxed);
            pinned = pinned_routes_.emplace(remote, std::pair{id, quick_address(id)}).first;
        }
        address = pinned->second.second;
        exists = std::any_of(peers_.begin(), peers_.end(), [&](const auto& p) {
            const auto alias = peer_aliases_.find(p.first);
            return (p.second->remote_id == name || (alias != peer_aliases_.end() && alias->second == name)) &&
                   p.second->current.load() &&
                   (p.second->route_ready.load() ||
                    std::chrono::steady_clock::now() - p.second->created < std::chrono::seconds(10));
        });
    }
    if (initiate && !exists) {
        try {
            create_offer(std::string(name));
        } catch (...) {
            error = "The direct peer route could not be prepared.";
            return false;
        }
    }
    error.clear();
    return true;
}
bool prepare_host(std::string bootstrap, std::string& error) override {
    if (host_) {
        error = "This player is already hosting.";
        return false;
    }
    preparing_host_.store(true);
    if (!rekey_quick_join(std::move(bootstrap), error)) {
        preparing_host_.store(false);
        return false;
    }
    return true;
}
bool alias_peer(const PeerAddress& address, std::string_view name, std::string& error) override {
    if (name.empty() || name.size() > 128 ||
        name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != name.npos) {
        error = "Invalid introduced peer alias.";
        return false;
    }
    std::scoped_lock lock(mutex_);
    for (const auto& [id, peer] : peers_)
        if (peer->address == address) {
            if (const auto pinned = pinned_routes_.find(std::string(name));
                pinned != pinned_routes_.end() && pinned->second.first != id) {
                error = "That peer name belongs to another lobby route.";
                return false;
            }
            authorized_remote_.erase(peer->remote_id);
            if (const auto prior = peer_aliases_.find(id); prior != peer_aliases_.end()) {
                authorized_remote_.erase(prior->second);
                if (prior->second != peer->remote_id)
                    pinned_routes_.erase(prior->second);
                remote_to_identifier_.erase(prior->second + "\n" + peer->connection_id);
            }
            const std::string alias(name);
            peer_aliases_[id] = alias;
            authorized_remote_.insert(alias);
            pinned_routes_[alias] = {id, address};
            remote_to_identifier_[alias + "\n" + peer->connection_id] = id;
            error.clear();
            return true;
        }
    // Admission can reserve a route before ICE creates its Peer. A quiet
    // successor registration may replace that name in the meantime.
    // Preserve the authenticated logical address and move its reservation;
    // waiting for a connection to the retired name cannot make progress.
    const auto reserved = std::find_if(pinned_routes_.begin(), pinned_routes_.end(),
                                       [&](const auto& p) { return p.second.second == address; });
    if (reserved != pinned_routes_.end()) {
        const auto id = reserved->second.first;
        const std::string alias(name);
        if (const auto collision = pinned_routes_.find(alias);
            collision != pinned_routes_.end() && collision->second.first != id) {
            error = "That peer name belongs to another lobby route.";
            return false;
        }
        for (auto it = pinned_routes_.begin(); it != pinned_routes_.end();) {
            if (it->second.first == id) {
                authorized_remote_.erase(it->first);
                it = pinned_routes_.erase(it);
            } else
                ++it;
        }
        std::erase_if(remote_to_identifier_, [&](const auto& p) { return p.second == id; });
        peer_aliases_[id] = alias;
        authorized_remote_.insert(alias);
        pinned_routes_[alias] = {id, address};
        error.clear();
        return true;
    }
    error = "The introduced route is no longer available.";
    return false;
}
bool apply_host_role(bool host, std::string_view remote, std::string& error) override {
    std::scoped_lock lock(mutex_);
    if (host && (!preparing_host_.load() || rekey_state_.load() != QuickJoinRekeyStatus::Committed)) {
        error = "The successor's Quick Join registration is not ready.";
        return false;
    }
    if (!host) {
        const auto route = std::find_if(peers_.begin(), peers_.end(), [&](const auto& p) {
            const auto alias = peer_aliases_.find(p.first);
            return (p.second->remote_id == remote || (alias != peer_aliases_.end() && alias->second == remote)) &&
                   p.second->route_ready.load();
        });
        if (route == peers_.end()) {
            error = "The successor's connection is not ready.";
            return false;
        }
        remote_host_id_ = std::string(remote);
    }
    for (const auto& [id, peer] : peers_) {
        const auto alias = peer_aliases_.find(id);
        const auto& name = alias == peer_aliases_.end() ? peer->remote_id : alias->second;
        peer->authority_route.store(!host && name == remote, std::memory_order_release);
    }
    host_ = host;
    preparing_host_.store(false);
    route_retry_requested_.store(false);
    error.clear();
    return true;
}
