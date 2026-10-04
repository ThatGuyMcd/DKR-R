#include "probe_magic.h"
#include "recomp.h"
#include "netplay/experimental_magic_codes.hpp"
#include "netplay/runtime_state.hpp"
#include <cassert>
#include <cstring>
#include <vector>
namespace {
const char* import_name=nullptr;
void entry(uint8_t* ram,recomp_context* context){dkr_probe_native(import_name,ram,context);}
}
bool dkr_probe_magic_checks() {
    using dkr::runtime::netplay::experimental::MagicCodes;
    MagicCodes owner;assert(owner.start(22,(1U<<7)|(1U<<26),true));
    std::vector<uint8_t> ram(dkr::runtime::netplay::kRollbackMemoryBytes);
    recomp_context context{};dkr::runtime::netplay::repair_float_register_pointer(context);context.r2=723;
    uint32_t addresses[2];dkr_probe_magic_addresses(addresses);
    const auto word=[&](uint32_t address){uint32_t value=0;memcpy(&value,ram.data()+address-0x80000000U,4);return value;};
    const auto call=[&](const char* name){import_name=name;return dkr_probe_run(entry,ram.data(),ram.size(),&context,10000);};
    dkr_probe_offline_services(1);dkr_probe_bind_magic(&owner);
    assert(!call("dkr_apply_launch_magic_codes").completed && !word(addresses[0]) && context.r2==723);
    std::array<uint8_t,MagicCodes::kCheckpointBytes> initial{},final{};assert(owner.capture(initial));
    assert(owner.begin_frame(0));assert(call("dkr_apply_launch_magic_codes").completed && context.r2==723);
    assert(word(addresses[0])==((1U<<7)|(1U<<26)) && word(addresses[1])==word(addresses[0]));
    assert(call("dkr_magic_code_balloon_awarded").completed);assert(!owner.confirmed_actions());
    assert(call("dkr_magic_codes_frame_complete").completed);std::vector<uint8_t> journal;
    assert(owner.end_frame(journal));assert(owner.capture(final));
    assert(owner.restore(initial));assert(owner.begin_frame(0));
    assert(call("dkr_apply_launch_magic_codes").completed);assert(call("dkr_magic_code_balloon_awarded").completed);
    assert(call("dkr_magic_codes_frame_complete").completed);std::vector<uint8_t> replay;assert(owner.end_frame(replay));
    assert(replay==journal);std::array<uint8_t,MagicCodes::kCheckpointBytes> after{};assert(owner.capture(after) && after==final);
    assert(owner.commit(22,0,journal) && owner.confirmed_actions()==(1U<<26));
    dkr_probe_bind_magic(nullptr);assert(!call("dkr_apply_launch_magic_codes").completed);
    assert(!call("dkr_magic_code_balloon_awarded").completed);return true;
}
