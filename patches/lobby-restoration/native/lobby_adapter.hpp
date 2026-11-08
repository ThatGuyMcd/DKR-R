#pragma once
#include "party_link.hpp"

namespace dkr::runtime::lobby_ui {
struct Metric {
    spectators::MemberId member=0;
    std::array<std::uint16_t,16> rtts{};
    unsigned count=0,index=0;
};
struct Snapshot {
    spectators::LinkView link;
    std::uint64_t room=0;
    bool save_ready=false,launching=false;
    unsigned maximum_racers=4;
    std::uint16_t host_rtt=0;
    std::vector<Metric> metrics;
    std::unordered_map<spectators::MemberId,bool> pending_watch;
};
Snapshot snapshot(const void* session);
struct Removal { std::uint64_t room=0,generation=0; spectators::MemberId member=0; };
bool request_removal(const void* session,const Snapshot&,spectators::MemberId,std::string& error);
bool take_removal(const void* session,Removal&);
}
