"""Restore the v1.1 model-cache failure invariant in the v1.0 loader.

Project Patch Pipeline only: native model loading and successful cache entries
remain untouched. Verified against the same reviewed retail ELF images as portraits.
"""

import copy
import struct

from legacy_character_presentation_policy import reviewed_revision


def compose_model_cache(policy, elf, revision, sections, symbols):
    if reviewed_revision(elf) != revision:
        raise ValueError("Model cache requires a reviewed retail ELF")
    result = copy.deepcopy(policy)
    words = {base + i: struct.unpack_from(">I", data, i)[0] for base, data in sections for i in range(0, len(data), 4)}

    def one(name):
        matches = symbols.get(name, set())
        if len(matches) != 1:
            raise ValueError("Ambiguous model-cache symbol: " + name)
        return next(iter(matches))

    def hook(name, pc, expected, text):
        start, size = one(name)
        if not start <= pc < start + size or words.get(pc) != expected:
            raise ValueError("Model-cache instruction changed: " + name)
        if any(int(h["beforeVram"], 0) == pc for h in result["functionHooks"]) or any(
            int(p["vram"], 0) == pc for p in result.get("instructionPatches", [])
        ):
            raise ValueError("Conflicting model-cache patch")
        result["functionHooks"].append(
            dict(
                function=name,
                beforeVram=hex(pc),
                text=text,
                reason="Failed model loads must not leave a phantom cache entry; preserve successful native allocations and ownership.",
            )
        )

    # Retail logs a warning then dereferences NULL. Callers already handle a
    # failed instance; propagate NULL without a host access violation instead.
    hook("model_instance_init", one("model_instance_init")[0], 0x27BDFFE0, "if (ctx->r4 == 0) { ctx->r2 = 0; return; }")
    heap = 0x80070B50 if revision == "us.v77" else 0x80070D90
    hook(
        "mempool_init_main",
        heap,
        0x01E42823,
        "extern void dkr_legacy_heap_capacity(uint8_t*, recomp_context*); dkr_legacy_heap_capacity(rdram, ctx);",
    )
    # The retail HUD silently returns when a texture/sprite/model cannot load.
    # Observe only that failed-load branch; do not change its draw state.
    hud_failure = 0x800AA7AC if revision == "us.v77" else 0x800AAD08
    hook(
        "hud_element_render",
        hud_failure,
        0,
        "if (ctx->r15 == 0) { extern void dkr_hud_asset_load_failed(uint8_t*, recomp_context*); dkr_hud_asset_load_failed(rdram, ctx); }",
    )
    if revision == "us.v77":
        count, count_size = one("gModelCacheCount")
        free, free_size = one("D_8011D634")
        if count_size != 4 or free_size != 4:
            raise ValueError("Model-cache counter sizes changed")
        hook(
            "object_model_init",
            0x8005F99C,
            0x27BDFFA8,
            f"int32_t dkr_model_count_before = MEM_W(0, (int32_t)0x{count:08x}U); "
            f"int32_t dkr_model_free_before = MEM_W(0, (int32_t)0x{free:08x}U);",
        )
        # Includes model allocation, texture, normals, animation and instance
        # failures. Cache hits do not change these counters. Snapshot locals
        # are invocation-owned, not shared state across guest threads/calls.
        hook(
            "object_model_init",
            0x8005FCB4,
            0x8FBF0024,
            f"if (ctx->r2 == 0) {{ MEM_W(0, (int32_t)0x{count:08x}U) = dkr_model_count_before; "
            f"MEM_W(0, (int32_t)0x{free:08x}U) = dkr_model_free_before; }}",
        )
    return result
