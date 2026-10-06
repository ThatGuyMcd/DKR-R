#pragma once
#include "legacy_cache_namespace.hpp"

namespace dkr::mods {
struct ResidentAssetLayout {
    // Six resident lookup pointers: 2D, 3D, sprites, models, animation IDs,
    // animation offsets. Then relocated sequence pointers and rounded lengths.
    std::array<std::uint32_t,8> table_pointers{};
    std::array<GuestCacheTable,3> caches{};
};
ResidentAssetLayout resident_asset_layout(const std::string& verified_revision);

class ResidentBank {
public:
    static std::shared_ptr<const ResidentBank> prepare(std::shared_ptr<const AssetBank> stock,
        std::shared_ptr<const AssetBank> candidate,AssetBus& bus);
    const std::array<Bytes,8>& tables() const {return tables_;}
    // Without a boot-owned additive namespace, restoration takes the untouched
    // cartridge path. With one, the same namespace stays installed all session.
    std::shared_ptr<const AssetBus::Mount> route() const {return stock_?nullptr:mount_;}
    const std::string& fingerprint() const {return bank_->fingerprint();}
private:
    friend class ResidentAssetState;
    bool stock_=false,boot_=false;
    std::shared_ptr<const AssetBank> bank_;
    std::shared_ptr<const AssetBus::Mount> mount_;
    std::shared_ptr<CacheContent> cache_;
    std::array<Bytes,8> tables_;
};

// A guest-thread transaction, NOT a replacement for the runtime scheduler.
// Integration must call prepare/commit at a serialized scene-loading boundary
// and acquire() around every asset API, including calls that yield on DMA.
// Renderer/audio resident pointers are retained, never forcibly freed here.
class ResidentAssetState {
    struct State {
        std::shared_ptr<const ResidentBank> bank;
        std::atomic_uint readers{0};
        explicit State(std::shared_ptr<const ResidentBank> b):bank(std::move(b)){}
    };
public:
    class Lease {
    public:
        Lease()=default;
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&&) noexcept;
        Lease& operator=(Lease&&) noexcept;
        ~Lease();
        std::shared_ptr<const AssetBus::Mount> route() const;
    private:
        friend class ResidentAssetState;
        std::shared_ptr<State> state_;
        explicit Lease(std::shared_ptr<State> s):state_(std::move(s)){++state_->readers;}
    };
    struct Plan {
    private:
        friend class ResidentAssetState;
        const ResidentAssetState* authority=nullptr;
        const std::uint8_t* guest_data=nullptr;
        std::size_t guest_size=0;
        std::uint64_t generation=0;
        CacheNamespace::Plan cache;
        struct BytesChange {std::uint32_t address;Bytes before,after;};
        std::vector<BytesChange> changes;
        std::shared_ptr<State> next;
    };
    explicit ResidentAssetState(std::shared_ptr<const ResidentBank> stock);
    Lease acquire();
    Plan prepare(View guest_words,std::shared_ptr<const ResidentBank> next);
    enum class Commit { Published, Busy, Stale };
    Commit commit(std::span<std::uint8_t> guest_words,Plan&&);
    // Cancellation invalidates prepared plans without changing guest state.
    void cancel();
    struct ReplayRestore {
    private:
        friend class ResidentAssetState;
        const ResidentAssetState* authority=nullptr;
        std::uint64_t expected=0,generation=0;
        CacheNamespace cache;
        std::shared_ptr<State> next;
    };
    Bytes checkpoint()const;
    ReplayRestore stage_checkpoint(View bytes,
        const std::map<std::string,std::shared_ptr<const ResidentBank>>& admitted)const;
    // Matching guest RAM must be restored in the same exclusive transaction.
    // No scene loading, RAM-table rewrites or freeing retained assets occurs.
    bool commit_checkpoint(ReplayRestore&&);
private:
    mutable std::mutex mutex_;
    std::shared_ptr<State> current_;
    ResidentAssetLayout layout_;
    CacheNamespace cache_;
    std::uint64_t generation_=0;
    // Never rewind transaction authority when restoring a replay generation.
    std::uint64_t mutation_=0;
};
} // namespace dkr::mods
