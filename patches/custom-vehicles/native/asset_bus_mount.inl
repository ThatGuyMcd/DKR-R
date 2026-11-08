namespace {
// Preserve AssetBus/Mount's admitted ABI. The shared_ptr control block owns
// this derived object, including pins to earlier identical section owners.
// All mounts in this composed bus are made here; no RTTI or raw downcast of
// caller-owned objects is involved. This metadata is prepared, never replayed.
struct SharedSectionMount final : AssetBus::Mount {
    std::array<std::string,50> hashes;
    std::vector<std::shared_ptr<const AssetBus::Mount>> pins;
};
}
std::shared_ptr<const AssetBus::Mount> AssetBus::mount(std::shared_ptr<const AssetDirectory> directory) {
    if(!directory)throw Error("Cannot mount an absent immutable directory.");
    auto mounted=std::make_shared<SharedSectionMount>();
    mounted->directory_=std::move(directory);mounted->pins.reserve(50);
    const auto fingerprint=mounted->directory_->fingerprint();
    // Exact canonical section bytes, not package names, exporter metadata or
    // record counts. Only one bounded section copy exists during hashing.
    for(unsigned s=0;s<50;++s)
        mounted->hashes[s]=sha256(mounted->directory_->read(s,0,mounted->directory_->section_size(s)));
    std::lock_guard lock(mutex_);
    auto banks=banks_;auto ranges=ranges_;
    auto previous=banks.find(fingerprint);
    if(previous!=banks.end()) {
        if(auto live=previous->second.owner.lock())return live;
        for(unsigned s=0;s<50;++s) {
            if(mounted->directory_->section_size(s)!=previous->second.sizes[s])
                throw Error("An existing bank fingerprint changed section sizes.");
            const auto base=previous->second.bases[s];
            auto& range=ranges.at(base);
            if(auto owner=banks.at(range.fingerprint).owner.lock())mounted->pins.push_back(std::move(owner));
            else range={fingerprint,s}; // Revive exactly the same immutable bytes.
        }
        mounted->bases_=previous->second.bases;previous->second.owner=mounted;
        banks_.swap(banks);ranges_.swap(ranges);return mounted;
    }
    if(banks.size()>=MaxBanks)throw Error("This game session has reached its mounted-content limit.");
    Bank bank;std::uint64_t next=next_,reused=0;unsigned shared=0;
    for(unsigned s=0;s<50;++s) {
        bank.sizes[s]=mounted->directory_->section_size(s);
        std::shared_ptr<const Mount> donor;std::uint32_t best=End;
        // Section zero stays unique and monotonic: allocation_address() is the
        // admitted world-clone ordering key. All other sections share ONLY
        // with the same section number, length and complete SHA-256 identity.
        if(s)for(const auto& [id,existing]:banks) {
            if(existing.sizes[s]!=bank.sizes[s]||existing.bases[s]>=best)continue;
            const auto live=existing.owner.lock();if(!live)continue;
            const auto typed=std::static_pointer_cast<const SharedSectionMount>(live);
            if(typed->hashes[s]!=mounted->hashes[s])continue;
            donor=live;best=existing.bases[s];
        }
        if(donor) {
            bank.bases[s]=best;mounted->pins.push_back(std::move(donor));
            reused+=bank.sizes[s];++shared;continue;
        }
        bank.bases[s]=static_cast<std::uint32_t>(next);
        next+=((std::uint64_t(bank.sizes[s])+4095U)&~std::uint64_t(4095U))+4096U;
        if(next>End)throw Error("This game session has exhausted its virtual asset address space.");
        ranges.emplace(bank.bases[s],Range{fingerprint,s});
    }
    mounted->bases_=bank.bases;bank.owner=mounted;
    banks.emplace(fingerprint,std::move(bank));
    // Publish only after every allocation, hash, owner pin and index succeeds.
    // Stale pointers never fall through to cartridge DMA and no different
    // bytes reuse a retired address. Read still pins its exact section owner.
    banks_.swap(banks);ranges_.swap(ranges);next_=static_cast<std::uint32_t>(next);
    std::fprintf(stderr,"[online-mods][asset-addresses] banks=%zu shared-sections=%u shared-bytes=%llu reserved-bytes=%u section-zero-unique=1\n",
        banks_.size(),shared,static_cast<unsigned long long>(reused),next_-Begin);
    return mounted;
}
