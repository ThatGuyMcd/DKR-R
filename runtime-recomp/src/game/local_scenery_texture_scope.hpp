#pragma once

#include "local_scenery_arena.hpp"
#include "hle/rt64_rdp.h"
#include <array>
#include <cstring>

namespace dkr::runtime::local_scenery {
// A native billboard must not leave its texture or palette in canonical
// TMEM. Save at the FIRST native load, after any preceding guest loads, and
// restore AFTER this call's texture sampling/uploads, not during DL decoding.
// Staged RT64 invokes this adapter; the dependency checkout is unchanged.
class TextureReplayScope final {
public:
    TextureReplayScope(RT64::RDP& rdp,bool enabled):rdp_(rdp),enabled_(enabled) {}
    ~TextureReplayScope() { after_call(); }
    void before_load(std::uint32_t address) noexcept {
        if(!enabled_)return;
        const bool native=address>=kTextureBegin&&address<kTextureEnd;
        if(!native) { after_call();return; }
        if(active_)return;
        std::memcpy(tmem_.data(),rdp_.TMEM,sizeof(rdp_.TMEM));
        std::memcpy(rice_.data(),rdp_.rice.lastLoadOpByTMEM,sizeof(rdp_.rice.lastLoadOpByTMEM));
        active_=true;
    }
    void after_call() noexcept {
        if(!active_)return;
        std::memcpy(rdp_.TMEM,tmem_.data(),sizeof(rdp_.TMEM));
        std::memcpy(rdp_.rice.lastLoadOpByTMEM,rice_.data(),sizeof(rdp_.rice.lastLoadOpByTMEM));
        active_=false;
    }
private:
    RT64::RDP& rdp_;
    bool enabled_=false;
    bool active_=false;
    std::array<std::uint64_t,RDP_TMEM_WORDS> tmem_;
    std::array<RT64::LoadOperation,RDP_TMEM_WORDS> rice_;
};
}
