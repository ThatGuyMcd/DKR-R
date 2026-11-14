#include "legacy_mod_format.hpp"
#include "legacy_mod_geometry.hpp"
#include "legacy_mod_dependencies.hpp"
#include "legacy_character_materialize.hpp"
#include "legacy_track_materialize.hpp"
#include <iostream>
int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--compatibility") {
        try {
            auto base = dkr::mods::read_file(dkr::mods::utf8_path(argv[2]), dkr::mods::MaxImage);
            dkr::mods::canonicalize_rom(base);
            for (const auto& patch : dkr::mods::read_patch_inputs(dkr::mods::utf8_path(argv[3]))) {
                const auto target = dkr::mods::decode_patch(base, patch.data);
                const auto hash = dkr::mods::sha256(patch.data);
                const auto a = dkr::mods::analyze(base, target, hash);
                for (const auto& c : a.character_roots)
                    try {
                        auto prepared = dkr::mods::prepare_character(base, target, hash, c.base_character);
                        auto names = dkr::mods::allocate_characters(dkr::mods::AssetBank::stock(base), {prepared});
                        std::cout << "CHAR " << c.base_character << " PASS " << prepared.records.size() << " records\n";
                        const dkr::mods::AssetImage retail(base, a.source_revision);
                        const auto& portrait = prepared.records.at({4, c.portrait});
                        std::cout << "  PORTRAIT " << c.portrait << " changed="
                                  << (dkr::mods::sha256(portrait) != dkr::mods::sha256(retail.record(4, c.portrait)))
                                  << " hash=" << dkr::mods::sha256(portrait) << '\n';
                        const auto original_audio = dkr::mods::prepare_character_race_audio(
                            retail.record(39, 2), retail.record(39, 3), retail.record(39, 7), c.base_character);
                        std::cout << "  RACE AUDIO changed="
                                  << (original_audio.control != prepared.race_audio.control ||
                                      original_audio.samples != prepared.race_audio.samples ||
                                      original_audio.cues != prepared.race_audio.cues)
                                  << '\n';
                    } catch (const std::exception& e) {
                        std::cout << "CHAR " << c.base_character << " FAIL " << e.what() << '\n';
                    }
                for (const auto& t : a.tracks)
                    try {
                        const auto p = dkr::mods::prepare_track_bank(base, target, hash, t.carrier);
                        std::cout << "TRACK " << t.carrier << " " << t.name << " PASS " << p.included.size()
                                  << " records\n";
                    } catch (const std::exception& e) {
                        std::cout << "TRACK " << t.carrier << " " << t.name << " FAIL " << e.what();
                        for (const auto& why : t.blockers)
                            std::cout << " (" << why << ")";
                        std::cout << '\n';
                    }
                for (const auto& why : a.blockers)
                    std::cout << "BLOCK " << why << '\n';
            }
            return 0;
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
    if (argc == 5 && std::string(argv[1]) == "--extract-audit") {
        try {
            auto base = dkr::mods::read_file(dkr::mods::utf8_path(argv[2]), dkr::mods::MaxImage);
            dkr::mods::canonicalize_rom(base);
            dkr::mods::verified_revision(base);
            const auto output = dkr::mods::private_storage_path(dkr::mods::utf8_path(argv[4]));
            if (!std::filesystem::create_directory(output))
                throw dkr::mods::Error("Audit output must be new.");
            unsigned i = 0;
            for (const auto& patch : dkr::mods::read_patch_inputs(dkr::mods::utf8_path(argv[3]))) {
                auto target = dkr::mods::decode_patch(base, patch.data);
                dkr::mods::canonicalize_rom(target);
                dkr::mods::write_new_file(output / (std::to_string(i++) + ".bin"), target);
                std::cout << patch.name << " " << dkr::mods::sha256(target) << "\n";
            }
            return 0;
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
    if (argc != 3 && argc != 4) {
        std::cerr
            << "Usage: DKRLegacyModInspect owned-base-rom patch-or-zip\n       DKRLegacyModInspect --geometry image revision\n";
        return 2;
    }
    try {
        if (argc == 4 && std::string(argv[1]) == "--assets") {
            const auto rom = dkr::mods::read_file(dkr::mods::utf8_path(argv[2]), dkr::mods::MaxImage);
            const dkr::mods::AssetImage image(rom, argv[3]);
            unsigned checked = 0, rejected = 0;
            for (unsigned section : {2U, 4U, 12U, 29U}) {
                const auto records = image.records(section);
                for (unsigned i = 0; i < records.size(); ++i)
                    if (!records[i].empty()) {
                        ++checked;
                        try {
                            if (section == 2 || section == 4)
                                dkr::mods::validate_texture_record(records[i]);
                            if (section == 12)
                                dkr::mods::inspect_sprite_textures(records[i]);
                            if (section == 29)
                                dkr::mods::inspect_model_textures(records[i]);
                        } catch (const std::exception& e) {
                            ++rejected;
                            std::cout << section << '/' << i << " size=" << records[i].size() << ": " << e.what()
                                      << '\n';
                        }
                    }
            }
            std::cout << checked << " assets inspected; " << rejected << " rejected.\n";
            return rejected ? 1 : 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--geometry") {
            const auto image = dkr::mods::read_file(dkr::mods::utf8_path(argv[2]), dkr::mods::MaxImage);
            const auto records = dkr::mods::AssetImage(image, argv[3]).records(27);
            for (unsigned i = 0; i < records.size(); ++i)
                if (records[i].size() >= 24) {
                    const auto decoded = dkr::mods::inflate_asset(records[i], 4 * dkr::mods::MiB);
                    std::cout << i << " packed=" << records[i].size() << " decoded=" << decoded.size()
                              << " modelSize=" << dkr::mods::be32(decoded, 0x48);
                    try {
                        const auto summary = dkr::mods::validate_geometry(decoded, records[i].size());
                        std::cout << " constructed=" << summary.constructed_bytes;
                    } catch (const std::exception& error) {
                        std::cout << " rejected=" << error.what();
                    }
                    std::cout << '\n';
                }
            return 0;
        }
        auto base = dkr::mods::read_file(dkr::mods::utf8_path(argv[1]), dkr::mods::MaxImage);
        dkr::mods::canonicalize_rom(base);
        dkr::mods::verified_revision(base);
        for (const auto& patch : dkr::mods::read_patch_inputs(dkr::mods::utf8_path(argv[2]))) {
            const auto target = dkr::mods::decode_patch(base, patch.data);
            std::cout << dkr::mods::analysis_json(dkr::mods::analyze(base, target, dkr::mods::sha256(patch.data)))
                      << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
