#include "party.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace dkr::runtime::spectators {
namespace {
bool no(std::string& error, const char* text) {
    error = text;
    return false;
}
bool name_ok(const std::string& text) {
    return !text.empty() && text.size() <= 24 &&
           std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c != 127; });
}
bool before(Cursor a, Cursor b) {
    return a.epoch < b.epoch || (a.epoch == b.epoch && a.frame < b.frame);
}
bool contains(const std::vector<MemberId>& values, MemberId id) {
    return std::find(values.begin(), values.end(), id) != values.end();
}
} // namespace
const Member* Party::find(MemberId id) const {
    const auto it =
        std::find_if(state_.members.begin(), state_.members.end(), [&](const auto& m) { return m.id == id; });
    return it == state_.members.end() ? nullptr : &*it;
}
bool Party::bump(std::string& error) {
    if (state_.generation == UINT64_MAX)
        return no(error, "Lobby membership generation exhausted.");
    ++state_.generation;
    error.clear();
    return true;
}
bool Party::create(MemberId id, std::string name, std::string& error) {
    if (!id || !name_ok(name))
        return no(error, "Invalid lobby host identity.");
    PartySnapshot next;
    next.host = id;
    next.generation = 1;
    next.members.push_back({id, std::move(name), Role::Racer, std::uint8_t(0), true, true});
    next.controls[0] = {id, 1, {}};
    state_ = std::move(next);
    proposal_serial_ = 0;
    error.clear();
    return true;
}
bool Party::add(MemberId id, std::string name, std::optional<std::uint8_t> port, std::string& error) {
    if (!state_.host || !id || !name_ok(name) || find(id) || state_.members.size() >= MaximumMembers ||
        state_.generation == UINT64_MAX)
        return no(error, "Invalid, duplicate or excess lobby member.");
    if (port && (*port >= RacingPorts || state_.controls[*port].member))
        return no(error, "That racing controller port is occupied.");
    const auto spectators = std::count_if(state_.members.begin(), state_.members.end(),
                                          [](const auto& m) { return m.role == Role::Spectator; });
    if (!port && spectators >= MaximumSpectators)
        return no(error, "The spectator places are full.");
    if (port && state_.handover)
        return no(error, "Finish the pending player swap before adding a racer.");
    if (port && state_.controls[*port].generation == UINT64_MAX)
        return no(error, "Controller ownership generation exhausted.");
    // Any membership edit retires an uncommitted agreement. A newcomer must
    // never leave an old roster-generation proposal waiting indefinitely.
    state_.handover.reset();
    state_.members.push_back({id, std::move(name), port ? Role::Racer : Role::Spectator, port, false, true});
    if (port) {
        auto& lease = state_.controls[*port];
        lease.member = id;
        ++lease.generation;
        lease.starts = {};
    }
    return bump(error);
}
bool Party::remove(MemberId id, std::string& error) {
    const auto* member = find(id);
    if (!member)
        return no(error, "That lobby member is not connected.");
    if (id == state_.host)
        return no(error, "The host closes the lobby; host migration is not implicit.");
    if (state_.generation == UINT64_MAX)
        return no(error, "Lobby membership generation exhausted.");
    if (member->port) {
        auto& lease = state_.controls[*member->port];
        if (lease.generation == UINT64_MAX)
            return no(error, "Controller ownership generation exhausted.");
        lease.member = 0;
        ++lease.generation;
    }
    state_.handover.reset();
    std::erase_if(state_.members, [&](const auto& m) { return m.id == id; });
    return bump(error);
}
bool Party::waiting_role(MemberId actor, MemberId id, std::optional<std::uint8_t> port, std::string& error) {
    // Caller proves that no game launch exists. Live ownership ALWAYS uses
    // the confirmed-boundary propose/acknowledge/transfer protocol instead.
    const auto* current = find(id);
    if (actor != state_.host || !current || state_.handover || state_.generation == UINT64_MAX ||
        (port && (*port >= RacingPorts || (state_.controls[*port].member && state_.controls[*port].member != id))))
        return no(error, "That waiting-lobby role cannot be assigned.");
    if (current->port == port) {
        error.clear();
        return true;
    }
    if (!port && std::count_if(state_.members.begin(), state_.members.end(),
                               [](const auto& m) { return m.role == Role::Spectator; }) >= MaximumSpectators)
        return no(error, "The spectator places are full.");
    auto next = state_;
    auto it = std::find_if(next.members.begin(), next.members.end(), [&](const auto& m) { return m.id == id; });
    if (it->port) {
        auto& lease = next.controls[*it->port];
        if (lease.generation == UINT64_MAX)
            return no(error, "Controller generation exhausted.");
        lease = {0, lease.generation + 1, {}};
    }
    it->port = port;
    it->role = port ? Role::Racer : Role::Spectator;
    if (port) {
        auto& lease = next.controls[*port];
        if (lease.generation == UINT64_MAX)
            return no(error, "Controller generation exhausted.");
        lease = {id, lease.generation + 1, {91, 0}};
    }
    ++next.generation;
    return replace_verified(std::move(next), error);
}
bool Party::propose(MemberId actor, std::uint8_t port, MemberId incoming, Cursor boundary,
                    const std::array<std::uint8_t, 32>& digest, std::string& error) {
    const auto* replacement = find(incoming);
    if (actor != state_.host || !actor || state_.handover || port >= RacingPorts || !boundary.epoch ||
        boundary.frame == UINT32_MAX || !state_.controls[port].member || !replacement ||
        replacement->role != Role::Spectator || !replacement->connected ||
        std::all_of(digest.begin(), digest.end(), [](auto b) { return !b; }))
        return no(error, "Only the host can arrange a swap into an occupied racing port.");
    if (proposal_serial_ == UINT64_MAX || state_.controls[port].generation == UINT64_MAX ||
        state_.generation == UINT64_MAX)
        return no(error, "Player-swap generation exhausted.");
    if (!bump(error))
        return false;
    Handover swap;
    swap.token = ++proposal_serial_;
    swap.roster_generation = state_.generation;
    swap.port = port;
    swap.outgoing = state_.controls[port].member;
    swap.incoming = incoming;
    swap.boundary = boundary;
    swap.world_digest = digest;
    for (const auto& m : state_.members)
        if (m.connected && (m.role == Role::Racer || m.id == state_.host || m.id == incoming))
            swap.required.push_back(m.id);
    state_.handover = std::move(swap);
    error.clear();
    return true;
}
bool Party::acknowledge(MemberId id, std::uint64_t token, Cursor caught_up, const std::array<std::uint8_t, 32>& digest,
                        std::string& error) {
    if (!state_.handover || state_.handover->token != token || !contains(state_.handover->required, id) ||
        caught_up != state_.handover->boundary || digest != state_.handover->world_digest || !find(id) ||
        !find(id)->connected)
        return no(error, "Player swap needs an exact synchronised boundary acknowledgement.");
    auto& acks = state_.handover->acknowledged;
    if (!contains(acks, id))
        acks.push_back(id);
    error.clear();
    return true;
}
bool Party::transfer(MemberId actor, Cursor boundary, std::string& error) {
    if (actor != state_.host || !state_.handover)
        return no(error, "Only the host can commit the agreed player swap.");
    const auto swap = *state_.handover;
    if (swap.boundary != boundary || swap.roster_generation != state_.generation ||
        state_.controls[swap.port].member != swap.outgoing ||
        std::any_of(swap.required.begin(), swap.required.end(),
                    [&](auto id) { return !contains(swap.acknowledged, id); }))
        return no(error, "The replacement and racing peers have not all synchronised for this swap.");
    if (state_.generation == UINT64_MAX || state_.controls[swap.port].generation == UINT64_MAX)
        return no(error, "Player-swap generation exhausted.");
    for (auto& m : state_.members) {
        if (m.id == swap.outgoing) {
            m.role = Role::Spectator;
            m.port.reset();
        }
        if (m.id == swap.incoming) {
            m.role = Role::Racer;
            m.port = swap.port;
        }
    }
    auto& lease = state_.controls[swap.port];
    lease.member = swap.incoming;
    ++lease.generation;
    lease.starts = boundary;
    state_.handover.reset();
    return bump(error);
}
bool Party::cancel(MemberId actor, std::uint64_t token, std::string& error) {
    if (!state_.handover || state_.handover->token != token ||
        (actor != state_.host && actor != state_.handover->outgoing && actor != state_.handover->incoming))
        return no(error, "That player swap cannot be cancelled by this member.");
    if (state_.generation == UINT64_MAX)
        return no(error, "Player-swap generation exhausted.");
    state_.handover.reset();
    return bump(error);
}
bool Party::accepts(MemberId id, unsigned port, std::uint64_t generation, Cursor input) const {
    if (!id || port >= RacingPorts || !input.epoch || input.frame == UINT32_MAX)
        return false;
    // Stop admitting outgoing future samples at the agreed cutover. They
    // cannot be placed in an immutable input ledger and revoked afterwards.
    if (state_.handover && state_.handover->port == port && !before(input, state_.handover->boundary))
        return false;
    const auto& lease = state_.controls[port];
    const auto* member = find(id);
    return member && member->connected && member->role == Role::Racer && member->port == port && lease.member == id &&
           lease.generation == generation && !before(input, lease.starts);
}
bool Party::valid(const PartySnapshot& value, std::string& error) {
    if (!value.host || !value.generation || value.members.empty() || value.members.size() > MaximumMembers)
        return no(error, "Invalid lobby membership snapshot.");
    std::set<MemberId> ids;
    std::set<unsigned> ports;
    unsigned hosts = 0, spectators = 0;
    for (const auto& m : value.members) {
        if (!m.id || !ids.insert(m.id).second || !name_ok(m.name) || m.role > Role::Spectator || !m.connected)
            return no(error, "Invalid or duplicate lobby identity.");
        if (m.host) {
            if (m.id != value.host)
                return no(error, "Lobby host identity changed.");
            ++hosts;
        }
        if (m.role == Role::Spectator) {
            if (m.port)
                return no(error, "A spectator cannot have a controller port.");
            ++spectators;
        } else if (!m.port || *m.port >= RacingPorts || !ports.insert(*m.port).second ||
                   value.controls[*m.port].member != m.id || !value.controls[*m.port].generation)
            return no(error, "Racing controller ownership is inconsistent.");
    }
    if (hosts != 1 || spectators > MaximumSpectators)
        return no(error, "Invalid host or spectator count.");
    for (unsigned p = 0; p < RacingPorts; ++p)
        if (value.controls[p].member && !ports.contains(p))
            return no(error, "A controller lease has no racer.");
    if (value.handover) {
        const auto& h = *value.handover;
        if (!h.token || h.roster_generation != value.generation || h.port >= RacingPorts || !h.boundary.epoch ||
            h.boundary.frame == UINT32_MAX || h.outgoing != value.controls[h.port].member ||
            !ids.contains(h.incoming) || !ids.contains(h.outgoing) || h.incoming == h.outgoing ||
            std::all_of(h.world_digest.begin(), h.world_digest.end(), [](auto b) { return !b; }))
            return no(error, "Invalid pending player swap.");
        const auto incoming =
            std::find_if(value.members.begin(), value.members.end(), [&](const auto& m) { return m.id == h.incoming; });
        if (incoming->role != Role::Spectator || h.required.size() > MaximumMembers ||
            h.acknowledged.size() > h.required.size())
            return no(error, "Invalid player swap participants.");
        std::set<MemberId> required(h.required.begin(), h.required.end()),
            acks(h.acknowledged.begin(), h.acknowledged.end());
        std::set<MemberId> expected;
        for (const auto& m : value.members)
            if (m.role == Role::Racer || m.id == value.host || m.id == h.incoming)
                expected.insert(m.id);
        if (required != expected || required.size() != h.required.size() || acks.size() != h.acknowledged.size() ||
            std::any_of(acks.begin(), acks.end(), [&](auto id) { return !required.contains(id); }))
            return no(error, "Player swap acknowledgement membership is invalid.");
    }
    error.clear();
    return true;
}
bool Party::replace_verified(PartySnapshot value, std::string& error) {
    if (!valid(value, error))
        return false;
    if (state_.host && (value.host != state_.host || value.generation < state_.generation))
        return no(error, "Stale or foreign lobby membership snapshot.");
    if (state_.host && value.generation == state_.generation &&
        (value.members != state_.members || value.controls != state_.controls))
        return no(error, "Lobby membership changed without a new generation.");
    if (state_.host && value.generation == state_.generation) {
        if (bool(value.handover) != bool(state_.handover))
            return no(error, "Player-swap state changed without a new generation.");
        if (value.handover) {
            const auto& old = *state_.handover;
            const auto& next = *value.handover;
            if (old.token != next.token || old.roster_generation != next.roster_generation || old.port != next.port ||
                old.outgoing != next.outgoing || old.incoming != next.incoming || old.boundary != next.boundary ||
                old.world_digest != next.world_digest || old.required != next.required ||
                std::any_of(old.acknowledged.begin(), old.acknowledged.end(),
                            [&](auto id) { return !contains(next.acknowledged, id); }))
                return no(error, "A player-swap agreement changed or lost acknowledgements.");
        }
    }
    if (value.handover && value.handover->token < proposal_serial_)
        return no(error, "A retired player-swap proposal cannot be revived.");
    if (value.handover && value.handover->token == proposal_serial_ &&
        (!state_.handover || state_.handover->token != value.handover->token))
        return no(error, "A completed or cancelled player-swap proposal cannot be revived.");
    if (state_.host)
        for (unsigned p = 0; p < RacingPorts; ++p) {
            const auto& before = state_.controls[p];
            const auto& after = value.controls[p];
            if (after.generation < before.generation || (after.generation == before.generation && after != before))
                return no(error, "Stale or changed controller lease without a new generation.");
        }
    state_ = std::move(value);
    if (state_.handover)
        proposal_serial_ = std::max(proposal_serial_, state_.handover->token);
    return true;
}
} // namespace dkr::runtime::spectators
