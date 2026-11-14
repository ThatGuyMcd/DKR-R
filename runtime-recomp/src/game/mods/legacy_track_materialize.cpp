#include "legacy_track_materialize.hpp"
#include "legacy_audio_bank.hpp"
#include <algorithm>
#include <set>

namespace dkr::mods {
TrackMaterialization prepare_track_bank(Bytes original, View reconstructed, const std::string& patch_digest,
                                        unsigned carrier, Bytes runtime_original, bool behaviour_enabled) {
    canonicalize_rom(original);
    const auto analysis = analyze(original, reconstructed, patch_digest, behaviour_enabled);
    if (!analysis.blockers.empty())
        throw Error("This package has no qualified offline preparation profile.");
    const auto found = std::find_if(analysis.tracks.begin(), analysis.tracks.end(),
                                    [carrier](const auto& root) { return root.carrier == carrier; });
    if (found == analysis.tracks.end() || !found->blockers.empty())
        throw Error("Selected custom track failed its structural checks.");
    const AssetImage base(original, analysis.source_revision), target(reconstructed, analysis.source_revision);
    const auto runtime = AssetBank::stock(runtime_original.empty() ? original : std::move(runtime_original));
    const bool crossing = runtime->revision() != analysis.source_revision;
    const auto texture_id = [&](unsigned id) {
        if (!crossing)
            return id;
        // US 1.1 inserts ten 2D textures at 394. The original banks are hash
        // verified, and every mapped source entry is checked below as well.
        if (analysis.source_revision == "us.v77")
            return id < 394 ? id : id + 10;
        if (id >= 394 && id < 404)
            throw Error("This mod uses a v 1.1-only 2D asset with no v 1.0 equivalent.");
        return id < 394 ? id : id - 10;
    };
    if (crossing) {
        if (!std::ranges::equal(base.sections[35], runtime->stock_section(35)) ||
            !std::ranges::equal(target.sections[35], base.sections[35]) ||
            !std::ranges::equal(base.sections[30], runtime->stock_section(30)) ||
            !std::ranges::equal(target.sections[30], base.sections[30]))
            throw Error("Track changes object/animation identities that need a native revision adapter.");
        const auto textures = base.records(4);
        for (unsigned id = 0; id < textures.size(); ++id) {
            if (analysis.source_revision == "us.v80" && id >= 394 && id < 404)
                continue;
            if (!std::ranges::equal(textures[id], runtime->record(4, texture_id(id))))
                throw Error("Native revision texture mapping did not match the verified source assets.");
        }
    }
    const auto dependencies = inspect_track_dependencies(base, target, *found);
    if (!dependencies.blockers.empty())
        throw Error("Selected track dependency: " + dependencies.blockers.front());
    std::set<AssetKey> selected;
    for (const auto& dependency : dependencies.records) {
        if (dependency.section == 34) {
            const auto fields =
                object_parameter_fields(runtime->record(34, dependency.id), target.record(34, dependency.id));
            if (fields && (!behaviour_enabled || (fields & ~found->behaviour.header_fields)))
                throw Error("Track object parameters exceed the selected behaviour adapter.");
            if (fields)
                selected.emplace(34, dependency.id);
            continue;
        }
        if (dependency.changed || crossing)
            selected.emplace(dependency.section, dependency.id);
    }
    // Explicit profile-owned dynamic additions: rocket objects spawn only
    // after pickups, so they aren't members of the placed-object graph.
    if (analysis.profile == "sixtyfour-big-boo-2018") {
        selected.emplace(29, 380);
        for (unsigned id : {306U, 307U, 308U}) {
            validate_texture_record(target.record(4, id));
            selected.emplace(4, id);
        }
        for (auto texture : inspect_model_textures(target.record(29, 380))) {
            const auto before = base.record(2, texture), after = target.record(2, texture);
            if (!std::ranges::equal(before, after)) {
                validate_texture_record(after);
                selected.emplace(2, texture);
            }
        }
    }
    // Header always belongs to the selected public track, even when a patch
    // only changed geometry. Other courses, names and unlocks remain original.
    selected.emplace(23, carrier);
    AssetBank::Overrides overrides;
    for (const auto& key : selected) {
        const auto bytes = target.record(key.first, key.second);
        auto mapped = key;
        Bytes copy(bytes.begin(), bytes.end());
        if (crossing && key.first == 4)
            mapped.second = texture_id(key.second);
        if (crossing && key.first == 12) {
            const auto frames = inspect_sprite_textures(copy);
            const auto first = texture_id(be16(copy, 0));
            for (unsigned i = 0; i < frames.size(); ++i)
                if (texture_id(frames[i]) != first + i)
                    throw Error("Sprite spans a revision-specific texture insertion.");
            copy[0] = first >> 8;
            copy[1] = first;
        }
        if (crossing && key.first == 23) {
            const auto source = base.record(23, key.second), native = runtime->record(23, key.second);
            if (source.size() != copy.size() || native.size() != copy.size())
                throw Error("Level header layout differs between revisions.");
            // Preserve native initialization/padding where the hack did not
            // change the source field. Copy fields atomically, never merge
            // individual bytes of a multibyte value or pointer.
            for (auto [at, size] : {std::pair{0x53U, 1U}, {0x72U, 2U}, {0xa8U, 4U}, {0xb9U, 1U}, {0xc4U, 4U}})
                if (std::ranges::equal(slice(source, at, size), slice(copy, at, size)))
                    std::copy_n(native.begin() + at, size, copy.begin() + at);
        }
        if (key.first == 23 || !std::ranges::equal(copy, runtime->record(mapped.first, mapped.second)))
            if (!overrides.emplace(mapped, std::move(copy)).second)
                throw Error("Two imported assets collide after revision mapping.");
    }
    const auto base_audio = base.records(39), target_audio = target.records(39);
    if (base_audio.size() != target_audio.size() || base_audio.size() < 8)
        throw Error("Audio bank layout requires an additional runtime adapter.");
    // Exporters can reorder the same instruments and sample storage. Resolve
    // every instrument before accepting its sequences, and keep the original
    // banks alive. Character/global SFX changes are not track-owned overlays.
    if ((!std::ranges::equal(base_audio[0], target_audio[0]) || !std::ranges::equal(base_audio[1], target_audio[1])) &&
        !equivalent_music_banks(base_audio[0], base_audio[1], target_audio[0], target_audio[1]))
        throw Error("Custom music changes instruments; a voice-lifetime adapter is required.");
    const auto original_music = inspect_sequence_directory(runtime->record(39, 5));
    const auto custom_music = inspect_sequence_directory(target_audio[5]);
    if (original_music.records.size() != custom_music.records.size())
        throw Error("Expanded music IDs require their own runtime adapter.");
    const auto music_id = target.record(23, carrier)[0x52];
    if (music_id >= custom_music.records.size())
        throw Error("Track references an absent music sequence.");
    TrackMaterialization result;
    result.root = *found;
    // A different song elsewhere in a multi-track patch must not replace a
    // stock jingle or leak into a different custom course's selected bank.
    if (music_id && original_music.records[music_id].digest != custom_music.records[music_id].digest) {
        const auto& seq = custom_music.records[music_id];
        const auto bytes = slice(target_audio[5], seq.offset, seq.length);
        overrides[{39, 5}] =
            build_sequence_bank(runtime->record(39, 5), {{music_id, Bytes(bytes.begin(), bytes.end())}},
                                original_music.maximum_loaded_length);
        selected.emplace(39, 5);
        result.music_sequences.push_back(music_id);
    }
    for (const auto& [key, bytes] : overrides)
        result.included.push_back(key);
    for (const auto& key : analysis.changed_records)
        if (!selected.contains(key))
            result.excluded.push_back(key);
    result.bank = AssetBank::derive(runtime, found->content_id, std::move(overrides));
    result.directory = AssetDirectory::build(result.bank);
    return result;
}
} // namespace dkr::mods
