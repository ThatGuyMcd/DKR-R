#include "legacy_resident_assets.hpp"
#include "legacy_checkpoint.hpp"
#include "legacy_audio_bank.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace dkr::mods {
namespace {
constexpr std::array<unsigned,6> Sections{5,3,13,28,30,31};
void put32(Bytes& bytes,std::size_t at,std::uint32_t value) {
    for(unsigned i=0;i<4;++i) bytes.at(at+i)=static_cast<std::uint8_t>(value>>(24-i*8));
}
std::size_t checked(View memory,std::uint32_t address,std::size_t length) {
    const auto physical=std::size_t(address&0x1fffffffU);
    if((address&0xe0000000U)!=0x80000000U || address%4 || memory.size()%4 ||
       physical>memory.size() || length>memory.size()-physical)
        throw Error("Resident asset table has an invalid guest range.");
    return physical;
}
Bytes read_guest(View memory,std::uint32_t address,std::size_t length) {
    const auto p=checked(memory,address,length);Bytes result(length);
    for(std::size_t i=0;i<length;++i) result[i]=memory[(p+i)^3];
    return result;
}
bool matches(View memory,std::uint32_t address,View expected) {
    const auto p=checked(memory,address,expected.size());
    for(std::size_t i=0;i<expected.size();++i) if(memory[(p+i)^3]!=expected[i]) return false;
    return true;
}
}
ResidentAssetLayout resident_asset_layout(const std::string& revision) {
    if(revision=="us.v77") return {{0x80126320,0x80126324,0x80126348,0x8011d620,0x8011d638,0x8011d63c,0x80115cf8,0x80115d0c},
        {{{CacheKind::Texture,0x80126330,0x80126328},{CacheKind::Sprite,0x80126358,0x8012634c},{CacheKind::Model,0x8011d62c,0x8011d624}}}};
    if(revision=="us.v80") return {{0x801268c0,0x801268c4,0x801268e8,0x8011dba0,0x8011dbb8,0x8011dbbc,0x80116278,0x8011628c},
        {{{CacheKind::Texture,0x801268d0,0x801268c8},{CacheKind::Sprite,0x801268f8,0x801268ec},{CacheKind::Model,0x8011dbac,0x8011dba4}}}};
    throw Error("Resident asset layout has no verified revision adapter.");
}
std::shared_ptr<const ResidentBank> ResidentBank::prepare(std::shared_ptr<const AssetBank> stock,
    std::shared_ptr<const AssetBank> candidate,AssetBus& bus) {
    if(!stock || !candidate || !stock->digest().empty() || stock->base_fingerprint()!=candidate->base_fingerprint() ||
       stock->augmented()!=candidate->augmented())
        throw Error("Resident candidate is not derived from this verified original ROM.");
    // Lookup expansion is legal ONLY when already part of the immutable boot
    // bank that native initialization will allocate. A scene cannot add/remove
    // character IDs or alter their header/animation ownership after that point.
    if(stock->augmented()) {
        for(unsigned section:{2U,4U,12U,29U,32U,34U}) {
            if(stock->record_count(section)!=candidate->record_count(section))
                throw Error("A scene changed the boot-owned character namespace size.");
            // Stock record counts are obtained from the original lookup bytes,
            // not the expanded candidate's claimed capacity.
            const auto table_id=section==2 || section==4 || section==12?section+1:section-1;
            const auto table=stock->stock_section(table_id);
            unsigned words=0;while(words*4+4<=table.size() && be32(table,words*4)!=0xffffffff)++words;
            if(words<2 || words*4+4>table.size())throw Error("Original character lookup lacks its sentinel.");
            for(unsigned id=words-1;id<stock->record_count(section);++id)
                if(!std::ranges::equal(stock->record(section,id),candidate->record(section,id)))
                    throw Error("A scene changed a live character dependency.");
        }
        if(!std::ranges::equal(stock->record(30,0),candidate->record(30,0)))
            throw Error("A scene changed live character animation ranges.");
    }
    if(stock->appended_courses()!=candidate->appended_courses())
        throw Error("A scene changed the boot-owned Track Lab namespace.");
    if(stock->appended_courses())for(unsigned section:{21U,23U,25U,27U}) {
        if(stock->record_count(section)!=candidate->record_count(section))
            throw Error("A scene changed the boot-owned course namespace size.");
        const auto table=stock->stock_section(section-1);
        unsigned words=0;while(words*4+4<=table.size() && be32(table,words*4)!=0xffffffff)++words;
        if(words<2 || words*4+4>table.size())throw Error("Original course lookup lacks its sentinel.");
        for(unsigned id=words-1;id<stock->record_count(section);++id)
            if(!std::ranges::equal(stock->record(section,id),candidate->record(section,id)))
                throw Error("A scene changed a live Track Lab dependency.");
    }
    auto result=std::shared_ptr<ResidentBank>(new ResidentBank);
    result->boot_=candidate->fingerprint()==stock->fingerprint();
    result->stock_=result->boot_ && !stock->augmented();result->bank_=std::move(candidate);
    const auto directory=AssetDirectory::build(result->bank_);
    const auto boot_directory=stock->augmented()?AssetDirectory::build(stock):nullptr;
    result->mount_=bus.mount(directory);result->cache_=std::make_shared<CacheContent>(result->bank_,result->mount_);
    for(unsigned i=0;i<Sections.size();++i) {
        const auto section=Sections[i];
        const auto boot_bytes=boot_directory?boot_directory->read(section,0,boot_directory->section_size(section)):Bytes{};
        const View original=boot_directory?View(boot_bytes):stock->stock_section(section);
        const auto size=directory->section_size(section);
        if(size>original.size()) throw Error("Resident lookup expansion exceeds the retained guest allocation.");
        // Retail allocations can include exporter padding after the sentinel.
        // Preserve that original tail, never overwrite beyond their capacity.
        result->tables_[i]=Bytes(original.begin(),original.end());
        const auto replacement=directory->read(section,0,size);
        std::copy(replacement.begin(),replacement.end(),result->tables_[i].begin());
    }
    const auto count=stock->record_count(39);
    if(count!=result->bank_->record_count(39) || count<=5) throw Error("Audio bank layout changed.");
    for(unsigned id=0;id<count;++id) if(id!=5) {
        const auto a=stock->record(39,id),b=result->bank_->record(39,id);
        if(!std::equal(a.begin(),a.end(),b.begin(),b.end()))
            throw Error("Instrument/sample banks cannot change underneath retained audio voices.");
    }
    const auto original_music=inspect_sequence_directory(stock->record(39,5));
    const auto song_bytes=result->bank_->record(39,5);
    const auto music=inspect_sequence_directory(song_bytes);
    if(music.records.size()!=original_music.records.size() || music.maximum_loaded_length>original_music.maximum_loaded_length)
        throw Error("Replacement music exceeds the retained sequence count/buffer capacity.");
    const auto audio_table=directory->read(38,0,directory->section_size(38));
    const auto sequence_offset=be32(audio_table,4*4); // audio record 5; zero is implicit
    const auto audio_base=result->stock_?stock->stock_section_rom_offset(39):result->mount_->address(39);
    result->tables_[6]=Bytes(song_bytes.begin(),song_bytes.begin()+4+music.records.size()*8);
    result->tables_[7].resize(music.records.size()*4);
    for(unsigned id=0;id<music.records.size();++id) {
        const auto& record=music.records[id];
        if(std::uint64_t(audio_base)+sequence_offset+record.offset>std::numeric_limits<std::uint32_t>::max())
            throw Error("Relocated music address overflowed.");
        put32(result->tables_[6],4+id*8,audio_base+sequence_offset+record.offset);
        put32(result->tables_[7],id*4,record.loaded_length);
    }
    return result;
}
ResidentAssetState::Lease::Lease(Lease&& other) noexcept:state_(std::move(other.state_)){}
ResidentAssetState::Lease& ResidentAssetState::Lease::operator=(Lease&& other) noexcept {
    if(this!=&other) {if(state_) --state_->readers;state_=std::move(other.state_);}return *this;
}
ResidentAssetState::Lease::~Lease(){if(state_) --state_->readers;}
std::shared_ptr<const AssetBus::Mount> ResidentAssetState::Lease::route() const {
    if(!state_) throw Error("Asset API requires an active resident lease.");return state_->bank->route();
}
std::shared_ptr<const ResidentBank> ResidentBank::instantiate(const ResidentBank& source,AssetBus& bus) {
    auto out=std::shared_ptr<ResidentBank>(new ResidentBank);
    out->stock_=source.stock_;out->boot_=source.boot_;out->bank_=source.bank_;
    out->mount_=bus.mount(source.mount_->directory());
    // Relocated audio tables are valid only with the exact deterministic
    // mount order used when qualifying the immutable template.
    for(unsigned section=0;section<50;++section)
        if(out->mount_->address(section)!=source.mount_->address(section))throw Error("Prepared world asset address order changed.");
    out->tables_=source.tables_;
    out->cache_=std::make_shared<CacheContent>(out->bank_,out->mount_);
    return out;
}
ResidentAssetState::ResidentAssetState(std::shared_ptr<const ResidentBank> stock) {
    if(!stock || !stock->boot_) throw Error("Resident routing must start from its verified boot content.");
    cache_=CacheNamespace(*stock->bank_);
    layout_=resident_asset_layout(stock->bank_->revision());current_=std::make_shared<State>(std::move(stock));
}
ResidentAssetState::Lease ResidentAssetState::acquire() {std::lock_guard lock(mutex_);return Lease(current_);}
ResidentAssetState::Plan ResidentAssetState::prepare(View memory,std::shared_ptr<const ResidentBank> next) {
    std::lock_guard lock(mutex_);
    if(!next || current_->bank->bank_->base_fingerprint()!=next->bank_->base_fingerprint() ||
       generation_==std::numeric_limits<std::uint64_t>::max() || mutation_==std::numeric_limits<std::uint64_t>::max()) throw Error("Invalid resident transition.");
    Plan plan;plan.authority=this;plan.generation=mutation_;plan.next=std::make_shared<State>(std::move(next));
    plan.guest_data=memory.data();plan.guest_size=memory.size();
    plan.cache=cache_.prepare(memory,layout_.caches,current_->bank->cache_,plan.next->bank->cache_);
    std::vector<std::pair<std::uint32_t,std::size_t>> ranges;
    auto reserve=[&](std::uint32_t address,std::size_t length) {
        checked(memory,address,length);
        for(const auto [start,size]:ranges) if(std::uint64_t(address)<std::uint64_t(start)+size && std::uint64_t(start)<std::uint64_t(address)+length)
            throw Error("Resident tables/cache descriptors overlap.");
        ranges.emplace_back(address,length);
    };
    for(const auto& word:plan.cache.words) reserve(word.address,4);
    for(unsigned i=0;i<layout_.table_pointers.size();++i) {
        const auto pointer=layout_.table_pointers[i];reserve(pointer,4);
        auto descriptor=read_guest(memory,pointer,4);const auto address=be32(descriptor,0);
        const auto& before=current_->bank->tables_[i];const auto& after=plan.next->bank->tables_[i];
        if(before.size()!=after.size()) throw Error("Resident table allocation size changed.");
        reserve(address,before.size());
        if(!matches(memory,address,before)) throw Error("Resident data differs from its declared current bank.");
        plan.changes.push_back({pointer,descriptor,descriptor});
        plan.changes.push_back({address,before,after});
    }
    return plan;
}
ResidentAssetState::Commit ResidentAssetState::commit(std::span<std::uint8_t> memory,Plan&& plan) {
    std::lock_guard lock(mutex_);
    if(plan.authority!=this || plan.generation!=mutation_ || mutation_==std::numeric_limits<std::uint64_t>::max() ||
       memory.data()!=plan.guest_data || memory.size()!=plan.guest_size) return Commit::Stale;
    if(current_->readers.load()!=0) return Commit::Busy;
    for(const auto& change:plan.changes) if(!matches(memory,change.address,change.before)) return Commit::Stale;
    if(!cache_.apply(memory,std::move(plan.cache))) return Commit::Stale;
    // All bounds, expected bytes and cache entries were checked before writing.
    // No allocations, hashing, guest calls or scheduler yields from here on.
    for(const auto& change:plan.changes) {
        const auto p=change.address&0x1fffffffU;
        for(std::size_t i=0;i<change.after.size();++i) memory[(p+i)^3]=change.after[i];
    }
    current_.swap(plan.next);++generation_;++mutation_;plan.authority=nullptr;
    return Commit::Published;
}
void ResidentAssetState::cancel() {
    std::lock_guard lock(mutex_);
    if(generation_==std::numeric_limits<std::uint64_t>::max() || mutation_==std::numeric_limits<std::uint64_t>::max()) throw Error("Resident generation exhausted.");++generation_;++mutation_;
}
Bytes ResidentAssetState::checkpoint()const {
    std::lock_guard lock(mutex_);
    if(current_->readers.load()!=0)throw Error("Cannot checkpoint an outstanding custom asset read.");
    CheckpointWriter out;out.u32(1);out.text(current_->bank->fingerprint(),64);out.u64(generation_);out.block(cache_.checkpoint());
    return std::move(out).finish();
}
ResidentAssetState::ReplayRestore ResidentAssetState::stage_checkpoint(View bytes,
    const std::map<std::string,std::shared_ptr<const ResidentBank>>& admitted)const {
    std::lock_guard lock(mutex_);
    if(current_->readers.load()!=0)throw Error("Cannot restore an outstanding custom asset read.");
    CheckpointReader in(bytes);if(in.u32()!=1)throw Error("Unsupported resident checkpoint schema.");
    const auto bank=in.text(64);const auto found=admitted.find(bank);
    if(found==admitted.end() || !found->second || found->second->bank_->revision()!=current_->bank->bank_->revision() ||
       found->second->bank_->base_fingerprint()!=current_->bank->bank_->base_fingerprint())
        throw Error("Resident checkpoint refers to unadmitted content.");
    ReplayRestore out;out.authority=this;out.expected=mutation_;out.generation=in.u64();
    if(out.generation==std::numeric_limits<std::uint64_t>::max())throw Error("Resident checkpoint generation is exhausted.");
    std::map<std::string,std::shared_ptr<CacheContent>> owners;
    for(const auto& [id,value]:admitted) {
        if(!value || !value->cache_ || value->fingerprint()!=id)throw Error("Invalid immutable resident registry.");
        owners.emplace(id,value->cache_);
    }
    out.cache=cache_.stage_checkpoint(in.block(),owners);in.end();out.next=std::make_shared<State>(found->second);return out;
}
bool ResidentAssetState::commit_checkpoint(ReplayRestore&& staged) {
    std::lock_guard lock(mutex_);
    if(staged.authority!=this || staged.expected!=mutation_ || mutation_==std::numeric_limits<std::uint64_t>::max() || !staged.next || current_->readers.load()!=0)return false;
    current_.swap(staged.next);cache_=std::move(staged.cache);generation_=staged.generation;++mutation_;staged.authority=nullptr;return true;
}
} // namespace dkr::mods
