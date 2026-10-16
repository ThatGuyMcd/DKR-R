"""Checked offline intro skip hooks; retail menu constructors stay intact."""
import copy
import struct

from compose_legacy_mod_policy import elf_functions, elf_sections
from legacy_character_presentation_policy import reviewed_revision

# Exact retail sites, not a guessed delta between revisions. Title injection
# is after storing the unmodified demo-table base and before reading its entry.
SITES = {
    'us.v77': [('menu_title_screen_init', 0x8008353C, 0x240E0001, 'dkr_skip_intro_audio'),
               ('menu_logo_screen_loop', 0x80082B84, 0x3C0E8000, 'dkr_skip_intro_logos'),
               ('menu_title_screen_init', 0x80083704, 0x80450001, 'dkr_skip_intro_title'),
               ('menu_title_screen_loop', 0x800839E4, 0x27BDFFD0, 'dkr_skip_intro_character_select')],
    'us.v80': [('menu_title_screen_init', 0x80083974, 0x240E0001, 'dkr_skip_intro_audio'),
               ('menu_logo_screen_loop', 0x80082FD4, 0x3C0E8000, 'dkr_skip_intro_logos'),
               ('menu_title_screen_init', 0x80083B3C, 0x80450001, 'dkr_skip_intro_title'),
               ('menu_title_screen_loop', 0x80083E1C, 0x27BDFFD0, 'dkr_skip_intro_character_select')],
}


def compose_startup_flow(policy, elf, revision):
    if reviewed_revision(elf) != revision or revision not in SITES:
        raise ValueError('Intro skip requires a reviewed retail ELF')
    symbols = elf_functions(elf, (1, 2))
    words = {base + i: struct.unpack_from('>I', data, i)[0]
             for base, data in elf_sections(elf) for i in range(0, len(data), 4)}
    result = copy.deepcopy(policy)
    hooks = result.setdefault('functionHooks', [])
    for function, address, expected, callback in SITES[revision]:
        matches = symbols.get(function, set())
        if len(matches) != 1:
            raise ValueError(f'Ambiguous startup function: {function}')
        entry, size = next(iter(matches))
        if not entry <= address < entry + size or words.get(address) != expected:
            raise ValueError(f'Startup signature changed: {function}')
        if callback == 'dkr_skip_intro_title' and words.get(address - 4) != 0xAC820000:
            raise ValueError('Title demo-table storage boundary changed')
        hook = dict(function=function, beforeVram=hex(address),
                    text=f'extern void {callback}(uint8_t*, recomp_context*); {callback}(rdram, ctx);',
                    reason='One-shot offline Play preference; preserve retail saves, title input, music and online startup.')
        existing = [item for item in hooks if int(item['beforeVram'], 0) == address]
        if existing and existing != [hook]:
            raise ValueError(f'Conflicting startup hook: {function}')
        if not existing:
            hooks.append(hook)
    return result
