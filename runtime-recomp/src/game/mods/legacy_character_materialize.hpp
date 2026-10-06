#pragma once
#include "legacy_character_limits.hpp"
#include "legacy_asset_bank.hpp"
#include "legacy_character_audio.hpp"
#include <map>

namespace dkr::mods {
// Private, immutable preparation result. Native gameplay still receives a
// retail behaviour ID; content identity and asset IDs live in this sidecar.
struct PreparedCharacter {
    CharacterRoot root;
    std::string source_revision,base_fingerprint;
    AssetBank::Overrides records;
    std::map<unsigned,std::vector<unsigned>> model_animations;
    bool runtime_certified=false;
    unsigned stage_header=0;
    CharacterAudio audio;
    CharacterRaceAudio race_audio;
};
PreparedCharacter prepare_character(View original,View reconstructed,std::string patch_digest,unsigned base_character);
// Revalidate persisted dependency graphs without trusting their manifest hashes.
// Returns the canonical content identity after complete bounds/closure checks.
std::string validate_prepared_character(const PreparedCharacter&,const AssetBank& original);

struct AllocatedCharacter {
    std::string id,name;
    unsigned base_character=0,portrait=0;
    std::array<unsigned,3> headers{};
    unsigned stage_header=0;
    unsigned stage_source_header=0;
    CharacterAudio audio;
    CharacterRaceAudio race_audio;
    bool custom_portrait=true;
    std::string portrait_identity,selection_audio_identity,race_audio_identity;
};
// Allocated once, before native asset-table initialization. No original record
// is replaced; all model/texture/animation/header references are relocated.
struct CharacterNamespace {
    std::string base_fingerprint;
    std::string roster_identity;
    AssetBank::Overrides additions;
    Bytes animation_ids;
    std::vector<AllocatedCharacter> characters;
    std::shared_ptr<const AssetBank> apply(std::shared_ptr<const AssetBank> bank) const;
};
CharacterNamespace allocate_characters(std::shared_ptr<const AssetBank> stock,
    std::vector<PreparedCharacter> characters);
// Exposed for bounds/negative tests and shared artifact verification.
void validate_character_animation(View packed,unsigned animated_vertices);
// Compare decoded face pixels, ignoring compression and exporter padding.
bool character_portrait_changed(View replacement,View original);
} // namespace dkr::mods
