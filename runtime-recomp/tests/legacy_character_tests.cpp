#include "legacy_character_materialize.hpp"
#include "legacy_asset_directory.hpp"
#include "legacy_mod_dependencies.hpp"
#include "legacy_resident_assets.hpp"
#include "legacy_runtime_session.hpp"
#include "legacy_character_roster.hpp"
#include <algorithm>
#include <iostream>
#include <set>

using namespace dkr::mods;
namespace {
unsigned checks=0;
void require(bool ok,const char* why) {++checks;if(!ok)throw Error(why);}
template<class F> void rejects(F fn) {bool caught=false;try{fn();}catch(const Error&){caught=true;}require(caught,"Invalid additive asset input was accepted.");}
void unit() {
    rejects([]{AssetBank::augment(nullptr,{},{});});
    rejects([]{allocate_characters(nullptr,{});});
    rejects([]{CharacterNamespace{}.apply(nullptr);});
    for(unsigned n=0;n<16;++n)rejects([&]{validate_character_animation(Bytes(n),10);});
    Bytes face(40,0);face[0]=face[1]=2;face[2]=1;face[0x12]=1;
    auto same=face;same.resize(48,0x55);same[0x16]=0;same[0x17]=48;
    require(!character_portrait_changed(same,face),"Exporter padding was mistaken for a replacement face.");
    auto changed=face;changed[32]=1;
    require(character_portrait_changed(changed,face),"Replacement face pixels were not recognized.");
    rejects([&]{character_portrait_changed(Bytes(32),face);});
    auto assets=std::make_shared<CharacterNamespace>();
    assets->characters={{std::string(64,'a'),"First",8,906,{304,305,306}},
                        {std::string(64,'b'),"Second",8,907,{310,311,312}}};
    CharacterRoster roster(assets);
    roster.request(0,assets->characters[0].id);roster.request(1,assets->characters[1].id);
    const std::array<std::uint8_t,4> native{8,8,8,9};
    for(unsigned i=0;i<4;++i)require(!roster.header(i,1),"Uncommitted preview inherited a custom character.");
    roster.commit(native,3);
    for(unsigned vehicle=0;vehicle<3;++vehicle) {
        require(roster.header(0,1+10*vehicle)==304+vehicle,"P1 lost per-vehicle identity.");
        require(roster.header(1,1+10*vehicle)==310+vehicle,"Two donor-derived custom racers aliased.");
        require(!roster.header(2,1+10*vehicle),"Stock T.T. was overwritten.");
        require(!roster.header(4,1+10*vehicle),"AI inherited a human's custom identity.");
    }
    for(unsigned id=30;id<304;++id)require(!roster.header(0,id),"Boss or unrelated object header was replaced.");
    rejects([&]{roster.request(4,"");});rejects([&]{roster.request(0,std::string(64,'c'));});
    const auto active=roster.active(0);
    rejects([&]{roster.commit(std::array<std::uint8_t,2>{3,8},2);});
    require(roster.active(0)==active,"Rejected roster commit partially changed active characters.");
    roster.clear_active();require(!roster.header(0,1),"Native character-menu reset kept a stale racer identity.");
}
void corpus(int argc,char** argv) {
    const auto source=read_file(utf8_path(argv[1]),MaxImage);
    auto stock=AssetBank::stock(source);
    std::vector<PreparedCharacter> characters;
    for(int i=2;i<argc;++i)for(const auto& patch:read_patch_inputs(utf8_path(argv[i]))) {
        const auto target=decode_patch(source,patch.data);
        const auto analysis=analyze(source,target,sha256(patch.data));
        require(analysis.character_roots.size()==1,"Character fixture not identified.");
        auto character=prepare_character(source,target,sha256(patch.data),analysis.character_roots[0].base_character);
        require(!character.runtime_certified,"Preparation must not imply gameplay certification.");
        const AssetImage image(target,analysis.source_revision);
        for(const auto& [key,bytes]:character.records)
            require(std::ranges::equal(bytes,image.record(key.first,key.second)),"Prepared dependency differs from decoded character.");
        for(auto header:character.root.headers)require(character.records.contains({34,header}),"Vehicle root is missing.");
        require(character.records.contains({4,character.root.portrait}),"Portrait is missing.");
        std::cout<<character.root.name<<": "<<character.records.size()<<" dependencies, "
                 <<character.model_animations.size()<<" models, id="<<character.root.content_id<<'\n';
        characters.push_back(std::move(character));
    }
    const auto allocated=allocate_characters(stock,characters);
    std::set<std::string> identities;
    for(const auto& character:characters)identities.insert(character.root.content_id);
    require(allocated.characters.size()==identities.size(),"Equivalent character editions were not deduplicated.");
    const auto composite=allocated.apply(stock);
    require(composite->augmented() && composite->base_fingerprint()==stock->fingerprint(),"Composite bank lost original provenance.");
    for(unsigned section=0;section<50;++section)for(unsigned id=0;id<stock->record_count(section);++id)
        if(section!=30)require(std::ranges::equal(composite->record(section,id),stock->record(section,id)),"Character namespace changed an original asset.");
    for(const auto& [key,bytes]:allocated.additions) {
        require(key.second>=stock->record_count(key.first),"Character asset reused an original ID.");
        require(std::ranges::equal(composite->record(key.first,key.second),bytes),"Appended record cannot be resolved.");
        if(key.first==29)for(auto texture:inspect_model_textures(bytes))
            require(allocated.additions.contains({2,texture}),"Model still refers to a shared original texture ID.");
        if(key.first==29 || key.first==32) {
            const auto decoded=inflate_asset(bytes,4*MiB);
            if((decoded.size()+0x80-bytes.size())%8)
                throw Error("Native tail DMA is unaligned: section="+std::to_string(key.first)+
                    ", record="+std::to_string(key.second)+", decoded="+std::to_string(decoded.size())+
                    ", packed="+std::to_string(bytes.size()));
            require(bytes.size()<=decoded.size()+0x80,"Native compressed input exceeds its allocation.");
        }
        if(key.first==34)for(auto field:{0x10U,0x14U}) {
            const auto count=bytes[field==0x10?0x55:0x56];
            const auto offset=be32(bytes,field);
            for(unsigned n=0;n<count;++n) {
                const auto id=be32(bytes,offset+4*n);
                if(id!=0xffffffff)require(allocated.additions.contains({field==0x10?(bytes[0x53]==0?29U:12U):34U,id}),"Header still refers to an original model/attachment.");
            }
        }
    }
    const auto directory=AssetDirectory::build(composite);
    for(auto section:{2U,4U,12U,29U,32U,34U}) {
        const auto table_id=section==2 || section==4 || section==12?section+1:section-1;
        const auto table=directory->read(table_id,0,directory->section_size(table_id));
        const auto count=composite->record_count(section);
        require(be32(table,4*(count+1))==0xffffffff,"Expanded table has no correctly placed sentinel.");
        require(be32(table,4*count)==directory->section_size(section),"Expanded lookup does not span all records.");
    }
    require(directory->read(30,0,directory->section_size(30))==allocated.animation_ids,"Expanded animation ranges not routed.");
    AssetBus bus;
    rejects([&]{ResidentBank::prepare(stock,composite,bus);}); // Cannot grow already allocated original tables.
    const auto boot=ResidentBank::prepare(composite,composite,bus);
    require(bool(boot->route()),"Additive boot bank did not route its expanded tables.");
    ResidentAssetState resident(boot);
    require(bool(resident.acquire().route()),"Initial native allocation would read old-sized lookup tables.");
    RuntimeSession session(stock,std::make_shared<const CharacterNamespace>(allocated));
    require(bool(session.acquire().route()),"Session did not install character namespace before native initialization.");
    CacheContent cache(composite,boot->route());
    for(const auto& [key,bytes]:allocated.additions)if(key.first==29 || key.first==12)
        require(cache.identity(key.first==29?CacheKind::Model:CacheKind::Sprite,key.second).size()==64,"Appended cache dependency identity failed.");
    auto reversed=characters;std::reverse(reversed.begin(),reversed.end());
    require(allocate_characters(stock,reversed).apply(stock)->fingerprint()==composite->fingerprint(),"Import order changed character asset IDs.");
    // Course overlays may not capture custom racer resources: same additive
    // namespace can be installed over either original or scoped course bank.
    const auto course=AssetBank::derive(stock,std::string(64,'a'),{{{27,5},Bytes{1,2,3}}});
    const auto combined=allocated.apply(course);
    require(bool(ResidentBank::prepare(composite,combined,bus)->route()),"Scoped course lost boot-owned character tables.");
    require(combined->digest()==course->digest() && combined->record(27,5)[0]==1,"Character namespace discarded a custom course.");
    for(const auto& [key,bytes]:allocated.additions)
        require(std::ranges::equal(combined->record(key.first,key.second),composite->record(key.first,key.second)),"Course bank changed character namespace.");
    rejects([&]{allocated.apply(composite);});
    auto bad=allocated.additions;bad[{2,0}]={1};
    rejects([&]{AssetBank::augment(stock,bad,allocated.animation_ids);});
    bad=allocated.additions;bad.erase(bad.begin());
    rejects([&]{AssetBank::augment(stock,bad,allocated.animation_ids);});
    auto bad_ids=allocated.animation_ids;bad_ids[0]^=1;
    rejects([&]{AssetBank::augment(stock,allocated.additions,bad_ids);});
    bad_ids=allocated.animation_ids;bad_ids.pop_back();
    rejects([&]{AssetBank::augment(stock,allocated.additions,bad_ids);});
    std::cout<<"Combined namespace: "<<allocated.characters.size()<<" characters, "<<allocated.additions.size()
             <<" appended records, fingerprint="<<composite->fingerprint()<<'\n';
}
}
int main(int argc,char** argv) {
    try {unit();if(argc>2)corpus(argc,argv);else if(argc!=1)throw Error("Usage: tests [owned-v77 patch-or-zip ...]");
        std::cout<<checks<<" character preparation/ownership checks passed. Runtime qualification remains separate.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
