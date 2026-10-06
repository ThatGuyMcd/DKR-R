// Headless production resident/cache transactions. Private owned ROMs supply
// verified banks; synthetic cache pointers are never rendered or decoded.
#include "legacy_resident_assets.hpp"
#include "legacy_asset_capacity.hpp"
#include "legacy_checkpoint.hpp"
#include <iostream>

using namespace dkr::mods;
namespace {
unsigned checks=0;
void check(bool value,const char* message){++checks;if(!value)throw Error(message);}
template<class F>void rejects(F fn,const char* message) {
    bool caught=false;try{fn();}catch(const Error&){caught=true;}check(caught,message);
}
void put(Bytes& memory,std::uint32_t address,std::uint32_t value) {
    const auto at=address&0x1fffffffU;
    for(unsigned i=0;i<4;++i)memory.at((at+i)^3)=static_cast<std::uint8_t>(value>>(24-i*8));
}
std::uint32_t get(const Bytes& memory,std::uint32_t address) {
    const auto at=address&0x1fffffffU;std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)value=value<<8|memory.at((at+i)^3);return value;
}
struct Fixture {
    Bytes memory=Bytes(8*MiB);
    std::shared_ptr<const AssetBank> boot;
    AssetBus bus;
    std::shared_ptr<const ResidentBank> resident;
    ResidentAssetLayout layout;
    std::array<std::uint32_t,4> limits;
    explicit Fixture(std::shared_ptr<const AssetBank> bank):boot(std::move(bank)),
        resident(ResidentBank::prepare(boot,boot,bus)),layout(resident_asset_layout(boot->revision())),
        limits(legacy_asset_cache_capacities(boot->augmented(),boot->record_count(2),
            boot->record_count(4),boot->record_count(12),boot->record_count(29))) {
        for(unsigned i=0;i<resident->tables().size();++i) {
            const auto address=0x80200000U+i*0x10000U;
            const auto& bytes=resident->tables()[i];check(bytes.size()<0x10000,"Fixture table overlaps its neighbour");
            put(memory,layout.table_pointers[i],address);
            for(unsigned j=0;j<bytes.size();++j)memory[((address&0x1fffffffU)+j)^3]=bytes[j];
        }
        counts({0,0,0});
    }
    static std::uint32_t base(unsigned kind){return 0x80400000U+kind*0x10000U;}
    void counts(std::array<unsigned,3> values) {
        for(unsigned kind=0;kind<3;++kind) {
            check(values[kind]*8<0x10000,"Fixture cache overlaps its neighbour");
            put(memory,layout.caches[kind].count_address,values[kind]);
            put(memory,layout.caches[kind].pointer_address,base(kind));
            for(unsigned slot=0;slot<values[kind];++slot) {
                put(memory,base(kind)+slot*8,0xffffffff);put(memory,base(kind)+slot*8+4,0xffffffff);
            }
        }
    }
    void live(unsigned kind,unsigned slot,unsigned id=0) {
        put(memory,base(kind)+slot*8,id);put(memory,base(kind)+slot*8+4,0x80500000U+kind*0x10000U+slot*8);
        put(memory,0x80500000U+kind*0x10000U+slot*8,0x10203040U+slot);
    }
    void commit(ResidentAssetState& state,std::shared_ptr<const ResidentBank> next) {
        auto plan=state.prepare(memory,std::move(next));
        check(state.commit(memory,std::move(plan))==ResidentAssetState::Commit::Published,"Resident transaction did not publish");
    }
};
void run(const std::filesystem::path& rom) {
    const auto stock=AssetBank::stock(read_file(rom,MaxImage));
    const auto record=stock->record(2,0);
    const auto originals=stock->record_count(2)+stock->record_count(4);
    const std::vector<Bytes> additions(std::max<std::size_t>(1,1100>originals?1100-originals:1),Bytes(record.begin(),record.end()));
    const auto boot=AssetBank::append_textures(stock,additions);
    Fixture f(boot);ResidentAssetState state(f.resident);
    std::cerr<<stock->revision()<<": verified expanded boot fixture ready\n";
    check(f.limits[0]>900 && f.limits[1]>100 && f.limits[2]>70,"Verified fixture does not exercise enlarged allocations");

    // Exact failure class from the user's log: correctly allocated high-water
    // counts exceed vanilla limits, even with few live assets and many holes.
    f.counts({701,101,71});for(unsigned k=0;k<3;++k)f.live(k,k==0?700:k==1?100:70);
    const auto before=f.memory;f.commit(state,f.resident);
    check(f.memory==before,"A same-bank transition changed cache pointers/refcounts");
    const auto saved=state.checkpoint();
    const std::map<std::string,std::shared_ptr<const ResidentBank>> owners{{f.resident->fingerprint(),f.resident}};
    check(state.commit_checkpoint(state.stage_checkpoint(saved,owners)),"Expanded resident rollback failed");
    f.commit(state,f.resident);
    std::cerr<<stock->revision()<<": expanded transition and restore passed\n";

    // Last physically allocated slot of every enlarged table, and no further.
    f.counts({f.limits[0],f.limits[1],f.limits[2]});
    for(unsigned k=0;k<3;++k)f.live(k,f.limits[k]-1);
    f.commit(state,f.resident);
    check(state.commit_checkpoint(state.stage_checkpoint(state.checkpoint(),owners)),"Highest allocated cache slots did not restore");
    for(unsigned k=0;k<3;++k) {
        put(f.memory,f.layout.caches[k].count_address,f.limits[k]+1);
        const auto unchanged=f.memory;
        rejects([&]{state.prepare(f.memory,f.resident);},"A count past the actual allocation was accepted");
        check(f.memory==unchanged,"Rejected cache count mutated guest RAM");
        put(f.memory,f.layout.caches[k].count_address,f.limits[k]);
    }

    // More than the old aggregate 870-entry restore bound, within the existing
    std::cerr<<stock->revision()<<": allocation edge checks passed\n";
    // 256 KiB sidecar budget. The safety budget itself is deliberately retained.
    f.counts({900,1,1});for(unsigned slot=0;slot<900;++slot)f.live(0,slot);f.live(1,0);f.live(2,0);
    f.commit(state,f.resident);
    const auto dense=state.checkpoint();check(dense.size()<CheckpointWriter::Budget,"Expanded checkpoint exceeds its fixed budget");
    check(state.commit_checkpoint(state.stage_checkpoint(dense,owners)),"Expanded aggregate cache checkpoint was rejected");
    f.commit(state,f.resident);
    auto stale=state.stage_checkpoint(dense,owners);state.cancel();
    check(!state.commit_checkpoint(std::move(stale)),"Stale expanded checkpoint authority was accepted");
    for(const auto length:{0U,1U,15U,static_cast<unsigned>(dense.size()-1)})
        rejects([&]{state.stage_checkpoint(View(dense).first(length),owners);},"Truncated checkpoint was accepted");
    std::cerr<<stock->revision()<<": dense rollback, stale and truncated checks passed\n";

    // A different scene changes one texture, hiding the live entries without
    // deleting them. Restore must retain allocation authority and their owner.
    const auto original2=stock->record(4,0);Bytes edited(original2.begin(),original2.end());edited.back()^=1;
    const auto variant=AssetBank::append_textures(AssetBank::derive(stock,std::string(64,'a'),{{{4,0},edited}}),additions);
    const auto next=ResidentBank::prepare(boot,variant,f.bus);
    const std::map<std::string,std::shared_ptr<const ResidentBank>> both{{f.resident->fingerprint(),f.resident},{next->fingerprint(),next}};
    const auto boot_memory=f.memory;f.commit(state,next);
    check(get(f.memory,Fixture::base(0)+899*8)==(CacheNamespace::Hidden|899),"Changed dependency did not namespace a high cache slot");
    check(get(f.memory,Fixture::base(0)+899*8+4)==get(boot_memory,Fixture::base(0)+899*8+4),"Transition freed a retained asset pointer");
    check(state.commit_checkpoint(state.stage_checkpoint(state.checkpoint(),both)),"Hidden expanded entries did not restore");
    f.commit(state,f.resident);check(f.memory==boot_memory,"Returning to the boot bank changed retained assets");
    const auto oversized=AssetBank::append_textures(stock,std::vector<Bytes>(additions.size()+1,Bytes(record.begin(),record.end())));
    rejects([&]{ResidentBank::prepare(boot,oversized,f.bus);},"A scene enlarged its immutable boot namespace");
    std::cerr<<stock->revision()<<": cross-scene ownership checks passed\n";

    // Vanilla remains vanilla, regardless of a larger bank elsewhere.
    Fixture vanilla(stock);ResidentAssetState vanilla_state(vanilla.resident);
    check(vanilla.limits==std::array<std::uint32_t,4>{700,100,70,100},"Vanilla allocations changed");
    for(unsigned k=0;k<3;++k) {
        vanilla.counts({0,0,0});put(vanilla.memory,vanilla.layout.caches[k].count_address,vanilla.limits[k]+1);
        rejects([&]{vanilla_state.prepare(vanilla.memory,vanilla.resident);},"An enlarged bank bypassed vanilla allocation bounds");
    }
    auto cache_owner=std::make_shared<CacheContent>(boot,f.bus.mount(AssetDirectory::build(boot)));
    const std::map<std::string,std::shared_ptr<CacheContent>> cache_owners{{boot->fingerprint(),cache_owner}};
    CheckpointWriter forged;forged.u32(1);forged.u64(0);forged.u32(1);
    forged.u32(0);forged.u32(700);forged.u32(0x80500000);forged.u32(0);
    forged.text(cache_owner->identity(CacheKind::Texture,0),64);forged.text(boot->fingerprint(),64);
    const auto expanded_slot=std::move(forged).finish();
    rejects([&]{CacheNamespace{}.stage_checkpoint(expanded_slot,cache_owners);},"Untrusted checkpoint increased vanilla allocation authority");
    rejects([&]{CheckpointReader reader(Bytes(CheckpointWriter::Budget+1));},"Checkpoint byte budget was removed");
    std::cout<<stock->revision()<<": enlarged cache transitions, rollback and bounds passed\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc<2)throw Error("Supply one or more private owned ROMs; no game window is opened.");
        for(int i=1;i<argc;++i)run(argv[i]);
        std::cout<<checks<<" cache capacity checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
