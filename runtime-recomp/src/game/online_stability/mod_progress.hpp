#pragma once
#include "control.hpp"
#include <algorithm>

namespace dkr::online_stability {
template<class Peers> void publish_mod_progress(const void* link,const Peers& peers) {
    const auto owner=mod_control(link);if(!owner||!owner->mod_update_due(Clock::now()))return;
    std::vector<Join> rows;rows.reserve(std::min<std::size_t>(MaximumJoins,peers.size()));
    for(const auto& peer:peers) {
        if(rows.size()==MaximumJoins)break;
        Stage stage=Stage::Review;
        using Phase=decltype(peer.phase);
        switch(peer.phase) {
        case Phase::Downloading:stage=peer.stage.find("Queued")!=std::string::npos||peer.stage.find("queue")!=std::string::npos?Stage::Queued:Stage::Downloading;break;
        case Phase::Preparing:stage=Stage::Preparing;break;
        case Phase::Verified:stage=Stage::Entering;break;
        default:break;
        }
        rows.push_back({peer.request,0,std::min(peer.received,peer.total),peer.total,{},stage});
    }
    owner->mods(std::move(rows));
}
} // namespace dkr::online_stability
