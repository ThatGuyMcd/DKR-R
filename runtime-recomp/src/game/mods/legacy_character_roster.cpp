#include "legacy_character_roster.hpp"
#include <algorithm>
#include <set>

namespace dkr::mods {
CharacterRoster::CharacterRoster(std::shared_ptr<const CharacterNamespace> assets):assets_(std::move(assets)) {
    if(!assets_ || assets_->characters.empty())throw Error("Character roster needs a prepared asset namespace.");
}
void CharacterRoster::request(unsigned slot,const std::string& id) {
    if(slot>=4)throw Error("Character selection refers to an invalid player slot.");
    if(!id.empty() && std::none_of(assets_->characters.begin(),assets_->characters.end(),[&](const auto& c){return c.id==id;}))
        throw Error("Selected character is not in this boot's prepared namespace.");
    requested_[slot]=id;
}
void CharacterRoster::clear_active(){humans_={};active_={};ai_={};allow_duplicates_=false;}
void CharacterRoster::clear_ai(){active_={};ai_={};std::copy(humans_.begin(),humans_.end(),active_.begin());}
void CharacterRoster::scene_humans(unsigned count){
    if(count>4)throw Error("Invalid scene human roster.");
    active_={};ai_={};std::copy_n(humans_.begin(),count,active_.begin());
}
void CharacterRoster::commit(std::span<const std::uint8_t> native,unsigned human_count) {
    if(human_count>4 || native.size()<human_count)throw Error("Invalid native human roster.");
    std::array<std::string,4> next{};
    std::set<std::string> used;
    for(unsigned i=0;i<human_count;++i) {
        if(native[i]>=10)throw Error("Native behaviour ID is outside the original roster.");
        if(requested_[i].empty())continue;
        const auto found=std::find_if(assets_->characters.begin(),assets_->characters.end(),[&](const auto& c){return c.id==requested_[i];});
        if(found==assets_->characters.end())throw Error("Logical character is missing at roster commit.");
        if(found->base_character!=native[i])
            throw Error("Logical character does not match the committed native behaviour (P"+std::to_string(i+1)+
                ", expected="+std::to_string(found->base_character)+", actual="+std::to_string(native[i])+").");
        if(!used.insert(found->id).second && !allow_duplicates_)throw Error("The same custom character was selected twice.");
        next[i]=found->id;
    }
    humans_=std::move(next);clear_ai();
}
void CharacterRoster::assign_ai(std::span<std::uint8_t> native,unsigned humans,unsigned racers,
    std::span<const std::uint8_t> unlocked,const std::function<unsigned(unsigned)>& random) {
    if(!humans || humans>4 || racers<humans || racers>8 || native.size()<racers || unlocked.empty() || unlocked.size()>10)
        throw Error("Invalid race AI topology/pool.");
    struct Candidate {std::uint8_t base;std::string id;};
    std::vector<Candidate> pool;std::set<unsigned> unique_stock;
    for(auto base:unlocked){
        if(base>=10 || !unique_stock.insert(base).second)throw Error("Invalid unlocked AI identity.");
        pool.push_back({base,{}});
    }
    for(const auto& c:assets_->characters){
        if(c.base_character>=10)throw Error("Invalid custom AI donor.");
        pool.push_back({static_cast<std::uint8_t>(c.base_character),c.id});
    }
    auto next=active_;auto ids=std::vector<std::uint8_t>(native.begin(),native.begin()+racers);
    for(unsigned h=0;h<humans;++h){
        if(native[h]>=10)throw Error("Invalid human behaviour in AI roster.");
        // Logical identities, not donor IDs: original Conker and Yooka may
        // race together, but the human's exact chosen character is reserved.
        std::erase_if(pool,[&](const Candidate& c){return humans_[h].empty()?c.id.empty() && c.base==native[h]:c.id==humans_[h];});
    }
    for(unsigned i=humans;i<8;++i)next[i].clear();
    for(unsigned i=humans;i<racers;++i){
        if(pool.empty())throw Error("Insufficient distinct AI characters.");
        const unsigned chosen=random(static_cast<unsigned>(pool.size()));
        if(chosen>=pool.size())throw Error("Native AI random result escaped its bounds.");
        ids[i]=pool[chosen].base;next[i]=pool[chosen].id;
        pool.erase(pool.begin()+chosen);
    }
    active_=std::move(next);std::copy(ids.begin()+humans,ids.end(),native.begin()+humans);
    ai_={};for(unsigned i=humans;i<racers;++i)ai_[i]=true;
}
std::optional<unsigned> CharacterRoster::header(unsigned player,unsigned native_header) const {
    if(player>=8 || active_[player].empty())return std::nullopt;
    const auto found=std::find_if(assets_->characters.begin(),assets_->characters.end(),[&](const auto& c){return c.id==active_[player];});
    if(found==assets_->characters.end())throw Error("A committed character lost its immutable assets.");
    constexpr unsigned headers[]{2,3,4,5,6,7,8,9,1,0};
    if(found->base_character>=10)throw Error("Invalid inherited character behaviour.");
    for(unsigned vehicle=0;vehicle<3;++vehicle)
        if(native_header==headers[found->base_character]+vehicle*10)return found->headers[vehicle];
    return std::nullopt;
}
const std::string& CharacterRoster::active(unsigned slot) const {
    if(slot>=8)throw Error("Invalid character racer slot.");return active_[slot];
}
} // namespace dkr::mods
