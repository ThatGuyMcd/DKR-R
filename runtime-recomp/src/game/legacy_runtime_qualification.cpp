#include "legacy_runtime_qualification.hpp"
#include "runtime_legacy_mods.hpp"
#include "recomp.h"
#include "mods/legacy_track_materialize.hpp"
#include "mods/legacy_mod_stage.hpp"
#include "mods/legacy_track_menu_adapter.hpp"
#include <json/json.hpp>
#include <cstdio>
#include <bit>
#include <algorithm>

namespace dkr::runtime::legacy {
namespace {
std::vector<std::string> sequence;
unsigned next_scene = 0, carrier = 0;
bool enabled = false;
bool open_menu = false, auto_menu = false, entered_menu = false;
unsigned menu_ticks = 0, menu_step = 0;
std::vector<mods::Root> menu_roots;
} // namespace
void configure_qualification(const std::filesystem::path& path, const std::filesystem::path& recipe) {
    enabled = false;
    sequence.clear();
    next_scene = 0;
    open_menu = auto_menu = entered_menu = false;
    menu_ticks = menu_step = 0;
    menu_roots.clear();
    const auto bytes = mods::read_file(recipe, 16384);
    const auto config = nlohmann::json::parse(bytes.begin(), bytes.end());
    const bool native_menu = config.value("native_menu", false);
    auto_menu = native_menu && config.value("auto_preview_cycle", false);
    open_menu = native_menu;
    const bool private_races = config.value("private_races", false);
    if (private_races && (!native_menu || auto_menu))
        throw mods::Error(
            "Playable qualification must use native character selection, not the automatic preview shortcut.");
    carrier = config.at("carrier").get<unsigned>();
    auto rom = mods::read_file(path, mods::MaxImage);
    mods::canonicalize_rom(rom);
    const auto stock = mods::AssetBank::stock(rom);
    std::shared_ptr<const mods::CharacterNamespace> characters;
    if (config.contains("characters")) {
        const auto& sources = config.at("characters");
        if (!private_races || !sources.is_array() || sources.empty() || sources.size() > 4)
            throw mods::Error("Character qualification requires bounded sources and interactive native races.");
        std::vector<mods::PreparedCharacter> prepared;
        for (const auto& source : sources)
            for (const auto& patch : mods::read_patch_inputs(mods::utf8_path(source.get<std::string>()))) {
                const auto decoded = mods::decode_patch(rom, patch.data);
                const auto digest = mods::sha256(patch.data);
                const auto analysis = mods::analyze(rom, decoded, digest);
                for (const auto& character : analysis.character_roots)
                    prepared.push_back(mods::prepare_character(rom, decoded, digest, character.base_character));
            }
        characters =
            std::make_shared<const mods::CharacterNamespace>(mods::allocate_characters(stock, std::move(prepared)));
    }
    auto active = std::make_shared<mods::RuntimeSession>(stock, characters);
    const auto& archives = config.at("archives");
    if (!archives.is_array() || archives.empty() || archives.size() > 4)
        throw mods::Error("Invalid private qualification recipe.");
    for (const auto& archive : archives) {
        const auto patches = mods::read_patch_inputs(mods::utf8_path(archive.get<std::string>()));
        for (const auto& patch : patches) {
            const auto decoded = mods::decode_patch(rom, patch.data);
            const auto patch_digest = mods::sha256(patch.data);
            const auto analysis = mods::analyze(rom, decoded, patch_digest);
            for (const auto& root : analysis.tracks)
                if (root.carrier == carrier) {
                    const auto prepared = mods::prepare_track_bank(rom, decoded, patch_digest, carrier);
                    active->admit(
                        {prepared.root, prepared.bank->fingerprint(), patch_digest, analysis.profile, prepared.bank});
                    sequence.push_back(root.content_id);
                    sequence.push_back("");
                }
        }
    }
    if (sequence.empty())
        throw mods::Error("Private qualification recipe matched no prepared courses.");
    menu_roots = active->tracks();
    begin_session(std::move(active));
    enabled = !native_menu;
    if (characters) {
        if (config.value("character_menu", false))
            begin_character_menu();
        else {
            std::array<std::string, 4> requested{};
            const auto slots = config.at("character_slots").get<std::vector<std::string>>();
            if (slots.size() > 4)
                throw mods::Error("Private character slots exceed four players.");
            for (unsigned i = 0; i < slots.size(); ++i)
                if (!slots[i].empty()) {
                    const auto found = std::find_if(characters->characters.begin(), characters->characters.end(),
                                                    [&](const auto& c) { return c.name == slots[i]; });
                    if (found == characters->characters.end())
                        throw mods::Error("Private character slot has no prepared character.");
                    requested[i] = found->id;
                    std::fprintf(stderr,
                                 "[legacy][qualification] P%u must select retail behaviour %u for %s resource proof\n",
                                 i + 1, found->base_character, found->name.c_str());
                }
            begin_character_roster(requested);
        }
    }
    if (native_menu)
        begin_track_menu(private_races);
    std::fprintf(stderr,
                 "[legacy][qualification] prepared %zu custom/stock menu-load steps; online must remain disconnected\n",
                 sequence.size());
}
void qualify_native_menu(std::uint8_t* rdram, recomp_context* ctx, unsigned event, const std::uint32_t* fields,
                         bool after) {
    if (!open_menu)
        return;
    using F = mods::MenuField;
    auto at = [&](F field) {
        return static_cast<gpr>(static_cast<std::int32_t>(fields[static_cast<unsigned>(field)]));
    };
    if (!after && event == 15) {
        // Only the noninteractive preview probe skips character selection.
        // Playable tests must use the native path that assigns controllers,
        // characters and Tracks mode before opening the custom catalogue.
        if (auto_menu && static_cast<std::uint32_t>(ctx->r4) == 0 && !entered_menu)
            ctx->r4 = 15;
        if (static_cast<std::uint32_t>(ctx->r4) == 15 && !entered_menu) {
            entered_menu = true;
            std::fprintf(stderr, "[legacy][menu-probe] entering native Track Select\n");
        }
    }
    if (!after && (event == 12 || event == 14 || event == 15)) {
        std::fprintf(stderr, "[legacy][menu-probe] event=%u argument=%u\n", event, static_cast<unsigned>(ctx->r4));
    }
    if (auto_menu && !after && event == 2) {
        // This private automatic cycle owns guest-menu input only. Idle
        // controller drift or global keyboard focus cannot alter the recipe.
        MEM_W(16, at(F::Buttons)) = 0;
        MEM_H(8, at(F::StickX)) = 0;
        MEM_H(8, at(F::StickY)) = 0;
    }
    // Playable tests never reposition the cursor. The original menu owns the
    // fresh Dino Domain entry and remembered selection on subsequent returns.
    // Only a non-playable automatic preview recipe may drive the cursor.
    if (!auto_menu)
        return;
    if (!entered_menu || (after && event != 1) || (!after && event != 2))
        return;
    if (event == 2 && ++menu_ticks % 360)
        return;
    const bool stock = menu_step % 2;
    const unsigned index = (menu_step / 2) % menu_roots.size();
    const int rows = MEM_H(0, at(F::FutureFunLand)) == -1 ? 4 : 5;
    const auto row = stock ? 0 : rows + static_cast<int>(index / 4);
    const auto col = stock ? 0 : static_cast<int>(index % 4);
    const auto selected_carrier = stock ? MEM_H(0, at(F::StockIDs)) : static_cast<int>(menu_roots[index].carrier);
    const int height = MEM_W(0, at(F::Height));
    MEM_W(0, at(F::CursorX)) = col;
    MEM_W(0, at(F::CursorY)) = row;
    MEM_W(0, at(F::TargetX)) = std::bit_cast<std::uint32_t>(static_cast<float>(col * 320));
    MEM_W(0, at(F::TargetY)) = std::bit_cast<std::uint32_t>(static_cast<float>(-row * height));
    if (event == 1) {
        MEM_W(0, at(F::X)) = MEM_W(0, at(F::TargetX));
        MEM_W(0, at(F::Y)) = MEM_W(0, at(F::TargetY));
    }
    MEM_W(0, at(F::PreviewCarrier)) = selected_carrier;
    MEM_W(0, at(F::LoadedCarrier)) = -1;
    MEM_W(0, at(F::Opacity)) = 32;
    MEM_W(0, at(F::SelectedX)) = -1;
    MEM_W(0, at(F::SelectedY)) = -1;
    ++menu_step;
    std::fprintf(stderr, "[legacy][menu-probe] step=%u row=%d col=%d carrier=%d content=%s\n", menu_step, row, col,
                 selected_carrier, stock ? "stock" : menu_roots[index].content_id.c_str());
}
} // namespace dkr::runtime::legacy
extern "C" void dkr_legacy_qualification_menu_load(std::uint8_t*, recomp_context* ctx) {
    using namespace dkr::runtime::legacy;
    if (!enabled || static_cast<std::int32_t>(ctx->r4) < 0)
        return;
    const auto& id = sequence[next_scene++ % sequence.size()];
    request_scene(id, carrier);
    // Exercise the real background/menu loader with an ordinary no-racer
    // preview. Only this explicitly compiled development probe changes args.
    ctx->r4 = carrier;
    ctx->r5 = static_cast<gpr>(-1);
    ctx->r6 = 1;
    std::fprintf(stderr, "[legacy][qualification] step=%u requested=%s\n", next_scene,
                 id.empty() ? "stock" : id.c_str());
}
