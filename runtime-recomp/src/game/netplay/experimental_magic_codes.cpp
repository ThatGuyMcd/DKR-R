#include "experimental_magic_codes.hpp"
#include <algorithm>
#include <array>

namespace dkr::runtime::netplay::experimental {
namespace {
void put(std::span<std::uint8_t> bytes,std::size_t offset,std::uint64_t value,unsigned size=4) {
    for(unsigned b=0;b<size;++b)bytes[offset+b]=std::uint8_t(value>>(8*b));
}
std::uint64_t get(std::span<const std::uint8_t> bytes,std::size_t offset,unsigned size=4) {
    std::uint64_t value=0;for(unsigned b=0;b<size;++b)value|=std::uint64_t(bytes[offset+b])<<(8*b);return value;
}
}
bool MagicCodes::start(std::uint64_t epoch,std::uint32_t selection,bool online) {
    if(epoch_ || !epoch || selection!=magic_codes::normalise_magic_code_mask(selection))return false;
    selection_=selection;online_=online;epoch_=epoch;
    initial_=working_=magic_codes::begin_magic_code_session(selection&magic_codes::kPersistentMagicCodeMask,
        selection&magic_codes::kOneShotMagicCodeMask,online);return true;
}
bool MagicCodes::valid(const magic_codes::MagicCodeSessionState& s) const {
    if(s.persistent_mask!=initial_.persistent_mask || s.deferred_action_mask!=initial_.deferred_action_mask ||
       (s.armed_action_mask&~initial_.deferred_action_mask) || (s.completed_action_mask&~s.armed_action_mask))return false;
    return s.applied || (!s.armed_action_mask && !s.completed_action_mask);
}
bool MagicCodes::begin_epoch(std::uint64_t epoch) {
    if(!epoch_ || epoch<=epoch_ || open_ || next_!=confirmed_next_)return false;
    epoch_=epoch;next_=confirmed_next_=0;return true;
}
bool MagicCodes::begin_frame(std::uint32_t frame) {
    if(!epoch_ || open_ || frame!=next_ || frame==UINT32_MAX)return false;
    actions_=0;open_=true;return true;
}
bool MagicCodes::apply(std::uint32_t active,std::uint32_t unlocked,std::uint32_t& next_active,std::uint32_t& next_unlocked) {
    if(!open_)return false;
    const auto update=magic_codes::apply_magic_code_session(working_,active,unlocked);
    working_=update.state;next_active=update.active;next_unlocked=update.unlocked;return true;
}
bool MagicCodes::complete(std::uint32_t action) {
    if(!open_ || !action || (action&~magic_codes::kOneShotMagicCodeMask))return false;
    // Unlike stable native authority, speculative completion IS reversible.
    // This does not acknowledge a launcher queue; only commit can publish it.
    working_=magic_codes::complete_magic_code_action(working_,action,true);return true;
}
bool MagicCodes::frame_complete() {
    if(!open_)return false;
    const auto completed=working_.completed_action_mask;actions_|=completed;
    working_=magic_codes::acknowledge_magic_code_actions(working_,completed);return true;
}
bool MagicCodes::end_frame(std::vector<std::uint8_t>& out) {
    if(!open_)return false;
    std::vector<std::uint8_t> journal(kJournalBytes,0);
    std::copy_n("DKMJ",4,journal.begin());journal[4]=1;
    put(journal,8,epoch_,8);put(journal,16,next_);put(journal,20,actions_);
    out=std::move(journal);++next_;open_=false;actions_=0;return true;
}
bool MagicCodes::capture(std::span<std::uint8_t> out) const {
    if(!epoch_ || open_ || out.size()!=kCheckpointBytes || !valid(working_))return false;
    std::fill(out.begin(),out.end(),0);std::copy_n("DKMC",4,out.begin());out[4]=1;out[5]=online_;
    put(out,8,epoch_,8);put(out,16,next_);put(out,20,selection_);
    put(out,24,working_.persistent_mask);put(out,28,working_.deferred_action_mask);
    put(out,32,working_.armed_action_mask);put(out,36,working_.completed_action_mask);out[40]=working_.applied;return true;
}
bool MagicCodes::restore(std::span<const std::uint8_t> in) {
    if(!epoch_ || in.size()!=kCheckpointBytes || !std::equal(in.begin(),in.begin()+4,"DKMC") || in[4]!=1 ||
       in[5]!=unsigned(online_) || in[6] || in[7] || get(in,8,8)!=epoch_ || get(in,20)!=selection_ || in[40]>1 ||
       std::any_of(in.begin()+41,in.end(),[](auto b){return b!=0;}))return false;
    const auto frame=std::uint32_t(get(in,16));
    magic_codes::MagicCodeSessionState staged{std::uint32_t(get(in,24)),std::uint32_t(get(in,28)),
        std::uint32_t(get(in,32)),std::uint32_t(get(in,36)),in[40]!=0};
    if(frame<confirmed_next_ || frame>next_ || !valid(staged) || (staged.armed_action_mask&confirmed_actions_))return false;
    working_=staged;next_=frame;open_=false;actions_=0;return true;
}
bool MagicCodes::commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> journal) {
    if(!epoch_ || epoch!=epoch_ || frame!=confirmed_next_ || frame>=next_ || journal.size()!=kJournalBytes ||
       !std::equal(journal.begin(),journal.begin()+4,"DKMJ") || journal[4]!=1 || journal[5] || journal[6] || journal[7] ||
       get(journal,8,8)!=epoch || get(journal,16)!=frame)return false;
    const auto actions=std::uint32_t(get(journal,20));
    if((actions&~initial_.deferred_action_mask) || (actions&confirmed_actions_))return false;
    confirmed_actions_|=actions;++confirmed_next_;return true;
}
}
