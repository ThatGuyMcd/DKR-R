#include "legacy_asset_bank.hpp"
#include <algorithm>
#include <limits>

namespace dkr::mods {
namespace {
bool digest_valid(const std::string& value) {
    return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c){
        return (c>='0' && c<='9') || (c>='a' && c<='f');
    });
}
}
std::shared_ptr<const AssetBank> AssetBank::stock(Bytes rom) {
    canonicalize_rom(rom);
    auto bank=std::shared_ptr<AssetBank>(new AssetBank);
    bank->revision_=verified_revision(rom);
    bank->fingerprint_=sha256(rom);
    bank->rom_=std::move(rom);
    const AssetImage image(bank->rom_,bank->revision_);
    bank->sections_=image.sections;
    for(unsigned section=0;section<50;++section) bank->records_[section]=image.records(section);
    return bank;
}
std::shared_ptr<const AssetBank> AssetBank::derive(std::shared_ptr<const AssetBank> base,
    std::string digest,Overrides overrides) {
    if(!base || base->stock_ || !digest_valid(digest))
        throw Error("Derived content requires a verified original bank and a full content digest.");
    std::size_t total=0;
    for(const auto& [key,bytes] : overrides) {
        // Explicit data sections only. Never mount lookup-table fragments,
        // executable regions, a retail name bank or stock progression data.
        switch(key.first) {
        case 2:case 4:case 12:case 21:case 23:case 27:case 29:case 32:case 39:break;
        default:throw Error("This section has no scoped asset-bank adapter.");
        }
        if(key.second>=32768 || bytes.empty() || bytes.size()>MaxImage || bytes.size()>MaxStaged-total)
            throw Error("Derived asset record is empty or exceeds the bank budget.");
        // Additive public slots are separate content identities, not writes
        // past an old table's end. A future ID allocator must explicitly own
        // expansion of all related tables before appended IDs are admitted.
        if(key.second>=base->record_count(key.first))
            throw Error("Unallocated legacy record ID cannot be mounted.");
        total+=bytes.size();
    }
    auto bank=std::shared_ptr<AssetBank>(new AssetBank);
    bank->revision_=base->revision_;
    bank->stock_=std::move(base);
    bank->digest_=std::move(digest);
    bank->overrides_=std::move(overrides);
    std::string descriptor="dkr-scoped-bank-v1:"+bank->stock_->fingerprint()+":"+bank->digest_;
    for(const auto& [key,bytes] : bank->overrides_)
        descriptor+=":"+std::to_string(key.first)+","+std::to_string(key.second)+","+sha256(bytes);
    bank->fingerprint_=sha256(View(reinterpret_cast<const std::uint8_t*>(descriptor.data()),descriptor.size()));
    return bank;
}
View AssetBank::record(unsigned section,unsigned id) const {
    if(section>=50) throw Error("Invalid bank asset section.");
    if(const auto override=overrides_.find({section,id});override!=overrides_.end()) return override->second;
    if(stock_) return stock_->record(section,id);
    if(id>=records_[section].size()) throw Error("The bank has no such asset record.");
    return records_[section][id];
}
std::shared_ptr<const AssetBank> AssetBank::augment(std::shared_ptr<const AssetBank> input,
    Overrides additions,Bytes animation_ids) {
    if(!input || input->augmented_ || additions.empty())throw Error("Invalid additive asset namespace.");
    auto base=input->stock_?input->stock_:input;
    auto bank=std::shared_ptr<AssetBank>(new AssetBank);
    bank->stock_=base;bank->revision_=input->revision_;bank->digest_=input->digest_;
    bank->overrides_=input->overrides_;bank->augmented_=true;
    for(unsigned s=0;s<50;++s)bank->counts_[s]=base->record_count(s);
    std::size_t total=0;
    for(auto& [key,bytes]:additions) {
        const auto [section,id]=key;
        if(section!=2 && section!=4 && section!=12 && section!=29 && section!=32 && section!=34)
            throw Error("Unowned additive asset section.");
        if(id!=bank->counts_[section] || id>=32767 || bytes.empty() || bytes.size()>MaxImage || bytes.size()>MaxStaged-total)
            throw Error("Additive IDs must be contiguous, bounded and strictly after original records.");
        total+=bytes.size();++bank->counts_[section];bank->overrides_.emplace(key,std::move(bytes));
    }
    const auto original_ids=base->stock_section(30);
    const auto original_models=base->record_count(29),models=bank->counts_[29];
    if(models==original_models || animation_ids.size()!=(models+1)*2 || original_ids.size()<(original_models+1)*2)
        throw Error("Additive animation table does not cover every model.");
    if(!std::equal(original_ids.begin(),original_ids.begin()+(original_models+1)*2,animation_ids.begin()))
        throw Error("Additive animation IDs changed an original model.");
    for(std::size_t i=original_models;i<models;++i)
        if(be16(animation_ids,i*2)>be16(animation_ids,i*2+2) || be16(animation_ids,i*2+2)>bank->counts_[32])
            throw Error("Additive animation range exceeds prepared records.");
    if(be16(animation_ids,models*2)!=bank->counts_[32])throw Error("Additive animations have no owning model.");
    bank->overrides_[{30,0}]=std::move(animation_ids);
    std::string identity="dkr-additive-assets-v1:"+input->fingerprint_;
    for(const auto& [key,bytes]:bank->overrides_)
        identity+=":"+std::to_string(key.first)+","+std::to_string(key.second)+","+sha256(bytes);
    bank->fingerprint_=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
    return bank;
}
std::shared_ptr<const AssetBank> AssetBank::append_textures(
    std::shared_ptr<const AssetBank> input,const std::vector<Bytes>& textures) {
    if(!input)throw Error("Shared artwork requires a verified asset bank.");
    if(textures.empty())return input;
    auto bank=std::shared_ptr<AssetBank>(new AssetBank);
    bank->stock_=input->stock_?input->stock_:input;
    bank->revision_=input->revision_;bank->digest_=input->digest_;
    bank->overrides_=input->overrides_;bank->augmented_=true;
    for(unsigned s=0;s<50;++s)bank->counts_[s]=input->record_count(s);
    std::size_t total=bank->owned_override_bytes();
    for(const auto& bytes:textures) {
        if(bank->counts_[2]>=32767 || bytes.empty() || bytes.size()>MaxImage || bytes.size()>MaxStaged-total)
            throw Error("Shared artwork exceeds the boot namespace budget.");
        total+=bytes.size();bank->overrides_.emplace(AssetKey{2,bank->counts_[2]++},bytes);
    }
    // The augmented-bank contract includes a raw model-animation range table,
    // even when no custom character models were appended.
    if(!input->augmented_) {
        const auto ids=input->stock_section(30);
        bank->overrides_[{30,0}]=Bytes(ids.begin(),ids.end());
    }
    std::string identity="dkr-shared-artwork-v1:"+input->fingerprint_;
    for(const auto& bytes:textures)identity+=":"+sha256(bytes);
    bank->fingerprint_=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
    return bank;
}
std::size_t AssetBank::record_count(unsigned section) const {
    if(section>=50) throw Error("Invalid bank asset section.");
    return augmented_?counts_[section]:stock_?stock_->record_count(section):records_[section].size();
}
std::shared_ptr<const AssetBank> AssetBank::append_courses(
    std::shared_ptr<const AssetBank> input,Overrides additions) {
    if(!input || input->courses_)throw Error("Track Lab namespace must be admitted exactly once before boot.");
    if(additions.empty())return input;
    auto bank=std::shared_ptr<AssetBank>(new AssetBank);
    bank->stock_=input->stock_?input->stock_:input;
    bank->revision_=input->revision_;bank->digest_=input->digest_;
    bank->overrides_=input->overrides_;bank->augmented_=bank->courses_=true;
    for(unsigned s=0;s<50;++s)bank->counts_[s]=input->record_count(s);
    std::size_t total=bank->owned_override_bytes();
    std::string identity="dkr-track-lab-namespace-v1:"+input->fingerprint_;
    for(auto& [key,bytes]:additions) {
        const auto [section,id]=key;
        if(section!=2 && section!=4 && section!=12 && section!=21 && section!=23 && section!=25 && section!=27)
            throw Error("Track Lab attempted to append an unowned asset section.");
        if(id!=bank->counts_[section] || id>=32767 || bytes.empty() || bytes.size()>MaxImage || bytes.size()>MaxStaged-total ||
           (section==23 && id>=128))
            throw Error("Track Lab IDs must be contiguous and inside the game's asset/menu limits.");
        total+=bytes.size();++bank->counts_[section];
        identity+=":"+std::to_string(section)+","+std::to_string(id)+","+sha256(bytes);
        bank->overrides_.emplace(key,std::move(bytes));
    }
    if(!input->augmented_) {
        const auto ids=input->stock_section(30);bank->overrides_[{30,0}]=Bytes(ids.begin(),ids.end());
    }
    bank->fingerprint_=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
    return bank;
}
View AssetBank::stock_section(unsigned section) const {
    if(section>=50) throw Error("Invalid bank asset section.");
    return stock_?stock_->stock_section(section):sections_[section];
}
std::uint32_t AssetBank::stock_section_rom_offset(unsigned section) const {
    if(section>=50) throw Error("Invalid bank asset section.");
    if(stock_) return stock_->stock_section_rom_offset(section);
    return static_cast<std::uint32_t>(sections_[section].data()-rom_.data());
}
bool AssetBank::overrides_section(unsigned section) const {
    if(section>=50) throw Error("Invalid bank asset section.");
    const auto first=overrides_.lower_bound({section,0});
    return first!=overrides_.end() && first->first.first==section;
}
SceneBankSlot::Lease::~Lease(){if(state_) --state_->borrowers;}
SceneBankSlot::Lease& SceneBankSlot::Lease::operator=(Lease&& other) noexcept {
    if(this!=&other) {
        if(state_) --state_->borrowers;
        state_=std::move(other.state_);
    }
    return *this;
}
View SceneBankSlot::Lease::record(unsigned section,unsigned id) const {
    if(!state_) throw Error("Asset access requires a live bank lease.");
    return state_->bank->record(section,id);
}
const BankBinding& SceneBankSlot::Lease::binding() const {
    if(!state_) throw Error("No bank binding is leased.");
    return state_->binding;
}
SceneBankSlot::SceneBankSlot(std::shared_ptr<const AssetBank> stock,unsigned carrier) {
    if(!stock || !stock->digest().empty()) throw Error("Scene banks must start from verified original content.");
    BankBinding binding{"",stock->fingerprint(),carrier,0};
    current_=std::make_shared<State>(std::move(binding),std::move(stock));
}
std::uint64_t SceneBankSlot::request(BankBinding binding) {
    std::lock_guard lock(mutex_);
    if(binding.generation<=highest_generation_ ||
        (!binding.content_digest.empty() && !digest_valid(binding.content_digest)) ||
        !digest_valid(binding.bank_fingerprint) ||
        ticket_==std::numeric_limits<std::uint64_t>::max())
        throw Error("Scene content request has an invalid identity or stale generation.");
    highest_generation_=binding.generation;
    requested_=std::move(binding);pending_.reset();return ++ticket_;
}
bool SceneBankSlot::prepared(std::uint64_t ticket,std::shared_ptr<const AssetBank> bank) {
    std::lock_guard lock(mutex_);
    if(ticket!=ticket_ || !requested_) return false;
    if(!bank || bank->digest()!=requested_->content_digest ||
        bank->fingerprint()!=requested_->bank_fingerprint || bank->revision()!=current_->bank->revision())
        throw Error("Prepared content does not match the requested scene binding.");
    pending_=std::make_shared<State>(*requested_,std::move(bank));return true;
}
SceneBankSlot::Publish SceneBankSlot::publish(std::uint64_t ticket) {
    std::lock_guard lock(mutex_);
    if(ticket!=ticket_ || !requested_) return Publish::Stale;
    if(!pending_) return Publish::NotPrepared;
    if(current_->borrowers.load()!=0) return Publish::WaitingForBorrowers;
    current_=std::move(pending_);requested_.reset();return Publish::Published;
}
void SceneBankSlot::cancel(std::uint64_t ticket) {
    std::lock_guard lock(mutex_);
    if(ticket==ticket_) {pending_.reset();requested_.reset();}
}
SceneBankSlot::Lease SceneBankSlot::acquire() {std::lock_guard lock(mutex_);return Lease(current_);}
BankBinding SceneBankSlot::binding() const {std::lock_guard lock(mutex_);return current_->binding;}
} // namespace dkr::mods
