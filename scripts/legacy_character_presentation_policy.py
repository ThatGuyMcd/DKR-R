"""Checked, additive per-racer portrait/voice hooks; no generated C edits.

Only the two retail ELF images are admitted (see elf_image_digest). The native
lookup/call instructions remain unchanged: callbacks redirect their local arguments.
"""

import copy
import hashlib
import struct

ELFS = {
    "us.v77": "1903f3d12581a3beb7288f32094dfe60ca766ada637de9d7bb27044146a25316",
    "us.v80": "1d8cf535e78e64ce5848338b13fd0a4674c3f44d68c9bdebec228d40e346a47c",
}


def elf_image_digest(path):
    """SHA-256 of the ELF's loaded image: every SHF_ALLOC section's type,
    address, size and (unless NOBITS) contents. The symbol and string tables
    are excluded: they vary with splat/binutils versions while the code and
    data, and therefore the ROM, stay byte-identical."""
    data = path.read_bytes()
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x02":
        raise ValueError("Expected a big-endian ELF32 DKR image")
    start = struct.unpack_from(">I", data, 32)[0]
    stride, count = struct.unpack_from(">HH", data, 46)
    if stride != 40 or start + stride * count > len(data):
        raise ValueError("Invalid ELF section directory")
    digest = hashlib.sha256()
    for index in range(count):
        _, kind, flags, address, offset, size, *_ = struct.unpack_from(">10I", data, start + index * stride)
        if not flags & 2:  # SHF_ALLOC
            continue
        digest.update(struct.pack(">III", kind, address, size))
        if kind != 8:  # SHT_NOBITS
            if offset + size > len(data):
                raise ValueError("Invalid allocated ELF section")
            digest.update(data[offset : offset + size])
    return digest.hexdigest()


def reviewed_revision(path):
    """The retail revision whose reviewed image this ELF matches, or None."""
    digest = elf_image_digest(path)
    return next((revision for revision, expected in ELFS.items() if expected == digest), None)


# function, address, Settings+racer*24 register, lookup-address register, lw word
PORTRAITS = {
    "us.v77": [
        ("postrace_load", 0x80094AE8, 10, 13, 0x8DAE0000),
        ("postrace_load", 0x80094B44, 2, 10, 0x8D4B0000),
        ("results_render", 0x80096B94, 16, 25, 0x8F250000),
        ("func_80098774", 0x80098984, 3, 25, 0x8F2E0000),
        ("func_80098774", 0x800989C0, 15, 14, 0x8DCF0000),
    ],
    "us.v80": [
        ("postrace_load", 0x80094FEC, 10, 13, 0x8DAE0000),
        ("postrace_load", 0x80095048, 2, 10, 0x8D4B0000),
        ("results_render", 0x800970BC, 16, 11, 0x8D650000),
        ("func_80098774", 0x80098EC0, 3, 25, 0x8F2E0000),
        ("func_80098774", 0x80098EFC, 15, 14, 0x8DCF0000),
    ],
}
# Call site, owner expression, HUD argument adjustment, original delay slot.
HUD_CALLS = {
    "us.v77": [
        ("hud_eggs_portrait", 0x800A1A6C, "ctx->r19", 0x640, 0x24E70640),
        ("hud_lives_render", 0x800A2380, "MEM_W(ctx->r29, 0x20)", 0x640, 0x24E70640),
        ("hud_treasure", 0x800A46BC, "ctx->r22", 0x640, 0x24E70640),
        ("hud_main_hub", 0x800A275C, "1U", 0, 0xA4F90006),
    ],
    "us.v80": [
        ("hud_eggs_portrait", 0x800A1FAC, "ctx->r19", 0x640, 0x24E70640),
        ("hud_lives_render", 0x800A28C0, "MEM_W(ctx->r29, 0x20)", 0x640, 0x24E70640),
        ("hud_treasure", 0x800A4C04, "ctx->r22", 0x640, 0x24E70640),
        ("hud_main_hub", 0x800A2C9C, "1U", 0, 0xA4F90006),
    ],
}
# PAL, unit-scale (batched/immediate), and arbitrary-scale texture lookups.
# Finish-position digits and all layout instructions remain unmodified.
HUD_LOADS = {
    "us.v77": [(0x800AAB2C, 12, 0x8D820000), (0x800AABD8, 14, 0x8DC30000), (0x800AAD30, 24, 0x8F020000)],
    "us.v80": [(0x800AB088, 12, 0x8D820000), (0x800AB134, 14, 0x8DC30000), (0x800AB28C, 24, 0x8F020000)],
}
CINEMATIC = {"us.v77": (0x800993D8, 0x8009AE94), "us.v80": (0x80099914, 0x8009B3D0)}


def compose_presentation(policy, elf, revision, sections, symbols, *, extended=True):
    if reviewed_revision(elf) != revision:
        raise ValueError("Custom presentation requires a reviewed retail ELF")
    result = copy.deepcopy(policy)
    words = {base + i: struct.unpack_from(">I", data, i)[0] for base, data in sections for i in range(0, len(data), 4)}

    def one(name):
        values = symbols.get(name, set())
        if len(values) != 1:
            raise ValueError("Ambiguous presentation function " + name)
        return next(iter(values))

    def hook(name, pc, text, expected):
        start, size = one(name)
        if pc % 4 or not start <= pc < start + size or words.get(pc) != expected:
            raise ValueError("Presentation instruction changed: " + name)
        if any(int(p["vram"], 0) == pc for p in result.get("instructionPatches", [])):
            raise ValueError("Presentation overlaps an instruction patch")
        owners = [h for h in result["functionHooks"] if int(h["beforeVram"], 0) == pc]
        if owners:
            # The only shared entry is the already-validated selection-sound
            # adapter. Its handle-scoped path remains first and unchanged.
            if (
                name != "sound_play"
                or len(owners) != 1
                or owners[0]["function"] != name
                or not owners[0]["text"].startswith("{ extern int dkr_legacy_character_menu(")
                or not owners[0]["text"].endswith("8U, dkr_character_menu_fields)) return; }")
            ):
                raise ValueError("Unreviewed presentation hook owner: " + name)
            owners[0]["text"] += " " + text
        else:
            result["functionHooks"].append(
                {
                    "function": name,
                    "beforeVram": hex(pc),
                    "text": text,
                    "reason": "Session-owned custom portraits/voices for committed human racers only; preserve native layout, RNG, gating and audio lifetimes.",
                }
            )

    for name, pc, subject, lookup, expected in PORTRAITS[revision]:
        hook(
            name,
            pc,
            "{ extern uint32_t dkr_legacy_character_portrait_lookup(uint8_t*, recomp_context*, uint32_t); "
            f"uint32_t cell = dkr_legacy_character_portrait_lookup(rdram, ctx, (uint32_t)ctx->r{subject}); "
            f"if (cell) ctx->r{lookup} = (int32_t)cell; }}",
            expected,
        )
    voice, horn = (0x800571F0, 0x8005708C) if revision == "us.v77" else (0x80057230, 0x800570CC)
    bonus, banana = (0x8003B30C, 0x8003DB10) if revision == "us.v77" else (0x8003B34C, 0x8003DB50)
    voices = [
        ("play_random_character_voice", voice, "audspat_play_sound_at_position", 18, 8, 0x3104FFFF),
        ("racer_play_sound", horn, "sound_play_spatial", 2, 4, 0xAFA00010),
    ]
    if extended:
        voices += [
            ("obj_loop_bonus", bonus, "sound_play_spatial", 17, 9, 0x01202025),
            ("obj_loop_banana", banana, "sound_play_spatial", 8, 4, 0xAFA00010),
        ]
    for name, pc, target, racer, value, delay in voices:
        if words.get(pc + 4) != delay:
            raise ValueError("Race voice delay-slot ownership changed")
        hook(
            name,
            pc,
            "{ extern unsigned dkr_legacy_character_race_sound(uint8_t*, recomp_context*, uint32_t, unsigned); "
            f"ctx->r{value} = dkr_legacy_character_race_sound(rdram, ctx, (uint32_t)ctx->r{racer}, (unsigned)ctx->r{value}); }}",
            0x0C000000 | ((one(target)[0] >> 2) & 0x3FFFFFF),
        )
    if extended and words.get(one("hud_element_render")[0]) != 0x27BDFF48:
        raise ValueError("HUD renderer stack ownership changed")
    for name, pc, owner, offset, delay in HUD_CALLS[revision] if extended else []:
        if words.get(pc + 4) != delay:
            raise ValueError("HUD portrait delay-slot ownership changed")
        hook(
            name,
            pc,
            "{ extern void dkr_legacy_character_hud_bind(uint8_t*, recomp_context*, uint32_t, uint32_t); "
            f"dkr_legacy_character_hud_bind(rdram, ctx, (uint32_t)ctx->r7 + {offset}U, (uint32_t)({owner})); }}",
            0x0C000000 | ((one("hud_element_render")[0] >> 2) & 0x3FFFFFF),
        )
        hook(
            name,
            pc + 8,
            "{ extern void dkr_legacy_character_hud_unbind(uint8_t*, recomp_context*); "
            "dkr_legacy_character_hud_unbind(rdram, ctx); }",
            words[pc + 8],
        )
    for pc, lookup, expected in HUD_LOADS[revision] if extended else []:
        hook(
            "hud_element_render",
            pc,
            "{ extern uint32_t dkr_legacy_character_hud_lookup(uint8_t*, recomp_context*, uint32_t, uint32_t); "
            "uint32_t cell = dkr_legacy_character_hud_lookup(rdram, ctx, (uint32_t)ctx->r29 + 0xB8U, (uint32_t)ctx->r16); "
            f"if (cell) ctx->r{lookup} = (int32_t)cell; }}",
            expected,
        )
    if extended:
        capture, draw = CINEMATIC[revision]
        if words.get(capture - 4) != 0x80820059:
            raise ValueError("Cinematic owner load changed")
        hook(
            "menu_trophy_race_rankings_loop",
            capture,
            "{ extern unsigned dkr_legacy_character_cinematic_id(uint8_t*, recomp_context*, unsigned, unsigned); "
            "ctx->r2 = dkr_legacy_character_cinematic_id(rdram, ctx, (unsigned)ctx->r14, (unsigned)ctx->r2); }",
            0x14600005,
        )
        hook(
            "menu_cinematic_loop",
            draw,
            "{ extern uint32_t dkr_legacy_character_cinematic_portrait(uint8_t*, recomp_context*, unsigned); "
            "uint32_t cell = dkr_legacy_character_cinematic_portrait(rdram, ctx, (unsigned)ctx->r17); "
            "if (cell) ctx->r24 = (int32_t)cell; }",
            0x8F050000,
        )
    for kind, name in enumerate(("audspat_play_sound_at_position", "sound_play_direct", "sound_play")):
        pc = one(name)[0]
        hook(
            name,
            pc,
            "{ extern int dkr_legacy_character_play_sound(uint8_t*, recomp_context*, unsigned); "
            f"if (dkr_legacy_character_play_sound(rdram, ctx, {kind}U)) return; }}",
            words[pc],
        )
    return result


def refresh_presentation(policy, elf, revision, sections, symbols):
    """Replace only an exact, known presentation layer in a composed policy.

    The committed base now includes the legacy adapter. Re-composing that whole
    adapter would duplicate its hooks. Strip only this layer's verified text,
    preserving shared owners and every unrelated patch byte-for-byte.
    """
    callbacks = (
        "dkr_legacy_character_portrait_lookup",
        "dkr_legacy_character_race_sound",
        "dkr_legacy_character_play_sound",
        "dkr_legacy_character_hud_",
        "dkr_legacy_character_cinematic_",
    )
    if not any(any(c in h["text"] for c in callbacks) for h in policy["functionHooks"]):
        return compose_presentation(policy, elf, revision, sections, symbols)
    for extended in (True, False):
        expected = compose_presentation(
            {"functionHooks": [], "instructionPatches": []}, elf, revision, sections, symbols, extended=extended
        )
        result = copy.deepcopy(policy)
        for old in expected["functionHooks"]:
            matches = [
                h
                for h in result["functionHooks"]
                if h["function"] == old["function"] and int(h["beforeVram"], 0) == int(old["beforeVram"], 0)
            ]
            if len(matches) != 1:
                break
            h = matches[0]
            if h["text"] == old["text"]:
                result["functionHooks"].remove(h)
            elif old["function"] == "sound_play" and h["text"].endswith(" " + old["text"]):
                h["text"] = h["text"][: -len(old["text"]) - 1]
            else:
                break
        else:
            return compose_presentation(result, elf, revision, sections, symbols)
    raise ValueError("Existing custom presentation is not an exact reviewed layer")
