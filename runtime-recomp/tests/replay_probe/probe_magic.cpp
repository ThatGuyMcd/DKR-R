#include "probe_magic.h"
#include "netplay/experimental_magic_codes.hpp"
#include "revision_addresses.hpp"
namespace {dkr::runtime::netplay::experimental::MagicCodes* owner=nullptr;}
extern "C" void dkr_probe_bind_magic(void* value){owner=static_cast<dkr::runtime::netplay::experimental::MagicCodes*>(value);}
extern "C" int dkr_probe_magic_enabled(){return owner!=nullptr;}
extern "C" void dkr_probe_magic_addresses(uint32_t addresses[2]) {
    const auto& table=DKR_PROBE_REVISION==77?dkr::runtime::revision_addresses::kUsV77:dkr::runtime::revision_addresses::kUsV80;
    addresses[0]=table.ActiveMagicCodes;addresses[1]=table.UnlockedMagicCodes;
}
extern "C" int dkr_probe_magic_request(unsigned operation,uint32_t words[2]) {
    if(!owner || !words)return 0;
    // Always return before the C trampoline traps. No C++ lifetime crosses a
    // longjmp, and no native stable session/launcher queue is reachable.
    try {
        if(operation==DKR_MAGIC_APPLY)return owner->apply(words[0],words[1],words[0],words[1]);
        if(operation==DKR_MAGIC_COMPLETE)return owner->complete(words[0]);
        if(operation==DKR_MAGIC_FRAME_COMPLETE)return owner->frame_complete();
    }catch(...){return 0;}return 0;
}
