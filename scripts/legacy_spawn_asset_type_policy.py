"""Preserve scenery asset type after retail's trophy-count scratch reuse.

Only the reviewed rocket-signpost-2 branch is changed. Trophy progress, model
index and sparse model slots remain retail; no saves or resource IDs change.
"""
import copy
import hashlib
import struct
from legacy_character_presentation_policy import ELFS
from compose_legacy_mod_policy import elf_sections, elf_functions


def compose_spawn_asset_type(policy, elf, revision):
    if revision not in ELFS or hashlib.sha256(elf.read_bytes()).hexdigest() != ELFS[revision]:
        raise ValueError('Spawn asset-type correction requires a reviewed retail ELF')
    pc = 0x8000ed8c
    symbols = elf_functions(elf).get('spawn_object', set())
    if len(symbols) != 1:
        raise ValueError('Ambiguous spawn asset-type function')
    begin, size = next(iter(symbols))
    words = {base+i: struct.unpack_from('>I', data, i)[0]
             for base, data in elf_sections(elf) for i in range(0, len(data), 4)}
    if not begin <= pc < begin+size or words.get(pc) != 0xa246003a:
        raise ValueError('Spawn trophy-count boundary changed')
    result = copy.deepcopy(policy)
    entry = dict(function='spawn_object', beforeVram=hex(pc),
                 text='MEM_W(0x64, ctx->r29) = MEM_B(0x53, MEM_W(0x40, ctx->r18));',
                 reason='After counting the first four world trophies, restore header modelType; fifth-world trophy bits must not choose a Sprite loader for a 3D model.')
    owners = [h for h in result.setdefault('functionHooks', []) if int(h['beforeVram'], 0) == pc]
    if any(int(p['vram'], 0) == pc for p in result.get('instructionPatches', [])):
        raise ValueError('Conflicting spawn asset-type instruction')
    if owners:
        if owners != [entry]:
            raise ValueError('Conflicting spawn asset-type hook')
    else:
        result['functionHooks'].append(entry)
    return result
