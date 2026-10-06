#include "legacy_cache_namespace.hpp"
#include "legacy_mod_dependencies.hpp"
#include "legacy_checkpoint.hpp"
#include <limits>
#include <set>

namespace dkr::mods {
namespace {
std::size_t physical(View words,std::uint32_t address,std::size_t length) {
    const auto start=std::size_t(address&0x1fffffff);
    if((address&0xe0000000U)!=0x80000000U || address%4 || words.size()%4 ||
       start>words.size() || length>words.size()-start)
        throw Error("Guest cache range is invalid.");
    return start;
}
std::uint32_t word(View words,std::uint32_t address) {
    const auto p=physical(words,address,4);
    return std::uint32_t(words[p^3])<<24 | std::uint32_t(words[(p+1)^3])<<16 |
           std::uint32_t(words[(p+2)^3])<<8 | words[(p+3)^3];
}
std::string text_hash(const std::string& value) {
    return sha256(View(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()));
}
}
CacheNamespace::CacheNamespace(const AssetBank& boot)
    :capacities_(legacy_asset_cache_capacities(boot.augmented(),
        boot.record_count(2),boot.record_count(4),boot.record_count(12),boot.record_count(29))) {}
std::uint32_t CacheNamespace::capacity(CacheKind kind)const {
    switch(kind) {
    case CacheKind::Texture:return capacities_[0];
    case CacheKind::Sprite:return capacities_[1];
    case CacheKind::Model:return capacities_[2];
    }
    throw Error("Unknown guest cache type.");
}
CacheContent::CacheContent(std::shared_ptr<const AssetBank> bank,std::shared_ptr<const AssetBus::Mount> mount)
    :bank_(std::move(bank)),mount_(std::move(mount)) {
    if(!bank_ || !mount_ || bank_->fingerprint()!=mount_->directory()->fingerprint())
        throw Error("Cache content does not match its retained address owner.");
}
const std::string& CacheContent::identity(CacheKind kind,std::uint32_t id) {
    const auto key=std::make_pair(kind,id);
    if(const auto it=identities_.find(key);it!=identities_.end()) return it->second;
    // DKR's section order is 3D=2, 2D=4 (not the cache table-type order).
    // The high bit on a texture ID selects the 3D section.
    const auto section=kind==CacheKind::Texture?(id&0x8000?2U:4U):(kind==CacheKind::Sprite?12U:29U);
    if(id>=(kind==CacheKind::Texture?0x10000U:0x8000U)) throw Error("Unallocated cache asset ID.");
    const auto index=kind==CacheKind::Texture?id&0x7fff:id;
    const auto record=bank_->record(section,index);
    std::string description="cache-v1:"+std::to_string(section)+":"+std::to_string(index)+":"+sha256(record);
    if(kind==CacheKind::Sprite) {
        for(const auto texture:inspect_sprite_textures(record)) description+=":"+identity(CacheKind::Texture,texture);
    } else if(kind==CacheKind::Model) {
        for(const auto texture:inspect_model_textures(record)) description+=":"+identity(CacheKind::Texture,texture|0x8000);
        const auto table=bank_->augmented()?bank_->record(30,0):bank_->stock_section(30);
        const auto start=be16(table,index*2),end=be16(table,(index+1)*2);
        if(start>end || end>bank_->record_count(32)) throw Error("Model cache animation range is invalid.");
        // Include the complete potential range, even when retail currently
        // selects only a prefix through gModelAnimOffsetID.
        for(unsigned animation=start;animation<end;++animation)
            description+=":"+std::to_string(animation)+":"+sha256(bank_->record(32,animation));
    }
    return identities_.emplace(key,text_hash(description)).first->second;
}
CacheNamespace::Plan CacheNamespace::prepare(View words,std::span<const GuestCacheTable> tables,
    std::shared_ptr<CacheContent> current,std::shared_ptr<CacheContent> next) const {
    if(!current || !next || current->revision()!=next->revision() || tables.size()!=3 ||
       generation_==std::numeric_limits<std::uint64_t>::max()) throw Error("Invalid cache transition.");
    Plan plan;plan.authority=this;plan.generation=generation_;
    plan.guest_data=words.data();plan.guest_size=words.size();
    std::set<CacheKind> kinds;
    std::set<std::uint32_t> addressed_words;
    auto remember=[&](std::uint32_t address,std::uint32_t expected,std::uint32_t value) {
        if(!addressed_words.insert(address).second) throw Error("Guest cache tables overlap.");
        plan.words.push_back({address,expected,value});
    };
    for(const auto& table:tables) {
        if(!kinds.insert(table.kind).second) throw Error("Duplicate guest cache table.");
        const auto count=word(words,table.count_address),base=word(words,table.pointer_address);
        const auto allocated=capacity(table.kind);
        if(count>allocated) throw Error("Guest cache kind "+std::to_string(static_cast<unsigned>(table.kind))+
            " count "+std::to_string(count)+" exceeds its boot-allocated capacity "+std::to_string(allocated)+".");
        remember(table.count_address,count,count);remember(table.pointer_address,base,base);
        if(!count) continue;
        physical(words,base,count*8);
        for(unsigned slot=0;slot<count;++slot) {
            const auto id_address=base+slot*8;
            const auto id=word(words,id_address),pointer=word(words,id_address+4);
            remember(id_address+4,pointer,pointer);
            if(id==0xffffffffU) {
                if(pointer!=0xffffffffU) throw Error("Empty cache ID has a live pointer.");
                remember(id_address,id,id);continue;
            }
            physical(words,pointer,4);
            Entry entry;
            if(id>=Hidden) {
                const auto old=entries_.find({table.kind,slot});
                if(id!=(Hidden|slot) || old==entries_.end() || old->second.pointer!=pointer)
                    throw Error("Retained cache entry lost its ownership metadata.");
                entry=old->second;
            } else {
                // New retail loads publish ordinary IDs. This replaces stale
                // metadata even if the allocator reused exactly the same pointer.
                entry={pointer,id,current->identity(table.kind,id),current};
            }
            const auto match=entry.identity==next->identity(table.kind,entry.legacy_id);
            remember(id_address,id,match?entry.legacy_id:(Hidden|slot));
            plan.entries.emplace(Slot{table.kind,slot},std::move(entry));
        }
    }
    return plan;
}
bool CacheNamespace::apply(std::span<std::uint8_t> words,Plan&& plan) {
    if(plan.authority!=this || plan.generation!=generation_ || words.data()!=plan.guest_data || words.size()!=plan.guest_size) return false;
    for(const auto& change:plan.words) if(word(words,change.address)!=change.expected) return false;
    // The caller's serialized guest transition is the synchronization boundary.
    // No renderer-owned pointers/refcounts or scheduler messages are touched.
    for(const auto& change:plan.words) if(change.expected!=change.value) {
        const auto p=change.address&0x1fffffff;
        for(unsigned i=0;i<4;++i) words[(p+i)^3]=static_cast<std::uint8_t>(change.value>>(24-i*8));
    }
    entries_.swap(plan.entries);++generation_;plan.authority=nullptr;
    return true;
}
Bytes CacheNamespace::checkpoint()const {
    CheckpointWriter out;out.u32(1);out.u64(generation_);out.u32(static_cast<std::uint32_t>(entries_.size()));
    for(const auto& [slot,entry]:entries_) {
        if(!entry.owner)throw Error("Cache entry has lost its immutable owner.");
        out.u32(static_cast<unsigned>(slot.first));out.u32(slot.second);out.u32(entry.pointer);out.u32(entry.legacy_id);
        out.text(entry.identity,64);out.text(entry.owner->fingerprint(),64);
    }
    return std::move(out).finish();
}
CacheNamespace CacheNamespace::stage_checkpoint(View bytes,
    const std::map<std::string,std::shared_ptr<CacheContent>>& owners)const {
    CheckpointReader in(bytes);if(in.u32()!=1)throw Error("Unsupported cache checkpoint schema.");
    CacheNamespace staged;staged.capacities_=capacities_;staged.generation_=in.u64();
    if(staged.generation_==std::numeric_limits<std::uint64_t>::max())throw Error("Cache checkpoint generation is exhausted.");
    const auto count=in.bounded(capacities_[0]+capacities_[1]+capacities_[2]);
    for(unsigned i=0;i<count;++i) {
        const auto kind=static_cast<CacheKind>(in.bounded(2));const auto slot=in.u32();
        Entry entry;entry.pointer=in.u32();entry.legacy_id=in.u32();entry.identity=in.text(64);const auto owner=in.text(64);
        const auto found=owners.find(owner);
        if(slot>=capacity(kind) || entry.pointer<0x80000000U || entry.pointer>0x807FFFFCU || (entry.pointer&3) ||
           !checkpoint_digest(entry.identity) || found==owners.end() || !found->second ||
           found->second->fingerprint()!=owner || found->second->identity(kind,entry.legacy_id)!=entry.identity ||
           !staged.entries_.emplace(Slot{kind,slot},Entry{entry.pointer,entry.legacy_id,entry.identity,found->second}).second)
            throw Error("Cache checkpoint refers to invalid or unadmitted content.");
    }
    in.end();return staged;
}
} // namespace dkr::mods
