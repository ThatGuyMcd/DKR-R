// Included inside dkr::sdk2. Authenticated transport calls this on its owner;
// only copied UI requests enter here, never a game pointer or a local write.
void network_epoch(std::uint64_t epoch, unsigned local, unsigned players) {
    std::lock_guard guard(mutex);
    if (!current.active || !current.online || !current.development || local >= players || players > 4)
        return;
    current.local = local;
    current.players = players;
    if (tool_epoch != epoch) {
        tool_epoch = epoch;
        tool_peers = {};
        current.requests.clear();
        current.grants = 0;
        cached.reset();
    }
}
std::vector<std::uint8_t> network_packet(unsigned peer) {
    std::lock_guard guard(mutex);
    if (!tool_epoch || !current.development || peer >= current.players || peer == current.local)
        return {};
    const auto& route = tool_peers[peer];
    const ToolMessage message = route.pending.empty() ? ToolMessage{} : route.pending.front();
    std::vector<std::uint8_t> out;
    out.reserve(96);
    const auto word = [&](std::uint32_t v) {
        for (unsigned b = 0; b < 4; ++b)
            out.push_back(std::uint8_t(v >> (24 - b * 8)));
    };
    word(0x444b5432);
    word(1);
    word(unsigned(tool_epoch >> 32));
    word(unsigned(tool_epoch));
    word(message.id);
    word(route.received);
    word(message.kind);
    word(message.target);
    for (auto value : message.command.words)
        word(value);
    return out;
}
bool network_receive(unsigned peer, std::span<const std::uint8_t> bytes) {
    std::lock_guard guard(mutex);
    if (bytes.size() != 96 || !tool_epoch || !current.active || !current.development || peer >= current.players ||
        peer == current.local)
        return false;
    const auto word = [&](unsigned offset) {
        std::uint32_t value = 0;
        for (unsigned b = 0; b < 4; ++b)
            value = (value << 8) | bytes[offset + b];
        return value;
    };
    const auto epoch = (std::uint64_t(word(8)) << 32) | word(12);
    if (word(0) != 0x444b5432 || word(4) != 1 || epoch != tool_epoch)
        return false;
    auto& route = tool_peers[peer];
    const unsigned id = word(16), ack = word(20), kind = word(24), target = word(28);
    if (ack > route.sent)
        return false;
    while (!route.pending.empty() && route.pending.front().id <= ack)
        route.pending.pop_front();
    if (!id)
        return kind == 0;
    if (id <= route.received)
        return true;
    if (id != route.received + 1)
        return false;
    Command command;
    for (unsigned i = 0; i < 16; ++i)
        command.words[i] = word(32 + i * 4);
    if (kind == 1 && current.host && peer) {
        if (!command.valid() || command.empty() || current.requests.size() >= 32)
            return false;
        const auto panel = std::find_if(current.panels.begin(), current.panels.end(),
                                        [&](const auto& p) { return p.owner + 1 == command.words[0]; });
        if (panel == current.panels.end() || !panel->development || command.words[15] != panel->scene)
            return false;
        if ((current.grants & (1U << peer)) && (route.pending.size() >= 32 || route.sent == UINT32_MAX))
            return false;
        if ((current.grants & (1U << peer)) && enqueue(*panel, command)) {
            if (!send_tool(peer, 2, target, command))
                return false;
            current.history.push_back("P" + std::to_string(peer + 1) + ": permitted tool request queued by host.");
        } else
            current.requests.push_back({peer, target, command, panel->name});
    } else if (kind == 2 && !current.host && peer == 0) {
        current.notice =
            command.words[0] ? "The host approved your tool request." : "The host declined your tool request.";
    } else if (kind == 3 && !current.host && peer == 0) {
        if (target >= current.players || command.words[0] > 1)
            return false;
        if (command.words[0])
            current.grants |= 1U << target;
        else
            current.grants &= ~(1U << target);
        current.notice = "The host updated development permissions.";
    } else
        return false;
    route.received = id;
    while (current.history.size() > 128)
        current.history.pop_front();
    cached.reset();
    return true;
}
bool decide(unsigned peer, unsigned request, bool approve) {
    std::lock_guard guard(mutex);
    if (!current.active || !current.host || !current.development || peer >= current.players)
        return false;
    const auto found = std::find_if(current.requests.begin(), current.requests.end(),
                                    [&](const auto& r) { return r.peer == peer && r.id == request; });
    if (found == current.requests.end() || tool_peers[peer].pending.size() >= 32)
        return false;
    const auto panel = std::find_if(current.panels.begin(), current.panels.end(),
                                    [&](const auto& p) { return p.owner + 1 == found->command.words[0]; });
    const bool valid = panel != current.panels.end() && panel->scene == found->command.words[15];
    if (approve && (!valid || !enqueue(*panel, found->command))) {
        current.notice = "That request is stale or the command queue is full.";
        cached.reset();
        return false;
    }
    if (!send_tool(peer, 2, request, approve ? found->command : Command{}))
        return false;
    current.history.push_back("P" + std::to_string(peer + 1) + " request #" + std::to_string(request) +
                              (approve ? " approved and queued." : " declined."));
    current.requests.erase(found);
    while (current.history.size() > 128)
        current.history.pop_front();
    cached.reset();
    return true;
}
bool grant(unsigned peer, bool allow) {
    std::lock_guard guard(mutex);
    if (!current.host || !current.development || peer == 0 || peer >= current.players)
        return false;
    for (unsigned route = 1; route < current.players; ++route)
        if (tool_peers[route].pending.size() >= 32)
            return false;
    Command flag;
    flag.words[0] = allow ? 1 : 0;
    for (unsigned route = 1; route < current.players; ++route)
        if (!send_tool(route, 3, peer, flag))
            return false;
    if (allow)
        current.grants |= 1U << peer;
    else
        current.grants &= ~(1U << peer);
    current.history.push_back("P" + std::to_string(peer + 1) +
                              (allow ? " granted tool-request permission." : " tool-request permission revoked."));
    while (current.history.size() > 128)
        current.history.pop_front();
    cached.reset();
    return true;
}
void observed(const Command& command) {
    if (command.empty())
        return;
    std::lock_guard guard(mutex);
    if (command.words[1] <= observed_serial)
        return;
    observed_serial = command.words[1];
    if (command.words[2] > 65532)
        return;
    current.history.push_back("Shared action " + std::to_string(command.words[2]) + " reached its ordered boundary (#" +
                              std::to_string(command.words[1]) + ").");
    while (current.history.size() > 128)
        current.history.pop_front();
    cached.reset();
}
