#pragma once
#include "party_link.hpp"
#include <cstdlib>
#include <cstdio>
#include <string_view>

namespace dkr::runtime::netplay::links {
// Opt-in only inside the existing disposable-profile --self-test-owned check.
// Ordinary launcher/overlay sessions never enable automatic consent.
class HandoverQualification {
    bool enabled_ = false, reported_ = false;
    Clock::time_point route_report_{};

  public:
    explicit HandoverQualification(bool disposable) {
        const auto* value = disposable ? std::getenv("DKR_OWNED_CHECK_HANDOVER") : nullptr;
        enabled_ = value && std::string_view(value) == "1";
    }
    bool enabled() const {
        return enabled_;
    }
    bool pulse(const void* session, std::string& error) {
        if (!enabled_)
            return true;
        auto hub = dkr::runtime::spectators::party_link(session);
        std::scoped_lock lock(hub->mutex);
        auto& state = hub->links;
        if (state.handover.phase() == Phase::Cancelled) {
            error = state.handover.status();
            return false;
        }
        if (!hub->enabled || hub->party.snapshot().members.size() != 12)
            return false;
        if (Clock::now() >= route_report_ && !state.finished) {
            route_report_ = Clock::now() + std::chrono::seconds(5);
            std::fprintf(
                stderr,
                "[online][handover-check] phase=%u local=%llu registration=%u/%u prepared-state=%u bytes=%zu/%zu inventory=%u status=%s\n",
                unsigned(state.handover.phase()), static_cast<unsigned long long>(hub->local),
                unsigned(state.registration_started), unsigned(state.registration_ready),
                unsigned(state.prepared_state), state.prepared_bytes.size(), state.expected_transfer,
                unsigned(state.inventory_ready), state.status.c_str());
            if (const auto& p = state.handover.proposal())
                for (const auto& m : p->before.members) {
                    const auto sample = state.samples.find({hub->local, m.id});
                    std::fprintf(stderr, "[online][handover-check] member=%llu prepared=%u pongs=%u age-ms=%lld\n",
                                 static_cast<unsigned long long>(m.id), unsigned(state.handover.ready(m.id)),
                                 sample == state.samples.end() ? 0 : sample->second.observations,
                                 sample == state.samples.end()
                                     ? -1
                                     : static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                                  Clock::now() - sample->second.measured)
                                                                  .count()));
                }
        }
        if (state.last_applied && state.finished) {
            const auto& p = state.handover.proposal();
            if (!p || state.save_owner != p->save_owner) {
                error = "Host transfer changed the agreed save owner.";
                return false;
            }
            for (const auto& before : p->before.members) {
                const auto* member = hub->party.find(before.id);
                if (!member || int(member->port ? *member->port : 255) != (before.port < 0 ? 255 : before.port)) {
                    error = "Host transfer changed a controller port.";
                    return false;
                }
            }
            if (!reported_) {
                reported_ = true;
                std::fprintf(
                    stderr,
                    "[online][handover-check] complete members=12 ports-preserved=1 save-owner-preserved=1 host=%llu\n",
                    static_cast<unsigned long long>(hub->party.snapshot().host));
            }
            if (hub->host && Clock::now() >= route_report_) {
                route_report_ = Clock::now() + std::chrono::seconds(5);
                for (const auto& m : hub->party.snapshot().members)
                    if (m.port && m.id != hub->local) {
                        const auto* peer = hub->peer(m.id);
                        const auto sample = state.samples.find({hub->local, m.id});
                        std::fprintf(
                            stderr, "[online][handover-check] racer=%llu heartbeats=%u heard-ms=%lld direct-pongs=%u\n",
                            static_cast<unsigned long long>(m.id), peer ? peer->rtt_count : 0,
                            peer ? static_cast<long long>(
                                       std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - peer->heard)
                                           .count())
                                 : -1,
                            sample == state.samples.end() ? 0 : sample->second.observations);
                    }
            }
            return true;
        }
        const auto& proposal = state.handover.proposal();
        if (proposal && proposal->successor == hub->local && state.handover.phase() == Phase::Offered)
            state.consent = true;
        if (hub->host && !busy(state)) {
            const auto target = std::find_if(hub->party.snapshot().members.begin(), hub->party.snapshot().members.end(),
                                             [](const auto& m) { return m.port && *m.port == 1; });
            if (target != hub->party.snapshot().members.end())
                state.request = target->id;
        }
        return false;
    }
};
} // namespace dkr::runtime::netplay::links
