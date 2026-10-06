#pragma once
#include "legacy_track_artifact.hpp"
#include "legacy_resident_assets.hpp"
#include "legacy_character_materialize.hpp"
#include <optional>
#include <functional>

namespace dkr::mods {
// Owns all immutable asset-address generations for one running guest session.
// Only the emulated scene-loading thread may publish a scene; UI preparation
// happens before admission and does not write RDRAM.
class RuntimeSession {
public:
    using CourseAugmentation=std::function<std::shared_ptr<const AssetBank>(std::shared_ptr<const AssetBank>)>;
    explicit RuntimeSession(std::shared_ptr<const AssetBank> stock,
        std::shared_ptr<const CharacterNamespace> characters=nullptr,
        std::vector<Bytes> shared_textures={},CourseAugmentation courses={});
    const std::shared_ptr<const AssetBank>& boot_bank()const{return boot_;}
    void admit(PreparedTrack track);
    // Explicit identity is required even if the carrier matches a loaded scene.
    // An empty identity means the ORIGINAL course, never "keep the last mod".
    void request(std::string content_id,unsigned carrier);
    void begin_scene(std::span<std::uint8_t> guest,unsigned carrier,bool external_course=false);
    ResidentAssetState::Lease acquire();
    const AssetBus& bus() const {return bus_;}
    std::string current_content() const;
    std::uint64_t published_scenes() const;
    std::vector<Root> tracks() const;
    const std::shared_ptr<const CharacterNamespace>& characters() const {return characters_;}
    std::uint32_t character_sample_address(unsigned index)const;
    std::uint32_t character_race_sample_address(unsigned index)const;
    struct ReplayRestore {
    private:
        friend class RuntimeSession;
        const RuntimeSession* authority=nullptr;
        std::uint64_t expected=0;
        std::string current;
        std::optional<std::pair<std::string,unsigned>> requested;
        std::uint64_t scenes=0;
        ResidentAssetState::ReplayRestore resident;
    };
    Bytes checkpoint()const;
    ReplayRestore stage_checkpoint(View bytes)const;
    bool commit_checkpoint(ReplayRestore&&);
private:
    std::shared_ptr<const AssetBank> stock_;
    std::shared_ptr<const CharacterNamespace> characters_;
    std::vector<Bytes> shared_textures_;
    CourseAugmentation courses_;
    std::shared_ptr<const AssetBank> boot_;
    AssetBus bus_;
    std::shared_ptr<const AssetBus::Mount> character_samples_;
    std::vector<std::uint32_t> character_audio_offsets_,character_race_audio_offsets_;
    std::shared_ptr<const ResidentBank> original_;
    ResidentAssetState resident_;
    struct Entry {PreparedTrack track;std::shared_ptr<const ResidentBank> resident;};
    std::map<std::string,Entry> admitted_;
    struct Request {std::string id;unsigned carrier;};
    mutable std::mutex mutex_;
    std::optional<Request> requested_;
    std::string current_;
    std::uint64_t scenes_=0;
    // Local transaction fencing is deliberately NOT checkpointed: restoring
    // the same scene count must never make an old prepared restore valid again.
    std::uint64_t mutation_=0;
};
} // namespace dkr::mods
