"""Register retail audio pools through checked Patch Pipeline output only."""

import copy
import struct

from compose_legacy_mod_policy import elf_functions, elf_sections
from legacy_character_presentation_policy import reviewed_revision


def compose_audio_queue(policy, elf, revision):
    if reviewed_revision(elf) != revision:
        raise ValueError("Audio queue ownership requires a reviewed retail ELF")
    symbols = elf_functions(elf, (1, 2))
    matches = symbols.get("alEvtqNew", set())
    if len(matches) != 1:
        raise ValueError("Ambiguous audio queue initialization")
    entry, size = next(iter(matches))
    words = {
        base + i: struct.unpack_from(">I", data, i)[0]
        for base, data in elf_sections(elf)
        for i in range(0, len(data), 4)
    }
    if size < 4 or words.get(entry) != 0x27BDFFD8:
        raise ValueError("Audio queue initialization signature changed")
    hook = dict(
        function="alEvtqNew",
        beforeVram=hex(entry),
        text="extern void dkr_audio_event_queue_initialize(uint8_t*, recomp_context*); "
        "dkr_audio_event_queue_initialize(rdram, ctx);",
        reason="Register the exact retail event pool before initialization; bounded recovery never invents allocation ownership.",
    )
    result = copy.deepcopy(policy)
    hooks = result.setdefault("functionHooks", [])
    existing = [x for x in hooks if int(x["beforeVram"], 0) == entry]
    if existing and existing != [hook]:
        raise ValueError("Conflicting audio queue initialization hook")
    if not existing:
        hooks.append(hook)
    return result
