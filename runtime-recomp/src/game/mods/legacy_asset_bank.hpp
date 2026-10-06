#pragma once
#include "legacy_mod_format.hpp"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace dkr::mods {
using AssetKey=std::pair<unsigned,unsigned>;

// Immutable record ownership. This is deliberately independent of the game
// adapter: constructing a bank does not patch ROM/RDRAM or change caches.
class AssetBank {
public:
    using Overrides=std::map<AssetKey,Bytes>;
    static std::shared_ptr<const AssetBank> stock(Bytes verified_rom);
    static std::shared_ptr<const AssetBank> derive(std::shared_ptr<const AssetBank> stock,
        std::string content_digest,Overrides overrides);
    // Append-only namespaces, admitted before the guest allocates its tables.
    // Existing record bytes/IDs stay unchanged. Character-specific model and
    // texture IDs never collide with a stock racer or a different character.
    static std::shared_ptr<const AssetBank> augment(std::shared_ptr<const AssetBank> bank,
        Overrides additions,Bytes animation_ids);
    // Shared dkrmap artwork follows the character namespace at boot and stays
    // at those same IDs in every legacy scene, including its cache identities.
    static std::shared_ptr<const AssetBank> append_textures(
        std::shared_ptr<const AssetBank> bank,const std::vector<Bytes>& textures);
    // Launch-only Track Lab namespace. Records must be contiguous after each
    // current table; no existing record, executable or global data is replaced.
    static std::shared_ptr<const AssetBank> append_courses(
        std::shared_ptr<const AssetBank> bank,Overrides additions);
    bool appended_courses()const{return courses_;}
    bool augmented() const {return augmented_;}
    std::size_t owned_override_bytes() const {
        std::size_t total=0;for(const auto& [key,bytes]:overrides_)total+=bytes.size();return total;
    }
    View record(unsigned section,unsigned id) const;
    std::size_t record_count(unsigned section) const;
    View stock_section(unsigned section) const;
    std::uint32_t stock_section_rom_offset(unsigned section) const;
    const std::string& base_fingerprint() const {return stock_?stock_->fingerprint():fingerprint_;}
    bool overrides_section(unsigned section) const;
    const std::string& digest() const {return digest_;}
    const std::string& fingerprint() const {return fingerprint_;}
    const std::string& revision() const {return revision_;}
private:
    AssetBank()=default;
    std::string digest_,revision_,fingerprint_;
    Bytes rom_;
    std::shared_ptr<const AssetBank> stock_;
    std::array<std::vector<View>,50> records_;
    std::array<View,50> sections_;
    Overrides overrides_;
    bool augmented_=false;
    bool courses_=false;
    std::array<std::size_t,50> counts_{};
};

struct BankBinding {
    std::string content_digest;
    std::string bank_fingerprint;
    unsigned carrier=0;
    std::uint64_t generation=0;
    bool operator==(const BankBinding&) const=default;
};

// Phase-two ownership prototype. Runtime integration must acquire a lease for
// every renderer/audio borrower and perform the actual game cache retirement
// BEFORE publish. These tests alone do not prove that integration exists.
class SceneBankSlot {
    struct State {
        BankBinding binding;
        std::shared_ptr<const AssetBank> bank;
        std::atomic_uint borrowers{0};
        State(BankBinding b,std::shared_ptr<const AssetBank> p):binding(std::move(b)),bank(std::move(p)){}
    };
public:
    class Lease {
    public:
        Lease()=default;
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other) noexcept:state_(std::move(other.state_)){}
        Lease& operator=(Lease&& other) noexcept;
        ~Lease();
        explicit operator bool() const {return bool(state_);}
        View record(unsigned section,unsigned id) const;
        const BankBinding& binding() const;
    private:
        friend class SceneBankSlot;
        explicit Lease(std::shared_ptr<State> state):state_(std::move(state)){++state_->borrowers;}
        std::shared_ptr<State> state_;
    };
    explicit SceneBankSlot(std::shared_ptr<const AssetBank> stock,unsigned carrier=0);
    // A newer request invalidates an earlier asynchronous completion. The
    // generation is supplied by the transition authority, not a pointer hash.
    std::uint64_t request(BankBinding binding);
    bool prepared(std::uint64_t ticket,std::shared_ptr<const AssetBank> bank);
    enum class Publish { Published, WaitingForBorrowers, NotPrepared, Stale };
    Publish publish(std::uint64_t ticket);
    void cancel(std::uint64_t ticket);
    Lease acquire();
    BankBinding binding() const;
private:
    mutable std::mutex mutex_;
    std::shared_ptr<State> current_,pending_;
    std::optional<BankBinding> requested_;
    std::uint64_t ticket_=0;
    std::uint64_t highest_generation_=0;
};
} // namespace dkr::mods
