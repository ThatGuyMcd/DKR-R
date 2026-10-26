#include "party_link.hpp"
#include "monocypher.h"
#include <algorithm>

namespace dkr::runtime::spectators {
void party_request_swap(const void* s, unsigned port, MemberId incoming) {
    auto h = party_link(s);
    std::scoped_lock lock(h->mutex);
    if (h->enabled && h->running && port < RacingPorts && incoming)
        h->requested_swap = {{port, incoming}};
}
void party_approve_swap(const void* s, unsigned port, MemberId incoming) {
    auto h = party_link(s);
    std::scoped_lock lock(h->mutex);
    if (h->host && h->running && port < RacingPorts && incoming)
        h->approved_swap = {{port, incoming}};
}
void party_cancel_swap(const void* s) {
    auto h = party_link(s);
    std::scoped_lock lock(h->mutex);
    h->cancel_swap = true;
}
bool party_waiting_role(const void* s, MemberId id, bool racer, std::string& e) {
    auto h = party_link(s);
    std::scoped_lock lock(h->mutex);
    if (!h->enabled || !h->host || h->launch || h->running || h->stopping) {
        e = "Roles can only be assigned directly while waiting in the lobby.";
        return false;
    }
    std::optional<std::uint8_t> port;
    if (racer) {
        for (unsigned p = 0; p < std::min(4U, h->maximum_racers); ++p)
            if (!h->party.snapshot().controls[p].member) {
                port = std::uint8_t(p);
                break;
            }
        if (!port) {
            e = "All racing places are occupied.";
            return false;
        }
    }
    if (!h->party.waiting_role(h->local, id, port, e))
        return false;
    h->ready.clear();
    h->advertised = {};
    return true;
}
namespace {
std::mutex bootstrap_mutex;
std::vector<char> bootstrap_bytes;
// DirectSession can be destroyed after translation-unit statics. The
// registry itself has process lifetime; normal session teardown still wipes
// keys and erases every retained PartyLink after the owner worker joins.
struct LinkRegistry {
    std::mutex mutex;
    std::unordered_map<const void*, std::shared_ptr<PartyLink>> links;
};
LinkRegistry& link_registry() {
    static auto* value = new LinkRegistry;
    return *value;
}
constexpr std::size_t QueueBytes = 4U * 1024U * 1024U, QueuePackets = 2048;
} // namespace
void party_bootstrap_seed(std::span<const std::uint8_t> bytes) {
    std::scoped_lock lock(bootstrap_mutex);
    bootstrap_bytes.assign(bytes.begin(), bytes.end());
}
bool party_bootstrap_active() {
    std::scoped_lock lock(bootstrap_mutex);
    return bootstrap_bytes.size() == 512;
}
std::filesystem::path party_bootstrap_path() {
    return {};
}
bool party_bootstrap_load(const std::filesystem::path&, std::vector<char>& bytes) {
    std::scoped_lock lock(bootstrap_mutex);
    if (bootstrap_bytes.size() != 512)
        return false;
    bytes = bootstrap_bytes;
    return true;
}
bool party_bootstrap_write(const std::filesystem::path&, const std::vector<char>& bytes) {
    std::scoped_lock lock(bootstrap_mutex);
    return bootstrap_bytes.size() == 512 && bytes == bootstrap_bytes;
}
std::vector<std::uint8_t> link_encode(LinkKind kind, std::span<const std::uint8_t> body) {
    if (kind < LinkKind::Accepted || kind > LinkKind::Leave || body.size() > 12300)
        return {};
    std::vector<std::uint8_t> out{'D', 'K', 'P', 'L', 1, std::uint8_t(kind)};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
std::vector<std::uint8_t> link_heartbeat(std::uint64_t token, bool reply) {
    std::vector<std::uint8_t> b{std::uint8_t(reply)};
    for (unsigned i = 0; i < 8; ++i)
        b.push_back(std::uint8_t(token >> (i * 8)));
    return b;
}
bool link_heartbeat(std::span<const std::uint8_t> b, std::uint64_t& token, bool& reply) {
    if (b.size() != 9 || b[0] > 1)
        return false;
    token = 0;
    reply = b[0] != 0;
    for (unsigned i = 0; i < 8; ++i)
        token |= std::uint64_t(b[i + 1]) << (i * 8);
    return token != 0;
}
bool link_decode(std::span<const std::uint8_t> b, LinkMessage& out) {
    if (b.size() < 6 || b.size() > 12306 ||
        !std::equal(b.begin(), b.begin() + 5, std::array<std::uint8_t, 5>{'D', 'K', 'P', 'L', 1}.begin()) ||
        b[5] < unsigned(LinkKind::Accepted) || b[5] > unsigned(LinkKind::Leave))
        return false;
    LinkMessage next{LinkKind(b[5]), {b.begin() + 6, b.end()}};
    out = std::move(next);
    return true;
}
LinkPeer* PartyLink::peer(MemberId id) {
    auto p = std::find_if(peers.begin(), peers.end(), [&](const auto& v) { return v.id == id; });
    return p == peers.end() ? nullptr : &*p;
}
LinkPeer* PartyLink::peer(const net::PeerAddress& a) {
    auto p = std::find_if(peers.begin(), peers.end(), [&](const auto& v) { return v.address == a; });
    return p == peers.end() ? nullptr : &*p;
}
bool PartyLink::local_watching() const {
    const auto* m = party.find(local);
    return admitted && m && m->role == Role::Spectator;
}
bool PartyLink::participant(MemberId id) const {
    const auto* m = party.find(id);
    return m && m->connected && (m->host || m->role == Role::Racer);
}
void PartyLink::reset_connection() {
    for (auto& p : peers)
        crypto_wipe(p.key.data(), p.key.size());
    enabled = host = admitted = running = stopping = returned = start_requested = ready_requested = launch_sent =
        stop_requested = false;
    local = authority = room_id = 0;
    party = {};
    ready.clear();
    pending_watch.clear();
    peers.clear();
    seed.clear();
    save_generation = 0;
    save_hash = 0;
    launch.reset();
    incoming.clear();
    for (auto& q : outgoing)
        q.clear();
    incoming_bytes = outgoing_bytes = 0;
    advertised = heartbeat = budget_at = {};
    bulk_tokens = 256 * 1024;
    status.clear();
    swap_requests.clear();
    requested_swap.reset();
    approved_swap.reset();
    cancel_swap = false;
    ping_token = 0;
    ping_sent = {};
    rtt_ms = 0;
}
bool PartyLink::enqueue(LinkOutgoing p) {
    const auto lane = unsigned(p.traffic);
    if (!enabled || lane >= outgoing.size() || p.bytes.size() > 12300 || p.bytes.size() > QueueBytes - outgoing_bytes)
        return false;
    auto& q = outgoing[lane];
    if (q.size() >= QueuePackets)
        return false;
    if (lane == 3 || lane == 4) {
        // Reserve racing input/control space independently of slow watchers.
        if (outgoing_bytes + p.bytes.size() > QueueBytes - 256 * 1024)
            return false;
        std::size_t retained = 0;
        for (const auto& item : q)
            if (item.target == p.target)
                retained += item.bytes.size();
        if (retained + p.bytes.size() > 256 * 1024)
            return false;
    }
    outgoing_bytes += p.bytes.size();
    q.push_back(std::move(p));
    return true;
}
bool PartyLink::admit_incoming(MemberId sender, std::span<const std::uint8_t> bytes) {
    if (!enabled || bytes.empty() || bytes.size() > 12000 || incoming.size() >= QueuePackets ||
        bytes.size() > QueueBytes - incoming_bytes)
        return false;
    if (host && !party.find(sender))
        return false;
    if (!host && sender != authority)
        return false;
    incoming.push_back({sender, {bytes.begin(), bytes.end()}});
    incoming_bytes += bytes.size();
    return true;
}
std::shared_ptr<PartyLink> party_link(const void* session) {
    auto& r = link_registry();
    std::scoped_lock lock(r.mutex);
    auto& result = r.links[session];
    if (!result)
        result = std::make_shared<PartyLink>();
    return result;
}
void forget_party_link(const void* session) {
    auto& r = link_registry();
    std::scoped_lock lock(r.mutex);
    r.links.erase(session);
}
void request_party_join(const void* session, bool spectate) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    p->requested = true;
    p->join_watch = spectate;
}
void request_party_host(const void* session, bool enabled) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    p->requested = enabled;
    p->join_watch = false;
}
LinkView party_link_view(const void* session) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    return {p->enabled,          p->host,  p->admitted, p->running, p->stopping, p->local,
            p->party.snapshot(), p->ready, p->status,   p->camera,  p->target,   p->swap_requests};
}
bool party_enabled(const void* session) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    return p->enabled;
}
bool party_watching(const void* session) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    return p->local_watching();
}
bool party_send(const void* session, MemberId target, std::span<const std::uint8_t> bytes,
                net::TransportTrafficClass traffic) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    return p->enqueue({target, LinkKind::Runtime, traffic, {bytes.begin(), bytes.end()}});
}
bool party_receive(const void* session, LinkIncoming& out) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    if (p->incoming.empty())
        return false;
    out = std::move(p->incoming.front());
    p->incoming_bytes -= out.bytes.size();
    p->incoming.pop_front();
    return true;
}
void party_ready(const void* session, bool ready) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    p->ready_requested = true;
    p->ready_value = ready;
}
void party_start(const void* session) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    if (p->host)
        p->start_requested = true;
}
void party_stop(const void* session, std::string reason) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    p->stop_requested = true;
    p->status = std::move(reason);
}
void party_returned(const void* session, std::span<const std::uint8_t> save) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    if (!p->enabled)
        return;
    p->returned = true;
    if (p->host && save.size() == 512)
        p->seed.assign(save.begin(), save.end());
}
void party_camera(const void* session, WatchCamera mode, unsigned target) {
    auto p = party_link(session);
    std::scoped_lock lock(p->mutex);
    if (mode <= WatchCamera::Director)
        p->camera = mode;
    p->target = std::min(target, 3U);
}
bool prepare_racing_ports(Party& party, std::string& error) {
    auto snapshot = party.snapshot();
    if (snapshot.handover || snapshot.generation == UINT64_MAX) {
        error = "Finish the pending player swap first.";
        return false;
    }
    std::vector<Member*> racers;
    for (auto& m : snapshot.members)
        if (m.role == Role::Racer)
            racers.push_back(&m);
    if (racers.size() < 2 || racers.size() > 4) {
        error = "A race needs two to four racers.";
        return false;
    }
    std::sort(racers.begin(), racers.end(),
              [](auto a, auto b) { return a->port.value_or(255) < b->port.value_or(255); });
    std::uint64_t generation = 0;
    for (const auto& c : snapshot.controls)
        generation = std::max(generation, c.generation);
    if (generation == UINT64_MAX) {
        error = "Controller ownership generation exhausted.";
        return false;
    }
    snapshot.controls = {};
    ++generation;
    ++snapshot.generation;
    for (auto& c : snapshot.controls)
        c.generation = generation;
    for (unsigned i = 0; i < racers.size(); ++i) {
        racers[i]->port = std::uint8_t(i);
        snapshot.controls[i] = {racers[i]->id, generation, {91, 0}};
    }
    return party.replace_verified(std::move(snapshot), error);
}
} // namespace dkr::runtime::spectators
