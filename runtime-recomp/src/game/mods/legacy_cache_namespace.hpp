#pragma once
#include "legacy_asset_bus.hpp"

namespace dkr::mods {
enum class CacheKind { Texture, Sprite, Model };

// Cache identity includes referenced textures/animations, not just an object's
// unchanged model bytes. This catalog is owned by a prepared immutable mount.
// Methods are used on the serialized scene-loading thread, not per-frame/UI.
class CacheContent {
public:
    CacheContent(std::shared_ptr<const AssetBank>,std::shared_ptr<const AssetBus::Mount>);
    const std::string& identity(CacheKind kind,std::uint32_t legacy_id);
    const std::string& revision() const {return bank_->revision();}
    const std::string& fingerprint()const{return bank_->fingerprint();}
private:
    std::shared_ptr<const AssetBank> bank_;
    std::shared_ptr<const AssetBus::Mount> mount_;
    std::map<std::pair<CacheKind,std::uint32_t>,std::string> identities_;
};

struct GuestCacheTable {
    CacheKind kind;
    std::uint32_t count_address,pointer_address;
};

// Namespace old entries instead of deleting them or changing their refcounts.
// Retail lookup compares IDs; retail free finds the entry by its live pointer.
// Only ID words change. No pointer, free list, allocation or reference is freed.
// A production transition must serialize this with guest loaders AND commit
// the corresponding resident offset tables/asset route in the same transaction.
class CacheNamespace {
    struct Entry {
        std::uint32_t pointer=0,legacy_id=0;
        std::string identity;
        std::shared_ptr<CacheContent> owner;
    };
    using Slot=std::pair<CacheKind,unsigned>;
public:
    static constexpr std::uint32_t Hidden=0x40000000U;
    struct Plan {
    private:
        friend class CacheNamespace;
        friend class ResidentAssetState;
        const CacheNamespace* authority=nullptr;
        const std::uint8_t* guest_data=nullptr;
        std::size_t guest_size=0;
        std::uint64_t generation=0;
        struct Word {std::uint32_t address,expected,value;};
        std::vector<Word> words;
        std::map<Slot,Entry> entries;
    };
    Plan prepare(View guest_words,std::span<const GuestCacheTable> tables,
        std::shared_ptr<CacheContent> current,std::shared_ptr<CacheContent> next) const;
    // All compare/range checks precede every write. No allocation or guest
    // scheduler call occurs once the first ID is published. Stale plans fail
    // without changes, including a replay of an already successful plan.
    bool apply(std::span<std::uint8_t> guest_words,Plan&& plan);
    // Quiesced private-owner rollback. Parsing does not modify guest/native
    // state; owners are resolved from pre-admitted immutable banks.
    Bytes checkpoint()const;
    CacheNamespace stage_checkpoint(View bytes,
        const std::map<std::string,std::shared_ptr<CacheContent>>& owners)const;
private:
    std::map<Slot,Entry> entries_;
    std::uint64_t generation_=0;
};
} // namespace dkr::mods
