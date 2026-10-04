#include "netplay/experimental_magic_codes.hpp"
#include <array>
#include <cassert>
#include <iostream>
using dkr::runtime::netplay::experimental::MagicCodes;
namespace codes=dkr::runtime::magic_codes;
using State=std::array<std::uint8_t,MagicCodes::kCheckpointBytes>;
State snapshot(const MagicCodes& owner){State bytes{};assert(owner.capture(bytes));return bytes;}
int main() {
    MagicCodes online;
    assert(!online.start(0,0,true));assert(!online.start(1,3,true));
    const auto selected=codes::magic_code_bit(7)|codes::magic_code_bit(10)|codes::magic_code_bit(26);
    assert(online.start(7,selected,true));const auto initial=snapshot(online);
    std::uint32_t active=0,unlocked=0;std::vector<std::uint8_t> journal;
    assert(!online.apply(1,2,active,unlocked));assert(!online.complete(1U<<26));assert(!online.frame_complete());
    State scratch{};assert(online.begin_frame(0));assert(!online.capture(scratch));
    assert(online.apply(1,2,active,unlocked));
    assert(active==(1U|(1U<<7)|(1U<<26)) && unlocked==(2U|(1U<<7)|(1U<<26)));
    assert(!online.complete(1U<<8));assert(online.complete(1U<<10));assert(online.complete(1U<<26));
    assert(online.frame_complete());assert(online.frame_complete());assert(online.end_frame(journal));
    const auto final=snapshot(online);const auto first_journal=journal;
    // Pending unconfirmed one-shot completion must disappear under rewind and
    // then reproduce EXACTLY. The stable launch queue is never called.
    assert(!online.confirmed_actions());assert(online.restore(initial));assert(online.begin_frame(0));
    assert(online.apply(1,2,active,unlocked));assert(online.complete(1U<<26));assert(online.frame_complete());
    assert(online.end_frame(journal));assert(journal==first_journal && snapshot(online)==final);
    for(unsigned offset=0;offset<MagicCodes::kJournalBytes;++offset) {
        auto bad=journal;bad[offset]^=128;
        assert(!online.commit(7,0,bad));assert(!online.confirmed_actions() && snapshot(online)==final);
    }
    assert(online.commit(7,0,journal));assert(online.confirmed_actions()==(1U<<26));
    assert(!online.commit(7,0,journal));assert(!online.restore(initial));
    for(unsigned offset:{0U,4U,5U,6U,8U,16U,20U,24U,28U,32U,36U,40U,41U,63U}) {
        auto bad=final;bad[offset]^=128;assert(!online.restore(bad));assert(snapshot(online)==final);
    }
    assert(online.begin_frame(1));assert(online.apply(0,0,active,unlocked));assert(!active && !unlocked);
    assert(online.complete(1U<<26));assert(online.frame_complete());assert(online.end_frame(journal));
    assert(online.commit(7,1,journal));assert(online.confirmed_actions()==(1U<<26));
    assert(online.begin_epoch(8));assert(online.confirmed_actions()==(1U<<26));
    const auto before_failure=snapshot(online);assert(online.begin_frame(0));assert(online.restore(before_failure));
    assert(online.begin_frame(0));assert(!online.begin_epoch(9));assert(online.end_frame(journal));
    assert(!online.begin_epoch(9));assert(online.commit(8,0,journal));assert(online.begin_epoch(9));
    // Offline policy retains credits; its separate owner cannot feed an online
    // checkpoint or consume an unselected balloon action.
    MagicCodes offline;assert(offline.start(7,1U<<10,false));assert(!offline.restore(initial));
    assert(offline.begin_frame(0));assert(offline.apply(0,0,active,unlocked));assert(active==(1U<<10));
    assert(offline.complete(1U<<26));assert(offline.complete(1U<<10));assert(offline.frame_complete());
    assert(offline.end_frame(journal));assert(offline.commit(7,0,journal));assert(offline.confirmed_actions()==(1U<<10));
    std::cout<<"Experimental Magic Code state: replayable launch, reversible actions, online policy, transactional confirmation; no launcher queue or filesystem.\n";
}
