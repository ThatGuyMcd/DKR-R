#include "magic_code_world.hpp"
#include "magic_code_game_world.hpp"
#include <iostream>

// Read-only SDK package inspector. It never installs packages, activates a
// profile, allocates guest RAM or executes any guest/native mod instruction.
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 4 || (argc == 3 && std::string_view(argv[2]) != "--core-link") ||
            (argc == 4 && std::string_view(argv[2]) != "--game-link")) {
            std::cerr << "Usage: DKR-MagicCode-Inspect package.nrm [--core-link | --game-link dispatch.json]\n";
            return 2;
        }
        using namespace dkr::mods;
        const auto package = code::inspect(read_file(utf8_path(argv[1]), code::MaxPackage), DKR_CODE_HOST_VERSION);
        code::Json output = {{"id", package->id},
                             {"name", package->name},
                             {"version", package->version},
                             {"sha256", package->digest},
                             {"api", package->api},
                             {"deterministic", package->deterministic},
                             {"revisions", package->revisions},
                             {"required_modules", package->modules},
                             {"code_bytes", package->binary.size()},
                             {"resources", package->resources.size()},
                             {"default_settings", code::configuration(*package, code::Json::object())},
                             {"executed", false}};
        if (argc == 4) {
            const auto bytes = read_file(utf8_path(argv[3]), 16 * MiB);
            const auto receipt = code::parse_document(bytes, 16 * MiB);
            const auto& functions =
                receipt.contains("functions") ? receipt.at("functions") : receipt.at("code_dispatch_functions");
            std::vector<std::string> names;
            std::vector<code::DispatchEntry> entries;
            names.reserve(functions.size());
            for (const auto& f : functions)
                names.push_back(f.at("name").get<std::string>());
            std::size_t i = 0;
            for (const auto& f : functions)
                entries.push_back({f.at("address").get<std::uint32_t>(), nullptr, f.at("protected").get<bool>(),
                                   f.at("protected_hook").get<bool>(), f.at("before_event").get<unsigned>(),
                                   f.at("after_event").get<unsigned>(), names[i++].c_str(),
                                   f.at("protected_call").get<bool>(), f.at("curated_call").get<bool>()});
            const auto revision = "us.v" + std::to_string(receipt.at("revision").get<unsigned>());
            const auto linked = code::link({package}, {}, revision, code::GameWorld::policy({entries}, true));
            output["game_link"] = {{"revision", revision},
                                   {"functions", linked->function_owners.size()},
                                   {"resident_bytes", linked->resident_bytes},
                                   {"imports", linked->imports.size()},
                                   {"namespace_verified", true}};
        } else if (argc == 3) {
            output["core_link"] = code::Json::object();
            for (const auto& revision : package->revisions) {
                const auto linked = code::link({package}, {}, revision, code::World::core_policy());
                output["core_link"][revision] = {{"resident_bytes", linked->resident_bytes},
                                                 {"functions", linked->function_owners.size()},
                                                 {"imports", linked->imports.size()},
                                                 {"session_identity", linked->identity}};
            }
        }
        std::cout << output.dump(2) << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
