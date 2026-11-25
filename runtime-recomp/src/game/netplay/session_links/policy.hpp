#pragma once

#include "netplay/secure_channel.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::netplay::links {

using Id = std::uint64_t;
using Clock = std::chrono::steady_clock;
using Digest = std::array<std::uint8_t, 32>;
inline constexpr unsigned MaximumMembers = 12, MaximumRacers = 4;

struct Member {
    Id id = 0;
    int port = -1;
    bool capable = false;
    bool operator==(const Member&) const = default;
};

struct Scope {
    Id room = 0, host = 0, generation = 0;
    std::vector<Member> members;
    bool operator==(const Scope&) const = default;
};

bool valid(const Scope&);
bool input_pair(const Scope&, Id first, Id second);
bool input_owner(const Scope&, Id sender, unsigned port);

// A sample describes the complete authenticated endpoint path. It must not be
// populated with the RTT to a proxy or a signaling service.
struct Sample {
    unsigned p95_ms = 0, loss_per_mille = 0, observations = 0;
    Clock::time_point measured{};
};
struct Capacity {
    unsigned step_p95_us = 0;
    bool measured = false, background_saturated = false;
    Clock::time_point sampled{};
};
struct Recommendation {
    Id member = 0;
    unsigned worst_p95_ms = 0, worst_loss_per_mille = 0;
    std::string reason;
    bool capacity_measured = false;
};
using Pair = std::pair<Id, Id>;
std::optional<Recommendation> recommend(const Scope&, const std::map<Pair, Sample>&, const std::map<Id, Capacity>&,
                                        Clock::time_point now);

// The host can change independently of controller-port and persistent-save
// ownership. The prepared digest binds their full native session snapshot.
struct Proposal {
    Scope before;
    Id serial = 0, successor = 0, save_owner = 0;
    Digest state{};
    bool operator==(const Proposal&) const = default;
};
enum class Phase { Idle, Offered, Preparing, Committing, Committed, Cancelled };

class Handover final {
  public:
    bool offer(Id authenticated, Proposal, bool waiting, Clock::time_point now, std::string& error);
    bool consent(Id authenticated, Id serial, bool accept, Clock::time_point now, std::string& error);
    bool bind(Id authenticated, Id serial, const Digest&, std::string& error);
    bool prepared(Id authenticated, Id serial, const Digest&, bool routes_ready, Clock::time_point now,
                  std::string& error);
    bool commit(Id authenticated, Id serial, const Digest&, std::string& error);
    bool acknowledge(Id authenticated, Id serial, const Digest&, std::string& error);
    bool cancel(Id authenticated, Id serial, std::string reason, std::string& error);
    void service(const Scope& current, bool waiting, Clock::time_point now);
    void reset();
    Phase phase() const {
        return phase_;
    }
    const std::optional<Proposal>& proposal() const {
        return proposal_;
    }
    const std::string& status() const {
        return status_;
    }
    bool can_commit() const;
    bool complete() const;
    bool frozen() const {
        return phase_ == Phase::Committing;
    }
    bool ready(Id member) const;
    bool acknowledged(Id member) const;
    std::optional<Scope> result() const;

  private:
    bool expected(Id, Id serial) const;
    Phase phase_ = Phase::Idle;
    std::optional<Proposal> proposal_;
    std::array<bool, MaximumMembers> prepared_{}, acknowledged_{};
    Clock::time_point deadline_{};
    Id highest_serial_ = 0;
    std::string status_;
};

} // namespace dkr::runtime::netplay::links
