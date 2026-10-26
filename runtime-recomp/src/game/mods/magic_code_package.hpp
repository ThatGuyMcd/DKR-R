#pragma once
#include "legacy_mod_format.hpp"
#include <json/json.hpp>
#include <map>
#include <memory>
#include <set>
#include <compare>

namespace N64Recomp {
class Context;
}
namespace dkr::mods::code {
using Json = nlohmann::json;
inline constexpr unsigned ApiVersion = 1, ExecutionVersion = 1;
inline constexpr std::size_t MaxPackage = 32 * MiB, MaxExpanded = 64 * MiB, MaxCode = 4 * MiB;
inline constexpr unsigned MaxMembers = 512, MaxFunctions = 16384, MaxDependencies = 128;
struct Version {
    unsigned major = 0, minor = 0, patch = 0;
    std::string prerelease;
    std::strong_ordering operator<=>(const Version&) const;
    bool operator==(const Version& other) const {
        return (*this <=> other) == 0;
    }
    static Version parse(std::string_view);
};
struct Dependency {
    std::string id;
    Version minimum;
};
struct Package {
    std::string id, name, version, description, digest;
    Version minimum_host;
    unsigned api = ApiVersion;
    bool deterministic = false;
    bool development = false;
    std::set<std::string> sdk2_live_options;
    bool sdk2_reload = false;
    unsigned sdk2_state_schema = 0;
    std::set<std::string> revisions;
    std::map<std::string, unsigned> modules;
    std::vector<Dependency> dependencies;
    std::set<std::string> conflicts, systems;
    std::vector<std::string> tracks;
    std::map<std::string, Bytes> resources;
    Json options = Json::array();
    std::map<std::string, std::string> debug_lines;
    unsigned sdk2_heap = 65536, sdk2_instructions = 200000, sdk2_native_calls = 128;
    std::string sdk2_profile = "standard";
    Bytes binary;
    std::shared_ptr<const N64Recomp::Context> symbols;
};
bool valid_id(std::string_view);
// Exact withdrawn sample images only; third-party IDs/versions are not banned.
std::string compatibility_issue(std::string_view digest);
inline constexpr std::string_view TrophyUpdateDigest =
    "6f900a1005c892e4b86a48a3e4bacb7a1e01a7364680ebdd61af954cd94cf154";
Json parse_document(View, std::size_t limit = 1024 * 1024);
std::shared_ptr<const Package> inspect(View, std::string_view host_version);
Json configuration(const Package&, const Json& selected);
Json structured_data(const Json&);
std::vector<std::shared_ptr<const Package>> resolve(const std::vector<std::shared_ptr<const Package>>&,
                                                    const std::set<std::string>& enabled, std::string_view revision,
                                                    bool online,
                                                    const std::map<std::string, unsigned>& available_modules);
std::string session_identity(const std::vector<std::shared_ptr<const Package>>&,
                             const std::map<std::string, Json>& settings, std::string_view revision,
                             const std::map<std::string, Json>& data = {});
} // namespace dkr::mods::code
