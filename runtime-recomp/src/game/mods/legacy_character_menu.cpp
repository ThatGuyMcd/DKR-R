#include "legacy_character_menu.hpp"
#include <algorithm>
#include <set>

namespace dkr::mods {
using F=CharacterMenuField;
CharacterMenuMemory::CharacterMenuMemory(std::span<std::uint8_t> memory,const CharacterMenuFields& fields)
    :memory_(memory),fields_(fields) {
    if(memory.size()%4)throw Error("Character menu RDRAM is truncated.");
    constexpr std::array<unsigned,static_cast<unsigned>(F::Count)> sizes{4,4,8,8,20,10,10,4,4,4,4,4,56,32,16,4,4,4};
    for(unsigned i=0;i<fields.size();++i)check(fields[i],sizes[i]);
}
std::size_t CharacterMenuMemory::check(std::uint32_t address,std::size_t size)const {
    const auto p=std::size_t(address&0x1fffffffU);
    if((address&0xe0000000U)!=0x80000000U || p>memory_.size() || size>memory_.size()-p)
        throw Error("Character menu accessed an invalid guest range.");
    return p;
}
std::uint32_t CharacterMenuMemory::address(F field,unsigned offset)const {
    const auto base=fields_.at(static_cast<unsigned>(field));
    if(offset>0xffffffffU-base)throw Error("Character menu address overflow.");
    return base+offset;
}
std::uint32_t CharacterMenuMemory::read(std::uint32_t address,unsigned size)const {
    if(!size || size>4)throw Error("Invalid character menu word size.");
    const auto p=check(address,size);std::uint32_t value=0;
    for(unsigned i=0;i<size;++i)value=(value<<8)|memory_[(p+i)^3];return value;
}
void CharacterMenuMemory::write(std::uint32_t address,std::uint32_t value,unsigned size) {
    if(!size || size>4)throw Error("Invalid character menu word size.");
    const auto p=check(address,size);
    for(unsigned i=0;i<size;++i)memory_[(p+i)^3]=static_cast<std::uint8_t>(value>>(8*(size-i-1)));
}
std::uint32_t CharacterMenuMemory::get(F field,unsigned offset,unsigned size)const{return read(address(field,offset),size);}
void CharacterMenuMemory::set(F field,std::uint32_t value,unsigned offset,unsigned size){write(address(field,offset),value,size);}
void CharacterMenuMemory::bytes(std::uint32_t address,View value) {
    const auto p=check(address,value.size());
    for(std::size_t i=0;i<value.size();++i)memory_[(p+i)^3]=value[i];
}
CharacterMenuAdapter::CharacterMenuAdapter(std::shared_ptr<const CharacterNamespace> assets):assets_(std::move(assets)) {
    if(!assets_ || assets_->characters.empty() || assets_->characters.size()>CustomCharacterLimit)throw Error("Invalid admitted character menu library.");
    std::set<std::string> ids;
    for(const auto& c:entries())if(c.base_character>=10 || c.id.size()!=64 || c.name.empty() || c.name.size()>255 || !ids.insert(c.id).second)
        throw Error("Invalid character menu identity.");
    reset();
}
void CharacterMenuAdapter::reset(){
    custom_={};displayed_={};owner_=0;duplicates_=false;
    for(unsigned s=0;s<std::min<std::size_t>(CustomStageSlots,entries().size());++s)displayed_[s]=s;
}
bool CharacterMenuAdapter::has_custom()const{return std::ranges::any_of(custom_,[](const auto& c){return c.has_value();});}
std::vector<unsigned> CharacterMenuAdapter::input(CharacterMenuMemory& g,bool duplicates) {
    std::vector<unsigned> sounds;
    if(static_cast<std::int32_t>(g.get(F::Delay))!=0)return sounds;
    duplicates_=duplicates;
    const unsigned stock_count=8+unsigned(drumstick_)+unsigned(tt_);
    struct Point{int x,y;};
    std::vector<Point> positions{{-33,0},{-5,0},{20,0},{48,0},{-28,1},{-5,1},{15,1},{36,1}};
    if(drumstick_)positions.push_back({8,0});
    if(tt_)positions.push_back({8,1});
    for(const auto& anchor:CustomStageAnchors)positions.push_back({anchor.x,anchor.row});
    auto slot_for=[&](std::size_t id) {
        for(unsigned s=0;s<CustomStageSlots;++s)if(displayed_[s]==id)return s;
        throw Error("Selected character lost its visible stage slot.");
    };
    auto set_custom=[&](unsigned p,std::size_t id) {
        custom_[p]=id;
        const auto base=entries()[id].base_character;unsigned placeholder=0;
        for(unsigned n=0;n<stock_count;++n)
            if(g.read(g.get(F::SelectTable)+14*n+12,2)==base){placeholder=n;break;}
        g.set(F::NativeIndices,placeholder,p,1);
    };
    auto taken=[&](unsigned p,unsigned logical) {
        if(duplicates)return false;
        for(unsigned i=0;i<4;++i)if(i!=p && g.get(F::Active,i,1)) {
            const auto selected=custom_[i]?stock_count+slot_for(*custom_[i]):g.get(F::NativeIndices,i,1);
            if(selected==logical)return true;
        }
        return false;
    };
    for(unsigned p=0;p<4;++p) {
        if(!g.get(F::Active,p,1)){custom_[p].reset();continue;}
        const auto buttons=g.get(F::Buttons,4*p),status=g.get(F::Status,p,1);
        const auto x=static_cast<std::int16_t>(g.get(F::StickX,2*p,2)),y=static_cast<std::int16_t>(g.get(F::StickY,2*p,2));
        if(status>2 || g.get(F::Ready)>4 || g.get(F::Players)>4)throw Error("Invalid native character ready state.");
        // Native confirmation/cancellation remains authoritative. L/R cycles
        // only an unconfirmed custom identity, never a global roster page.
        if(status || (buttons&0xd000))continue;
        const auto shoulders=buttons&0x30U;
        if(custom_[p] && shoulders && shoulders!=0x30U && entries().size()>CustomStageSlots) {
            const auto old_id=*custom_[p];const auto current=slot_for(old_id);bool moved=false;
            // A shared DOUBLEVISION cursor cannot replace its actor without
            // silently changing the other player's identity. Keep it pinned.
            bool shared=false;
            for(unsigned q=0;q<4;++q)if(q!=p && g.get(F::Active,q,1) && custom_[q]==old_id)shared=true;
            for(std::size_t step=1;step<entries().size();++step) {
                if(shared)break;
                const auto id=(old_id+(shoulders==0x10U?step:entries().size()-step))%entries().size();
                // Visible actors are reserved even without a human cursor.
                // Browsing never changes the player's physical stage slot.
                if(std::ranges::find(displayed_,std::optional<std::size_t>(id))!=displayed_.end())continue;
                displayed_[current]=id;
                set_custom(p,id);owner_=p;moved=true;break;
            }
            if(moved) {
                g.set(F::CurrentMusic,entries()[*custom_[p]].base_character,0,1);
                g.set(F::CurrentMusic,0,2,2);g.set(F::CurrentMusic,20,1,1);
            }
            sounds.push_back(moved?0xEC:0x15C);
            g.set(F::Buttons,buttons&~0x30U,4*p);g.set(F::StickX,0,2*p,2);g.set(F::StickY,0,2*p,2);
            continue;
        }
        if(!x && !y)continue;
        const unsigned old=custom_[p]?stock_count+slot_for(*custom_[p]):g.get(F::NativeIndices,p,1);
        if(old>=positions.size())throw Error("Invalid stage cursor identity.");
        const auto origin=positions[old];
        unsigned candidate=old;int best=1000000;
        for(unsigned n=0;n<positions.size();++n) {
            if(n==old || (n>=stock_count && !displayed_[n-stock_count]) || taken(p,n))continue;
            const auto point=positions[n];const int dx=point.x-origin.x,dy=point.y-origin.y;
            // Physical negative X is left; positive Y is up. Never reuse the
            // misleading leftInput/rightInput member names in the retail table.
            if(y ? (y>0?dy>=0:dy<=0) : (dy!=0 || (x<0?dx>=0:dx<=0)))continue;
            const int score=y?std::abs(dx):std::abs(dx);
            if(score<best){best=score;candidate=n;}
        }
        if(candidate!=old) {
            if(candidate>=stock_count) {
                set_custom(p,*displayed_[candidate-stock_count]);owner_=p;
            } else {custom_[p].reset();g.set(F::NativeIndices,candidate,p,1);}
            // Directional input is consumed before native charselect_input.
            // Preserve its music-channel handoff and 20-tick fade request.
            const auto voice=custom_[p]?entries()[*custom_[p]].base_character:
                g.read(g.get(F::SelectTable)+14*candidate+12,2);
            if(voice>=10)throw Error("Invalid selection music channel.");
            g.set(F::CurrentMusic,voice,0,1);
            g.set(F::CurrentMusic,0,2,2);
            g.set(F::CurrentMusic,20,1,1);
            sounds.push_back(0xEC);
        } else sounds.push_back(0x15C);
        g.set(F::StickX,0,2*p,2);g.set(F::StickY,0,2*p,2);
    }
    return sounds;
}
CharacterMenuView CharacterMenuAdapter::view(const CharacterMenuMemory& g)const {
    CharacterMenuView v;v.owner=owner_;v.custom=custom_;v.displayed=displayed_;
    const auto delay=static_cast<std::int32_t>(g.get(F::Delay));v.visible=delay>-23 && delay<23;
    for(unsigned p=0;p<4;++p) {v.active[p]=g.get(F::Active,p,1)!=0;v.ready[p]=g.get(F::Status,p,1)!=0;v.native[p]=g.get(F::NativeIndices,p,1);}
    if(v.active[owner_] && custom_[owner_]){v.custom_page=true;v.page=static_cast<unsigned>(*custom_[owner_]/8);}
    return v;
}
void CharacterMenuAdapter::commit(CharacterMenuMemory& g,CharacterRoster& roster,unsigned humans) {
    unsigned count=0;std::array<std::string,4> selected{};std::array<unsigned,4> ids{};
    for(unsigned p=0;p<4;++p)if(g.get(F::Active,p,1)) {
        if(count>=4 || !g.get(F::Status,p,1))throw Error("Custom character launch contains an unready player.");
        ids[count]=g.get(F::NativeIDs,count,1);
        if(custom_[p]){const auto& c=entries().at(*custom_[p]);ids[count]=c.base_character;selected[count]=c.id;}
        if(ids[count]>=10)throw Error("Character launch contains an invalid native behaviour.");
        ++count;
    }
    if(!humans || humans!=count)throw Error("Logical character roster disagrees with native controller ownership.");
    roster.set_duplicate_policy(duplicates_);
    for(unsigned i=0;i<4;++i)roster.request(i,selected[i]);
    for(unsigned i=0;i<count;++i)g.set(F::NativeIDs,ids[i],i,1);
}
bool CharacterMenuAdapter::native_move(CharacterMenuMemory& g,unsigned player,std::uint32_t direction,unsigned bounds,bool duplicates) {
    if(player>=4 || !bounds || bounds>4 || direction>0xffffffffU-bounds)throw Error("Invalid native character direction list.");
    for(unsigned j=0;j<bounds;++j) {
        const auto next=g.read(direction+j,1);if(next==255)return false;
        if(next>=10)throw Error("Native character direction is out of range.");
        bool taken=false;
        if(!duplicates)for(unsigned i=0;i<4;++i)if(i!=player && !custom_[i] && g.get(F::NativeIndices,i,1)==next)taken=true;
        if(!taken){g.set(F::NativeIndices,next,player,1);return true;}
    }
    return false;
}
}
