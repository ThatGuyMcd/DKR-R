#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::spectators {
using MemberId = std::uint64_t;
inline constexpr unsigned RacingPorts = 4, MaximumSpectators = 8, MaximumMembers = RacingPorts + MaximumSpectators;
enum class Role : std::uint8_t { Racer, Spectator };
struct Cursor {
    std::uint64_t epoch = 0;
    std::uint32_t frame = 0;
    bool operator==(const Cursor&) const = default;
};
struct Member {
    MemberId id = 0;
    std::string name;
    Role role = Role::Spectator;
    std::optional<std::uint8_t> port;
    bool host = false, connected = false;
    bool operator==(const Member&) const = default;
};
struct ControlLease {
    MemberId member = 0;
    std::uint64_t generation = 0;
    Cursor starts{};
    bool operator==(const ControlLease&) const = default;
};
struct Handover {
    std::uint64_t token = 0, roster_generation = 0;
    std::uint8_t port = 0;
    MemberId outgoing = 0, incoming = 0;
    Cursor boundary{};
    std::array<std::uint8_t, 32> world_digest{};
    std::vector<MemberId> required, acknowledged;
    bool operator==(const Handover&) const = default;
};
struct PartySnapshot {
    MemberId host = 0;
    std::uint64_t generation = 0;
    std::array<ControlLease, RacingPorts> controls{};
    std::vector<Member> members;
    std::optional<Handover> handover;
};
// Owner-thread policy. Synchronisation and authentication belong to its caller;
// packet-supplied host/port claims are never authority for these operations.
class Party final {
  public:
    bool create(MemberId host, std::string name, std::string& error);
    bool add(MemberId authenticated, std::string name, std::optional<std::uint8_t> port, std::string& error);
    bool waiting_role(MemberId actor, MemberId member, std::optional<std::uint8_t> port, std::string& error);
    bool remove(MemberId authenticated, std::string& error);
    bool propose(MemberId actor, std::uint8_t port, MemberId replacement, Cursor confirmed_boundary,
                 const std::array<std::uint8_t, 32>& world_digest, std::string& error);
    bool acknowledge(MemberId authenticated, std::uint64_t token, Cursor caught_up,
                     const std::array<std::uint8_t, 32>& world_digest, std::string& error);
    bool transfer(MemberId actor, Cursor confirmed_boundary, std::string& error);
    bool cancel(MemberId actor, std::uint64_t token, std::string& error);
    bool accepts(MemberId authenticated, unsigned port, std::uint64_t generation, Cursor input) const;
    const Member* find(MemberId id) const;
    const PartySnapshot& snapshot() const {
        return state_;
    }
    bool replace_verified(PartySnapshot value, std::string& error);
    static bool valid(const PartySnapshot& value, std::string& error);

  private:
    bool bump(std::string& error);
    PartySnapshot state_;
    std::uint64_t proposal_serial_ = 0;
};
} // namespace dkr::runtime::spectators
