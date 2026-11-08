"""Portrait correction stage: read-only pinned inputs, NEW outputs only.

The native overlay preserves v2 artifact/save identities and authenticates the
missing portrait from its retained source review. New imports use the correct
table. An explicit legacy preparation mode is solely for frozen online v2 IDs.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
AUTH = ROOT / "patches/character-portraits"
MODS = ROOT / "runtime-recomp/src/game/mods"
PINS = {
    "legacy_mod_format.cpp": "74adeea776dc34cb2516fd14b4578c4f81f048f61f4fd452faa1e1e5d66642b3",
    "legacy_character_materialize.hpp": "bd4b2f1104257de526bbe05cbfa1aac0ec0f3b528f908de55d7ac16e5e992397",
    "legacy_character_materialize.cpp": "6d9657e9dabb748a13f9733e201411b02cb8f94cb1b33f14bca64fa90f4db203",
    "legacy_track_catalog.cpp": "37cf82e6987a5cc06743fc86144eabb5e299f797c37a8ab7e959771c68bd3875",
    "legacy_track_prepare_job.hpp": "2672949857d0bf106ba702a47d153725878f10058934c11ba3d2cf83429e542a",
    "legacy_track_prepare_job.cpp": "716b637ca843b7f1fd87d95bd3dbaaf37df80828fa3adb43e0b7f742cc2a771d",
    "online_mod_prepare.cpp": "73921b793921da7bb0bab3763eb1c42dc40fafa067a01c57155a38533d22cf06",
}
GUEST_PINS = {
    77: "006cc572caf0097392aeccac974dc4574813eeea81c09b6eabc5b1b7a9199842",
    80: "9fc26cc8bb1762db7a78f5979cea65835ccd7c84e6ed325d411ad149f98df070",
}
sha = lambda b: hashlib.sha256(b).hexdigest()


def new_output(path, *inputs):
    path = path.resolve()
    if path.exists() or any(path == p.resolve() or p.resolve() in path.parents for p in inputs):
        raise ValueError("Portrait pipeline output must be NEW and outside every read-only input")
    if any(part.lower() in ("extern", "recompiledfuncs", "recompiledpatches") for part in path.parts):
        raise ValueError("Portrait output must not enter a protected dependency/generated tree")
    return path


def replace(text, old, new, count=1):
    if text.count(old) != count:
        raise ValueError("Unreviewed portrait edit site: " + old[:100])
    return text.replace(old, new)


def transform(name, text):
    if name == "legacy_mod_format.cpp":
        return replace(
            text,
            "portraits[]{122,123,125,126,124,127,129,128,130,131}",
            "portraits[]{122,123,124,126,125,127,128,129,130,131}",
        )
    if name == "legacy_character_materialize.hpp":
        # Only PreparedCharacter grows; AllocatedCharacter's ABI stays intact.
        at = text.index("struct AllocatedCharacter")
        first = replace(
            text[:at],
            "    CharacterRaceAudio race_audio;",
            """    CharacterRaceAudio race_audio;
    // Recovered only by the authenticated source-review loader.
    Bytes source_portrait;
    unsigned source_portrait_id=0;""",
        )
        text = first + text[at:]
        return replace(
            text, "bool behaviour_enabled=true);", "bool behaviour_enabled=true,bool legacy_portrait_layout=false);"
        )
    if name == "legacy_character_materialize.cpp":
        text = replace(
            text,
            "unsigned base_character,bool behaviour_enabled) {",
            "unsigned base_character,bool behaviour_enabled,bool legacy_portrait_layout) {",
        )
        text = replace(
            text,
            "    result.base_fingerprint=sha256(original);",
            """    result.base_fingerprint=sha256(original);
    if(legacy_portrait_layout) {
        constexpr unsigned previous[]{122,123,125,126,124,127,129,128,130,131};
        result.root.portrait=previous[base_character];
    }""",
        )
        text = replace(
            text,
            "        const auto& portrait=character.records.at({4,character.root.portrait});",
            """        constexpr unsigned portrait_ids[]{122,123,124,126,125,127,128,129,130,131};
        const bool recovered=!character.source_portrait.empty();
        if(recovered && (character.source_portrait_id!=portrait_ids[character.root.base_character] || character.source_portrait.size()>4*MiB))
            throw Error("Invalid recovered source portrait.");
        const auto portrait_id=recovered?character.source_portrait_id:character.root.portrait;
        const auto& portrait=recovered?character.source_portrait:character.records.at({4,character.root.portrait});
        validate_texture_record(portrait);""",
        )
        text = replace(
            text,
            "        for(unsigned i=0;i<3;++i)slot.headers[i]=remap.at({34,character.root.headers[i]});",
            """        if(recovered) {
            if(next[4]>=32767)throw Error("Recovered portrait namespace is full.");
            slot.portrait=next[4]++;
            result.additions.emplace(AssetKey{4,slot.portrait},portrait);
        }
        for(unsigned i=0;i<3;++i)slot.headers[i]=remap.at({34,character.root.headers[i]});""",
        )
        text = replace(
            text,
            "slot.custom_portrait=character_portrait_changed(portrait,stock->record(4,character.root.portrait));",
            "slot.custom_portrait=character_portrait_changed(portrait,stock->record(4,portrait_id));",
        )
        return replace(
            text,
            "for(const auto& c:result.characters)roster_ids+=c.id;",
            'for(const auto& c:result.characters)roster_ids+=c.id+":portrait-source-v1:"+c.portrait_identity;',
        )
    if name == "legacy_track_catalog.cpp":
        text = replace(
            text,
            "    std::size_t cached_audio_bytes=0;",
            "    std::size_t cached_audio_bytes=0,source_portrait_bytes=0;",
        )
        anchor = "            std::array<Bytes,3> audio;"
        text = replace(text, anchor, (AUTH / "load_reviewed_portrait.inc").read_text() + "\n" + anchor)
        return replace(
            text,
            "        result.push_back(std::move(character));",
            """        constexpr unsigned correct_portraits[]{122,123,124,126,125,127,128,129,130,131};
        const auto correct_portrait=correct_portraits[character.root.base_character];
        if(character.root.portrait!=correct_portrait && character.source_portrait.empty()) {
            const auto bytes=stock->record(4,correct_portrait);
            character.source_portrait.assign(bytes.begin(),bytes.end());
            character.source_portrait_id=correct_portrait;
        }
        if(character.source_portrait.size()>32*MiB-source_portrait_bytes)
            throw Error("Recovered character portraits exceed the presentation memory budget.");
        source_portrait_bytes+=character.source_portrait.size();
        result.push_back(std::move(character));""",
        )
    if name == "legacy_track_prepare_job.hpp":
        at = text.index("void prepare_imported_characters")
        return text[:at] + replace(
            text[at:],
            "bool behaviour_enabled=true);",
            "bool behaviour_enabled=true,bool legacy_portrait_layout=false);",
        )
    if name == "legacy_track_prepare_job.cpp":
        text = replace(
            text,
            "bool characters,const ImportPreparation* preparation,bool behaviour_enabled) {",
            "bool characters,const ImportPreparation* preparation,bool behaviour_enabled,bool legacy_portrait_layout=false) {",
        )
        text = replace(
            text,
            "prepare_character(base,target,patch_id,root.base_character,behaviour_enabled);",
            "prepare_character(base,target,patch_id,root.base_character,behaviour_enabled,legacy_portrait_layout);",
        )
        at = text.index("void prepare_imported_characters(")
        tail = replace(text[at:], "bool behaviour_enabled) {", "bool behaviour_enabled,bool legacy_portrait_layout) {")
        tail = replace(
            tail,
            "progress,true,preparation,behaviour_enabled);",
            "progress,true,preparation,behaviour_enabled,legacy_portrait_layout);",
        )
        return text[:at] + tail
    if name == "online_mod_prepare.cpp":
        text = replace(
            text,
            "            const auto prepared=job/(std::string(kind==Kind::Track?",
            """            // Old host selections retain v2 IDs/save associations. Rebuild
            // that exact edition only when requested, never accept a mismatch.
            for(const bool previous_portraits:{false,true}) {
            if(previous_portraits && kind!=Kind::Character)continue;
            bool pending=false;
            for(const auto& content:manifest.content)
                if(content.payload==payload.digest && content.kind==kind && (content.behaviour_abi!=0)==enabled &&
                   !found.contains({kind,content.id}))pending=true;
            if(!pending)continue;
            const auto prepared=job/(std::string(previous_portraits?"previous-portraits-":"")+std::string(kind==Kind::Track?""",
        )
        text = replace(
            text,
            "prepare_imported_characters(review,owned_roms,prepared,progress,&preparation,enabled);",
            "prepare_imported_characters(review,owned_roms,prepared,progress,&preparation,enabled,previous_portraits);",
        )
        text = replace(
            text,
            "                if(entry==expected.end() || entry->second.payload!=payload.digest)continue;",
            "                if(entry==expected.end() || entry->second.payload!=payload.digest || found.contains(key))continue;",
        )
        text = replace(
            text,
            "            receipt[entries]=std::move(selected);",
            """            // A corrected pass may have no roots matching an old host ID.
            // Leave that scratch unpublished; empty groups are not catalogues.
            if(selected.empty())continue;
            receipt[entries]=std::move(selected);""",
        )
        return replace(
            text,
            "            }\n        }\n        storage_budget(destination);",
            "            }\n            }\n        }\n        storage_budget(destination);",
        )
    raise ValueError(name)


def native(output):
    sources = ROOT / "runtime-recomp/src"
    output = new_output(output, sources)
    for name, pin in PINS.items():
        if sha((MODS / name).read_bytes()) != pin:
            raise ValueError("Portrait source pin changed: " + name)
    for p in sources.rglob("*"):
        if p.is_symlink():
            raise ValueError("Native source input must not contain links")
    shutil.copytree(sources, output / "src")
    for name in PINS:
        dest = output / "src/game/mods" / name
        dest.write_text(transform(name, (MODS / name).read_text()), newline="\n")
    # Every staged consumer must use the same struct definition. The complete
    # first-party src tree is copied unchanged except the seven pinned files.
    files = {
        p.relative_to(output).as_posix(): sha(p.read_bytes())
        for p in sorted((output / "src").rglob("*"))
        if p.is_file()
    }
    receipt = dict(schema=1, original_pins=PINS, files=files)
    (output / "native-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("Native portrait stage:", output, len(files), "first-party inputs, seven pinned edits")
    symbols(output.parent / "modding")


def symbols(output):
    output = new_output(output, ROOT / "modding")
    for revision in (77, 80):
        rel = Path("reference-us-v" + str(revision)) / "game_symbols.inc"
        source = ROOT / "modding" / rel
        if source.is_symlink() or not source.is_file():
            raise ValueError("Invalid symbol dependency")
        dest = output / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
    print("Read-only source-relative symbol dependencies copied to", output)


def verify_native(output):
    receipt = json.loads((output / "native-receipt.json").read_text())
    if receipt["schema"] != 1 or receipt["original_pins"] != PINS:
        raise ValueError("Unknown portrait stage")
    originals = ROOT / "runtime-recomp/src"
    expected = {p.relative_to(output).as_posix() for p in (output / "src").rglob("*") if p.is_file()}
    if expected != set(receipt["files"]):
        raise ValueError("Portrait stage file set changed")
    for rel, digest in receipt["files"].items():
        file = output / rel
        if file.is_symlink() or sha(file.read_bytes()) != digest:
            raise ValueError("Portrait staged file changed: " + rel)
        source = originals / Path(rel).relative_to("src")
        name = source.name
        if source.parent == MODS and name in PINS:
            if sha(source.read_bytes()) != PINS[name] or file.read_text() != transform(name, source.read_text()):
                raise ValueError("Unreviewed portrait source difference: " + rel)
        elif file.read_bytes() != source.read_bytes():
            raise ValueError("Unrelated native source changed: " + rel)
    for revision in (77, 80):
        rel = Path("reference-us-v" + str(revision)) / "game_symbols.inc"
        copy = output.parent / "modding" / rel
        if copy.is_symlink() or copy.read_bytes() != (ROOT / "modding" / rel).read_bytes():
            raise ValueError("Source-relative game symbols differ from the checked online contract")
    print("Portrait native stage matches authored pipeline and current first-party inputs")


def guest(source, output, revision, config, output_config, native_stage):
    from save_layout_pipeline import DEF

    source = source.resolve()
    output = new_output(output, source)
    output_config = new_output(output_config, source, config)
    for path in source.rglob("*"):
        if path.is_symlink():
            raise ValueError("Guest portrait input cannot contain links")
    edits = {}
    found = None
    for file in source.glob("*.c"):
        text = file.read_text()
        matches = list(DEF.finditer(text))
        for i, m in enumerate(matches):
            if m[1] != "obj_loop_characterflag":
                continue
            if found:
                raise ValueError("Duplicate world portrait function")
            end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            body = text[m.start() : end]
            if sha(body.encode()) != GUEST_PINS[revision]:
                raise ValueError("Unreviewed CharacterFlag guest instructions")
            replacement = replace(
                body,
                "    MEM_W(0X24, ctx->r3) = ctx->r11;",
                (AUTH / "world_portrait.inc").read_text() + "\n    MEM_W(0X24, ctx->r3) = ctx->r11;",
            )
            edits[file.name] = text[: m.start()] + replacement + text[end:]
            found = dict(original_sha256=sha(body.encode()), patched_sha256=sha(replacement.encode()))
    if not found:
        raise ValueError("Missing in-world portrait guest function")
    shutil.copytree(source, output)
    for name, text in edits.items():
        (output / name).write_text(text, newline="\n")
    receipt = dict(
        schema=1,
        revision=revision,
        function=found,
        native_sha256=sha((native_stage / "native-receipt.json").read_bytes()),
        world_source_sha256=sha((AUTH / "world_portrait.inc").read_bytes()),
    )
    (output / "character-portrait-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    content = config.read_text().replace(source.resolve().as_posix(), output.resolve().as_posix())
    address = {77: "80035FE4", 80: "80036024"}[revision]
    # Protect the entire function from code-mod replacement, like other port hooks.
    content += (
        '\n# In-world custom participant portraits\n[[patches.hook]]\nfunc = "obj_loop_characterflag"\nbefore_vram = 0x'
        + address
        + '\ntext = "/* dkr_character_portrait_pipeline: protected source hook */"\n'
    )
    output_config.write_text(content, newline="\n")
    print("World portrait guest stage v" + str(revision) + ":", output)


def owned_receipt(source, revision):
    path = source / "character-portrait-receipt.json"
    if not path.exists():
        return {}
    receipt = json.loads(path.read_text())
    if (
        receipt["schema"] != 1
        or receipt["revision"] != revision
        or receipt["function"]["original_sha256"] != GUEST_PINS[revision]
    ):
        raise ValueError("Unreviewed portrait guest provenance")
    if receipt["world_source_sha256"] != sha((AUTH / "world_portrait.inc").read_bytes()):
        raise ValueError("World portrait source has changed")
    native_stage = source.parent / "native"
    verify_native(native_stage)
    if receipt["native_sha256"] != sha((native_stage / "native-receipt.json").read_bytes()):
        raise ValueError("Portrait guest/native stages disagree")
    from save_layout_pipeline import DEF

    found = []
    for file in source.glob("*.c"):
        text = file.read_text()
        matches = list(DEF.finditer(text))
        for i, m in enumerate(matches):
            if m[1] == "obj_loop_characterflag":
                end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
                found.append(sha(text[m.start() : end].encode()))
    if found != [receipt["function"]["patched_sha256"]]:
        raise ValueError("World portrait guest body changed")
    return receipt


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("native", "verify-native", "guest", "symbols"))
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--source", type=Path)
    p.add_argument("--revision", type=int, choices=(77, 80))
    p.add_argument("--config", type=Path)
    p.add_argument("--output-config", type=Path)
    p.add_argument("--native-stage", type=Path)
    a = p.parse_args()
    if a.mode == "native":
        native(a.output)
    elif a.mode == "verify-native":
        verify_native(a.output)
    elif a.mode == "symbols":
        symbols(a.output)
    else:
        guest(a.source, a.output, a.revision, a.config, a.output_config, a.native_stage)
