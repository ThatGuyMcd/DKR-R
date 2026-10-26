#pragma once
#include "magic_code_trophy_definition.hpp"
#include "magic_code_library.hpp"
#include "legacy_runtime_session.hpp"
#include "../custom_tracks.hpp"
#include "legacy_track_menu_adapter.hpp"
namespace dkr::mods::code::trophy {
std::vector<Course> catalogue(const AssetBank&, const std::vector<Root>&,
                              const dkr::runtime::custom_tracks::PreparedTracks&,
                              const std::map<std::string, unsigned>& legacy_defaults = {});
std::vector<Course> installed_catalogue(const std::filesystem::path&, const std::filesystem::path&,
                                        const std::vector<std::filesystem::path>&, std::stop_token,
                                        bool canonical_order = true, Json* sdk2_inventory = nullptr);
Json course_document(const Course&);
std::vector<Course> read_catalogue(const Json&);
Json publication(const std::vector<Definition>&, std::string_view, const std::vector<Course>&, std::uint64_t seed);
Frozen active(const Json&);
std::vector<Frozen> all(const Json&);
std::string menu_id(std::string_view package, std::string_view cup);
std::vector<TrackMenuCup> menu_entries(const FrozenSet&);
void verify_configuration(const FrozenSet&, const std::vector<Course>&);
Json sdk2_inventory(const AssetBank&, Json characters);
void verify_sdk2_inventory(const FrozenSet&, const AssetBank&, const CharacterNamespace*);
void rebind(FrozenSet&, const std::vector<Course>&, std::string_view revision, const Json& inventory = Json::object());
// Data-only exchange format: no executable code, assets, paths or save state.
struct SharedCups {
    std::vector<Definition> cups;
    std::vector<Course> tracks;
};
inline constexpr std::size_t MaxSharedCups = 256 * 1024;
Bytes share(const std::vector<Definition>&, const std::vector<Course>&);
SharedCups read_share(View);
std::vector<std::string> share_requirements(const SharedCups&, const std::vector<Course>&);
std::vector<Definition> merge_shared(const std::vector<Definition>&, const SharedCups&);
} // namespace dkr::mods::code::trophy
