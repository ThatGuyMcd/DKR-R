#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace dkr::online_performance {
// Call-local permission: only an authenticated ModData send or the initial
// pre-Go checkpoint can enter the larger SDK queue. No transport ABI changes.
inline thread_local bool setup_bulk = false;
class SetupBulk {
    bool previous_;
public:
    explicit SetupBulk(bool enabled):previous_(setup_bulk){setup_bulk=enabled;}
    ~SetupBulk(){setup_bulk=previous_;}
    SetupBulk(const SetupBulk&)=delete;
    SetupBulk& operator=(const SetupBulk&)=delete;
};
inline constexpr std::size_t SetupBytes=256U*1024U;
inline constexpr std::size_t ControlReserve=32U*1024U;
inline constexpr std::size_t DurableBatch=1024U*1024U;
constexpr bool fits(std::size_t used,std::size_t bytes,std::size_t limit) {
    return bytes<=limit && used<=limit-bytes;
}
template<class Traffic> constexpr bool additional_capacity(Traffic traffic,bool setup,
    std::size_t lane,std::size_t aggregate,std::size_t bytes) {
    if(setup && traffic==Traffic::Checkpoint)
        return fits(lane,bytes,SetupBytes)&&fits(aggregate,bytes,SetupBytes);
    // Ready/repair/heartbeat traffic retains headroom even while pre-game
    // bulk drains. The ordinary racing bulk limits remain unchanged.
    if(traffic==Traffic::Control||traffic==Traffic::Authoritative)
        return fits(lane,bytes,ControlReserve)&&fits(aggregate,bytes,SetupBytes+ControlReserve);
    return false;
}
constexpr std::uint64_t resource_budget(std::uint64_t physical,std::uint64_t available,bool mobile) {
    constexpr std::uint64_t MiB=1024*1024;
    if(!physical||!available)return 0;
    return (std::min<std::uint64_t>)({physical/16,available/4,(mobile?256ULL:768ULL)*MiB});
}
}
