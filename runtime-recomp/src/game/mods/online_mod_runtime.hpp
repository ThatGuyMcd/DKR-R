#pragma once
#include "online_mod_profile.hpp"
#include "legacy_runtime_session.hpp"
#include "online_mod_music.hpp"
#include "../custom_tracks.hpp"

namespace dkr::mods::online {
// Immutable, locally reconstructed game resources, pinned through a lobby and
// match. Every running guest obtains a DIFFERENT mutable RuntimeSession.
// Neither receipt metadata nor sharing another world's resident caches can
// stand in for this preparation.
class RuntimeResources {
public:
    static std::shared_ptr<const RuntimeResources> prepare(std::shared_ptr<const Profile> profile,
        const std::filesystem::path& original_rom,std::stop_token stop={});
    std::shared_ptr<RuntimeSession> new_session()const;
    const std::shared_ptr<const Profile>& profile()const{return profile_;}
    const std::shared_ptr<const dkr::runtime::custom_tracks::PreparedTracks>& authored()const{return authored_;}
    const std::string& fingerprint()const{return fingerprint_;}
    const std::shared_ptr<const MusicLibrary>& music()const{return music_;}
private:
    std::shared_ptr<const Profile> profile_;
    std::shared_ptr<const AssetBank> stock_;
    std::shared_ptr<const CharacterNamespace> characters_;
    std::shared_ptr<const dkr::runtime::custom_tracks::PreparedTracks> authored_;
    std::vector<PreparedTrack> tracks_;
    AssetBank::Overrides additions_;
    std::array<std::size_t,7> base_counts_{};
    std::string fingerprint_;
    std::shared_ptr<const MusicLibrary> music_;
    std::shared_ptr<const RuntimeSession> prepared_world_;
    std::shared_ptr<const AssetBank> augment(std::shared_ptr<const AssetBank>)const;
};
}
