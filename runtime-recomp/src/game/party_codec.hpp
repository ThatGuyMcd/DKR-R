#pragma once
#include "party.hpp"
#include "confirmed_feed.hpp"
#include <span>
#include <vector>

namespace dkr::runtime::spectators::wire {
inline constexpr unsigned Version = 2;
inline constexpr std::size_t MaximumPacket = 12000, MaximumChunk = 11264;
enum class Kind : std::uint8_t {
    Roster = 1,
    Frames = 2,
    BaselineOffer = 3,
    BaselineChunk = 4,
    BaselineAck = 5,
    NeedBaseline = 6,
    TakeOver = 7,
    HandoverAck = 8,
    Leave = 9,
    Heartbeat = 10,
    Input = 11,
    Reject = 12
};
struct Packet {
    Kind kind = Kind::Heartbeat;
    std::uint64_t match = 0, serial = 0;
    std::vector<std::uint8_t> body;
};
struct ControllerSample {
    Cursor cursor;
    std::uint8_t port = 0;
    std::uint64_t lease = 0;
    PortInput input;
};
std::vector<std::uint8_t> encode(const Packet& value);
bool decode(std::span<const std::uint8_t> bytes, Packet& out, std::string& error);
std::vector<std::uint8_t> roster(const PartySnapshot& value);
bool roster(std::span<const std::uint8_t> bytes, PartySnapshot& out, std::string& error);
std::vector<std::uint8_t> frames(std::span<const ConfirmedFrame> values);
bool frames(std::span<const std::uint8_t> bytes, std::vector<ConfirmedFrame>& out, std::string& error);
std::vector<std::uint8_t> controller(const ControllerSample& value);
bool controller(std::span<const std::uint8_t> bytes, ControllerSample& out, std::string& error);
} // namespace dkr::runtime::spectators::wire
