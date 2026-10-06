#include "legacy_track_artifact.hpp"
#include "legacy_runtime_session.hpp"
#include "legacy_checkpoint.hpp"
#include <iostream>
#include <chrono>

namespace {
using namespace dkr::mods;
unsigned checks=0;
void check(bool value) {++checks;if(!value)throw Error("Prepared track test failed.");}
template<class F> void rejects(F&& f) {bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
struct Temporary {
    std::filesystem::path path;
    Temporary() {
        path=std::filesystem::current_path()/("track-artifacts-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if(!std::filesystem::create_directory(path))throw Error("Could not create isolated artifact test directory.");
    }
    ~Temporary(){std::error_code e;std::filesystem::remove_all(path,e);}
};
void session_tests(const std::shared_ptr<const AssetBank>& stock, const std::vector<PreparedTrack>& tracks) {
    Bytes memory(0x800000);
    const auto put_bytes=[&](std::uint32_t address,View data) {
        const auto at=address&0x1fffffff;
        if(at>memory.size() || data.size()>memory.size()-at)throw Error("Session fixture range invalid.");
        for(std::size_t i=0;i<data.size();++i)memory[(at+i)^3]=data[i];
    };
    const auto put=[&](std::uint32_t address,std::uint32_t value) {
        const Bytes bytes{std::uint8_t(value>>24),std::uint8_t(value>>16),std::uint8_t(value>>8),std::uint8_t(value)};
        put_bytes(address,bytes);
    };
    AssetBus fixture_bus;
    const auto original=ResidentBank::prepare(stock,stock,fixture_bus);
    const auto layout=resident_asset_layout(stock->revision());
    for(unsigned i=0;i<8;++i) {
        const auto address=0x80400000+i*0x10000;
        put(layout.table_pointers[i],address);put_bytes(address,original->tables()[i]);
    }
    for(const auto& cache:layout.caches) {put(cache.count_address,0);put(cache.pointer_address,0);}
    const auto original_memory=memory;
    RuntimeSession session(stock);
    for(const auto& track:tracks)session.admit(track);
    session.begin_scene(memory,5);
    check(session.current_content().empty());check(memory==original_memory);
    const auto stock_checkpoint=session.checkpoint();
    // A rejected sidecar never changes its owner or guest memory. Include
    // every truncated length, an alien boot identity and trailing fields.
    for(std::size_t cut=0;cut<stock_checkpoint.size();++cut)
        rejects([&]{session.stage_checkpoint(View(stock_checkpoint).first(cut));});
    auto malformed=stock_checkpoint;malformed.push_back(0);
    rejects([&]{session.stage_checkpoint(malformed);});
    malformed=stock_checkpoint;malformed[8]=malformed[8]=='a'?'b':'a';
    rejects([&]{session.stage_checkpoint(malformed);});
    check(session.checkpoint()==stock_checkpoint && memory==original_memory);
    {
        auto lease=session.acquire();
        rejects([&]{session.checkpoint();});
        rejects([&]{session.stage_checkpoint(stock_checkpoint);});
    }
    RuntimeSession foreign(stock);
    auto foreign_plan=foreign.stage_checkpoint(foreign.checkpoint());
    check(!session.commit_checkpoint(std::move(foreign_plan)));
    rejects([&]{session.admit(tracks.front());});
    rejects([&]{session.request(std::string(64,'0'),5);});
    for(const auto& track:tracks) {
        session.request(track.root.content_id,track.root.carrier);
        const auto previous=session.published_scenes();
        rejects([&]{session.begin_scene(memory,track.root.carrier+1);});
        check(memory==original_memory);check(session.published_scenes()==previous);
        {
            auto lease=session.acquire();
            rejects([&]{session.begin_scene(memory,track.root.carrier);});
            check(memory==original_memory);check(session.published_scenes()==previous);
        }
        session.begin_scene(memory,track.root.carrier);
        check(session.current_content()==track.root.content_id);
        check(session.acquire().route()->directory()->fingerprint()==track.bank->fingerprint());
        const auto custom_checkpoint=session.checkpoint(),custom_memory=memory;
        const auto custom_scenes=session.published_scenes();
        // No request must return to stock, even for the exact same retail carrier.
        session.begin_scene(memory,track.root.carrier);
        check(session.current_content().empty());check(!session.acquire().route());
        check(memory==original_memory);
        auto restore=session.stage_checkpoint(custom_checkpoint);
        // Only the exclusive owned CPU caller restores RAM; committing this
        // validated native sidecar cannot free/rewrite retained guest assets.
        memory=custom_memory;
        check(session.commit_checkpoint(std::move(restore)));
        check(session.current_content()==track.root.content_id && session.published_scenes()==custom_scenes);
        check(session.checkpoint()==custom_checkpoint && memory==custom_memory);
        check(!session.commit_checkpoint(std::move(restore))); // exactly once
        auto invalidated=session.stage_checkpoint(stock_checkpoint);
        session.request("",track.root.carrier);
        check(!session.commit_checkpoint(std::move(invalidated))); // request changed
        check(memory==custom_memory && session.current_content()==track.root.content_id);
        const auto requested_checkpoint=session.checkpoint();
        session.begin_scene(memory,track.root.carrier);
        check(memory==original_memory);
        auto requested_restore=session.stage_checkpoint(requested_checkpoint);
        memory=custom_memory;check(session.commit_checkpoint(std::move(requested_restore)));
        check(session.checkpoint()==requested_checkpoint);
        session.begin_scene(memory,track.root.carrier);
        check(memory==original_memory);
        auto stale=session.stage_checkpoint(stock_checkpoint);
        auto latest=session.stage_checkpoint(stock_checkpoint);
        check(session.commit_checkpoint(std::move(latest)));
        check(!session.commit_checkpoint(std::move(stale))); // identical restored counters cannot revive old plans
        check(session.checkpoint()==stock_checkpoint && memory==original_memory);
    }
    // Direct A -> B for the same carrier, then explicit stock, must not reuse A.
    for(const auto& a:tracks)for(const auto& b:tracks) {
        if(a.root.carrier!=b.root.carrier || a.root.content_id==b.root.content_id)continue;
        session.request(a.root.content_id,a.root.carrier);session.begin_scene(memory,a.root.carrier);
        session.request(b.root.content_id,b.root.carrier);session.begin_scene(memory,b.root.carrier);
        check(session.current_content()==b.root.content_id);
        check(session.acquire().route()->directory()->fingerprint()==b.bank->fingerprint());
        session.request("",b.root.carrier);session.begin_scene(memory,b.root.carrier);
        check(memory==original_memory);
    }
    std::cout<<"Session custom/stock/same-carrier identity, read leases and exact resident restoration passed.\n";
}
}
int main(int argc,char** argv) {
    using namespace dkr::mods;
    try {
        rejects([]{read_track_artifact({},"bad",{});});
        rejects([]{write_track_artifact({},TrackMaterialization{},std::string(64,'a'),"unknown");});
        if(argc==1) {std::cout<<checks<<" prepared-track admission checks passed (no private fixtures).\n";return 0;}
        if(argc<3)throw Error("Usage: DKRLegacyTrackArtifactTests base-rom patch-or-zip [patch-or-zip ...]");
        auto rom=read_file(utf8_path(argv[1]),MaxImage);canonicalize_rom(rom);
        auto stock=AssetBank::stock(rom);Temporary temporary;unsigned counter=0;
        std::vector<PreparedTrack> tracks;
        for(int argument=2;argument<argc;++argument) {
            for(const auto& patch:read_patch_inputs(utf8_path(argv[argument]))) {
                const auto decoded=decode_patch(rom,patch.data);const auto patch_hash=sha256(patch.data);
                const auto analysis=analyze(rom,decoded,patch_hash);
                for(const auto& root:analysis.tracks) {
                    const auto track=prepare_track_bank(rom,decoded,patch_hash,root.carrier);
                    const auto destination=temporary.path/std::to_string(counter++);
                    const auto digest=write_track_artifact(destination,track,patch_hash,analysis.profile);
                    const auto loaded=read_track_artifact(destination,digest,stock);
                    tracks.push_back(loaded);
                    check(loaded.root.content_id==root.content_id);check(loaded.root.name==root.name);
                    check(loaded.bank->fingerprint()==track.bank->fingerprint());
                    for(const auto& key:track.included)check(sha256(loaded.bank->record(key.first,key.second))==sha256(track.bank->record(key.first,key.second)));
                    rejects([&]{write_track_artifact(destination,track,patch_hash,analysis.profile);});
                    rejects([&]{read_track_artifact(destination,std::string(64,'0'),stock);});
                    const auto blob=destination/"blobs"/sha256(track.bank->record(23,root.carrier));
                    const auto moved=temporary.path/("header-"+std::to_string(counter));
                    std::filesystem::rename(blob,moved);
                    rejects([&]{read_track_artifact(destination,digest,stock);});
                    std::filesystem::rename(moved,blob);
                    check(read_track_artifact(destination,digest,stock).bank->fingerprint()==track.bank->fingerprint());
                    std::cout<<root.name<<" artifact="<<digest<<" bank="<<loaded.bank->fingerprint()<<'\n';
                }
            }
        }
        check(counter>0);session_tests(stock,tracks);
        std::cout<<checks<<" prepared-track checks passed across "<<counter<<" courses.\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
