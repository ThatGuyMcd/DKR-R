"""Qualify the follow-camera Patch Pipeline ABI against both pinned retail ELFs."""
import hashlib
import struct
from compose_legacy_mod_policy import elf_sections, elf_functions
from legacy_character_presentation_policy import ELFS

BODY = {
    'us.v77': (0x80057A40, '4ea4efe47c86284398a2498af7eac0ed66112f99dde87153c7efd1b88d59203c'),
    'us.v80': (0x80057A80, '49cf0f61f401286458ff813975d24a0da7b8aaaa36d8c8c88ff582952c2f51d0'),
}
FIELDS = {
    'us.v77': (0x801234EC,0x80121168,0x80120CE0,0x80120D14,0x800DC918,0x8011D508,0x8011D586,0x8011D55C),
    'us.v80': (0x80123A6C,0x801216E8,0x80121260,0x80121294,0x800DCE88,0x8011DA88,0x8011DB06,0x8011DADC),
}
NAMES = ('gGameMode','gCurrentLevelHeader','gViewportLayout','gCutsceneCameraActive',
         'gCurrentLevelModel','gCameraObject','gDialogueCameraAngle','gCurrentPlayerIndex')


def verify_camera_obstruction_policy(policy, elf):
    hooks = [h for h in policy.get('functionHooks', [])
             if 'dkr_resolve_follow_camera' in h.get('text', '')]
    if not hooks:
        return  # Older fixtures are allowed; release CMake requires the new hook.
    digest = hashlib.sha256(elf.read_bytes()).hexdigest()
    revisions = [r for r, expected in ELFS.items() if expected == digest]
    if len(revisions) != 1:
        raise ValueError('Camera collision requires the reviewed retail ELF')
    revision = revisions[0]
    start, expected_body = BODY[revision]
    symbols, sections = elf_functions(elf, (1, 2)), elf_sections(elf)
    size = 0x674
    if symbols.get('update_player_camera') != {(start, size)}:
        raise ValueError('Camera-update function bounds changed')
    raw = [data[start-base:start-base+size] for base, data in sections
           if base <= start and start+size <= base+len(data)]
    if len(raw) != 1 or hashlib.sha256(raw[0]).hexdigest() != expected_body:
        raise ValueError('Camera-update body changed')
    signatures = {0:0x27BDFFC8,0x18:0xAFA40038,size-12:0x27BD0038,size-8:0x03E00008,size-4:0}
    for offset, word in signatures.items():
        if struct.unpack_from('>I', raw[0], offset)[0] != word:
            raise ValueError('Camera stack/original object/return ABI changed')
    expected_text = ('extern void dkr_resolve_follow_camera(uint8_t*, recomp_context*, uint32_t); '
                     'dkr_resolve_follow_camera(rdram, ctx, (uint32_t)MEM_W(0x38, ctx->r29));')
    if len(hooks) != 1 or hooks[0]['function'] != 'update_player_camera' or \
            int(hooks[0]['beforeVram'], 0) != start+size-12 or hooks[0]['text'] != expected_text:
        raise ValueError('Camera hook moved away from the shared stack-release boundary')
    for name, address, size in zip(NAMES, FIELDS[revision], (4,4,4,1,4,4,2,4)):
        if symbols.get(name) != {(address,size)}:
            raise ValueError('Camera field ABI changed: '+name)
    site = int(hooks[0]['beforeVram'], 0)
    if sum(int(h['beforeVram'],0)==site for h in policy['functionHooks']) != 1 or any(
            int(p['vram'],0)==site for p in policy.get('instructionPatches', [])):
        raise ValueError('Conflicting camera return owner')
