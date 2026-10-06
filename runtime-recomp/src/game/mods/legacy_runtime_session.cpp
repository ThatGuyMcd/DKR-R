#include "legacy_runtime_session.hpp"
#include "legacy_checkpoint.hpp"
#include <limits>
#include <algorithm>

namespace dkr::mods {
RuntimeSession::RuntimeSession(std::shared_ptr<const AssetBank> stock,std::shared_ptr<const CharacterNamespace> characters,
    std::vector<Bytes> shared_textures,CourseAugmentation courses)
    :stock_(std::move(stock)),characters_(std::move(characters)),shared_textures_(std::move(shared_textures)),courses_(std::move(courses)),
     boot_(AssetBank::append_textures(characters_?characters_->apply(stock_):stock_,shared_textures_)),
     original_(nullptr),resident_([&]{if(courses_)boot_=courses_(boot_);return original_=ResidentBank::prepare(boot_,boot_,bus_);}()) {
    if(characters_) {
        // One immutable sample mount, not two full-ROM virtual mounts per
        // character. Repeated donor samples share bytes, never voices/cues.
        Bytes samples;std::map<std::string,std::uint32_t> offsets;
        const auto append=[&](View bytes) {
            const auto id=sha256(bytes);
            if(const auto found=offsets.find(id);found!=offsets.end())return found->second;
            const auto offset=static_cast<std::uint32_t>(samples.size());
            if(bytes.size()>MaxImage-samples.size())throw Error("Enabled character voices exceed the 64 MiB sample-data budget. Reduce the enabled collection; installed mods are retained.");
            samples.insert(samples.end(),bytes.begin(),bytes.end());
            samples.resize((samples.size()+15)&~std::size_t(15));
            if(samples.size()>MaxImage)throw Error("Enabled character sample alignment exceeds its budget.");
            offsets.emplace(id,offset);return offset;
        };
        for(const auto& character:characters_->characters) {
            character_audio_offsets_.push_back(append(character.audio.samples));
            if(character.race_audio.control.empty())character_race_audio_offsets_.push_back(UINT32_MAX);
            else {validate_character_race_audio(character.race_audio);character_race_audio_offsets_.push_back(append(character.race_audio.samples));}
        }
        const auto digest=sha256(samples);
        const auto bank=AssetBank::derive(stock_,digest,{{{39,3},std::move(samples)}});
        character_samples_=bus_.mount(AssetDirectory::build(bank));
    }
}
RuntimeSession::RuntimeSession(const RuntimeSession& source,PreparedCopy)
    :stock_(source.stock_),characters_(source.characters_),boot_(source.boot_),
     original_(ResidentBank::instantiate(*source.original_,bus_)),resident_(original_) {
    character_audio_offsets_=source.character_audio_offsets_;
    character_race_audio_offsets_=source.character_race_audio_offsets_;
    if(source.character_samples_) {
        character_samples_=bus_.mount(source.character_samples_->directory());
        for(unsigned section=0;section<50;++section)
            if(character_samples_->address(section)!=source.character_samples_->address(section))throw Error("Prepared audio address order changed.");
    }
    std::vector<const Entry*> order;
    for(const auto& [id,entry]:source.admitted_)order.push_back(&entry);
    std::sort(order.begin(),order.end(),[](const auto* a,const auto* b){return a->resident->allocation_address()<b->resident->allocation_address();});
    for(const auto* entry:order)
        admitted_.emplace(entry->track.root.content_id,Entry{entry->track,ResidentBank::instantiate(*entry->resident,bus_)});
    // Allocation templates may not subsequently admit unrelated content.
    mutation_=source.mutation_;shared_textures_=source.shared_textures_;courses_=source.courses_;
}
std::shared_ptr<RuntimeSession> RuntimeSession::instantiate()const {
    std::lock_guard lock(mutex_);
    if(scenes_ || requested_ || !current_.empty())throw Error("Cannot instantiate a world after gameplay has started.");
    return std::shared_ptr<RuntimeSession>(new RuntimeSession(*this,PreparedCopy{}));
}
std::size_t RuntimeSession::prepared_bytes()const {
    std::lock_guard lock(mutex_);
    std::size_t bytes=MaxImage+boot_->owned_override_bytes()+original_->owned_bytes();
    for(const auto& [id,entry]:admitted_)bytes+=entry.resident->owned_bytes()+entry.track.bank->owned_override_bytes();
    if(character_samples_)bytes+=character_samples_->directory()->section_size(39);
    return bytes;
}
std::uint32_t RuntimeSession::character_sample_address(unsigned index)const {
    std::size_t offset=0;for(unsigned i=0;i<3;++i)offset+=stock_->record(39,i).size();
    return character_samples_->address(39,offset+character_audio_offsets_.at(index));
}
std::uint32_t RuntimeSession::character_race_sample_address(unsigned index)const {
    const auto sample=character_race_audio_offsets_.at(index);if(sample==UINT32_MAX)return 0;
    std::size_t offset=0;for(unsigned i=0;i<3;++i)offset+=stock_->record(39,i).size();
    return character_samples_->address(39,offset+sample);
}
void RuntimeSession::admit(PreparedTrack track) {
    std::lock_guard lock(mutex_);
    if(scenes_ || requested_ || mutation_==std::numeric_limits<std::uint64_t>::max()) throw Error("Scene content cannot be admitted after guest loading begins.");
    if(!track.bank || track.bank->base_fingerprint()!=stock_->fingerprint() ||
       track.bank->digest()!=track.root.content_id || track.artifact_digest.size()!=64)
        throw Error("Scene content does not match this session's verified Game Pak.");
    const auto id=track.root.content_id;
    if(auto found=admitted_.find(id);found!=admitted_.end()) {
        if(found->second.track.bank->fingerprint()!=track.bank->fingerprint())
            throw Error("Two different course banks claim the same content identity.");
        return;
    }
    auto candidate=AssetBank::append_textures(characters_?characters_->apply(track.bank):track.bank,shared_textures_);
    if(courses_)candidate=courses_(std::move(candidate));
    auto resident=ResidentBank::prepare(boot_,std::move(candidate),bus_);
    admitted_.emplace(id,Entry{std::move(track),std::move(resident)});
    ++mutation_;
}
void RuntimeSession::request(std::string id,unsigned carrier) {
    std::lock_guard lock(mutex_);
    if(mutation_==std::numeric_limits<std::uint64_t>::max())throw Error("Custom session transaction exhausted.");
    if(carrier>=stock_->record_count(23)) throw Error("Scene request has an invalid retail carrier.");
    if(!id.empty()) {
        const auto found=admitted_.find(id);
        if(found==admitted_.end() || found->second.track.root.carrier!=carrier)
            throw Error("Scene request refers to unprepared content or a different carrier.");
    }
    requested_=Request{std::move(id),carrier};++mutation_;
}
void RuntimeSession::begin_scene(std::span<std::uint8_t> guest,unsigned carrier,bool external_course) {
    std::lock_guard lock(mutex_);
    if(scenes_==std::numeric_limits<std::uint64_t>::max() || mutation_==std::numeric_limits<std::uint64_t>::max())throw Error("Scene generation exhausted.");
    // A request only belongs to its exact load; unrelated menus/cutscenes are
    // always original. New custom loads need a fresh explicit request.
    std::string next;
    if(requested_ && !external_course) {
        if(requested_->carrier==carrier)next=requested_->id;
        else if(!requested_->id.empty())throw Error("Pending custom scene does not match the actual scene load.");
    }
    if(next!=current_) {
        const auto bank=next.empty()?original_:admitted_.at(next).resident;
        auto plan=resident_.prepare(guest,bank);
        const auto result=resident_.commit(guest,std::move(plan));
        if(result!=ResidentAssetState::Commit::Published)
            throw Error(result==ResidentAssetState::Commit::Busy?
                "Scene loading crossed an outstanding asset read; custom content was not published.":
                "Scene loading changed resident tables during custom-content preparation.");
        current_=std::move(next);
    }
    requested_.reset();++scenes_;++mutation_;
}
ResidentAssetState::Lease RuntimeSession::acquire(){return resident_.acquire();}
std::string RuntimeSession::current_content()const{std::lock_guard lock(mutex_);return current_;}
std::uint64_t RuntimeSession::published_scenes()const{std::lock_guard lock(mutex_);return scenes_;}
std::vector<Root> RuntimeSession::tracks()const {
    std::lock_guard lock(mutex_);std::vector<Root> result;result.reserve(admitted_.size());
    for(const auto& [id,entry]:admitted_)result.push_back(entry.track.root);
    return result;
}
Bytes RuntimeSession::checkpoint()const {
    std::lock_guard lock(mutex_);CheckpointWriter out;
    out.u32(1);out.text(boot_->fingerprint(),64);out.text(current_,64);out.u64(scenes_);out.flag(requested_.has_value());
    if(requested_){out.text(requested_->id,64);out.u32(requested_->carrier);}
    out.block(resident_.checkpoint());return std::move(out).finish();
}
RuntimeSession::ReplayRestore RuntimeSession::stage_checkpoint(View bytes)const {
    std::lock_guard lock(mutex_);CheckpointReader in(bytes);
    if(in.u32()!=1 || in.text(64)!=boot_->fingerprint())throw Error("Custom checkpoint belongs to a different immutable boot bank.");
    ReplayRestore out;out.authority=this;out.expected=mutation_;out.current=in.text(64);out.scenes=in.u64();
    if(out.scenes==std::numeric_limits<std::uint64_t>::max() || (!out.current.empty() && !admitted_.contains(out.current)))
        throw Error("Custom checkpoint names an unadmitted scene.");
    if(in.flag()) {
        const auto id=in.text(64);const auto carrier=in.u32();
        if(carrier>=stock_->record_count(23) || (!id.empty() && (!admitted_.contains(id) || admitted_.at(id).track.root.carrier!=carrier)))
            throw Error("Custom checkpoint names an invalid scene request.");
        out.requested=std::make_pair(id,carrier);
    }
    std::map<std::string,std::shared_ptr<const ResidentBank>> owners{{original_->fingerprint(),original_}};
    for(const auto& [id,entry]:admitted_)owners.emplace(entry.resident->fingerprint(),entry.resident);
    const auto resident=in.block();
    CheckpointReader header(resident);
    if(header.u32()!=1 || header.text(64)!=(out.current.empty()?original_:admitted_.at(out.current).resident)->fingerprint())
        throw Error("Custom checkpoint scene and resident route disagree.");
    out.resident=resident_.stage_checkpoint(resident,owners);in.end();return out;
}
bool RuntimeSession::commit_checkpoint(ReplayRestore&& staged) {
    std::lock_guard lock(mutex_);
    if(staged.authority!=this || staged.expected!=mutation_ || mutation_==std::numeric_limits<std::uint64_t>::max() ||
       !resident_.commit_checkpoint(std::move(staged.resident)))return false;
    current_.swap(staged.current);requested_.reset();
    if(staged.requested)requested_=Request{std::move(staged.requested->first),staged.requested->second};
    scenes_=staged.scenes;++mutation_;staged.authority=nullptr;return true;
}
} // namespace dkr::mods
