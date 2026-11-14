#pragma once
#include "party_link.hpp"
#include "owned_observer.hpp"
#include "owned_game.hpp"
#include "online_stability/party_pacing.hpp"
#include "online_stability/peer_clock.hpp"
#include <functional>

namespace dkr::runtime::spectators {
// All gameplay calls and all protocol decisions live on the simulation owner.
// Transport/UI threads exchange only bounded messages and copied lobby views.
class PartyRuntime final {
public:
    using Clock=std::chrono::steady_clock;
    PartyRuntime(const void* link,net::experimental::OwnedGame& world,
                 net::experimental::PresentationMailbox& presentation,net::LaunchDescriptor launch,HashState hash);
    ~PartyRuntime();
    bool begin(std::string& error);
    bool pulse(Clock::time_point now,net::PackedInput input,std::string& error);
    bool watching()const;
    unsigned local_port()const;
    Cursor next()const;
    std::uint32_t confirmed()const;
    net::experimental::Statistics statistics()const;
    std::string status()const;
    bool replay_ready()const;
    dkr::online_stability::Pause pause_reason()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}

