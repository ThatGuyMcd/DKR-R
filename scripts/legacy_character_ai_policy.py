"""Checked, offline-only AI identity hooks; never edit generated functions."""
import argparse
import copy
import hashlib
import json
import struct
from pathlib import Path
from compose_legacy_mod_policy import elf_functions, elf_sections
from legacy_character_presentation_policy import ELFS

BODY = {
    'us.v77': '293ecb84ee5f83794329821a968781bb283ee98ccbef329c3a8a219fea43228e',
    'us.v80': '61d525b58f578bc7816ea402b577fdc9721a0fae8edc2526a1f9df0d4e320013',
}

def checked_sites(elf):
    digest = hashlib.sha256(elf.read_bytes()).hexdigest()
    revisions = [r for r, h in ELFS.items() if h == digest]
    if len(revisions) != 1:
        raise ValueError('AI adapter requires the pinned retail ELF')
    revision = revisions[0]
    symbols = elf_functions(elf)
    start, size = 0x8000cc7c, 0x1434
    if symbols.get('track_setup_racers') != {(start, size)}:
        raise ValueError('Race-construction bounds changed')
    raw = next(data[start-base:start-base+size] for base, data in elf_sections(elf)
               if base <= start and start+size <= base+len(data))
    if hashlib.sha256(raw).hexdigest() != BODY[revision]:
        raise ValueError('Race-construction body changed')
    # Includes topology stack slots, Settings register, racer-index register,
    # entry register, primary (not ghost) spawn call and its continuation.
    signatures = {
        0: 0x27bdfeb0, 0x10c: 0xafa20138, 0x110: 0x0040b825,
        0x318: 0xafa20144, 0x474: 0x8cc30000,
        0x7c8: 0x0200a025, 0xb00: 0x02c02025,
        0xb0c: 0x0c003a95, 0xb10: 0xafa7005c, 0xb14: 0x8fb80044,
    }
    for offset, word in signatures.items():
        if struct.unpack_from('>I', raw, offset)[0] != word:
            raise ValueError(f'AI register/stack ABI changed at {start+offset:#x}')
    return [(start, 0), (start+0x474, 1), (start+0xb0c, 2), (start+0xb14, 3)]

def hook_text(event):
    return ('extern void dkr_legacy_character_ai_event(uint8_t*, recomp_context*, unsigned, '
            'void (*)(uint8_t*, recomp_context*)); '
            f'dkr_legacy_character_ai_event(rdram, ctx, {event}U, rand_range);')

def verify_character_ai_policy(policy, elf):
    hooks = [h for h in policy.get('functionHooks', []) if 'dkr_legacy_character_ai_event' in h.get('text', '')]
    if not hooks:
        return
    sites = checked_sites(elf)
    expected = {(pc, hook_text(event)) for pc, event in sites}
    actual = {(int(h['beforeVram'], 0), h['text']) for h in hooks if h['function'] == 'track_setup_racers'}
    if len(hooks) != 4 or actual != expected:
        raise ValueError('AI hook ownership changed')
    for pc, _ in sites:
        if sum(int(h['beforeVram'], 0) == pc for h in policy['functionHooks']) != 1 or any(
                int(p['vram'], 0) == pc for p in policy.get('instructionPatches', [])):
            raise ValueError('Conflicting AI hook owner')

def compose_character_ai(policy, elf):
    result = copy.deepcopy(policy)
    if any('dkr_legacy_character_ai_event' in h.get('text', '') for h in result.get('functionHooks', [])):
        verify_character_ai_policy(result, elf)
        return result
    for pc, event in checked_sites(elf):
        result.setdefault('functionHooks', []).append({
            'function': 'track_setup_racers', 'beforeVram': hex(pc), 'text': hook_text(event),
            'reason': 'Offline custom AI identities are selected after native topology and scoped to the primary racer spawn; boss, hub, demo and ghost paths remain stock.'})
    verify_character_ai_policy(result, elf)
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policy', type=Path, required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    updated = compose_character_ai(json.loads(args.policy.read_text()), args.elf)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(updated, indent=2)+'\n')
    print('Verified all four AI identity boundaries; original instructions preserved.')
