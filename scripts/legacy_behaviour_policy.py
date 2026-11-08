"""Closed legacy executable components, emitted only by the Patch Pipeline.

Original instructions remain intact. Overrides run after the reviewed load,
and only a currently admitted custom course can request the replacement value.
No retail unlock globals, files, assets or protected generated source edited.
"""

import copy
import struct

from compose_legacy_mod_policy import elf_functions, elf_sections
from legacy_character_presentation_policy import reviewed_revision

SITES = ((0x8000C928, 0x8D030000, 3), (0x8000C92C, 0x9450000C, 16), (0x8000C974, 0x8D090000, 9))
STATS = {
    "us.v77": (
        ("update_player_racer", 0x8004E244, 0xC5480000, 8, 0),
        ("update_player_racer", 0x8004E2A8, 0xC5E40000, 4, 1),
        ("update_player_racer", 0x8004E2D4, 0xC5260000, 6, 2),
        ("update_AI_racer", 0x8005A7BC, 0xC5C40000, 4, 0),
        ("update_AI_racer", 0x8005A820, 0xC5C60000, 6, 1),
        ("update_AI_racer", 0x8005A84C, 0xC5A80000, 8, 2),
    ),
    "us.v80": (
        ("update_player_racer", 0x8004E284, 0xC5480000, 8, 0),
        ("update_player_racer", 0x8004E2E8, 0xC5E40000, 4, 1),
        ("update_player_racer", 0x8004E314, 0xC5260000, 6, 2),
        ("update_AI_racer", 0x8005A828, 0xC5C40000, 4, 0),
        ("update_AI_racer", 0x8005A88C, 0xC5C60000, 6, 1),
        ("update_AI_racer", 0x8005A8B8, 0xC5A80000, 8, 2),
    ),
}


def compose_behaviour(policy, elf, revision):
    if reviewed_revision(elf) != revision:
        raise ValueError("Behaviour bridge requires the pinned retail ELF")
    symbols = elf_functions(elf).get("track_spawn_objects", set())
    if len(symbols) != 1:
        raise ValueError("Behaviour spawn function has ambiguous bounds")
    begin, size = next(iter(symbols))
    words = {
        base + i: struct.unpack_from(">I", data, i)[0]
        for base, data in elf_sections(elf)
        for i in range(0, len(data), 4)
    }
    result = copy.deepcopy(policy)
    for event, (pc, word, reg) in enumerate(SITES):
        if not begin <= pc or pc + 8 > begin + size or words.get(pc) != word or pc + 4 not in words:
            raise ValueError("Behaviour spawn register/instruction boundary changed")
        hook = dict(
            function="track_spawn_objects",
            beforeVram=hex(pc + 4),
            text=(
                "extern uint32_t dkr_legacy_course_component(uint8_t*, recomp_context*, unsigned, uint32_t); "
                f"ctx->r{reg} = (gpr)(int32_t)dkr_legacy_course_component(rdram, ctx, {event}U, (uint32_t)ctx->r{reg});"
            ),
            reason="Execute only the admitted course's reviewed constant-load component; native instructions and original unlock/save state retained.",
        )
        owners = [h for h in result.setdefault("functionHooks", []) if int(h["beforeVram"], 0) == pc + 4]
        if any(int(p["vram"], 0) in (pc, pc + 4) for p in result.get("instructionPatches", [])):
            raise ValueError("Behaviour bridge conflicts with an instruction owner")
        if owners:
            if owners != [hook]:
                raise ValueError("Behaviour bridge conflicts with a hook owner")
        else:
            result["functionHooks"].append(hook)
    functions = elf_functions(elf)
    name = "obj_tex_animate"
    bounds = functions.get(name, set())
    if len(bounds) != 1:
        raise ValueError("Texture animation entry has ambiguous bounds")
    pc, size = next(iter(bounds))
    if size != 304 or words.get(pc) != 0x27BDFF98 or len(functions.get("tex_animate_texture", set())) != 1:
        raise ValueError("Texture animation entry/callback ABI changed")
    hook = dict(
        function=name,
        beforeVram=hex(pc),
        text=(
            "extern int dkr_legacy_texture_animation(uint8_t*, recomp_context*, recomp_func_t*); "
            "if (dkr_legacy_texture_animation(rdram, ctx, tex_animate_texture)) return;"
        ),
        reason="Reviewed random-texture phase sharing for admitted custom character headers only; original animation/RNG routine and guest checkpoint state retained.",
    )
    owners = [h for h in result["functionHooks"] if int(h["beforeVram"], 0) == pc]
    if any(int(p["vram"], 0) == pc for p in result.get("instructionPatches", [])):
        raise ValueError("Texture animation entry conflicts with an instruction owner")
    if owners:
        if owners != [hook]:
            raise ValueError("Texture animation entry conflicts with a hook owner")
    else:
        result["functionHooks"].append(hook)
    for name, pc, word, reg, stat in STATS[revision]:
        bounds = functions.get(name, set())
        if len(bounds) != 1:
            raise ValueError("Racer parameter function has ambiguous bounds")
        begin, size = next(iter(bounds))
        if not begin <= pc or pc + 8 > begin + size or words.get(pc) != word or pc + 4 not in words:
            raise ValueError("Racer parameter load/register ABI changed")
        hook = dict(
            function=name,
            beforeVram=hex(pc + 4),
            text=(
                "extern uint32_t dkr_legacy_racer_stat(uint8_t*, recomp_context*, unsigned, uint32_t, uint32_t); "
                f"ctx->f{reg}.u32l = dkr_legacy_racer_stat(rdram, ctx, {stat}U, (uint32_t)ctx->r16, ctx->f{reg}.u32l);"
            ),
            reason="Apply finite per-instance custom racer stats immediately after the original table lookup; stock, other donors, ghosts and boss objects unchanged.",
        )
        owners = [h for h in result["functionHooks"] if int(h["beforeVram"], 0) == pc + 4]
        if any(int(p["vram"], 0) in (pc, pc + 4) for p in result.get("instructionPatches", [])):
            raise ValueError("Racer parameter bridge conflicts with an instruction owner")
        if owners:
            if owners != [hook]:
                raise ValueError("Racer parameter bridge conflicts with a hook owner")
        else:
            result["functionHooks"].append(hook)
    return compose_analogue_controls(result, elf)


def compose_analogue_controls(policy, elf):
    """Authorised beta-3 hooks at checked native effort-consumption sites.

    Derive every PC from the selected ELF symbol. The original instructions,
    timers, buildup, coast and boosts remain in the generated guest body.
    """
    functions = elf_functions(elf)
    words = {
        base + i: struct.unpack_from(">I", data, i)[0]
        for base, data in elf_sections(elf)
        for i in range(0, len(data), 4)
    }
    result = copy.deepcopy(policy)

    def bounds(name, expected):
        entries = functions.get(name, set())
        if len(entries) != 1 or next(iter(entries))[1] != expected:
            raise ValueError("Analogue driving function boundary changed: " + name)
        return next(iter(entries))[0]

    def hook(name, pc, instruction, text):
        if words.get(pc) != instruction:
            raise ValueError("Analogue driving consumer instruction changed: " + name)
        row = dict(
            function=name,
            beforeVram=hex(pc),
            text=text,
            reason="Apply the admitted recorded analogue request to human vehicle effort; preserve native restrictions, coast and boost ownership.",
        )
        owners = [h for h in result.setdefault("functionHooks", []) if int(h["beforeVram"], 0) == pc]
        if owners and owners != [row] or any(int(p["vram"], 0) == pc for p in result.get("instructionPatches", [])):
            raise ValueError("Analogue driving hook conflicts with another policy")
        if not owners:
            result["functionHooks"].append(row)

    declaration = "extern uint32_t dkr_analogue_driving(uint8_t*, recomp_context*, unsigned, uint32_t, uint32_t); "
    car = bounds("handle_car_velocity_control", 236)
    # The callee preserves A0; consume the completed native buildup only after
    # its JAL + delay slot has returned. The AI call is deliberately a no-op.
    for name, offset in (("func_8004CC20", 0x1F4), ("func_8004F7F4", 0x370), ("update_onscreen_AI_racer", 0x58)):
        entries = functions.get(name, set())
        if len(entries) != 1:
            raise ValueError("Ambiguous analogue car caller: " + name)
        begin, actual_size = next(iter(entries))
        if actual_size <= offset + 8 or words.get(begin + offset) != (0x0C000000 | ((car >> 2) & 0x3FFFFFF)):
            raise ValueError("Analogue car return site changed: " + name)
        hook(
            name,
            begin + offset + 8,
            words[begin + offset + 8],
            declaration + "dkr_analogue_driving(rdram, ctx, 2U, (uint32_t)ctx->r4, 0U);",
        )

    hover = bounds("func_80046524", 10072)
    plane = bounds("func_80049794", 10508)
    for name, begin, offset, word, register, channel in (
        ("func_80046524", hover, 0x354, 0x3C014040, 0, 0),
        ("func_80046524", hover, 0x4E8, 0x44814000, 2, 1),
        ("func_80049794", plane, 0x600, 0x332B4000, 18, 0),
        ("func_80049794", plane, 0x738, 0x862E0000, 2, 1),
    ):
        text = (
            declaration
            + f"ctx->f{register}.u32l = dkr_analogue_driving(rdram, ctx, {channel}U, (uint32_t)ctx->r16, ctx->f{register}.u32l);"
        )
        if name == "func_80049794" and channel == 1:
            # Both original control branches have already stored this local.
            text += " MEM_W(196, ctx->r29) = ctx->f2.u32l;"
        hook(name, begin + offset, word, text)
    return result
