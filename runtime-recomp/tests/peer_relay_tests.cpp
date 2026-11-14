#include "netplay/peer_relay/transport.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <memory>
#include <thread>
#include <cstring>
#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace dkr::relay;
namespace n = dkr::runtime::netplay;
using namespace std::chrono_literals;
namespace {
unsigned checks = 0;
void check(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "relay failure: %s\n", why);
        std::abort();
    }
}
struct Simulation {
    struct Packet {
        Id from, to, route;
        Bytes bytes;
        Clock::time_point at;
        bool control = false;
    };
    Clock::time_point now = Clock::time_point{} + 10s;
    std::array<std::unique_ptr<Engine>, MaxLobbyMembers + 1> nodes;
    Id count = 3;
    std::array<unsigned, MaxLobbyMembers + 1> opened{};
    std::vector<Packet> packets;
    std::vector<std::pair<Id, Bytes>> delivered;
    std::vector<std::string> signals;
    bool direct_good = false, proxy_dead = false, drop_control = false, drop_signal = false;
    bool blackhole = false, bad_quality = false, block_forward = false, repair_block = false, substitute = false,
         block_origin = false, reject_delivery = false;
    unsigned blackholed_data = 0, realtime_forwarded = 0;
    unsigned proxy_leg_ms = 10, direct_leg_ms = 100;
    std::map<Id, unsigned> probe_counts;
    unsigned dropped_control = 0;
    secure::Key original = secure::generate_key();
    Simulation(bool allow = true, Id peers = 3) : count(peers) {
        for (Id id = 1; id <= count; ++id) {
            Io io;
            io.send = [this, id](const Grant& g, Id to, std::span<const std::uint8_t> b, unsigned lane) {
                if (proxy_dead && (id == 3 || to == 3))
                    return false;
                Id sender = 0, sequence = 0;
                Bytes plain;
                check(secure::open(b.subspan(13), g.key, g.id, sender, sequence, plain),
                      "fixture relay envelope authenticates");
                const unsigned kind = plain[8];
                if (block_origin && id == 2 && to == 3 && kind == 0)
                    return false;
                if (kind == 0 && id == 3 && to == 1) {
                    if (blackhole) {
                        ++blackholed_data;
                        return true;
                    }
                    if (block_forward || (repair_block && lane == 1))
                        return false;
                    if (lane == 2)
                        ++realtime_forwarded;
                }
                if (bad_quality && g.endpoint == 2 && g.proxy == 3 && kind == 1 && (id == 1 || id == 2) &&
                    ++probe_counts[id] % 4 == 0)
                    return true;
                const bool direct = (id == 1 && to == 2) || (id == 2 && to == 1);
                const auto latency = bad_quality
                                         ? (direct ? 1600ms : 250ms)
                                         : (direct ? (direct_good ? 5ms : std::chrono::milliseconds(direct_leg_ms))
                                                   : std::chrono::milliseconds(proxy_leg_ms));
                Bytes outgoing(b.begin(), b.end());
                if (substitute && kind == 0 && id == 3 && to == 1) {
                    plain.back() ^= 1;
                    const auto forged = secure::seal(plain, g.key, g.proxy, sequence, g.id);
                    outgoing.resize(13);
                    outgoing.insert(outgoing.end(), forged.begin(), forged.end());
                }
                packets.push_back({id, to, g.id, std::move(outgoing), now + latency, false});
                return true;
            };
            io.ready = [this](const Grant& g) { return !g.proxy || !proxy_dead; };
            io.open = [this, id](const Grant&, bool) { ++opened[id]; };
            io.close = [](Id) {};
            io.signal = [this](Id, std::string s) { signals.push_back(std::move(s)); };
            io.deliver = [this](Id from, Bytes b, unsigned) {
                if (reject_delivery)
                    return false;
                delivered.emplace_back(from, std::move(b));
                return true;
            };
            nodes[id] = std::make_unique<Engine>(std::move(io));
        }
        configure(77, true, allow);
    }
    void configure(Id room = 77, bool volunteer = true, bool allow = true) {
        for (Id id = 1; id <= count; ++id) {
            Context c;
            c.room = room;
            c.host = 1;
            c.local = id;
            c.active = true;
            c.allow = allow;
            c.volunteer = (id == 3 || (count > 3 && id > 1)) && volunteer;
            for (Id peer = 1; peer <= count; ++peer)
                c.members.push_back({peer, "Player " + std::to_string(peer), "", count == 3 || peer != 1});
            nodes[id]->configure(c, now);
        }
    }
    void step() {
        now += 5ms;
        for (Id id = 1; id <= count; ++id) {
            nodes[id]->service(now);
            for (auto& c : nodes[id]->controls()) {
                if (drop_control && c.bytes.size() > 6 && c.bytes[6] == 3 && dropped_control++ == 0)
                    continue;
                if (drop_signal && c.bytes.size() > 39 && c.bytes[6] == 6 && c.bytes[39] == 4) {
                    drop_signal = false;
                    continue;
                }
                packets.push_back({id, c.target, 0, std::move(c.bytes), now + 5ms, true});
            }
        }
        auto pending = std::move(packets);
        packets.clear();
        for (auto& p : pending) {
            if (p.at > now) {
                packets.push_back(std::move(p));
                continue;
            }
            if (p.control)
                nodes[p.to]->control(p.from, p.bytes, now);
            else
                nodes[p.to]->wire(p.route, p.from, p.bytes, now);
        }
    }
    void run(std::chrono::milliseconds duration) {
        for (auto end = now + duration; now < end;)
            step();
    }
    Id selected(Id id = 1) const {
        for (const auto& r : nodes[id]->view(now).rows)
            if (r.endpoint == 2 && r.selected)
                return r.route;
        return 0;
    }
};
void policy_tests() {
    Samples s;
    const auto t = Clock::time_point{} + 10s;
    for (unsigned i = 1; i <= 64; ++i)
        s.add(i, false, t);
    check(s.summary(t).p50 == 32 && s.summary(t).p95 == 61 && s.summary(t).p99 == 64, "percentile tails");
    check(s.fresh(t) && !s.fresh(t + 4s) && s.summary(t + 33s).samples == 0, "measurement expiry");
    {
        Samples samples;
        for (unsigned n = 0; n < 16; ++n)
            samples.add(40, false, t);
        samples.add(2500, true, t + 1ms);
        check(samples.fresh(t + 1ms), "one failed probe preserves established liveness");
        check(samples.summary(t + 1ms).p95 == 40 && samples.summary(t + 1ms).lost == 1,
              "timeout is loss, not a fictitious measured RTT");
        samples.add(2500, true, t + 2ms);
        samples.add(2500, true, t + 3ms);
        check(!samples.fresh(t + 3ms), "consecutive probe failures retire liveness");
    }
    {
        Simulation sim;
        sim.run(14s);
        check(sim.selected() && sim.selected(2), "data health fixture selected");
        sim.blackhole = true;
        unsigned fallback = 0;
        for (unsigned n = 0; n < 80; ++n) {
            const auto bytes = secure::seal(Bytes{1, 2, 3}, sim.original, 2, n + 1, 77);
            fallback += !sim.nodes[2]->send(1, bytes, 2, sim.now);
            sim.run(50ms);
        }
        check(sim.blackholed_data > 0 && sim.delivered.empty(), "application data blackhole independent of probes");
        check(fallback > 0 && !sim.selected() && !sim.selected(2),
              "application stall falls back at both ends despite healthy pings");
    }
    {
        Simulation sim;
        sim.bad_quality = true;
        unsigned selections = 0;
        for (unsigned n = 0; n < 16000; ++n) {
            sim.step();
            selections += sim.selected() != 0;
        }
        check(selections == 0, "timeout-heavy or excessive-latency candidate never earns selection");
    }
    {
        Simulation sim;
        sim.proxy_leg_ms = 125;
        sim.direct_leg_ms = 400;
        sim.run(16s);
        check(sim.selected() && sim.selected(2), "long-RTT useful route selected for burst soak");
        unsigned sent = 0;
        for (unsigned n = 0; n < 1500; ++n) {
            for (unsigned burst = 0; burst < 4; ++burst) {
                const auto bytes = secure::seal(Bytes{1, 2, 3}, sim.original, 2, ++sent, 77);
                check(sim.nodes[2]->send(1, bytes, 2, sim.now), "healthy burst keeps a useful selected path");
            }
            sim.run(5ms);
        }
        sim.run(750ms);
        check(sim.selected() && sim.selected(2) && sim.delivered.size() == sent,
              "bounded receipt proofs survive 6000 packets over a 500ms route without false fallback");
    }
    {
        Simulation sim;
        sim.run(14s);
        sim.reject_delivery = true;
        for (unsigned n = 0; n < 80; ++n) {
            const auto bytes = secure::seal(Bytes{1, 2, 3}, sim.original, 2, n + 1, 77);
            sim.nodes[2]->send(1, bytes, 2, sim.now);
            sim.run(50ms);
        }
        check(sim.delivered.empty() && !sim.selected(2),
              "a full receiving queue cannot issue successful delivery receipts");
    }
    {
        Simulation sim;
        sim.run(14s);
        check(sim.selected() != 0, "send-pressure fixture selected");
        sim.block_origin = true;
        for (unsigned n = 0; n < 80; ++n) {
            const auto bytes = secure::seal(Bytes{1, 2, 3}, sim.original, 2, n + 1, 77);
            check(!sim.nodes[2]->send(1, bytes, 2, sim.now),
                  "blocked proxy allows immediate direct transport fallback");
            sim.run(50ms);
        }
        check(!sim.selected() && !sim.selected(2),
              "persistent send pressure retires proxy selection and updates both route cards");
    }
    {
        Simulation sim;
        sim.run(14s);
        sim.substitute = true;
        for (unsigned n = 0; n < 80; ++n) {
            const auto bytes = secure::seal(Bytes{1, 2, 3}, sim.original, 2, n + 1, 77);
            sim.nodes[2]->send(1, bytes, 2, sim.now);
            sim.run(50ms);
        }
        check(!sim.delivered.empty() && !sim.selected(2),
              "a proxy cannot forge useful-delivery health by substituting an invalid original payload");
    }
    {
        Simulation sim;
        sim.run(14s);
        check(sim.selected() != 0, "input-progress recovery fixture selected");
        sim.nodes[1]->recover(sim.now);
        sim.nodes[1]->recover(sim.now, 3);
        sim.run(500ms);
        check(sim.selected() != 0, "unattributed or another owner's input stall preserves the healthy selected route");
        sim.nodes[1]->recover(sim.now, 2);
        sim.run(500ms);
        check(!sim.selected() && !sim.selected(2) && sim.nodes[1]->view(sim.now).enabled,
              "confirmed input stall synchronizes direct fallback without disabling proxy setting");
        sim.run(8s);
        check(!sim.selected(), "input recovery quarantine prevents immediate re-selection");
    }
    {
        Simulation sim;
        sim.run(14s);
        const auto route = sim.selected(2);
        check(route != 0, "queue priority fixture selected");
        sim.block_forward = true;
        unsigned rejected = 0;
        for (unsigned n = 0; n < 128; ++n) {
            const auto bytes = secure::seal(Bytes{1, 2, 3}, sim.original, 2, n + 1, 77);
            check(sim.nodes[2]->send(1, bytes, n == 127 ? 2 : 1, sim.now), "endpoint accepts fixture input");
            const auto at = std::find_if(sim.packets.begin(), sim.packets.end(),
                                         [&](const auto& p) { return p.route == route && p.from == 2 && p.to == 3; });
            check(at != sim.packets.end(), "queued endpoint packet found");
            auto wire = std::move(at->bytes);
            sim.packets.erase(at);
            const bool accepted = sim.nodes[3]->wire(route, 2, wire, sim.now);
            if (n == 127)
                check(accepted, "realtime has reserved route headroom");
            else
                rejected += !accepted;
        }
        check(sim.nodes[3]->view(sim.now).queued < 128 * 1024, "priority fixture below byte bound");
        check(rejected > 0, "a blocked route cannot consume every peer's queue space");
        const auto dropped = sim.nodes[3]->view(sim.now).dropped;
        sim.block_forward = false;
        sim.repair_block = true;
        for (unsigned n = 0; n < 10; ++n) {
            sim.now += 10ms;
            sim.nodes[3]->service(sim.now);
        }
        check(sim.realtime_forwarded == 1 && sim.nodes[3]->view(sim.now).dropped == dropped,
              "127 blocked repairs cannot expire a writable realtime packet");
    }
    {
        Simulation sim(false);
        sim.run(2s);
        check(sim.nodes[1]->view(sim.now).rows.empty(), "default off creates no routes");
    }
    {
        Simulation sim(false);
        sim.run(2s);
        for (Id id = 1; id <= 3; ++id) {
            const auto v = sim.nodes[id]->view(sim.now);
            check(lobby_route(v, "2").text == "Direct", "all members see direct status with proxy policy off");
            check(lobby_route(v, "1", true).text == "Host", "host card is not a self-connection");
        }
        check(lobby_route(sim.nodes[3]->view(sim.now + 5s), "2").checking,
              "missing host heartbeat expires other-player status");
        check(lobby_route(sim.nodes[3]->view(sim.now), "184467440737095516160").checking,
              "invalid card identity cannot overflow");
    }
    {
        Simulation sim(false, 6);
        auto c = sim.nodes[1]->context();
        c.generation = 42;
        for (auto& m : c.members) {
            m.slot = m.id <= 4 ? unsigned(m.id - 1) : 255;
            m.ping_ms = m.id == 1 ? 0 : unsigned(25 * m.id);
        }
        sim.nodes[1]->configure(c, sim.now);
        sim.run(2s);
        for (Id viewer = 1; viewer <= 6; ++viewer) {
            const auto v = sim.nodes[viewer]->view(sim.now);
            for (Id id = 2; id <= 6; ++id) {
                const auto ping = lobby_card_ping(v, std::to_string(id), false, id == viewer, unsigned(id - 1), 42);
                check(ping.ms == 25 * id && ping.text == std::to_string(25 * id) + " ms to host",
                      "all clients and spectators see each host-relative RTT, including their own");
            }
            const auto host = lobby_card_ping(v, "1", true, viewer == 1, 0, 42);
            check(!host.ms && host.text == (viewer == 1 ? "This PC" : "Host"),
                  "host has no fabricated zero-ms network measurement");
        }
        c.members[1].ping_ms = 177;
        sim.nodes[1]->configure(c, sim.now);
        sim.run(500ms);
        check(lobby_card_ping(sim.nodes[2]->view(sim.now), "2", false, true, 1, 42).ms == 177,
              "own ping updates live with proxy policy off");
        c.members[1].ping_ms = 0;
        sim.nodes[1]->configure(c, sim.now);
        sim.run(500ms);
        check(lobby_card_ping(sim.nodes[2]->view(sim.now), "2", false, true, 1, 42).text == "Measuring...",
              "expired host probe clears an earlier RTT");
        check(!lobby_card_ping(sim.nodes[3]->view(sim.now), "2", false, false, 1, 43).ms,
              "different roster generation cannot relabel an old RTT");
        check(!lobby_card_ping(sim.nodes[3]->view(sim.now + 5s), "3", false, true, 2, 42).ms,
              "silent host expires local and remote ping readings");
        sim.configure(78);
        check(!lobby_card_ping(sim.nodes[2]->view(sim.now), "2", false, true, 1, 42).ms,
              "rehosting clears previous ping readings");
    }
    {
        Simulation sim;
        sim.run(14s);
        const auto v = sim.nodes[3]->view(sim.now);
        check(lobby_card_ping(v, "2", false, false, 1, 0).ms > 0 && lobby_route(v, "2").proxy,
              "selected proxy RTT is shared with another client");
        check(lobby_card_ping(v, "2", false, false, 1, 0).ms == sim.nodes[1]->rtt(2, sim.now),
              "proxy card and active measured route use the same RTT");
    }
    {
        Simulation sim;
        for (Id id = 1; id <= 3; ++id) {
            auto c = sim.nodes[id]->context();
            c.generation = 42;
            for (auto& m : c.members) {
                m.slot = unsigned(m.id - 1);
                m.name = "Same name";
            }
            sim.nodes[id]->configure(std::move(c), sim.now);
        }
        sim.run(14s);
        for (Id id = 1; id <= 3; ++id) {
            const auto v = sim.nodes[id]->view(sim.now);
            check(lobby_card_route(v, "", false, id == 2, 1, 42).proxy,
                  "classic client cards resolve an empty peer ID using authenticated slots, including duplicate names");
            check(lobby_card_peer(v, "", false, id == 2, 1, 42) == "2",
                  "picture and friend controls resolve the same authenticated member as the route label");
            check(lobby_card_route(v, "", true, false, 0, 42).text == "Host",
                  "classic host card stays distinct from the route to the host");
        }
        const auto client = sim.nodes[2]->view(sim.now);
        check(lobby_card_route(client, "", false, true, 1, 43).proxy,
              "local route uses the endpoint's own confirmed selection during a roster update");
        check(lobby_card_route(sim.nodes[3]->view(sim.now), "", false, false, 1, 43).checking,
              "old roster positions cannot identify a new occupant");
        check(lobby_card_peer(sim.nodes[3]->view(sim.now), "", false, false, 1, 43).empty(),
              "stale slots cannot select another player's picture or friend link");
        sim.proxy_dead = true;
        sim.run(5s);
        for (Id id = 1; id <= 3; ++id)
            check(lobby_card_route(sim.nodes[id]->view(sim.now), "", false, id == 2, 1, 42).text == "Direct",
                  "classic cards update live on direct fallback");
        auto host = sim.nodes[1]->context();
        host.generation = 43;
        std::swap(host.members[1].slot, host.members[2].slot);
        sim.nodes[1]->configure(std::move(host), sim.now);
        sim.run(1s);
        const auto moved = sim.nodes[2]->view(sim.now);
        check(moved.lobby_generation == 43 && moved.lobby[1].slot == 2 && moved.lobby[2].slot == 1,
              "slot moves replace the authenticated display mapping");
        check(lobby_card_route(sim.nodes[3]->view(sim.now + 5s), "", false, false, 1, 43).checking,
              "expired heartbeats cannot leave stale slot routes");
    }
    {
        Simulation sim;
        sim.run(14s);
        for (Id id = 1; id <= 3; ++id) {
            const auto v = sim.nodes[id]->view(sim.now);
            check(lobby_route(v, "2").text == "Via Player 3" && lobby_route(v, "2").proxy,
                  "host, client and relay see the chosen proxy name");
        }
        const auto own = sim.nodes[2]->view(sim.now);
        auto without_report = own;
        without_report.lobby.clear();
        check(lobby_route(without_report, "2").proxy, "own confirmed selection is visible before a status heartbeat");
        auto fallen_back = own;
        for (auto& row : fallen_back.rows)
            row.selected = false;
        check(lobby_route(fallen_back, "2").text == "Direct", "own fallback overrides an older host snapshot");
        sim.proxy_dead = true;
        sim.run(5s);
        for (Id id = 1; id <= 3; ++id)
            check(lobby_route(sim.nodes[id]->view(sim.now), "2").text == "Direct",
                  "fallback is published to every member");
        sim.configure(78);
        check(lobby_route(sim.nodes[3]->view(sim.now), "2").checking, "rehosting clears previous lobby route labels");
    }
    {
        Simulation sim(true, 4);
        for (Id id = 1; id <= 4; ++id) {
            auto c = sim.nodes[id]->context();
            for (auto& m : c.members)
                if (m.id == 4)
                    m.racer = false;
            sim.nodes[id]->configure(std::move(c), sim.now);
        }
        sim.run(14s);
        const auto v = sim.nodes[4]->view(sim.now);
        check(lobby_route(v, "2").text == "Via Player 3", "spectators receive selected racing routes too");
        check(sim.nodes[1]->view(sim.now).rows.size() == 4, "spectators do not become relay candidates");
    }
    {
        Simulation sim;
        sim.run(14s);
        const auto route = sim.selected();
        check(route != 0, "role swap fixture has an active relay");
        for (Id id = 1; id <= 3; ++id) {
            auto c = sim.nodes[id]->context();
            for (auto& m : c.members)
                if (m.id == 3)
                    m.racer = false;
            sim.nodes[id]->configure(std::move(c), sim.now);
        }
        sim.run(2s);
        check(!sim.selected() && !sim.selected(2) && !sim.nodes[3]->grant(route),
              "moving the relay to spectators retires its gameplay routes");
        for (Id id = 1; id <= 3; ++id)
            check(lobby_route(sim.nodes[id]->view(sim.now), "2").text == "Direct",
                  "role swap keeps status visible without retaining a spectator relay");
    }
    {
        auto append = [](Bytes& b, Id n, unsigned width = 8) {
            for (unsigned i = 0; i < width; ++i)
                b.push_back(std::uint8_t(n >> (i * 8)));
        };
        auto packet = [&](Id sequence) {
            Bytes b{'D', 'K', 'R', 'X', 1, 3, 12};
            append(b, 77);
            append(b, sequence);
            append(b, 3, 1);
            for (Id id = 1; id <= 3; ++id) {
                append(b, id);
                append(b, id == 2 ? 3 : 0);
                const std::string name = "Player " + std::to_string(id), via = id == 2 ? "Player 3" : "";
                append(b, name.size(), 2);
                b.insert(b.end(), name.begin(), name.end());
                append(b, via.size(), 2);
                b.insert(b.end(), via.begin(), via.end());
            }
            return b;
        };
        Engine observer(Io{});
        Context c;
        c.room = 77;
        c.local = 3;
        c.host = 1;
        c.active = true;
        observer.configure(c, t);
        const auto newer = packet(2), older = packet(1);
        check(!observer.control(2, newer, t), "a client cannot publish another player's route");
        check(observer.control(1, newer, t), "authenticated host publishes display metadata without granting a route");
        check(observer.view(t).rows.empty() && !observer.usable(2, t) && !observer.rtt(2, t),
              "status snapshots cannot create transport permissions");
        check(!observer.control(1, older, t) && !observer.control(1, newer, t),
              "reordered and duplicate snapshots cannot replace newer status");
        auto malformed = packet(3);
        malformed.pop_back();
        check(!observer.control(1, malformed, t), "truncated snapshot rejected atomically");
        auto wrong_room = packet(3);
        wrong_room[7] ^= 1;
        check(!observer.control(1, wrong_room, t), "another room cannot update cards");
        auto duplicate = packet(3);
        duplicate[24] = 2;
        check(!observer.control(1, duplicate, t), "duplicate member identities rejected");
        auto self_proxy = packet(3);
        self_proxy[32] = 1;
        check(!observer.control(1, self_proxy, t), "a host cannot advertise a proxy for itself");
        check(lobby_route(observer.view(t), "2").text == "Via Player 3",
              "malformed snapshots preserve last valid state");
        check(observer.control(1, packet(3), t + 1s), "invalid newer snapshot does not consume its sequence number");
        check(lobby_route(observer.view(t + 6s), "2").checking, "silent host does not leave a stale relay label");
        auto mapped = packet(4);
        append(mapped, 0x53, 1);
        append(mapped, 42);
        for (unsigned slot = 0; slot < 3; ++slot)
            append(mapped, slot, 1);
        auto invalid = mapped;
        invalid.back() = 1;
        check(!observer.control(1, invalid, t + 2s), "duplicate racing slots cannot replace the display mapping");
        invalid = mapped;
        invalid.back() = 4;
        check(!observer.control(1, invalid, t + 2s), "out-of-range racing slots are rejected");
        invalid = mapped;
        invalid.pop_back();
        check(!observer.control(1, invalid, t + 2s), "truncated slot mapping cannot publish partial identities");
        check(observer.control(1, mapped, t + 2s) &&
                  lobby_card_route(observer.view(t + 2s), "", false, false, 1, 42).proxy,
              "valid slot map is accepted after invalid copies without consuming its sequence");
        auto measured = packet(5);
        append(measured, 0x53, 1);
        append(measured, 42);
        for (unsigned slot = 0; slot < 3; ++slot)
            append(measured, slot, 1);
        append(measured, 0x50, 1);
        append(measured, 0, 2);
        append(measured, 135, 2);
        append(measured, 80, 2);
        invalid = measured;
        invalid.pop_back();
        check(!observer.control(1, invalid, t + 3s), "truncated RTT list is rejected atomically");
        invalid = measured;
        invalid[invalid.size() - 6] = 1;
        check(!observer.control(1, invalid, t + 3s), "host self-RTT cannot masquerade as a measurement");
        invalid = measured;
        invalid[invalid.size() - 4] = 0xff;
        invalid[invalid.size() - 3] = 0xff;
        check(!observer.control(1, invalid, t + 3s), "out-of-range RTT snapshot is rejected");
        check(!observer.control(2, measured, t + 3s), "a client cannot forge another player's ping");
        check(observer.control(1, measured, t + 3s) &&
                  lobby_card_ping(observer.view(t + 3s), "", false, false, 1, 42).ms == 135,
              "valid RTT list survives rejected copies and resolves authenticated classic slots");
        check(lobby_card_ping(observer.view(t + 3s), "3", false, true, 2, 42).ms == 80,
              "spectator sees its own host-relative ping");
        check(!observer.control(1, mapped, t + 3s), "old status cannot roll a ping reading back");
    }
    {
        Engine host(Io{});
        Context c;
        c.room = 77;
        c.local = c.host = 1;
        c.active = true;
        for (Id id = 1; id <= MaxLobbyMembers; ++id)
            c.members.push_back({id, std::string(24, 'a'), {}, false});
        host.configure(c, t);
        host.service(t);
        unsigned received = 0;
        for (;;) {
            const auto packets = host.controls();
            if (packets.empty())
                break;
            for (const auto& p : packets) {
                check(p.bytes.size() <= 900, "maximum lobby snapshot stays within control packet budget");
                Engine member(Io{});
                Context m;
                m.room = 77;
                m.local = p.target;
                m.host = 1;
                m.active = true;
                member.configure(m, t);
                check(member.control(1, p.bytes, t) && member.view(t).lobby.size() == MaxLobbyMembers,
                      "all twelve lobby members fit one snapshot");
                ++received;
            }
        }
        check(received == MaxLobbyMembers - 1, "every client and spectator receives maximum lobby status");
    }
    {
        Simulation sim;
        sim.drop_control = true;
        sim.run(13s);
        const auto route = sim.selected();
        check(route && sim.selected(2) == route, "beneficial path selected at both ends despite lost grant");
        check(sim.nodes[1]->rtt(2, sim.now) < 100, "full route RTT measured");
        const Bytes plain{8, 7, 6, 5};
        const auto cipher = secure::seal(plain, sim.original, 2, 777, 77);
        for (unsigned lane = 0; lane < 5; ++lane)
            check(sim.nodes[2]->send(1, cipher, lane, sim.now), "five traffic lanes accepted");
        sim.run(200ms);
        check(sim.delivered.size() == 5, "five lanes forwarded");
        for (const auto& [origin, b] : sim.delivered) {
            Id from = 0, seq = 0;
            Bytes opened;
            check(origin == 2 && b == cipher && secure::open(b, sim.original, 77, from, seq, opened) && from == 2 &&
                      seq == 777 && opened == plain,
                  "original ciphertext and ownership preserved");
        }
        const auto* g = sim.nodes[2]->grant(route);
        check(g && g->key != sim.original, "proxy key differs from private gameplay key");
        const auto* proxy_grant = sim.nodes[3]->grant(route);
        check(proxy_grant && proxy_grant->proof == secure::Key{} && g->proof != secure::Key{},
              "proxy cannot forge endpoint measurement proofs");
        // Reordered and interleaved SDP/candidate fragments must all survive.
        const std::string offer(23001, 'o'), candidate = "candidate";
        sim.drop_signal = true;
        sim.nodes[2]->signal(route, offer);
        sim.nodes[2]->signal(route, candidate);
        sim.run(2s);
        check(std::find(sim.signals.begin(), sim.signals.end(), offer) != sim.signals.end() &&
                  std::find(sim.signals.begin(), sim.signals.end(), candidate) != sim.signals.end(),
              "bounded signaling fragments reassemble");
        sim.run(65s);
        check(sim.selected() && sim.selected(2), "grants renew without disrupting healthy routes");
        sim.proxy_dead = true;
        sim.run(4s);
        check(!sim.selected() && !sim.selected(2), "dead relay falls back to direct");
        check(!sim.nodes[2]->send(1, cipher, 0, sim.now), "failed route leaves original transport available");
        sim.configure(78);
        check(!sim.nodes[1]->grant(route) && !sim.nodes[2]->grant(route), "rehost retires keys and routes");
        check(!sim.nodes[1]->control(2, Bytes(900, 0), sim.now), "invalid control rejected");
    }
    {
        Simulation sim;
        sim.direct_good = true;
        sim.run(14s);
        check(!sim.selected() && !sim.selected(2), "already better direct route retained");
    }
    {
        Simulation sim;
        sim.configure(77, false);
        sim.run(14s);
        check(!sim.selected(), "proxy requires separate device consent");
    }
    {
        Simulation sim;
        sim.run(13s);
        const auto route = sim.selected();
        check(route != 0, "withdrawal fixture selected");
        sim.configure(77, false);
        sim.run(2s);
        check(!sim.selected() && !sim.selected(2) && !sim.nodes[3]->grant(route),
              "withdrawal revokes forwarding at all ends");
    }
    {
        Simulation sim;
        sim.run(13s);
        const auto route = sim.selected();
        const Bytes plain{1, 2, 3};
        const auto b = secure::seal(plain, sim.original, 2, 999, 77);
        check(sim.nodes[2]->send(1, b, 1, sim.now), "malformed fixture queued");
        auto p = sim.packets.back();
        check(p.to == 3, "one hop targets proxy");
        auto broken = p.bytes;
        broken.back() ^= 1;
        check(!sim.nodes[3]->wire(route, 2, broken, sim.now), "tampered envelope rejected");
        check(!sim.nodes[3]->wire(route, 1, p.bytes, sim.now), "physical sender substitution rejected");
        check(sim.nodes[3]->wire(route, 2, p.bytes, sim.now), "authenticated envelope accepted");
        check(!sim.nodes[3]->wire(route, 2, p.bytes, sim.now), "duplicate encrypted envelope rejected");
        auto forwarded = sim.packets.back();
        check(forwarded.to == 1, "relay terminates at original host");
        check(!sim.nodes[3]->wire(route, 3, forwarded.bytes, sim.now), "recursive relay hop rejected");
        check(!sim.nodes[1]->wire(route, 3, std::span(forwarded.bytes).first(40), sim.now),
              "truncated packet rejected");
    }
    {
        Simulation sim;
        sim.run(13s);
        const auto route = sim.selected();
        check(route != 0, "private measurement fixture selected");
        sim.nodes[1]->service(sim.now + 600ms);
        const auto packet = std::find_if(sim.packets.begin(), sim.packets.end(), [&](const auto& p) {
            return !p.control && p.from == 1 && p.to == 3 && p.route == route;
        });
        check(packet != sim.packets.end(), "captured host probe");
        const auto* proxy = sim.nodes[3]->grant(route);
        Id sender = 0, seq = 0;
        Bytes plain;
        check(secure::open(std::span(packet->bytes).subspan(13), proxy->key, route, sender, seq, plain) &&
                  plain[8] == 1,
              "proxy can read routing envelope");
        plain[8] = 2;
        for (unsigned i = 0; i < 8; ++i)
            std::swap(plain[9 + i], plain[17 + i]);
        plain[34] = 1;
        const auto cipher = secure::seal(plain, proxy->key, 3, 900000, route);
        Bytes forged(packet->bytes.begin(), packet->bytes.begin() + 13);
        forged.insert(forged.end(), cipher.begin(), cipher.end());
        check(!sim.nodes[1]->wire(route, 3, forged, sim.now),
              "proxy cannot shortcut the endpoint's authenticated round trip");
    }
    {
        Simulation sim(true, 5);
        sim.run(8s);
        const auto v = sim.nodes[1]->view(sim.now);
        check(v.rows.size() == 16 && std::all_of(v.rows.begin(), v.rows.end(),
                                                 [](const auto& r) { return r.ready && r.local.samples >= 8; }),
              "spectator host and four racers measure all sixteen routes");
        for (Id id = 2; id <= 5; ++id)
            check(sim.opened[id] == 6, "four-racer extra links fit six-per-device bound");
    }
    std::printf("Peer relay policy checks=%u\n", checks);
}
// Actual local UDP carries all authenticated host control and original gameplay
// bytes. Only the original Host/Client link is delayed. In RTC mode the extra
// Client/Proxy leg is a real libdatachannel connection with five native lanes.
// The RTC host legs use a wider localhost UDP fixture because the production
// UDP socket intentionally rejects WebRTC-size datagrams at its 2048-byte cap.
class WideTestUdp final : public n::SessionTransport {
#if defined(_WIN32)
    using Socket = SOCKET;
    static constexpr Socket invalid = INVALID_SOCKET;
#else
    using Socket = int;
    static constexpr Socket invalid = -1;
#endif
    Socket socket_ = invalid;
    std::uint16_t port_ = 0;

  public:
    ~WideTestUdp() override {
        close();
    }
    bool open(std::uint16_t port, std::string&) override {
#if defined(_WIN32)
        static const bool initialized = [] {
            WSADATA w;
            return WSAStartup(MAKEWORD(2, 2), &w) == 0;
        }();
        if (!initialized)
            return false;
#endif
        socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_ == invalid)
            return false;
        // macOS caps a UDP datagram at the send buffer, 9216 bytes by default and below MaxWire.
        // Ask for the same queue the production socket does.
        const int queue = 1 << 20;
        setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&queue), sizeof(queue));
        setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&queue), sizeof(queue));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(port);
        if (::bind(socket_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0)
            return false;
#if defined(_WIN32)
        u_long nonblocking = 1;
        ioctlsocket(socket_, FIONBIO, &nonblocking);
        int size = sizeof(a);
#else
        fcntl(socket_, F_SETFL, O_NONBLOCK);
        socklen_t size = sizeof(a);
#endif
        getsockname(socket_, reinterpret_cast<sockaddr*>(&a), &size);
        port_ = ntohs(a.sin_port);
        return true;
    }
    void close() override {
        if (socket_ != invalid) {
#if defined(_WIN32)
            closesocket(socket_);
#else
            ::close(socket_);
#endif
            socket_ = invalid;
        }
    }
    bool is_open() const override {
        return socket_ != invalid;
    }
    std::uint16_t local_port() const override {
        return port_;
    }
    n::DatagramSendStatus send_status(const n::PeerAddress& a, std::span<const std::uint8_t> b,
                                      n::TransportTrafficClass, std::string&) override {
        const auto result = sendto(socket_, reinterpret_cast<const char*>(b.data()), int(b.size()), 0,
                                   reinterpret_cast<const sockaddr*>(a.storage.data()), int(a.size));
        return result == int(b.size()) ? n::DatagramSendStatus::Sent : n::DatagramSendStatus::Error;
    }
    bool receive(n::PeerAddress& a, Bytes& b, std::string&) override {
#if defined(_WIN32)
        int size = int(a.storage.size());
#else
        socklen_t size = socklen_t(a.storage.size());
#endif
        b.resize(MaxWire + 512);
        const auto result = recvfrom(socket_, reinterpret_cast<char*>(b.data()), int(b.size()), 0,
                                     reinterpret_cast<sockaddr*>(a.storage.data()), &size);
        if (result < 0) {
            b.clear();
            return false;
        }
        a.size = std::uint32_t(size);
        b.resize(std::size_t(result));
        return true;
    }
};
class DelayedUdp final : public n::SessionTransport {
  public:
    explicit DelayedUdp(bool quick) : quick_(quick) {
        if (quick)
            base_ = std::make_unique<WideTestUdp>();
        else
            base_ = n::make_udp_session_transport();
    }
    n::PeerAddress delayed;
    bool open(std::uint16_t p, std::string& e) override {
        return base_->open(p, e);
    }
    void close() override {
        queue_.clear();
        base_->close();
    }
    bool is_open() const override {
        return base_->is_open();
    }
    std::uint16_t local_port() const override {
        return base_->local_port();
    }
    n::DatagramSendStatus send_status(const n::PeerAddress& a, std::span<const std::uint8_t> b,
                                      n::TransportTrafficClass c, std::string& e) override {
        if (a == delayed) {
            if (queue_.size() >= 1024)
                return n::DatagramSendStatus::WouldBlock;
            queue_.push_back({a, Bytes(b.begin(), b.end()), c, Clock::now() + 70ms});
            return n::DatagramSendStatus::Sent;
        }
        return base_->send_status(a, b, c, e);
    }
    bool receive(n::PeerAddress& a, Bytes& b, std::string& e) override {
        return base_->receive(a, b, e);
    }
    void service() override {
        while (!queue_.empty() && queue_.front().at <= Clock::now()) {
            auto p = std::move(queue_.front());
            queue_.pop_front();
            std::string e;
            check(base_->send_status(p.to, p.bytes, p.lane, e) == n::DatagramSendStatus::Sent, "queued local UDP send");
        }
    }
    bool quick_join() const override {
        return quick_;
    }

  private:
    struct Packet {
        n::PeerAddress to;
        Bytes bytes;
        n::TransportTrafficClass lane;
        Clock::time_point at;
    };
    std::unique_ptr<n::SessionTransport> base_;
    bool quick_;
    std::deque<Packet> queue_;
};
void socket_tests(bool rtc) {
    std::array<std::unique_ptr<n::SessionTransport>, 4> sockets;
    std::array<DelayedUdp*, 4> delayed{};
    std::array<n::PeerAddress, 4> addresses{};
    std::array<int, 4> keys{};
    std::array<Id, 4> nonce{};
    const auto akey = secure::generate_key(), pkey = secure::generate_key();
    auto key = [&](Id from, Id to) -> const secure::Key& { return from == 3 || to == 3 ? pkey : akey; };
    std::string error;
    for (Id id = 1; id <= 3; ++id) {
        auto b = std::make_unique<DelayedUdp>(rtc);
        delayed[id] = b.get();
        sockets[id] = wrap(&keys[id], std::move(b), NetworkOptions{{}});
        check(sockets[id]->open(0, error), "socket open");
        check(n::DatagramSocket::resolve("127.0.0.1", sockets[id]->local_port(), addresses[id], error),
              "socket address");
    }
    delayed[1]->delayed = addresses[2];
    delayed[2]->delayed = addresses[1];
    for (Id id = 1; id <= 3; ++id) {
        Context c;
        c.room = 901;
        c.host = 1;
        c.local = id;
        c.active = true;
        c.allow = true;
        c.volunteer = id == 3;
        c.quick = rtc;
        c.members = {{1, "Host", n::DatagramSocket::describe(addresses[1]), true},
                     {2, "Client", n::DatagramSocket::describe(addresses[2]), true},
                     {3, "Proxy", n::DatagramSocket::describe(addresses[3]), true}};
        std::vector<Address> peers;
        if (id == 1)
            peers = {{2, addresses[2]}, {3, addresses[3]}};
        else
            peers = {{1, addresses[1]}};
        configure(&keys[id], c, std::move(peers));
    }
    const unsigned expected = rtc ? 11 : 10;
    unsigned received = 0;
    bool sent = false, selected = false;
    std::set<std::pair<Id, Id>> seen;
    const auto start = Clock::now(), end = start + 15s;
    while (Clock::now() < end) {
        for (Id id = 1; id <= 3; ++id) {
            sockets[id]->service();
            for (auto& c : controls(&keys[id])) {
                auto cipher = secure::seal(c.bytes, key(id, c.target), id, ++nonce[id], 901);
                check(sockets[id]->send_status(addresses[c.target], cipher, n::TransportTrafficClass::Control, error) ==
                          n::DatagramSendStatus::Sent,
                      "authenticated signaling through original transport");
            }
            for (unsigned limit = 0; limit < 128; ++limit) {
                n::PeerAddress source;
                Bytes b;
                if (!sockets[id]->receive(source, b, error))
                    break;
                Id physical = 0;
                for (Id peer = 1; peer <= 3; ++peer)
                    if (source == addresses[peer])
                        physical = peer;
                check(physical != 0, "logical sender address restored");
                Id origin = 0, seq = 0;
                Bytes plain;
                check(secure::open(b, key(physical, id), 901, origin, seq, plain) && origin == physical,
                      "original pair key and sender still verify");
                if (plain.size() >= 6 && plain[0] == 'D' && (plain[5] == 3 || plain[5] == 6))
                    control(&keys[id], origin, plain);
                else {
                    check((plain.size() == 512 || (rtc && plain.size() == 14400)) && plain[0] == 0xee && plain[1] < 5,
                          "gameplay bytes retained");
                    if (seen.emplace(origin, seq).second)
                        ++received;
                }
            }
        }
        const auto v = view(&keys[1]);
        bool host_selected = false;
        for (const auto& row : v.rows)
            if (row.endpoint == 2 && row.selected)
                host_selected = true;
        const auto client = view(&keys[2]);
        bool client_selected = false;
        for (const auto& row : client.rows)
            if (row.endpoint == 2 && row.selected)
                client_selected = true;
        if (host_selected && client_selected) {
            selected = true;
            if (!sent) {
                sent = true;
                for (unsigned lane = 0; lane < 5; ++lane)
                    for (Id id : {1ULL, 2ULL}) {
                        const Id to = id == 1 ? 2 : 1;
                        Bytes plain(512, 0xab);
                        plain[0] = 0xee;
                        plain[1] = std::uint8_t(lane);
                        const auto cipher = secure::seal(plain, akey, id, ++nonce[id], 901);
                        check(sockets[id]->send_status(addresses[to], cipher, n::TransportTrafficClass(lane), error) ==
                                  n::DatagramSendStatus::Sent,
                              "actual relay sends on all lanes");
                    }
            }
        }
        if (sent && rtc && nonce[0] == 0) {
            nonce[0] = 1;
            Bytes plain(14400, 0xad);
            plain[0] = 0xee;
            plain[1] = 4;
            const auto cipher = secure::seal(plain, akey, 2, ++nonce[2], 901);
            check(cipher.size() == MaxInner, "full-size original datagram fixture");
            check(sockets[2]->send_status(addresses[1], cipher, n::TransportTrafficClass::Checkpoint, error) ==
                      n::DatagramSendStatus::Sent,
                  "full-size checkpoint uses relay without shrinking native datagrams");
        }
        if (received == expected)
            break;
        std::this_thread::sleep_for(1ms);
    }
    if (!selected || received != expected)
        std::fprintf(stderr, "host diagnostics:\n%s\nclient:\n%s\n", diagnostics(view(&keys[1])).c_str(),
                     diagnostics(view(&keys[2])).c_str());
    check(selected && received == expected, rtc ? "real WebRTC extra leg and all five lanes relay original packets"
                                                : "real UDP proxy and all five lanes relay original packets");
    check(view(&keys[3]).forwarded > 0, "proxy forwarding recorded");
    Context withdraw;
    withdraw.room = 901;
    withdraw.host = 1;
    withdraw.local = 3;
    withdraw.active = true;
    withdraw.quick = rtc;
    withdraw.members = {{1, "Host", "", true}, {2, "Client", "", true}, {3, "Proxy", "", true}};
    configure(&keys[3], withdraw, {{1, addresses[1]}});
    bool fallback = false;
    for (const auto deadline = Clock::now() + 3s; Clock::now() < deadline;) {
        for (Id id = 1; id <= 3; ++id) {
            sockets[id]->service();
            for (auto& c : controls(&keys[id])) {
                auto cipher = secure::seal(c.bytes, key(id, c.target), id, ++nonce[id], 901);
                sockets[id]->send_status(addresses[c.target], cipher, n::TransportTrafficClass::Control, error);
            }
            for (unsigned limit = 0; limit < 128; ++limit) {
                n::PeerAddress a;
                Bytes b;
                if (!sockets[id]->receive(a, b, error))
                    break;
                Id from = 0;
                for (Id peer = 1; peer <= 3; ++peer)
                    if (a == addresses[peer])
                        from = peer;
                if (!from)
                    continue;
                Id sender = 0, seq = 0;
                Bytes plain;
                if (secure::open(b, key(from, id), 901, sender, seq, plain))
                    control(&keys[id], sender, plain);
            }
        }
        fallback = true;
        for (Id id : {1ULL, 2ULL})
            for (const auto& row : view(&keys[id]).rows)
                if (row.selected)
                    fallback = false;
        if (fallback)
            break;
        std::this_thread::sleep_for(1ms);
    }
    check(fallback, "real connection consent withdrawal falls back to original host transport");
    reset(&keys[2]);
    check(!view(&keys[2]).enabled && view(&keys[2]).rows.empty(), "close clears diagnostic state immediately");
    for (Id id = 1; id <= 3; ++id)
        sockets[id]->close();
    std::printf("Peer relay %s socket checks=%u original-packets=%u elapsed-ms=%lld\n", rtc ? "WebRTC" : "UDP", checks,
                received,
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 1 || std::string(argv[1]) == "--policy")
        policy_tests();
    if (argc == 1 || std::string(argv[1]) == "--udp")
        socket_tests(false);
#if defined(DKR_RELAY_WITH_RTC)
    if (argc == 1 || std::string(argv[1]) == "--rtc")
        socket_tests(true);
#endif
    return 0;
}
