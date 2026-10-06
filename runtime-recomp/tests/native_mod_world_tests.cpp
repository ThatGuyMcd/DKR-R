#include "runtime_legacy_mods.hpp"
#include "mods/legacy_track_catalog.hpp"
#include "mods/online_course_policy.hpp"
#include "runtime_netplay.hpp"
#include "ultramodern/ultramodern.hpp"
#include <iostream>
#include <algorithm>

namespace {
unsigned checks=0;
void check(bool value){if(!value)throw dkr::mods::Error("Native world check "+std::to_string(checks));++checks;}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}check(rejected);}
const dkr::mods::AssetBus* asset_bus=nullptr;
void table_allocate(std::uint8_t*,recomp_context* ctx){ctx->r2=static_cast<std::int32_t>(0x80600000U);}
void table_release(std::uint8_t*,recomp_context*){}
void table_copy(std::uint8_t* ram,recomp_context* ctx){
    const auto read=asset_bus->resolve(static_cast<std::uint32_t>(ctx->r4),static_cast<std::uint32_t>(ctx->r6));
    if(!read)throw dkr::mods::Error("Headless table copy escaped its private asset bus.");
    read->copy_to_guest({ram,8U*dkr::mods::MiB},static_cast<std::uint32_t>(ctx->r5));
}
}
int main(int argc,char** argv) {
    try {
        using namespace dkr::mods;
        if(argc<3)throw Error("Pass a read-only original ROM and read-only prepared legacy library.");
        const auto stock=AssetBank::stock(read_file(utf8_path(argv[1]),MaxImage));
        auto characters=TrackCatalog::load_enabled_characters(utf8_path(argv[2]),stock);
        // Admission/materialization regression beyond the former limit. The
        // synthetic editions vary sample bytes, retain valid native bank
        // structure, and are never installed or played in a user's library.
        std::vector<PreparedCharacter> editions;
        for(unsigned i=0;i<32;++i) {
            auto edition=characters.at(0);edition.audio.samples.at(0)^=std::uint8_t(i+1);
            edition.root.content_id=validate_prepared_character(edition,*stock);
            editions.push_back(std::move(edition));
        }
        auto expanded=std::make_shared<const CharacterNamespace>(allocate_characters(stock,std::move(editions)));
        check(expanded->characters.size()==32);
        RuntimeSession thirty_two(stock,expanded);
        for(unsigned i=1;i<32;++i)check(thirty_two.character_sample_address(i)!=thirty_two.character_sample_address(i-1));
        auto names=std::make_shared<const CharacterNamespace>(allocate_characters(stock,std::move(characters)));
        auto a=std::make_shared<RuntimeSession>(stock,names),b=std::make_shared<RuntimeSession>(stock,names);
        // Thousands of logical identities must not consume one full-ROM
        // virtual mount each. Identical verified sample payloads share a mount
        // and retain the same addresses even at the last representable index.
        auto wide=std::make_shared<CharacterNamespace>(*names);
        const auto donor=wide->characters.at(0);wide->characters.clear();
        for(unsigned i=0;i<MaxEnabledCharacters;++i) {
            auto entry=donor;const auto label=std::to_string(i);
            entry.id=sha256(View(reinterpret_cast<const std::uint8_t*>(label.data()),label.size()));
            wide->characters.push_back(std::move(entry));
        }
        RuntimeSession many(stock,wide);
        check(many.character_sample_address(0)==many.character_sample_address(MaxEnabledCharacters-1));
        check(many.character_race_sample_address(0)==many.character_race_sample_address(MaxEnabledCharacters-1));
        check(many.bus().resolve(many.character_sample_address(MaxEnabledCharacters-1),donor.audio.samples.size())->bytes()==donor.audio.samples);
        dkr::runtime::GamePayload calls{};
        dkr::runtime::legacy::ModWorld host(a,calls,nullptr,nullptr,false),client(b,calls,nullptr,nullptr,false);
        const auto baseline=host.checkpoint();check(baseline==client.checkpoint());
        for(std::size_t length=0;length<baseline.size();++length)rejects([&]{host.stage_checkpoint(View(baseline).first(length));});
        auto extra=baseline;extra.push_back(0);rejects([&]{host.stage_checkpoint(extra);});
        check(host.checkpoint()==baseline);
        auto first=host.stage_checkpoint(baseline),stale=host.stage_checkpoint(baseline),foreign=host.stage_checkpoint(baseline);
        check(!client.commit_checkpoint(std::move(foreign)));
        check(host.commit_checkpoint(std::move(first)) && host.checkpoint()==baseline);
        check(!host.commit_checkpoint(std::move(stale)));
        auto pending=host.stage_checkpoint(baseline);
        std::vector<std::uint8_t> ram(8U*MiB);recomp_context ctx{};std::uint64_t result=0;
        const auto put=[&](std::uint32_t address,std::uint32_t word){for(unsigned i=0;i<4;++i)ram[((address&0x7fffff)+i)^3]=std::uint8_t(word>>(24-i*8));};
        const auto word=[&](std::uint32_t address){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value=(value<<8)|ram[((address&0x7fffff)+i)^3];return value;};
        check(host.dispatch("unknown",ram.data(),&ctx,nullptr,0,nullptr,0,result)==0);
        check(host.commit_checkpoint(std::move(pending)));
        ctx.r15=0x80400000U;
        check(host.dispatch("dkr_legacy_heap_capacity",ram.data(),&ctx,nullptr,0,nullptr,0,result)==1 && std::uint32_t(ctx.r15)==0x80800000U);
        const std::uint64_t arg=0;
        for(std::uint64_t kind=0;kind<4;++kind){
            std::uint64_t client_capacity=0;
            check(host.dispatch("dkr_legacy_asset_cache_capacity",ram.data(),&ctx,&kind,1,nullptr,0,result)==1);
            check(client.dispatch("dkr_legacy_asset_cache_capacity",ram.data(),&ctx,&kind,1,nullptr,0,client_capacity)==1 && result==client_capacity);
            const std::uint64_t valid[]{kind,result-1},invalid[]{kind,result};
            check(host.dispatch("dkr_legacy_asset_cache_guard",ram.data(),&ctx,valid,2,nullptr,0,result)==1);
            check(host.dispatch("dkr_legacy_asset_cache_guard",ram.data(),&ctx,invalid,2,nullptr,0,result)==-1 && host.checkpoint()==baseline);
        }
        check(host.dispatch("dkr_legacy_racer_spawn_failed",ram.data(),&ctx,nullptr,0,nullptr,0,result)==-1 && host.checkpoint()==baseline);
        check(host.dispatch("dkr_legacy_asset_api",ram.data(),&ctx,&arg,0,nullptr,0,result)==-1);
        check(!host.error().empty() && host.checkpoint()==baseline && client.checkpoint()==baseline);
        std::uint32_t source=0;
        {auto lease=a->acquire();source=lease.route()->address(3);}
        const auto expected=host.bus().resolve(source,16)->bytes();
        const std::uint64_t dma[]{source,0x80002000U,16};
        check(host.dispatch("dkr_owned_asset_dma",ram.data(),&ctx,dma,3,nullptr,0,result)==1);
        for(unsigned i=0;i<expected.size();++i)check(ram[(0x2000+i)^3]==expected[i]);
        const auto unchanged=ram;
        const std::uint64_t corrupt[]{0x7fffffffU,0x80002000U,16};
        check(host.dispatch("dkr_owned_asset_dma",ram.data(),&ctx,corrupt,3,nullptr,0,result)==-1 && ram==unchanged);
        const std::uint64_t overflow[]{source,0x807ffff8U,16};
        check(host.dispatch("dkr_owned_asset_dma",ram.data(),&ctx,overflow,3,nullptr,0,result)==-1 && ram==unchanged);
        const std::uint64_t retail[]{0x10000000U,0x80002000U,16};
        check(host.dispatch("dkr_owned_asset_dma",ram.data(),&ctx,retail,3,nullptr,0,result)==0 && ram==unchanged);
        // level_global_init reads section 22 before display heaps exist;
        // level_load reads the SAME header table again for INTRO/menu/races.
        // This is the real mod adapter with a private in-memory DMA, not a
        // window, scheduler or simulated multiplayer match.
        const auto budget_address=stock->revision()=="us.v80"?0x800DD920U:0x800DD3B0U;
        const auto lists_address=stock->revision()=="us.v80"?0x80121770U:0x801211F0U;
        dkr::runtime::GamePayload table_calls{};
        table_calls.asset_allocate=table_allocate;table_calls.asset_release=table_release;table_calls.asset_copy=table_copy;
        dkr::runtime::legacy::ModWorld boot_world(std::make_shared<RuntimeSession>(stock,names),table_calls,nullptr,nullptr,false);
        asset_bus=&boot_world.bus();
        const auto read_header=[&](dkr::runtime::legacy::ModWorld& world){
            ctx.r4=22;ctx.r5=static_cast<gpr>(-1);
            const std::uint64_t operation=0;
            return world.dispatch("dkr_legacy_asset_api",ram.data(),&ctx,&operation,1,nullptr,0,result);
        };
        const auto retail_budgets=dkr::mods::online::course_display_budgets(nullptr);
        put(lists_address,0);
        for(unsigned i=0;i<4;++i)put(budget_address+4*i,0);
        check(read_header(boot_world)==1 && std::uint32_t(ctx.r2)==0x80600000U);
        const auto header=AssetDirectory::build(a->boot_bank());
        const auto header_bytes=header->read(22,0,header->section_size(22));
        for(unsigned i=0;i<header_bytes.size();++i)check(ram[(0x600000+i)^3]==header_bytes[i]);
        for(unsigned i=0;i<4;++i)check(word(budget_address+4*i)==retail_budgets[i]);
        put(lists_address,0x80212110U); // Existing pre-INTRO display allocation.
        for(unsigned repeat=0;repeat<3;++repeat) {
            if(read_header(boot_world)!=1)throw Error("INTRO/header reload rejected: "+boot_world.error());
            check(word(lists_address)==0x80212110U);
            for(unsigned i=0;i<4;++i)check(word(budget_address+4*i)==retail_budgets[i]);
        }
        put(budget_address,retail_budgets[0]+1);
        check(read_header(boot_world)==-1 && word(budget_address)==retail_budgets[0]+1 && word(lists_address)==0x80212110U);
        auto courses=std::make_shared<dkr::runtime::custom_tracks::PreparedTracks>();
        dkr::runtime::custom_tracks::TrackSelectEntry course;course.level_id=65;course.id="private-test";course.name="Private Test";course.vehicles=1;
        courses->courses.push_back(course);courses->model_arenas.push_back(0x90000);courses->model_batches.push_back(300);
        const auto budgets=dkr::mods::online::course_display_budgets(courses.get());
        check(budgets==std::array<std::uint32_t,4>{7500,13000,20000,23000});
        dkr::runtime::legacy::ModWorld enlarged(std::make_shared<RuntimeSession>(stock,names),table_calls,nullptr,courses,false);
        asset_bus=&enlarged.bus();put(lists_address,0);
        check(read_header(enlarged)==1);
        for(unsigned i=0;i<4;++i)check(word(budget_address+4*i)==budgets[i]);
        put(lists_address,0x80212110U);
        check(read_header(enlarged)==1 && word(lists_address)==0x80212110U);
        for(unsigned i=0;i<4;++i)check(word(budget_address+4*i)==budgets[i]);
        put(budget_address+12,budgets[3]-1);
        check(read_header(enlarged)==-1 && word(budget_address+12)==budgets[3]-1);
        for(unsigned i=0;i<3;++i)check(word(budget_address+4*i)==budgets[i]);
        check(dkr::mods::online::course_heap(courses.get(),65)==0x91000);
        check(dkr::mods::online::course_heap(courses.get(),64)==0);
        check(dkr::mods::online::admitted_course_heap(courses.get(),0x91000));
        check(!dkr::mods::online::admitted_course_heap(courses.get(),0x92000));
        dkr::runtime::legacy::ModWorld with_courses(std::make_shared<RuntimeSession>(stock,names),calls,nullptr,courses,false);
        for(unsigned i=0;i<4;++i)put(budget_address+4*i,budgets[i]);
        const std::uint64_t custom_level=65,unadmitted_level=66,normal_level=1;
        check(with_courses.dispatch("dkr_owned_scene_admission",ram.data(),&ctx,&custom_level,1,nullptr,0,result)==1 && result==1);
        check(with_courses.dispatch("dkr_owned_scene_admission",ram.data(),&ctx,&unadmitted_level,1,nullptr,0,result)==1 && result==0);
        check(with_courses.dispatch("dkr_owned_scene_admission",ram.data(),&ctx,&normal_level,1,nullptr,0,result)==1 && result==1);
        put(budget_address,budgets[0]+1);
        check(with_courses.dispatch("dkr_owned_scene_admission",ram.data(),&ctx,&custom_level,1,nullptr,0,result)==-1);
        put(budget_address,budgets[0]);ctx.r4=65;
        check(with_courses.dispatch("dkr_owned_course_prepare",ram.data(),&ctx,nullptr,0,nullptr,0,result)==1);
        const auto pending_course=with_courses.checkpoint();
        ctx.r21=dkr::runtime::custom_tracks::kRetailTrackHeap;
        check(with_courses.dispatch("dkr_custom_tracks_track_heap",ram.data(),&ctx,nullptr,0,nullptr,0,result)==1 && ctx.r21==0x91000);
        check(with_courses.commit_checkpoint(with_courses.stage_checkpoint(pending_course)));
        ctx.r21=dkr::runtime::custom_tracks::kRetailTrackHeap;
        check(with_courses.dispatch("dkr_custom_tracks_track_heap",ram.data(),&ctx,nullptr,0,nullptr,0,result)==1 && ctx.r21==0x91000);
        courses->model_batches[0]=0x20000;rejects([&]{dkr::mods::online::course_display_budgets(courses.get());});
        courses->model_batches[0]=300;courses->model_arenas.clear();rejects([&]{dkr::mods::online::course_display_budgets(courses.get());});
        std::cout<<checks<<" native mod-world isolation/checkpoint checks passed; no game window opened.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
