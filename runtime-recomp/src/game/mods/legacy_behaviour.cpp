#include "legacy_behaviour.hpp"
#include "legacy_mod_format.hpp"
#include <algorithm>
#include <limits>
#include <json/json.hpp>
#include <set>
#include <bit>
#include <cmath>

namespace dkr::mods {
unsigned linked_behaviour_profile(View prefix) {
    if (prefix.size() != 922160 && prefix.size() != 922176 && prefix.size() != 922320)
        return 0;
    static constexpr std::array<std::pair<const char*, unsigned>, 18> profiles{
        {{"c289788e282a9fd5478d01cb3a1a9cb743df987f6e79ec14bb8a6e04e4f315ce", 1},
         {"5937a08d3b050f93ae79a2cf785795554848b0a5acfc466a3cae44cbfea761ad", 1},
         {"8653939aca0ee4ea519d8cbaaed38e8e75ff9f6a6030a08ad7d75168f28487b2", 1},
         {"1e72caf9b43c5a56a3e2bf0b2685c24c5683df54c76c021d09f9dfd3ffb4f6d1", 2},
         {"115bf6e94a28922c9804311b31731d5dacbd1fcf4f2ac3d651beb819d7d435ef", 2},
         {"8ac840253e41acea8bd03e304dff50729853b37a41e8c0cc939f9d2170b5ad8b", 1},
         {"465c9218ffcdebac7e083f546d6a14d4db09fcf838ad1964db194748b0ab96d6", 1},
         {"f0ed8aab4237439c6de25ffea9548e5245674cb91950776c5b3b538c4d92666c", 2},
         {"dde92418853c43cac637ca870952e7fb1db346021388815ae393577ea3d63cd2", 1},
         {"9935a7d929891191289152863d8646e7ff8fd77acd6f971cd6d68e749dd0fce2", 1},
         {"4ed843e944c96435b338f269509b2df3c0be3a536e423e975a99f542d9c48c9c", 2},
         {"cdd23f8c096d09e0bc662e4b0355aad56a4bb97ea7c0b1ce7a825eb1238c20c9", 2},
         {"36ee94acfc2aa9e3a5f1ee64a8d8ef614694e3289dc2c14c925d456590792b6f", 2},
         {"b7a1e05ef4e4122e91277ec8c1569236f2bc24d11add46c3cd18bfb27ed14087", 1},
         {"cffbeb9f783b962c65deb307e6950b59efe9a2753d6a594480ba41fd597a8227", 1},
         {"4d706919054e863dc76b292d84bcd0e472fc472c9ecd0ab252169e9421d817a2", 2},
         {"90108555765b3aee4e76c15040467e9e00deee7e8776409f578bd1943258be72", 2},
         {"3ef57b4dd18d548f03f3b32eb003bc9f58d77287a04b8feae2ee0bddb00eb3d0", 1}}};
    const auto hash = sha256(prefix);
    for (const auto& [digest, profile] : profiles)
        if (hash == digest)
            return profile;
    return 0;
}
const std::array<BehaviourSite, BehaviourSites>& behaviour_sites() {
    static constexpr std::array<BehaviourSite, BehaviourSites> sites{
        {{"track_spawn_objects", 0x8000c928, 0x8d030000, 0x24030007, 0x8000c928, 0x8d030000},
         {"track_spawn_objects", 0x8000c92c, 0x9450000c, 0x2410ffff, 0x8000c92c, 0x9450000c},
         {"track_spawn_objects", 0x8000c974, 0x8d090000, 0x24090007, 0x8000c974, 0x8d090000},
         {"trackmenu_set_records", 0x80082988, 0x94cb000e, 0x240bffff, 0x80082dd8, 0x94cb000e},
         {"trackmenu_set_records", 0x800829a0, 0x94c9000c, 0x2409ffff, 0x80082df0, 0x94c9000c},
         {"menu_track_select_init", 0x8008ed98, 0x8d2a0000, 0x240a0007, 0x8008f250, 0x8d2a0000},
         {"menu_track_select_init", 0x8008edf0, 0x8dcf0000, 0x240f0007, 0x8008f2a8, 0x8dcf0000},
         {"menu_track_select_init", 0x8008ee20, 0x9689000c, 0x2409ffff, 0x8008f2d8, 0x9689000c},
         {"menu_track_select_init", 0x8008ee54, 0x968e0008, 0x240e001f, 0x8008f30c, 0x968e0008},
         {"trackmenu_render_names", 0x800900f8, 0x95cf000e, 0x240fffff, 0x800905b0, 0x95cf000e},
         {"trackmenu_render_names", 0x8009012c, 0x8dcf0000, 0x240f0007, 0x800905e4, 0x8dcf0000},
         {"trackmenu_setup_render", 0x80091968, 0x8d8d0000, 0x240d00ff},
         {"trackmenu_setup_render", 0x80091f70, 0x8dd90000, 0x241900ff},
         {"trackmenu_setup_render", 0x80091fc8, 0x8dee0000, 0x240e00ff},
         {"func_80092188", 0x80092638, 0x8dcf0000, 0x240f00ff},
         {"is_tt_unlocked", 0x8009ecbc, 0x8c42fd98, 0x3402ffff, 0x8009f200, 0x8c420318},
         {"is_drumstick_unlocked", 0x8009ecd4, 0x8c42fd98, 0x3402ffff, 0x8009f218, 0x8c420318}}};
    return sites;
}
void validate_behaviour(const BehaviourContract& value) {
    if (!value.abi) {
        if (!value.evidence.empty() || value.changed_bytes || value.unresolved_bytes || value.stat_mask ||
            value.header_fields || value.native_profile ||
            std::any_of(value.stat_bits.begin(), value.stat_bits.end(), [](auto bits) { return bits != 0; }) ||
            std::any_of(value.instructions.begin(), value.instructions.end(), [](auto word) { return word != 0; }))
            throw Error("An asset-only mod contains an unversioned behaviour contract.");
        return;
    }
    if (value.abi != BehaviourAbi || value.evidence.size() != 64 || value.changed_bytes > MaxImage ||
        value.unresolved_bytes > value.changed_bytes ||
        !std::all_of(value.evidence.begin(), value.evidence.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        throw Error("This mod requires a different reviewed behaviour ABI.");
    for (unsigned i = 0; i < BehaviourSites; ++i)
        if (value.instructions[i] && value.instructions[i] != behaviour_sites()[i].replacement)
            throw Error("A legacy executable component is outside its reviewed instruction contract.");
    if (value.stat_mask > 7)
        throw Error("Unsupported per-racer parameter fields.");
    if (value.header_fields > 511)
        throw Error("Unsupported object parameter fields.");
    if (value.native_profile > 2 ||
        (value.native_profile &&
         (!value.changed_bytes || value.unresolved_bytes ||
          std::any_of(value.instructions.begin(), value.instructions.end(), [](auto word) { return word != 0; }))))
        throw Error("Unsupported rebuilt-code compatibility profile.");
    for (unsigned i = 0; i < 3; ++i) {
        const auto number = std::bit_cast<float>(value.stat_bits[i]);
        if (!(value.stat_mask & (1U << i))) {
            if (value.stat_bits[i])
                throw Error("Inactive racer parameter has data.");
            continue;
        }
        if (!std::isfinite(number) || number < 0 || number > (i == 0 ? 100.0f : 65536.0f) || (i == 0 && number == 0))
            throw Error("A custom racer parameter is outside its finite native budget.");
    }
}
std::string behaviour_identity(const BehaviourContract& value) {
    validate_behaviour(value);
    if (!value.abi)
        return {};
    std::string canonical = "dkr-legacy-behaviour-1:" + value.evidence + ":" + std::to_string(value.changed_bytes) +
                            ":" + std::to_string(value.unresolved_bytes);
    for (auto word : value.instructions)
        canonical += ":" + std::to_string(word);
    canonical += ":stats," + std::to_string(value.stat_mask);
    for (auto bits : value.stat_bits)
        canonical += "," + std::to_string(bits);
    canonical += ":headers," + std::to_string(value.header_fields);
    // Keep all previously frozen ABI-1 identities byte-for-byte stable.
    if (value.native_profile)
        canonical += ":native-profile," + std::to_string(value.native_profile);
    return sha256(View(reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size()));
}
std::string behaviour_summary(const BehaviourContract& value) {
    validate_behaviour(value);
    if (!value.abi)
        return "ASSET-ONLY: no executable differences detected. Native gameplay retained.";
    const auto count =
        std::count_if(value.instructions.begin(), value.instructions.end(), [](auto word) { return word != 0; });
    std::string out = value.unresolved_bytes ? "PARTIAL COMPATIBILITY: " : "NATIVE-ADAPTED: ";
    if (value.native_profile)
        out +=
            "The complete rebuilt code/data prefix matches a reviewed reference profile; linker relocation, cartridge checksum and authored title-name differences do not require executing a second copy of the game. ";
    if (value.native_profile == 2)
        out +=
            "Repeated random-animated texture batches share the authored phase/direction on this custom character only, using checkpointed native texture/RNG state. ";
    if (value.stat_mask)
        out += "Authored racer parameters are scoped to this custom racer (human and AI). ";
    if (value.header_fields)
        out +=
            "Authored scale/shadow/lighting/draw-distance parameters use owned native-layout object headers; behaviour IDs, pointers and allocation counts remain original. ";
    out +=
        count
            ? "Reviewed access edits detected: custom roster/course access uses the additive menus, and course setup components are scoped to the selected custom course. Original unlock flags and saves are never modified."
        : value.stat_mask || value.header_fields || value.native_profile
            ? "These native adapters do not execute unreviewed ROM code."
            : "No executable component is approved for this patch.";
    if (value.unresolved_bytes)
        out +=
            " " + std::to_string(value.unresolved_bytes) +
            " pre-asset byte differences remain unclassified (including possible linked data/relocations); they are not executed. A behaviour adapter is still required for any additional gameplay changes.";
    else if (count)
        out +=
            " The recognised executable differences are covered by the compatibility contract; whole-game unlock/menu changes are not applied to original content.";
    return out;
}
std::uint64_t execute_behaviour_component(const BehaviourContract& value, unsigned site, std::uint64_t native_value) {
    // Contracts are validated once at preparation/admission, not hashed in a
    // frame loop. The exact opcode check remains at this dispatch boundary.
    if (site >= BehaviourSites)
        throw Error("Unknown legacy behaviour call site.");
    const auto word = value.instructions[site];
    if (!word)
        return native_value;
    if (value.abi != BehaviourAbi || word != behaviour_sites()[site].replacement || ((word >> 21) & 31) != 0)
        throw Error("Legacy behaviour execution failed its closed instruction boundary.");
    const auto op = word >> 26;
    if (op == 9) { // ADDIU rT,r0,imm: 32-bit result sign-extended to the MIPS III GPR.
        const auto immediate = static_cast<std::int16_t>(word & 0xffff);
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(immediate));
    }
    if (op == 13)
        return word & 0xffffU; // ORI rT,r0,imm: zero-extended immediate.
    throw Error("Unsupported instruction in reviewed legacy component.");
}
std::string encode_behaviour(const BehaviourContract& value) {
    validate_behaviour(value);
    nlohmann::json doc = {{"abi", value.abi},
                          {"evidence", value.evidence},
                          {"changed_bytes", value.changed_bytes},
                          {"unresolved_bytes", value.unresolved_bytes},
                          {"instructions", value.instructions},
                          {"stat_mask", value.stat_mask},
                          {"stat_bits", value.stat_bits},
                          {"header_fields", value.header_fields}};
    if (value.native_profile)
        doc["native_profile"] = value.native_profile;
    return doc.dump();
}
BehaviourContract decode_behaviour(const std::string& text) {
    if (text.empty() || text.size() > 2048)
        throw Error("Legacy behaviour metadata exceeds its fixed budget.");
    std::set<std::string> keys;
    const auto doc =
        nlohmann::json::parse(text, [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& value) {
            if (depth > 2)
                throw Error("Legacy behaviour metadata is nested too deeply.");
            if (event == nlohmann::json::parse_event_t::key && !keys.insert(value.get<std::string>()).second)
                throw Error("Duplicate legacy behaviour contract field.");
            return true;
        });
    if (!doc.is_object() || (doc.size() != 8 && !(doc.size() == 9 && doc.contains("native_profile"))) ||
        !doc.contains("abi") || !doc.contains("evidence") || !doc.contains("changed_bytes") ||
        !doc.contains("unresolved_bytes") || !doc.contains("instructions"))
        throw Error("Unsupported legacy behaviour contract fields.");
    const auto number = [](const auto& value) {
        if (!value.is_number_unsigned() || value.template get<std::uint64_t>() > UINT32_MAX)
            throw Error("Invalid legacy behaviour contract integer.");
        return value.template get<unsigned>();
    };
    BehaviourContract out;
    out.abi = number(doc.at("abi"));
    out.evidence = doc.at("evidence").get<std::string>();
    out.changed_bytes = number(doc.at("changed_bytes"));
    out.unresolved_bytes = number(doc.at("unresolved_bytes"));
    const auto& words = doc.at("instructions");
    if (!words.is_array() || words.size() != BehaviourSites)
        throw Error("Incomplete legacy behaviour component table.");
    for (unsigned i = 0; i < BehaviourSites; ++i)
        out.instructions[i] = number(words[i]);
    out.stat_mask = number(doc.at("stat_mask"));
    const auto& stats = doc.at("stat_bits");
    if (!stats.is_array() || stats.size() != 3)
        throw Error("Incomplete per-racer parameter table.");
    for (unsigned i = 0; i < 3; ++i)
        out.stat_bits[i] = number(stats[i]);
    out.header_fields = number(doc.at("header_fields"));
    if (doc.contains("native_profile"))
        out.native_profile = number(doc.at("native_profile"));
    validate_behaviour(out);
    return out;
}
unsigned object_parameter_fields(View original, View target) {
    if (original.size() < 0x78 || target.size() != original.size())
        throw Error("Object header layout needs a separate adapter.");
    constexpr std::array<std::pair<unsigned, unsigned>, 9> fields{
        {{4, 4}, {8, 4}, {12, 4}, {0x28, 4}, {0x2c, 4}, {0x3d, 1}, {0x3e, 2}, {0x40, 2}, {0x4e, 2}}};
    unsigned changed = 0;
    std::array<bool, 0x78> owned{};
    for (unsigned i = 0; i < fields.size(); ++i) {
        const auto [at, size] = fields[i];
        for (unsigned b = 0; b < size; ++b)
            owned[at + b] = true;
        if (std::ranges::equal(slice(original, at, size), slice(target, at, size)))
            continue;
        if (i < 5) {
            const auto number = std::bit_cast<float>(be32(target, at));
            if (!std::isfinite(number) || number < 0 || number > (i >= 3 ? 8.0f : 64.0f) || (i == 2 && number < 0.001f))
                throw Error("Object scale/lighting parameter exceeds its finite native budget.");
        } else if (i == 8 && static_cast<std::int16_t>(be16(target, at)) < 0)
            throw Error("Negative custom object draw distance needs a separate adapter.");
        changed |= 1U << i;
    }
    for (std::size_t i = 0; i < target.size(); ++i)
        if ((i >= owned.size() || !owned[i]) && target[i] != original[i])
            throw Error("Object pointer/count/particle/behaviour changes require a separate native adapter.");
    return changed;
}
} // namespace dkr::mods
