"""Hash-pinned cache allocation/limit fixes, authored Patch Pipeline only.

Retail writes cache entries before its late overflow checks. Additive assets
must expand both allocations and comparisons; pre-store guards prevent an
unexpected exhaustion from corrupting adjacent guest allocations.
"""

import copy
import struct

from compose_legacy_mod_policy import elf_functions, elf_sections
from legacy_character_presentation_policy import reviewed_revision

# function, pc v77/v80, expected operand, cache kind (textures/sprites/models/free)
ALLOCATIONS = [
    ("tex_init_textures", 0x8007AC84, 0x8007B0D4, 0x240415E0, 0, 8),
    ("tex_init_textures", 0x8007AD48, 0x8007B198, 0x24040320, 1, 8),
    ("allocate_object_model_pools", 0x8005F864, 0x8005FA04, 0x24040230, 2, 8),
    ("allocate_object_model_pools", 0x8005F87C, 0x8005FA1C, 0x24040190, 3, 4),
]
COMPARISONS = {
    "us.v77": [
        ("load_texture", 0x8007B288, 0x2B0102BD, 0x14200003, 24, 0, "<="),
        ("tex_load_sprite", 0x8007C4C4, 0x29C10064, 0x14200003, 14, 1, "<"),
        ("object_model_init", 0x8005FC94, 0x29210046, 0x10200003, 9, 2, "<"),
    ],
    "us.v80": [
        ("load_texture", 0x8007B6D8, 0x2B0102BD, 0x14200003, 24, 0, "<="),
        ("tex_load_sprite", 0x8007C914, 0x29C10064, 0x14200003, 14, 1, "<"),
        ("object_model_init", 0x8005FE98, 0x2B210046, 0x10200004, 25, 2, "<"),
    ],
}
STORES = {
    "us.v77": [
        ("load_texture", 0x8007B104, 0xADB00000, 5, 0),
        ("tex_load_sprite", 0x8007C4EC, 0xAD8B0000, 15, 1),
        ("object_model_init", 0x8005FC78, 0xAF130000, 13, 2),
    ],
    "us.v80": [
        ("load_texture", 0x8007B554, 0xADB00000, 5, 0),
        ("tex_load_sprite", 0x8007C93C, 0xAD8B0000, 15, 1),
        ("object_model_init", 0x8005FE7C, 0xADB20000, 11, 2),
    ],
}
REASON = "Immutable additive asset cache capacities and pre-write bounds; preserve stock limits and prevent adjacent guest-memory corruption."
DECL = "extern uint32_t dkr_legacy_asset_cache_capacity(uint8_t*, recomp_context*, unsigned); "


def compose_asset_capacity(policy, elf, revision, sections=None, symbols=None):
    if reviewed_revision(elf) != revision:
        raise ValueError("Asset cache capacity requires a reviewed retail ELF")
    if sections is None:
        sections = elf_sections(elf)
    if symbols is None:
        symbols = elf_functions(elf)
    words = {base + i: struct.unpack_from(">I", data, i)[0] for base, data in sections for i in range(0, len(data), 4)}
    result = copy.deepcopy(policy)
    result.setdefault("functionHooks", [])
    result.setdefault("instructionPatches", [])

    def checked(name, pc, expected):
        matches = symbols.get(name, set())
        if len(matches) != 1:
            raise ValueError("Ambiguous asset cache symbol: " + name)
        start, size = next(iter(matches))
        if not start <= pc < start + size or words.get(pc) != expected:
            raise ValueError("Asset cache instruction changed: " + name + " " + hex(pc))

    def insert(key, address, entry):
        own = [h for h in result[key] if int(h[address], 0) == int(entry[address], 0)]
        if own:
            if len(own) != 1 or own[0] != entry:
                raise ValueError("Conflicting asset cache patch")
        else:
            result[key].append(entry)

    def hook(name, pc, expected, text, paired=False):
        checked(name, pc, expected)
        if not paired and any(int(p["vram"], 0) == pc for p in result["instructionPatches"]):
            raise ValueError("Conflicting asset cache instruction patch")
        insert("functionHooks", "beforeVram", dict(function=name, beforeVram=hex(pc), text=text, reason=REASON))

    for name, v77, v80, word, kind, stride in ALLOCATIONS:
        pc = v77 if revision == "us.v77" else v80
        checked(name, pc, word)
        # This operand is sometimes in a JAL delay slot. Replace the fixed
        # immediate with an identity operation so the hook's a0 survives the
        # delay instruction; allocator, colour tag, and call remain unchanged.
        hook(
            name, pc, word, DECL + f"ctx->r4 = dkr_legacy_asset_cache_capacity(rdram, ctx, {kind}U) * {stride}U;", True
        )
        insert("instructionPatches", "vram", dict(function=name, vram=hex(pc), value="0x00802025", reason=REASON))
    for name, pc, word, branch, register, kind, op in COMPARISONS[revision]:
        checked(name, pc, word)
        hook(
            name,
            pc + 4,
            branch,
            DECL
            + f"ctx->r1 = (int32_t)ctx->r{register} {op} (int32_t)dkr_legacy_asset_cache_capacity(rdram, ctx, {kind}U);",
        )
    for name, pc, word, register, kind in STORES[revision]:
        hook(
            name,
            pc,
            word,
            "extern void dkr_legacy_asset_cache_guard(uint8_t*, recomp_context*, unsigned, uint32_t); "
            + f"dkr_legacy_asset_cache_guard(rdram, ctx, {kind}U, (uint32_t)ctx->r{register});",
        )
    hook(
        "track_setup_racers",
        0x8000D7A4,
        0xA44F0000,
        "if (ctx->r2 == 0) { extern void dkr_legacy_racer_spawn_failed(uint8_t*, recomp_context*); dkr_legacy_racer_spawn_failed(rdram, ctx); }",
    )
    return result
