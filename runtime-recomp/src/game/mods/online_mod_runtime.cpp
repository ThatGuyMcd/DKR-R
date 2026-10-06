#include "online_mod_runtime.hpp"
#include "legacy_track_catalog.hpp"
#include "legacy_asset_directory.hpp"
#include "online_course_policy.hpp"
#include <algorithm>
#include <set>

namespace dkr::mods::online {
namespace {
constexpr std::array<unsigned,7> Data{23,21,25,27,2,4,12};
constexpr std::array<unsigned,7> Tables{22,20,24,26,3,5,13};
void cancelled(std::stop_token stop){if(stop.stop_requested())throw Error("Online game resource preparation cancelled.");}
}
std::shared_ptr<const RuntimeResources> RuntimeResources::prepare(std::shared_ptr<const Profile> profile,
    const std::filesystem::path& rom,std::stop_token stop) {
    if(!profile)throw Error("Online resources require a verified host profile.");
    cancelled(stop);validate_profile(*profile);check_storage(rom,false);
    auto out=std::shared_ptr<RuntimeResources>(new RuntimeResources);out->profile_=std::move(profile);
    auto bytes=read_file(rom,MaxImage);canonicalize_rom(bytes);out->stock_=AssetBank::stock(std::move(bytes));
    if(out->stock_->revision()!=out->profile_->manifest.revision)
        throw Error("The locally imported Game Pak revision does not match this lobby.");
    const auto library=out->profile_->directory/"legacy";
    cancelled(stop);
    auto characters=TrackCatalog::load_enabled_characters(library,out->stock_);
    if(!characters.empty())out->characters_=std::make_shared<const CharacterNamespace>(allocate_characters(out->stock_,std::move(characters)));
    cancelled(stop);out->tracks_=TrackCatalog::load_enabled(library,out->stock_);
    // Runtime ordering is content identity, not a translated/display name or
    // filesystem scan. The native character namespace already sorts IDs.
    std::sort(out->tracks_.begin(),out->tracks_.end(),[](const auto& a,const auto& b){return a.root.content_id<b.root.content_id;});
    std::map<std::string,dkr::runtime::custom_tracks::Track> labs;
    for(const auto& entry:std::filesystem::directory_iterator(out->profile_->directory/"tracks")) {
        cancelled(stop);dkr::runtime::custom_tracks::Track track;std::string error;
        if(!dkr::runtime::custom_tracks::inspect_package(entry.path(),track,error))throw Error(error);
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
    auto music=std::make_shared<MusicLibrary>();std::size_t music_budget=128*MiB;
    std::map<std::string,std::shared_ptr<const dkr::runtime::custom_music::DecodedMusic>> decoded;
    std::atomic<bool> cancel_decode{false};std::stop_callback cancel_music(stop,[&]{cancel_decode=true;});
    std::string music_identity;
    for(const auto& course:out->authored_->courses) {
        const auto track=std::ranges::find_if(out->authored_->tracks,[&](const auto& t){return t.id==course.id;});
        if(track==out->authored_->tracks.end())throw Error("Frozen course has no authored resource root.");
        if(!track->music)continue;
        MusicSong song;song.info=*track->music;
        if(song.info.kind==dkr::runtime::custom_tracks::MusicKind::Recording) {
            auto found=decoded.find(song.info.sha256);
            if(found!=decoded.end())song.pcm=found->second;
            else {
                cancelled(stop);const auto input=read_file(song.info.file,dkr::runtime::custom_tracks::kMaxMusicBytes);
                if(sha256(input)!=song.info.sha256)throw Error("Online music changed after package verification.");
                auto pcm=std::make_shared<dkr::runtime::custom_music::DecodedMusic>();std::string error;
                if(!dkr::runtime::custom_music::decode_bytes(input.data(),input.size(),song.info.codec,*pcm,error,&cancel_decode,std::min<std::size_t>(64*MiB,music_budget)))
                    throw Error("Online track music cannot be prepared: "+error);
                if(pcm->frames!=song.info.frames || pcm->sample_rate!=song.info.sample_rate ||
                   song.info.loop_start>=pcm->frames || (song.info.loop_end && (song.info.loop_end>pcm->frames || song.info.loop_end<=song.info.loop_start)))
                    throw Error("Online music does not match its measured loop and sample contract.");
                music_budget-=pcm->samples.size()*2;song.pcm=pcm;decoded.emplace(song.info.sha256,pcm);
                // Verify the decoded representation too: a platform whose
                // decoder differs must not enter deterministic replay.
                Bytes canonical;canonical.reserve(pcm->samples.size()*2);
                for(auto sample:pcm->samples){canonical.push_back(std::uint8_t(sample));canonical.push_back(std::uint8_t(unsigned(sample)>>8));}
                music_identity+="\n"+song.info.sha256+":"+sha256(canonical);
            }
        }
        music->emplace(course.level_id,std::move(song));
    }
    out->music_=std::move(music);
    for(unsigned i=0;i<Data.size();++i)for(unsigned j=0;j<out->authored_->additions[i].size();++j)
        out->additions_.emplace(AssetKey{Data[i],static_cast<unsigned>(out->base_counts_[i]+j)},out->authored_->additions[i][j]);
    // Fully prepare every route NOW, before proof/readiness. Scene switches
    // subsequently select immutable banks and never decode/hash/read a file.
    auto prepared=out->new_session();
    std::string identity="dkr-online-runtime-2\n"+out->profile_->digest+"\n"+prepared->boot_bank()->fingerprint()+music_identity;
    for(const auto& track:out->tracks_)identity+="\n"+track.root.content_id+":"+track.bank->fingerprint();
    out->fingerprint_=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
    cancelled(stop);return out;
}
std::shared_ptr<const AssetBank> RuntimeResources::augment(std::shared_ptr<const AssetBank> bank)const {
    for(unsigned i=0;i<Data.size();++i)if(bank->record_count(Data[i])!=base_counts_[i])
        throw Error("Online course changed the frozen Track Lab ID namespace.");
    return AssetBank::append_courses(std::move(bank),additions_);
}
std::shared_ptr<RuntimeSession> RuntimeResources::new_session()const {
    // Capture immutable additions/counts by value; the session may outlive the
    // caller without retaining a dangling RuntimeResources pointer.
    const auto additions=std::make_shared<const AssetBank::Overrides>(additions_);
    const auto counts=base_counts_;
    auto result=std::make_shared<RuntimeSession>(stock_,characters_,std::vector<Bytes>{},
        [additions,counts](std::shared_ptr<const AssetBank> bank) {
            for(unsigned i=0;i<Data.size();++i)if(bank->record_count(Data[i])!=counts[i])
                throw Error("Online course changed the frozen Track Lab namespace.");
            return AssetBank::append_courses(std::move(bank),*additions);
        });
    for(const auto& track:tracks_)result->admit(track);
    return result;
}
}
