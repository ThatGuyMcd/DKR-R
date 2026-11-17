#include "session_log.hpp"
#include "policy.hpp"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <set>
#include <tuple>

namespace dkr::runtime::netplay::links {
namespace {
bool fail(std::string& error, std::string reason) {
    error = std::move(reason);
    return false;
}
const Member* member(const Scope& scope, Id id) {
    const auto at =
        std::find_if(scope.members.begin(), scope.members.end(), [id](const Member& m) { return m.id == id; });
    return at == scope.members.end() ? nullptr : &*at;
}
std::size_t index(const Scope& scope, Id id) {
    const auto at =
        std::find_if(scope.members.begin(), scope.members.end(), [id](const Member& m) { return m.id == id; });
    return std::size_t(at - scope.members.begin());
}
} // namespace

bool valid(const Scope& scope) {
    if (!scope.room || !scope.host || !scope.generation || scope.members.empty() ||
        scope.members.size() > MaximumMembers || !member(scope, scope.host))
        return false;
    std::set<Id> ids;
    std::set<int> ports;
    for (const auto& m : scope.members)
        if (!m.id || !ids.insert(m.id).second || m.port < -1 || m.port >= int(MaximumRacers) ||
            (m.port >= 0 && !ports.insert(m.port).second))
            return false;
    return scope.members.size() - ports.size() <= MaximumMembers - MaximumRacers;
}

bool input_pair(const Scope& scope, Id a, Id b) {
    const auto* first = member(scope, a);
    const auto* second = member(scope, b);
    return valid(scope) && a != b && first && second && first->port >= 0 && second->port >= 0 && first->capable &&
           second->capable;
}
bool input_owner(const Scope& scope, Id sender, unsigned port) {
    const auto* owner = member(scope, sender);
    return valid(scope) && port < MaximumRacers && owner && owner->capable && owner->port == int(port);
}

std::optional<Recommendation> recommend(const Scope& scope, const std::map<Pair, Sample>& samples,
                                        const std::map<Id, Capacity>& capacities, Clock::time_point now) {
    if (!valid(scope))
        return {};
    std::optional<Recommendation> best, current;
    for (const auto& candidate : scope.members) {
        if (!candidate.capable)
            continue;
        const auto capacity = capacities.find(candidate.id);
        if (capacity != capacities.end() && capacity->second.measured &&
            (capacity->second.sampled > now || now - capacity->second.sampled > std::chrono::seconds(10) ||
             capacity->second.step_p95_us >= 25000 || capacity->second.background_saturated))
            continue;
        Recommendation score{candidate.id};
        score.capacity_measured = capacity != capacities.end() && capacity->second.measured;
        bool measured = true;
        for (const auto& racer : scope.members) {
            if (racer.port < 0 || racer.id == candidate.id)
                continue;
            const auto at = samples.find({candidate.id, racer.id});
            if (at == samples.end() || at->second.observations < 8 || at->second.measured > now ||
                now - at->second.measured > std::chrono::seconds(5)) {
                measured = false;
                break;
            }
            score.worst_p95_ms = std::max(score.worst_p95_ms, at->second.p95_ms);
            score.worst_loss_per_mille = std::max(score.worst_loss_per_mille, at->second.loss_per_mille);
        }
        if (!measured)
            continue;
        if (candidate.id == scope.host)
            current = score;
        const auto rank = [](const Recommendation& value) {
            return std::tuple{value.worst_loss_per_mille, value.worst_p95_ms, value.member};
        };
        if (!best || rank(score) < rank(*best))
            best = score;
    }
    if (!best || best->member == scope.host)
        return {};
    // Avoid presenting a recommendation from incomplete comparisons or tiny
    // fluctuations. Missing host capacity is a separate, explicit reason.
    if (current && best->worst_loss_per_mille >= current->worst_loss_per_mille &&
        best->worst_p95_ms + 20 >= current->worst_p95_ms)
        return {};
    best->reason = !best->capacity_measured
                       ? "Shorter measured network paths. Processing headroom has not been measured."
                   : current ? "More consistent measured connections to the racers."
                             : "Measured connections and processing headroom are available on this player.";
    return best;
}

bool Handover::expected(Id authenticated, Id serial) const {
    return proposal_ && proposal_->serial == serial && member(proposal_->before, authenticated);
}
bool Handover::offer(Id authenticated, Proposal value, bool waiting, Clock::time_point now, std::string& error) {
    if (proposal_ && *proposal_ == value && authenticated == value.before.host && phase_ != Phase::Cancelled) {
        error.clear();
        return true;
    }
    if (!waiting || phase_ == Phase::Offered || phase_ == Phase::Preparing || phase_ == Phase::Committing)
        return fail(error, "Finish the current operation before transferring host.");
    if (!valid(value.before) || authenticated != value.before.host || !value.serial ||
        value.serial <= highest_serial_ || value.successor == value.before.host ||
        !member(value.before, value.successor) || !value.save_owner ||
        value.before.generation == std::numeric_limits<Id>::max() ||
        std::all_of(value.state.begin(), value.state.end(), [](auto b) { return b == 0; }) ||
        !std::all_of(value.before.members.begin(), value.before.members.end(), [](const auto& m) { return m.capable; }))
        return fail(error, "Every connected player must support this host transfer.");
    highest_serial_ = value.serial;
    proposal_ = std::move(value);
    prepared_ = {};
    acknowledged_ = {};
    phase_ = Phase::Offered;
    deadline_ = now + std::chrono::seconds(20);
    status_ = "Waiting for the new host to accept.";
    error.clear();
    return true;
}
bool Handover::consent(Id authenticated, Id serial, bool accept, Clock::time_point now, std::string& error) {
    if (!expected(authenticated, serial) || authenticated != proposal_->successor)
        return fail(error, "Only the proposed host can accept this transfer.");
    if (!accept && (phase_ == Phase::Offered || phase_ == Phase::Preparing))
        return cancel(authenticated, serial, "The proposed host declined.", error);
    if (accept && phase_ == Phase::Preparing) {
        error.clear();
        return true;
    }
    if (!accept || phase_ != Phase::Offered || now > deadline_)
        return fail(error, "That host transfer is no longer waiting for acceptance.");
    phase_ = Phase::Preparing;
    deadline_ = now + std::chrono::seconds(120);
    status_ = "Preparing the new host connections.";
    error.clear();
    return true;
}
bool Handover::bind(Id authenticated, Id serial, const Digest& digest, std::string& error) {
    if (!expected(authenticated, serial) || authenticated != proposal_->before.host || phase_ != Phase::Preparing ||
        std::any_of(prepared_.begin(), prepared_.end(), [](bool b) { return b; }) ||
        std::all_of(digest.begin(), digest.end(), [](auto b) { return !b; }))
        return fail(error, "The prepared host transfer cannot change after a readiness vote.");
    proposal_->state = digest;
    error.clear();
    return true;
}
bool Handover::prepared(Id authenticated, Id serial, const Digest& digest, bool routes_ready, Clock::time_point now,
                        std::string& error) {
    if (!expected(authenticated, serial) || phase_ != Phase::Preparing || now > deadline_ || !routes_ready ||
        digest != proposal_->state)
        return fail(error, "The host transfer state or connections are not ready.");
    prepared_[index(proposal_->before, authenticated)] = true;
    error.clear();
    return true;
}
bool Handover::can_commit() const {
    return proposal_ && phase_ == Phase::Preparing &&
           std::all_of(prepared_.begin(), prepared_.begin() + proposal_->before.members.size(),
                       [](bool b) { return b; });
}
bool Handover::commit(Id authenticated, Id serial, const Digest& digest, std::string& error) {
    if (!expected(authenticated, serial) || authenticated != proposal_->before.host || digest != proposal_->state)
        return fail(error, "Only the current host can commit the prepared transfer.");
    if (phase_ == Phase::Committing || phase_ == Phase::Committed) {
        error.clear();
        return true;
    }
    if (!can_commit())
        return fail(error, "Every connected player must prepare before host ownership changes.");
    phase_ = Phase::Committing;
    status_ = "Transferring host. Waiting for acknowledgements.";
    error.clear();
    return true;
}
bool Handover::acknowledge(Id authenticated, Id serial, const Digest& digest, std::string& error) {
    if (!expected(authenticated, serial) || digest != proposal_->state ||
        (phase_ != Phase::Committing && phase_ != Phase::Committed))
        return fail(error, "That acknowledgement does not belong to this host transfer.");
    acknowledged_[index(proposal_->before, authenticated)] = true;
    if (complete()) {
        phase_ = Phase::Committed;
        status_ = "Host transferred.";
    }
    error.clear();
    return true;
}
bool Handover::complete() const {
    return proposal_ && (phase_ == Phase::Committing || phase_ == Phase::Committed) &&
           std::all_of(acknowledged_.begin(), acknowledged_.begin() + proposal_->before.members.size(),
                       [](bool b) { return b; });
}
bool Handover::cancel(Id authenticated, Id serial, std::string reason, std::string& error) {
    if (!expected(authenticated, serial) ||
        (authenticated != proposal_->before.host && authenticated != proposal_->successor) ||
        (phase_ != Phase::Offered && phase_ != Phase::Preparing))
        return fail(error, "A committed host transfer cannot be rolled back.");
    dkr::runtime::session_log::write(
        "[online][handover-cancel] at-us=%llu actor=%llu serial=%llu phase=%u reason=%s\n",
        (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch())
            .count(),
        (unsigned long long)authenticated, (unsigned long long)serial, unsigned(phase_), reason.c_str());
    phase_ = Phase::Cancelled;
    status_ = std::move(reason);
    error.clear();
    return true;
}
void Handover::service(const Scope& current, bool waiting, Clock::time_point now) {
    if (!proposal_ || (phase_ != Phase::Offered && phase_ != Phase::Preparing))
        return;
    if (current != proposal_->before || !waiting || now > deadline_) {
        const auto reason = current != proposal_->before ? "scope" : !waiting ? "busy" : "deadline";
        dkr::runtime::session_log::write(
            "[online][handover-cancel] reason=%s serial=%llu host=%llu/%llu generation=%llu/%llu members=%zu/%zu waiting=%u\n",
            reason, (unsigned long long)proposal_->serial, (unsigned long long)current.host,
            (unsigned long long)proposal_->before.host, (unsigned long long)current.generation,
            (unsigned long long)proposal_->before.generation, current.members.size(), proposal_->before.members.size(),
            unsigned(waiting));
        if (current != proposal_->before)
            for (const auto& m : current.members) {
                const auto* old = member(proposal_->before, m.id);
                if (!old || *old != m)
                    dkr::runtime::session_log::write("[online][handover-cancel] member=%llu port=%d/%d capable=%u/%u\n",
                                                     (unsigned long long)m.id, m.port, old ? old->port : -2,
                                                     unsigned(m.capable), old ? unsigned(old->capable) : 0);
            }
        phase_ = Phase::Cancelled;
        status_ =
            current != proposal_->before
                ? "Host transfer cancelled because the lobby changed. The original host is still in charge."
            : !waiting
                ? "Host transfer cancelled because another lobby operation started. The original host is still in charge."
                : "Host transfer timed out. The original host is still in charge.";
    }
    // Once committed, never restore old authority because an ACK was lost.
    // The owner must retransmit the commit and hold race start until complete.
}
bool Handover::ready(Id id) const {
    return proposal_ && member(proposal_->before, id) && prepared_[index(proposal_->before, id)];
}
bool Handover::acknowledged(Id id) const {
    return proposal_ && member(proposal_->before, id) && acknowledged_[index(proposal_->before, id)];
}
std::optional<Scope> Handover::result() const {
    if (!proposal_ || (phase_ != Phase::Committing && phase_ != Phase::Committed))
        return {};
    auto result = proposal_->before;
    result.host = proposal_->successor;
    ++result.generation;
    return result;
}
void Handover::reset() {
    phase_ = Phase::Idle;
    proposal_.reset();
    prepared_ = {};
    acknowledged_ = {};
    deadline_ = {};
    highest_serial_ = 0;
    status_.clear();
}

} // namespace dkr::runtime::netplay::links
