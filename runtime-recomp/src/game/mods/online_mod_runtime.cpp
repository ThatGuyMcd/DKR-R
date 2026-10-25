#include "vehicle_online.hpp"
#include "vehicle_binding.hpp"
#include "resident_music.hpp"
#include "online_mod_runtime.hpp"
#include "legacy_track_catalog.hpp"
#include "legacy_asset_directory.hpp"
#include "online_course_policy.hpp"
#include "online_mod_cache_policy.hpp"
#include "magic_code_game_world.hpp"
#include <algorithm>
#include <set>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
#include <sys/sysinfo.h>
#endif

namespace dkr::mods::online {
namespace {
constexpr std::array<unsigned,7> Data{23,21,25,27,2,4,12};
constexpr std::array<unsigned,7> Tables{22,20,24,26,3,5,13};
void cancelled(std::stop_token stop){if(stop.stop_requested())throw Error("Online game resource preparation cancelled.");}
// One bounded, in-process cache. Its prepared world is never run and only
// immutable assets are instantiated into fresh per-game mutable owners.
std::mutex resource_cache_mutex;
std::string resource_cache_key;
std::shared_ptr<const RuntimeResources> resource_cache;
std::weak_ptr<const RuntimeResources> live_resource_cache;
std::uint64_t retention_budget() {
#ifdef _WIN32
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
    return GlobalMemoryStatusEx(&memory)?resource_cache_budget(memory.ullTotalPhys,memory.ullAvailPhys,false):0;
#elif defined(__APPLE__)
    // Available approximates Linux free+buffers: free, inactive and speculative pages.
    std::uint64_t physical=0;std::size_t size=sizeof(physical);
    vm_statistics64_data_t vm{};mach_msg_type_number_t count=HOST_VM_INFO64_COUNT;
    if(sysctlbyname("hw.memsize",&physical,&size,nullptr,0)!=0 ||
        host_statistics64(mach_host_self(),HOST_VM_INFO64,reinterpret_cast<host_info64_t>(&vm),&count)!=KERN_SUCCESS)
        return 0;
    const std::uint64_t available=(std::uint64_t(vm.free_count)+vm.inactive_count+vm.speculative_count)*vm_page_size;
    return resource_cache_budget(physical,available,false);
#else
    struct sysinfo memory{};
    if(sysinfo(&memory)!=0)return 0;
    return resource_cache_budget(std::uint64_t(memory.totalram)*memory.mem_unit,
        std::uint64_t(memory.freeram+memory.bufferram)*memory.mem_unit,
#ifdef __ANDROID__
        true
#else
        false
#endif
    );
#endif
}
}
std::shared_ptr<const RuntimeResources> RuntimeResources::prepare(std::shared_ptr<const Profile> profile,
    const std::filesystem::path& rom,std::stop_token stop,std::string_view version) {
    const auto began=std::chrono::steady_clock::now();
    if(!profile)throw Error("Online resources require a verified host profile.");
    cancelled(stop);const auto sources=profile_sources(*profile);check_storage(rom,false);
    auto out=std::shared_ptr<RuntimeResources>(new RuntimeResources);out->profile_=std::move(profile);
    auto bytes=read_file(rom,MaxImage);canonicalize_rom(bytes);
    const auto key=out->profile_->digest+":"+sha256(bytes)+":selected-music-v1";
    {
        std::lock_guard lock(resource_cache_mutex);
        if(auto reusable=live_resource_cache.lock();reusable && resource_cache_key==key) {
            cancelled(stop);
            std::fprintf(stderr,"[online-mods][runtime] immutable-cache-hit=1 elapsed-ms=%lld\n",
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count()));
            return reusable;
        }
        // Do not keep an unrelated collection while preparing a new one.
        resource_cache.reset();live_resource_cache.reset();resource_cache_key.clear();
    }
    out->stock_=AssetBank::stock(std::move(bytes));
    if(out->stock_->revision()!=out->profile_->manifest.revision)
        throw Error("The locally imported Game Pak revision does not match this lobby.");
    cancelled(stop);
    if(!out->profile_->manifest.code.empty()) {
        auto code_set=std::make_shared<code::FrozenSet>();std::vector<std::shared_ptr<const code::Package>> packages;
        std::set<std::string> enabled;std::size_t resident=0;
        for(const auto& wanted:out->profile_->manifest.code) {
            cancelled(stop);const auto bytes=std::make_shared<const Bytes>(read_file(sources.code.at(wanted.payload),code::MaxPackage));
            const auto package=code::inspect(*bytes,version);
            if(package->digest!=wanted.payload||package->id!=wanted.id||package->name!=wanted.name||package->version!=wanted.version)
                throw Error("Executable package metadata differs from the exact host offer.");
            const auto settings=code::parse_document(View(reinterpret_cast<const std::uint8_t*>(wanted.settings.data()),wanted.settings.size()),512*1024);
            const auto data=code::parse_document(View(reinterpret_cast<const std::uint8_t*>(wanted.data.data()),wanted.data.size()),65536);
            const auto options=code::configuration(*package,settings),documents=code::structured_data(data);
            if(options.dump()!=wanted.settings||documents.dump()!=wanted.data)throw Error("Executable code configuration differs from the host offer.");
            std::size_t bytes_owned=bytes->size()+package->binary.size()*2;
            for(const auto& [id,value]:package->resources)bytes_owned+=value.size();
            if(bytes_owned>256*MiB-resident)throw Error("Executable package resources exceed the online memory budget.");
            resident+=bytes_owned;enabled.insert(package->id);packages.push_back(package);
            code_set->settings.emplace(package->id,options);code_set->data.emplace(package->id,documents);
            code_set->payloads.emplace(package->digest,bytes);
        }
        code_set->ordered=code::resolve(packages,enabled,out->profile_->manifest.revision,true,code::GameWorld::policy().modules);
        if(code_set->ordered!=packages)throw Error("The host code packages are not in canonical dependency order.");
        code_set->identity=code::session_identity(code_set->ordered,code_set->settings,out->profile_->manifest.revision,code_set->data);
        out->code_=std::move(code_set);
    }
    auto characters=TrackCatalog::load_selected_characters(sources.characters,out->stock_);
    if(!characters.empty())out->characters_=std::make_shared<const CharacterNamespace>(allocate_characters(out->stock_,std::move(characters)));
    cancelled(stop);out->tracks_=TrackCatalog::load_selected(sources.tracks,out->stock_);
    // Runtime ordering is content identity, not a translated/display name or
    // filesystem scan. The native character namespace already sorts IDs.
    std::sort(out->tracks_.begin(),out->tracks_.end(),[](const auto& a,const auto& b){return a.root.content_id<b.root.content_id;});
    std::map<std::string,dkr::runtime::custom_tracks::Track> labs;
    for(const auto& course:sources.courses) {
        cancelled(stop);dkr::runtime::custom_tracks::Track track;std::string error;
        if(!dkr::runtime::custom_tracks::inspect_package(course,track,error))throw Error(error);
        const auto name=std::string("dkrmap:")+track.id;
        const auto id=sha256(View(reinterpret_cast<const std::uint8_t*>(name.data()),name.size()));
        if(!labs.emplace(id,std::move(track)).second)throw Error("Duplicate online Track Lab identity.");
    }
    std::vector<dkr::runtime::custom_tracks::Track> selected;
    for(const auto& item:out->profile_->manifest.content)if(item.kind==Kind::TrackLab) {
        const auto found=labs.find(item.id);if(found==labs.end())throw Error("Online Track Lab root is missing.");
        selected.push_back(std::move(found->second));
    }
    if(selected.size()!=labs.size())throw Error("An online Track Lab directory contains unselected courses.");
    auto boot=out->characters_?out->characters_->apply(out->stock_):out->stock_;
    const auto directory=AssetDirectory::build(boot);
    std::array<std::vector<std::int32_t>,7> tables;
    for(unsigned i=0;i<Data.size();++i) {
        out->base_counts_[i]=boot->record_count(Data[i]);
        const auto table=directory->read(Tables[i],0,directory->section_size(Tables[i]));
        for(std::size_t at=0;at+4<=table.size();at+=4) {
            tables[i].push_back(static_cast<std::int32_t>(be32(table,at)));
            if(tables[i].back()==-1)break;
        }
    }
    out->authored_=std::make_shared<const dkr::runtime::custom_tracks::PreparedTracks>(
        dkr::runtime::custom_tracks::prepare_tracks(std::move(selected),tables));
    (void)course_display_budgets(out->authored_.get());
    auto music=std::make_shared<MusicLibrary>();std::size_t music_encoded_bytes=0;
    std::map<std::string,resident_music::Prepared> decoded;
    std::string music_identity;
    for(const auto& course:out->authored_->courses) {
        const auto track=std::ranges::find_if(out->authored_->tracks,[&](const auto& t){return t.id==course.id;});
        if(track==out->authored_->tracks.end())throw Error("Frozen course has no authored resource root.");
        if(!track->music)continue;
        MusicSong song;song.info=*track->music;
        if(song.info.kind==dkr::runtime::custom_tracks::MusicKind::Recording) {
            cancelled(stop);auto input=read_file(song.info.file,dkr::runtime::custom_tracks::kMaxMusicBytes);
            if(input.size()!=song.info.bytes || sha256(input)!=song.info.sha256)
                throw Error("Music for \""+track->name+"\" changed after package verification. Re-import that track.");
            auto found=decoded.find(song.info.sha256);
            if(found==decoded.end()) {
                if(input.size()>128*MiB-music_encoded_bytes)
                    throw Error("Encoded custom music exceeds the 128 MiB session limit.");
                const auto encoded_size=input.size();resident_music::Prepared prepared;
                try{prepared=resident_music::prepare(std::move(input),song.info,stop);}
                catch(const std::exception& e){throw Error("Music for \""+track->name+"\" cannot be prepared: "+e.what());}
                music_encoded_bytes+=encoded_size;
                music_identity+="\n"+song.info.sha256+":"+prepared.pcm_sha256;
                found=decoded.emplace(song.info.sha256,std::move(prepared)).first;
            }
            song.pcm=found->second.metadata;const auto& shape=*song.pcm;
            if(!shape.frames || shape.frames!=song.info.frames || shape.sample_rate!=song.info.sample_rate ||
               song.info.loop_start>=shape.frames ||
               (song.info.loop_end && (song.info.loop_end>shape.frames || song.info.loop_end<=song.info.loop_start)))
                throw Error("Music for \""+track->name+"\" has invalid frame, sample-rate or loop settings. Re-export that track.");
            std::fprintf(stderr,"[online-mods][music] course=%s frames=%llu rate=%u unique-recordings=%zu encoded-bytes=%zu idle-decoded-bytes=0\n",
                track->id.c_str(),static_cast<unsigned long long>(shape.frames),shape.sample_rate,decoded.size(),music_encoded_bytes);
        }
        music->emplace(course.level_id,std::move(song));
    }
    out->music_=std::move(music);

    for(unsigned i=0;i<Data.size();++i)for(unsigned j=0;j<out->authored_->additions[i].size();++j)
        out->additions_.emplace(AssetKey{Data[i],static_cast<unsigned>(out->base_counts_[i]+j)},out->authored_->additions[i][j]);
    // Fully prepare every route NOW, before proof/readiness. Scene switches
    // subsequently select immutable banks and never decode/hash/read a file.
    std::vector<std::shared_ptr<const vehicles::Package>> cars;
    for(const auto& c:out->profile_->manifest.content)if(c.kind==CarContent) {
        cancelled(stop);cars.push_back(verified_car(out->profile_->manifest,c,read_file(sources.code.at(c.payload),vehicles::MaxPackageBytes)));
    }
    if(!cars.empty()) {
        auto resources=vehicles::prepare_resources(out->stock_,AssetBank::append_courses(boot,out->additions_),out->characters_,vehicles::freeze_packages(std::move(cars)));
        vehicles::bind_resources(out,resources);
    }
    auto prepared=out->new_session();
    if(out->code_) {
        std::map<std::string,unsigned> defaults;
        for(const auto& track:out->tracks_) {
            const auto header=track.bank->record(23,track.root.carrier);
            if(header.size()<0x50)throw Error("A custom course is missing its default vehicle header.");
            defaults.emplace(track.root.content_id,header[0x4d]);
        }
        code::trophy::verify_sdk2_inventory(*out->code_,*out->stock_,out->characters_.get());
        code::trophy::verify_configuration(*out->code_,code::trophy::catalogue(*out->stock_,prepared->tracks(),*out->authored_,defaults));
    }
    out->prepared_world_=prepared;
    std::string identity="dkr-online-runtime-2\n"+out->profile_->digest+"\n"+prepared->boot_bank()->fingerprint()+music_identity;
    if(out->code_)identity+="\ncode:"+out->code_->identity;
    if(const auto cars=vehicles::resources_for(out.get()))identity+="\ncars:"+cars->identity;
    for(const auto& track:out->tracks_)identity+="\n"+track.root.content_id+":"+track.bank->fingerprint();
    out->fingerprint_=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
    cancelled(stop);
    std::size_t cache_bytes=prepared->prepared_bytes()+music_encoded_bytes;
    if(const auto cars=vehicles::resources_for(out.get())) {
        for(const auto& car:cars->library.packages) {
            cache_bytes+=car->source.size();
            for(const auto& lod:car->lods)for(const auto& mesh:lod.meshes)
                cache_bytes+=mesh.vertices.size()*sizeof(vehicles::Vec3)+mesh.uv.size()*sizeof(vehicles::Vec2)+mesh.faces.size()*sizeof(std::array<unsigned,3>);
            for(const auto& t:car->textures)cache_bytes+=t.image.rgba.size();
        }
    }
    if(out->code_)for(const auto& package:out->code_->ordered) {
        cache_bytes+=out->code_->payloads.at(package->digest)->size()+package->binary.size()*2;
        for(const auto& [id,value]:package->resources)cache_bytes+=value.size();
    }
    for(const auto& [key,data]:out->additions_)cache_bytes+=data.size();
    for(const auto& section:out->authored_->additions)for(const auto& data:section)cache_bytes+=data.size();
    for(const auto& track:out->authored_->tracks)for(const auto& entry:track.entries)cache_bytes+=entry.bytes.size();
    if(out->characters_) {
        for(const auto& [key,data]:out->characters_->additions)cache_bytes+=data.size();
        for(const auto& c:out->characters_->characters)cache_bytes+=c.audio.samples.size()+c.audio.control.size()+c.race_audio.samples.size()+c.race_audio.control.size();
    }
    const auto budget=retention_budget();
    {
        std::lock_guard lock(resource_cache_mutex);resource_cache_key=key;live_resource_cache=out;
        if(cache_bytes<=budget)resource_cache=out;
    }
    std::fprintf(stderr,"[online-mods][runtime] roots=%zu immutable-cache-hit=0 cache-bytes=%zu cache-budget=%llu cache-retained=%u elapsed-ms=%lld\n",
        out->profile_->manifest.content.size(),cache_bytes,static_cast<unsigned long long>(budget),unsigned(cache_bytes<=budget),
        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count()));
    return out;
}
std::shared_ptr<const AssetBank> RuntimeResources::augment(std::shared_ptr<const AssetBank> bank)const {
    for(unsigned i=0;i<Data.size();++i)if(bank->record_count(Data[i])!=base_counts_[i])
        throw Error("Online course changed the frozen Track Lab ID namespace.");
    return AssetBank::append_courses(std::move(bank),additions_);
}
std::shared_ptr<RuntimeSession> RuntimeResources::new_session()const {
    if(prepared_world_) {
        auto copy=prepared_world_->instantiate();vehicles::inherit_resources(prepared_world_.get(),copy);return copy;
    }
    const auto cars=vehicles::resources_for(this);
    // Capture immutable additions/counts by value; the session may outlive the
    // caller without retaining a dangling RuntimeResources pointer.
    const auto additions=std::make_shared<const AssetBank::Overrides>(additions_);
    const auto counts=base_counts_;
    auto result=std::make_shared<RuntimeSession>(stock_,characters_,std::vector<Bytes>{},
        [additions,counts,cars](std::shared_ptr<const AssetBank> bank) {
            for(unsigned i=0;i<Data.size();++i)if(bank->record_count(Data[i])!=counts[i])
                throw Error("Online course changed the frozen Track Lab namespace.");
            auto next=AssetBank::append_courses(std::move(bank),*additions);return cars?cars->apply(std::move(next)):next;
        });
    vehicles::bind_resources(result,cars);
    for(const auto& track:tracks_)result->admit(track);
    return result;
}
}
