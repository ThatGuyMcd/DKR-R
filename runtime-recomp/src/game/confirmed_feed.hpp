#pragma once
#include "party.hpp"
#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <span>
#include <vector>

namespace dkr::runtime::spectators {
struct PortInput {
    std::uint16_t buttons=0;
    std::int8_t x=0,y=0;
    bool operator==(const PortInput&)const=default;
};
struct ConfirmedFrame {
    Cursor cursor;
    std::array<PortInput,RacingPorts> inputs{};
    std::vector<std::uint8_t> command;
    bool scene_boundary=false;
    bool operator==(const ConfirmedFrame&)const=default;
};
struct Baseline {
    Cursor next;
    std::uint64_t match=0,schema=0,control_generation=0;
    std::array<std::uint8_t,32> digest{};
    std::shared_ptr<const std::vector<std::uint8_t>> state;
    std::uint32_t magic_actions=0;
};
enum class FeedResult {Accepted,Duplicate,Stale,Gap,Invalid,Capacity};
// Contains confirmed data only. The simulation owner publishes here; a
// separate transport worker owns compression and per-spectator delivery.
// One immutable checkpoint is shared, not copied for each watching member.
class ConfirmedFeed final {
public:
    static constexpr std::size_t MaximumState=128U*1024U*1024U;
    static constexpr std::size_t MaximumCommand=1024,MaximumFrames=3600,MaximumTail=4U*1024U*1024U;
    FeedResult baseline(Baseline next);
    FeedResult append(ConfirmedFrame frame);
    std::shared_ptr<const Baseline> current()const{return baseline_;}
    std::vector<ConfirmedFrame> after(Cursor first,std::size_t maximum=30)const;
    bool covers(Cursor first)const;
    Cursor next()const{return next_;}
    void clear();
private:
    std::shared_ptr<const Baseline> baseline_;
    std::deque<ConfirmedFrame> tail_;
    Cursor next_{};
    std::size_t bytes_=0;
};
}
