#include "netplay/session_links/policy.hpp"
#include "netplay/session_links/state.hpp"
#include "netplay/session_links/registry.hpp"
#include "netplay/session_links/menu_control.hpp"
#include "start_gate.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace links = dkr::runtime::netplay::links;
using namespace std::chrono_literals;
namespace {
void check(bool value, const char* reason) {
    if (!value) {
        std::fprintf(stderr, "Session links: %s\n", reason);
        std::abort();
    }
}
links::Scope full() {
    links::Scope result{77, 1, 9};
    for (unsigned i = 0; i < 12; ++i)
        result.members.push_back({i + 1, i < 4 ? int(i) : -1, true});
    return result;
}
links::Proposal proposal() {
    links::Proposal result{full(), 1, 3, 1};
    result.state.fill(42);
    return result;
}
} // namespace
int main() {
    {
        namespace start = dkr::runtime::party_start;
        start::Gate gate;
        gate.launch.emplace();
        gate.hash = 17;
        gate.roster = 3;
        gate.save_generation = 2;
        gate.save_hash = 11;
        check(!start::needs_prepare(gate, 17, 3, 2, 11), "an unchanged verified start remains idempotent");
        check(start::needs_prepare(gate, 18, 3, 2, 11), "a changed launch descriptor re-arms the receipt");
        check(start::needs_prepare(gate, 17, 4, 2, 11), "a changed roster re-arms the receipt");
        check(start::needs_prepare(gate, 17, 3, 3, 11), "a new save generation re-arms the receipt");
        check(start::needs_prepare(gate, 17, 3, 2, 12), "a changed save hash re-arms the receipt");
        gate.launch.reset();
        check(start::needs_prepare(gate, 17, 3, 2, 11), "an absent launch is prepared");
    }
    std::array<unsigned, 4> pads{11, 22, 33, 44};
    for (unsigned host = 0; host < 4; ++host)
        for (unsigned guest = 0; guest < 4; ++guest) {
            check(links::menu_control::input(pads, host, guest, true, 15) == (guest ? 0 : pads[host]),
                  "shared menus follow the successor without moving racing seats");
            check(links::menu_control::input(pads, host, guest, false, 15) == pads[guest],
                  "assigned gameplay ports retain their own input");
            check(links::menu_control::input(pads, host, guest, false, 5) ==
                      ((guest == 0 || guest == 2) ? pads[guest] : 0),
                  "unassigned racing ports remain neutral");
        }
    {
        links::menu_control::CreationPort outer(2);
        check(links::menu_control::creating_port() == 2, "owned construction captures the host port");
        {
            links::menu_control::CreationPort inner(3);
            check(links::menu_control::creating_port() == 3, "nested construction is scoped");
        }
        check(links::menu_control::creating_port() == 2, "nested construction restores its caller");
    }
    check(links::menu_control::creating_port() == 0, "ordinary world construction retains its original menu owner");
    auto scope = full();
    const auto now = links::Clock::now();
    std::string error;
    check(links::valid(scope), "four racers and eight spectators are supported");
    unsigned pairs = 0;
    for (auto a : scope.members)
        for (auto b : scope.members)
            if (a.id < b.id && links::input_pair(scope, a.id, b.id))
                ++pairs;
    check(pairs == 6, "the racer mesh has six pairs, not a twelve-player mesh");
    check(!links::input_owner(scope, 5, 0) && links::input_owner(scope, 4, 3), "spectators cannot supply racer input");
    auto wrong = scope;
    wrong.members[3].port = 0;
    check(!links::valid(wrong), "controller ownership cannot alias");
    wrong = scope;
    wrong.members.push_back({13, -1, true});
    check(!links::valid(wrong), "lobby membership is bounded");
    wrong = scope;
    wrong.members[0].port = -1;
    check(!links::valid(wrong), "a ninth spectator is rejected");

    std::map<links::Pair, links::Sample> samples;
    std::map<links::Id, links::Capacity> capacity;
    for (auto m : scope.members) {
        capacity[m.id] = {10000, true, false, now};
        for (auto racer : scope.members)
            if (racer.port >= 0 && racer.id != m.id)
                samples[{m.id, racer.id}] = {m.id == 3 ? 40U : 150U, 0, 12, now};
    }
    auto best = links::recommend(scope, samples, capacity, now);
    check(best && best->member == 3 && best->worst_p95_ms == 40, "ranking uses the worst measured racer path");
    capacity[3].step_p95_us = 30000;
    check(!links::recommend(scope, samples, capacity, now), "a faster ping cannot recommend an overloaded host");
    capacity[3].step_p95_us = 10000;
    capacity[3].background_saturated = true;
    check(!links::recommend(scope, samples, capacity, now), "saturated background capacity disqualifies a host");
    capacity[3].background_saturated = false;
    samples[{3, 4}].observations = 0;
    check(!links::recommend(scope, samples, capacity, now),
          "missing racer evidence prevents a misleading recommendation");
    samples[{3, 4}] = {40, 0, 12, now - 6s};
    check(!links::recommend(scope, samples, capacity, now), "stale routes are not ranked");
    samples[{3, 4}] = {40, 0, 12, now};
    capacity.erase(3);
    best = links::recommend(scope, samples, capacity, now);
    check(best && best->member == 3 && !best->capacity_measured, "unmeasured CPU capacity is explicitly identified");

    links::TransferBuffer buffer;
    std::vector<std::uint8_t> content(900, 42);
    check(!buffer.accept(1, links::MaximumTransferBytes + 1, 0, {}),
          "transfer allocation is bounded before allocating");
    check(!buffer.accept(1, 900, 1, std::span(content).first(640)), "misaligned offsets are rejected");
    check(buffer.accept(1, 900, 640, std::span(content).subspan(640)) && !buffer.complete(),
          "out of order chunks cannot complete a partial transfer");
    check(buffer.accept(1, 900, 0, std::span(content).first(640)) && buffer.complete(),
          "all bounded chunks reconstruct the exact session snapshot");
    auto altered = content;
    altered[0] ^= 1;
    check(!buffer.accept(1, 900, 0, std::span(altered).first(640)), "duplicate conflicting save chunks are rejected");
    check(buffer.bytes == content, "invalid duplicate retains the original prepared snapshot");
    auto wire = links::encode({links::Kind::Offer, 77, 9, {1, 2, 3}});
    links::Packet decoded;
    check(links::decode(wire, decoded) && decoded.room == 77 && decoded.generation == 9,
          "wire scope survives the bounded codec");
    for (std::size_t i = 0; i < 23; ++i)
        check(!links::decode(std::span(wire).first(i), decoded), "truncated host control is rejected");
    wire[6] = 255;
    check(!links::decode(wire, decoded), "unknown host control cannot enter the mailbox");
    int owner = 0;
    dkr::runtime::netplay::PeerAddress address;
    std::string address_error;
    check(dkr::runtime::netplay::DatagramSocket::resolve("127.0.0.1", 22222, address, address_error),
          "test native route resolves");
    dkr::runtime::netplay::secure::Key pair_key;
    pair_key.fill(7);
    check(links::install(&owner, 77, 2, address, pair_key), "authenticated owner can introduce a pair key");
    check(links::receive_key(&owner, 77, 2, address) == pair_key, "pair key is available only on its introduced route");
    check(!links::receive_key(&owner, 78, 2, address) && !links::receive_key(&owner, 77, 3, address),
          "room and sender confusion cannot reveal a pair key");
    links::forget(&owner);
    check(!links::receive_key(&owner, 77, 2, address), "disconnect revokes introduced keys");

    links::Handover transfer;
    auto p = proposal();
    check(!transfer.offer(2, p, true, now, error), "a client cannot offer a host transfer");
    check(!transfer.offer(1, p, false, now, error), "a running game cannot start a waiting-lobby transfer");
    auto old = p;
    old.before.members.back().capable = false;
    check(!transfer.offer(1, old, true, now, error), "an unsupported spectator prevents an unsafe authority change");
    check(transfer.offer(1, p, true, now, error), "the host can propose a full-lobby transfer");
    check(!transfer.consent(2, 1, true, now, error), "only the candidate can accept");
    check(transfer.consent(3, 1, true, now, error), "candidate accepts without leaving the lobby");
    check(!transfer.bind(3, 1, p.state, error), "only the original host can bind the successor registration");
    auto registered = p.state;
    registered[0] ^= 1;
    check(transfer.bind(1, 1, registered, error), "registration and shared save bind before readiness votes");
    p.state = registered;
    for (unsigned id = 1; id < 12; ++id)
        check(transfer.prepared(id, 1, p.state, true, now, error), "member prepares matching state and routes");
    check(!transfer.can_commit(), "the slow twelfth member does not allow a partial commit");
    check(!transfer.bind(1, 1, registered, error), "prepared state cannot change after the first readiness vote");
    auto bad = p.state;
    bad[0] ^= 1;
    check(!transfer.prepared(12, 1, bad, true, now, error), "a different save/session snapshot cannot be acknowledged");
    check(!transfer.prepared(12, 1, p.state, false, now, error), "unconnected candidates cannot prepare");
    check(!transfer.prepared(13, 1, p.state, true, now, error), "an outsider cannot vote");
    check(transfer.prepared(12, 1, p.state, true, now, error), "the final spectator prepares");
    check(!transfer.commit(3, 1, p.state, error), "the candidate cannot seize ownership");
    check(transfer.commit(1, 1, p.state, error), "the old host commits only after all twelve prepare");
    const auto result = transfer.result();
    check(result && result->host == 3 && result->generation == 10 && result->members == scope.members,
          "transfer changes authority and preserves all members and controller ports");
    check(transfer.proposal()->save_owner == 1, "networking handover preserves persistent save ownership");
    for (unsigned id = 1; id < 12; ++id)
        check(transfer.acknowledge(id, 1, p.state, error), "committed member acknowledges");
    transfer.service(scope, true, now + 90s);
    check(transfer.frozen() && !transfer.complete(), "a lost commit ACK holds start and never restores old authority");
    check(!transfer.cancel(1, 1, "timeout", error), "post-commit timeout cannot produce two hosts");
    check(transfer.commit(1, 1, p.state, error), "commit retransmission is idempotent");
    check(transfer.acknowledge(12, 1, p.state, error) && transfer.complete(),
          "delayed spectator ACK finishes transfer");
    check(transfer.acknowledge(12, 1, p.state, error), "duplicate ACK is harmless");
    check(!transfer.acknowledge(12, 2, p.state, error), "stale/mismatched transfer serial is rejected");
    p.serial = 2;
    p.before = *result;
    p.successor = 4;
    check(transfer.offer(3, p, true, now, error), "the new host can offer another transfer");
    transfer.service(p.before, true, now + 21s);
    check(transfer.phase() == links::Phase::Cancelled && !transfer.result(),
          "pre-commit timeout retains old ownership");
    p.serial = 3;
    check(transfer.offer(3, p, true, now, error), "a new proposal can follow cancellation");
    check(transfer.consent(4, 3, false, now, error) && transfer.phase() == links::Phase::Cancelled,
          "candidate can decline without losing the existing lobby");
    p.serial = 4;
    check(transfer.offer(3, p, true, now, error), "another proposal is allowed");
    auto changed = p.before;
    ++changed.generation;
    transfer.service(changed, true, now);
    check(transfer.phase() == links::Phase::Cancelled, "membership changes invalidate preparation");
    transfer.reset();
    check(transfer.phase() == links::Phase::Idle && !transfer.proposal(), "disconnect clears transfer state");
    std::puts(
        "Four-racer/eight-spectator ownership policy, ranking, authentication, rollback-before-commit and commit-ACK recovery passed.");
}
