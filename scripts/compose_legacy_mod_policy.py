#!/usr/bin/env python3
"""Compose experimental project-owned hooks after checking the actual ELF.

Never edit generated C or the input policy. The original delay instruction is
retained; the bridge runs immediately after it, before the caller reuses a0.
Ordinary PI calls must still dispatch through osPiStartDma_recomp unchanged.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct

MENU_FIELDS = [
    ("gTrackSelectCursorX", 4), ("gTrackSelectCursorY", 4),
    ("gTrackSelectX", 4), ("gTrackSelectY", 4),
    ("gTrackSelectTargetX", 4), ("gTrackSelectTargetY", 4),
    ("gSelectedTrackX", 4), ("gSelectedTrackY", 4),
    ("gTrackSelectViewportY", 4), ("gTrackSelectViewPortHalfY", 4),
    ("gTrackIdForPreview", 4), ("gTrackmenuLoadedLevel", 4),
    ("gOpacityDecayTimer", 4), ("gFFLUnlocked", 2),
    ("gTrackSelectRenderDetails", 144), ("gMenuDelay", 4),
    ("gMenuButtons", 20), ("gMenuStickX", 10), ("gMenuStickY", 10),
    ("gTrackSelectRow", 4), ("gTrackSelectIDs", 48),
    ("gTrackNameVoiceDelay", 4), ("gTrackmenuType", 4),
    ("gIsInTracksMenu", 4), ("gTrackIdToLoad", 4),
    ("gThread30NeedToLoadLevel", 4), ("gTitleScreenLoaded", 4),
    ("sMenuCurrDisplayList", 4),
    ("gTrackSelectVertsFlip", 4),
]
MENU_EVENTS = ["menu_track_select_init", "menu_track_select_init", "trackmenu_input", "trackmenu_input",
               "trackmenu_render_names", "trackmenu_track_view", "bgload_start", "bgload_start",
               "load_level_for_menu", "level_name", "leveltable_vehicle_default", "leveltable_vehicle_usable",
               "trackmenu_assets", "func_80092188", "load_level_game", "menu_init", "func_8008F618", "func_8008F618"]


def elf_sections(path: Path):
    data = path.read_bytes()
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x02":
        raise ValueError("Expected a big-endian ELF32 DKR image")
    start = struct.unpack_from(">I", data, 32)[0]
    stride, count = struct.unpack_from(">HH", data, 46)
    if stride != 40 or count > 4096 or start + stride * count > len(data):
        raise ValueError("Invalid ELF section directory")
    sections = []
    for i in range(count):
        _, kind, flags, address, offset, size, *_ = struct.unpack_from(">10I", data, start + i * stride)
        if kind == 1 and flags & 4:
            if offset + size > len(data) or size % 4 or address % 4:
                raise ValueError("Invalid executable ELF section")
            sections.append((address, data[offset:offset + size]))
    return sections


def elf_functions(path: Path, symbol_types=(2,)) -> dict:
    # Parse symbol bounds as well as opcodes: a matching short prologue is not
    # permission to put an entry hook in a different generated function.
    elf_sections(path)  # validates the ELF32 header/directory first
    data = path.read_bytes()
    start = struct.unpack_from(">I", data, 32)[0]
    count = struct.unpack_from(">H", data, 48)[0]
    headers = [struct.unpack_from(">10I", data, start + i * 40) for i in range(count)]
    result = {}
    for header in headers:
        _, kind, _, _, offset, size, link, _, _, stride = header
        if kind != 2:
            continue
        if stride != 16 or size % stride or offset + size > len(data) or link >= count:
            raise ValueError("Invalid ELF function symbol table")
        strings_header = headers[link]
        strings_at, strings_size = strings_header[4:6]
        if strings_header[1] != 3 or strings_at + strings_size > len(data):
            raise ValueError("Invalid ELF symbol string table")
        strings = data[strings_at:strings_at + strings_size]
        for at in range(offset, offset + size, stride):
            name, address, length, info, _, _ = struct.unpack_from(">IIIBBH", data, at)
            if info & 15 not in symbol_types or not address or not length:
                continue
            end = strings.find(b"\0", name)
            if end < name:
                raise ValueError("Unterminated ELF function name")
            label = strings[name:end].decode("ascii")
            result.setdefault(label, set()).add((address, length))
    return result


def verify_function_bounds(policy: dict, fragment: dict, functions: dict) -> None:
    functions = copy.deepcopy(functions)
    for manual in policy.get("manualFunctions", []):
        functions[manual["name"]] = {(int(manual["vram"], 0), int(manual["size"], 0))}
    def bounds(name):
        entries = functions.get(name, set())
        if len(entries) != 1:
            raise ValueError(f"Missing or ambiguous function bounds: {name}")
        return next(iter(entries))
    if bounds(fragment["callTarget"])[0] != int(fragment["callTargetVram"], 0):
        raise ValueError("PI target does not match its declared ELF symbol")
    for site in fragment["sites"] + fragment["assetApis"]:
        begin, size = bounds(site["function"])
        address = int(site["vram"], 0)
        if not begin <= address or address + 12 > begin + size:
            raise ValueError(f"Hook signature falls outside its function: {site['function']}")
        if "operation" in site and address != begin:
            raise ValueError(f"Asset hook is not at its declared function entry: {site['function']}")


def compose(policy: dict, fragment: dict, sections: list) -> dict:
    if policy.get("schemaVersion") != 1 or fragment.get("schemaVersion") != 1:
        raise ValueError("Unsupported policy/fragment schema")
    if fragment.get("revision") not in ("us.v77", "us.v80"):
        raise ValueError("Unsupported legacy asset revision")
    result = copy.deepcopy(policy)
    patches = result.setdefault("instructionPatches", [])
    hooks = result.setdefault("functionHooks", [])
    words = {start + i: struct.unpack_from(">I", data, i)[0]
             for start, data in sections for i in range(0, len(data), 4)}
    target = int(fragment["callTargetVram"], 0)
    jal = 0x0C000000 | ((target >> 2) & 0x03FFFFFF)
    sites = fragment["sites"]
    declared = {int(s["vram"], 0) for s in sites}
    found = {address for address, word in words.items() if word == jal}
    if len(declared) != len(sites) or found != declared:
        raise ValueError(f"PI call coverage mismatch: declared={sorted(declared)}, ELF={sorted(found)}")
    for site in sites:
        address = int(site["vram"], 0)
        expected = [int(word, 0) for word in site["expected"]]
        if len(expected) != 3 or expected[0] != jal or address % 4:
            raise ValueError("Invalid PI call-site signature")
        if [words.get(address + i * 4) for i in range(3)] != expected:
            raise ValueError(f"PI instruction/delay/continuation changed at {address:#x}")
        # A branch targeting the delay/continuation would be another execution
        # path that the simple JAL-to-NOP bridge does not model.
        for pc, word in words.items():
            op = word >> 26
            if op in (1, 4, 5, 6, 7, 20, 21, 22, 23):
                displacement = (word & 0xFFFF) - (0x10000 if word & 0x8000 else 0)
                destination = pc + 4 + displacement * 4
            elif op == 2:
                destination = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
            else:
                continue
            if destination in (address + 4, address + 8):
                raise ValueError(f"An independent branch enters the PI bridge at {destination:#x}")
        for entry in patches:
            if int(entry["vram"], 0) in (address, address + 4, address + 8):
                raise ValueError("Legacy PI bridge conflicts with an existing instruction patch")
        for entry in hooks:
            if int(entry["beforeVram"], 0) in (address, address + 4, address + 8):
                raise ValueError("Legacy PI bridge conflicts with an existing hook")
        patches.append({
            "function": site["function"], "vram": f"0x{address:08X}", "value": "0x00000000",
            "reason": "Route the checked PI JAL through the project asset bus; retain the original delay-slot instruction."})
        hooks.append({
            "function": site["function"], "beforeVram": f"0x{address + 8:08X}",
            "text": "extern void dkr_legacy_pi_start_dma(uint8_t*, recomp_context*); dkr_legacy_pi_start_dma(rdram, ctx);",
            "reason": "Dispatch after the original delay slot has populated the DMA arguments and before a0 is reused. Stock uses the original PI handler."})
    api_names = ["asset_table_load", "asset_load", "asset_rom_offset", "asset_table_size",
                 "asset_table_load_addr", "asset_table_load_zipped"]
    api_sites = fragment.get("assetApis", [])
    if len(api_sites) != len(api_names):
        raise ValueError("Incomplete asset API coverage")
    for name, site in zip(api_names, api_sites, strict=True):
        operation = api_names.index(name)
        address = int(site["vram"], 0)
        if site["function"] != name or site["operation"] != operation or address % 4:
            raise ValueError("Asset operation/function mapping changed")
        expected = [int(word, 0) for word in site["expected"]]
        if len(expected) != 3 or [words.get(address + i * 4) for i in range(3)] != expected:
            raise ValueError(f"Asset API entry signature changed: {name}")
        owners=[entry for entry in hooks if int(entry["beforeVram"],0)==address]
        text="extern int dkr_legacy_asset_api(uint8_t*, recomp_context*, unsigned); " + f"if (dkr_legacy_asset_api(rdram, ctx, {operation}U)) return;"
        shared={"asset_table_load":"dkr_custom_tracks_table_load_begin", "asset_load":"dkr_custom_tracks_asset_load_begin"}
        callback=shared.get(name)
        allowed=f"extern void {callback}(uint8_t*, recomp_context*); {callback}(rdram, ctx);" if callback else None
        if any(int(entry["vram"], 0) == address for entry in patches) or (owners and
                (len(owners)!=1 or owners[0].get('function')!=name or owners[0].get('text')!=allowed)):
            raise ValueError(f"Asset API entry conflicts with an existing policy: {name}")
        if owners:
            # Mounted legacy reads explicitly run the Blender extension via
            # the runtime bridge. Unmounted reads retain its retail hooks.
            owners[0]['text']=text+' '+owners[0]['text']
        else:hooks.append({"function": name, "beforeVram": f"0x{address:08X}",
                      "text": text,
                      "reason": "Resolve a mounted immutable bank at the original function entry; leave the complete retail path intact without a mount."})
    return result


def compose_scene_runtime(policy: dict, fragment: dict, sections: list, functions: dict,
                          qualification: bool = False) -> dict:
    """Compose with, never replace, the existing presentation scene owner."""
    result = copy.deepcopy(policy)
    revision = fragment["revision"]
    entries = {
        "us.v77": (0x8006B250, 0x8006E2E8, 0x81CE3514),
        "us.v80": (0x8006B490, 0x8006E528, 0x81CE3A94),
    }
    if revision not in entries:
        raise ValueError("Unsupported scene lifecycle revision")
    level, menu, load_word = entries[revision]
    words = {start + i: struct.unpack_from(">I", data, i)[0]
             for start, data in sections for i in range(0, len(data), 4)}

    def verify(name, address, expected):
        bounds = functions.get(name, set())
        if len(bounds) != 1 or next(iter(bounds))[0] != address or next(iter(bounds))[1] < 12:
            raise ValueError(f"Scene lifecycle symbol changed: {name}")
        if [words.get(address + i * 4) for i in range(3)] != expected:
            raise ValueError(f"Scene lifecycle signature changed: {name}")

    verify("level_load", level, [0x27BDFFA0, 0xAFBF002C, 0xAFB10028])
    owner = [hook for hook in result["functionHooks"] if int(hook["beforeVram"], 0) == level]
    expected = ("extern void dkr_runtime_scene_reset(uint8_t*, recomp_context*); "
                "extern void dkr_presentation_scene_begin(uint8_t*, recomp_context*); "
                "dkr_runtime_scene_reset(rdram, ctx); dkr_presentation_scene_begin(rdram, ctx);")
    if len(owner) != 1 or owner[0]["function"] != "level_load" or owner[0]["text"] != expected:
        raise ValueError("The existing scene owner changed; review lifecycle composition before enabling mods")
    owner[0]["text"] += (" extern void dkr_legacy_scene_begin(uint8_t*, recomp_context*); "
                         "dkr_legacy_scene_begin(rdram, ctx);")
    owner[0]["reason"] += " Then publish only an explicitly prepared custom asset scene at this same boundary."
    if qualification:
        verify("load_level_for_menu", menu, [0x27BDFFE0, 0x3C0E8012, load_word])
        if any(int(hook["beforeVram"], 0) == menu for hook in result["functionHooks"]) or any(
                int(patch["vram"], 0) == menu for patch in result["instructionPatches"]):
            raise ValueError("Private menu-load qualification conflicts with an existing hook")
        result["functionHooks"].append({"function": "load_level_for_menu", "beforeVram": f"0x{menu:08X}",
            "text": "extern void dkr_legacy_qualification_menu_load(uint8_t*, recomp_context*); dkr_legacy_qualification_menu_load(rdram, ctx);",
            "reason": "PRIVATE QUALIFICATION ONLY: exercise custom/stock scenes through the real menu/background loader."})
    return result


def compose_track_menu(policy: dict, fragment: dict, sections: list, symbols: dict) -> dict:
    """Keep native drawing/zoom functions; add bounded catalogue data hooks."""
    if fragment.get("schema") != 1 or fragment.get("abi") != 4 or fragment.get("revision") not in ("us.v77", "us.v80"):
        raise ValueError("Unsupported track-menu adapter")
    words = {start+i: struct.unpack_from(">I", data, i)[0]
             for start, data in sections for i in range(0, len(data), 4)}
    fields = fragment.get("fields", [])
    if [(f["name"], f["size"]) for f in fields] != MENU_FIELDS:
        raise ValueError("Incomplete track-menu field ABI")
    for field in fields:
        if symbols.get(field["name"]) != {(int(field["address"], 0), field["size"])}:
            raise ValueError(f"Track-menu data symbol changed: {field['name']}")
    bodies = fragment["functions"]
    for name, body in bodies.items():
        start, size = int(body["vram"], 0), body["size"]
        if size <= 0 or size % 4 or symbols.get(name) != {(start, size)}:
            raise ValueError(f"Track-menu function bounds changed: {name}")
        try:
            digest = hashlib.sha256(b"".join(struct.pack(">I", words[pc]) for pc in range(start, start+size, 4))).hexdigest()
        except KeyError as error:
            raise ValueError(f"Track-menu body missing: {name}") from error
        if digest != body["sha256"]:
            raise ValueError(f"Track-menu function changed: {name}")
    result = copy.deepcopy(policy)
    hooks = result.setdefault("functionHooks", [])
    patches = result.setdefault("instructionPatches", [])
    seen = set()
    field_data = ", ".join(f"{int(f['address'],0):#010x}U" for f in fields)
    for site in fragment["hooks"]:
        name, pc, event = site["function"], int(site["vram"],0), site["event"]
        body = bodies[name]; begin = int(body["vram"],0); end = begin+body["size"]
        at = int(site["expectedAt"],0)
        if pc in seen or pc % 4 or not begin <= pc < end or at < begin or at+12 > end or not 0 <= event <= 17:
            raise ValueError("Invalid/duplicate track-menu hook")
        if name != MENU_EVENTS[event] or site["returns"] != (event in (9, 10, 11)):
            raise ValueError("Track-menu event/return ABI changed")
        observed = 0
        if event == 7:
            delay = words.get(pc+4)
            if words.get(pc) != 0x03e00008:
                raise ValueError("Background-loader outcome hook is not at a return")
            if delay == 0x24020001:
                observed = 1
            elif delay != 0 or words.get(pc-4) != 0x00001025:
                raise ValueError("Background-loader outcome delay slot changed")
            if site.get("observedReturn") != observed:
                raise ValueError("Background-loader observation differs from its actual return sequence")
        seen.add(pc)
        if len(site["expected"]) != 3 or [words.get(at+i*4) for i in range(3)] != [int(w,0) for w in site["expected"]]:
            raise ValueError(f"Track-menu hook signature changed: {name}")
        if any(int(p["vram"],0) == pc for p in patches):
            raise ValueError(f"Track-menu hook collides with an instruction patch: {name}")
        owners = [h for h in hooks if int(h["beforeVram"],0) == pc]
        event_fields=field_data
        if event==4:
            event_fields=", ".join("(uint32_t)ctx->r29 + 0x90U" if f['name']=='gTrackSelectRenderDetails'
                else f"{int(f['address'],0):#010x}U" for f in fields)
            observed=5 # Five draw-only columns, three rows; separate stack storage.
        text = ("extern int dkr_legacy_track_menu(uint8_t*, recomp_context*, unsigned, const uint32_t*, unsigned); "
                "{ "+("" if event==4 else "static ")+"const uint32_t dkr_legacy_fields[] = {"+event_fields+"}; "
                + ("if (" if site["returns"] else "")
                + f"dkr_legacy_track_menu(rdram, ctx, {event}U, dkr_legacy_fields, {observed}U)"
                + (") return; }" if site["returns"] else "; }"))
        if owners:
            # One deliberate composition point, not a blanket hook override.
            expected = ("extern void dkr_netplay_gameplay_level_begin(uint8_t*, recomp_context*); "
                        "dkr_netplay_gameplay_level_begin(rdram, ctx);")
            if len(owners) != 1 or event != 14 or name != "load_level_game" or owners[0]["text"] != expected:
                raise ValueError(f"Unreviewed existing owner at track-menu hook: {name}")
            owners[0]["text"] += " " + text
            owners[0]["reason"] += " Bind an explicitly selected custom course after the unchanged online entry callback."
        else:
            hooks.append({"function":name,"beforeVram":hex(pc),"text":text,
                          "reason":"Additive custom-track catalogue adapter; preserve the complete original stock menu/drawing/zoom path."})
    if {h["event"] for h in fragment["hooks"]} != set(range(18)):
        raise ValueError("Incomplete track-menu event coverage")
    return result


def compose_characters(policy: dict, fragment: dict, sections: list, symbols: dict) -> dict:
    if fragment.get('schema')!=1 or fragment.get('abi')!=1 or fragment.get('revision') not in ('us.v77','us.v80'):
        raise ValueError('Unsupported character adapter')
    fields=fragment.get('fields',[])
    if [(f['name'],f['size']) for f in fields]!=[('gCharacterIdSlots',8)]:
        raise ValueError('Incomplete character field ABI')
    for f in fields:
        if symbols.get(f['name'])!={(int(f['address'],0),f['size'])}:raise ValueError('Character symbol changed')
    words={base+i:struct.unpack_from('>I',data,i)[0] for base,data in sections for i in range(0,len(data),4)}
    names=['spawn_object','charselect_assign_ai','menu_character_select_init']
    if set(fragment.get('functions',{}))!=set(names) or len(fragment.get('hooks',[]))!=3:
        raise ValueError('Incomplete character lifecycle coverage')
    for name,body in fragment['functions'].items():
        start,size=int(body['vram'],0),body['size']
        if size<=0 or size%4 or symbols.get(name)!={(start,size)}:raise ValueError('Character function bounds changed')
        try:digest=hashlib.sha256(b''.join(struct.pack('>I',words[pc]) for pc in range(start,start+size,4))).hexdigest()
        except KeyError as e:raise ValueError('Character function backing is missing') from e
        if digest!=body['sha256']:raise ValueError('Character function body changed: '+name)
    result=copy.deepcopy(policy)
    hooks=result.setdefault('functionHooks',[]);patches=result.setdefault('instructionPatches',[])
    for event,site in enumerate(fragment['hooks']):
        name,pc=site['function'],int(site['vram'],0)
        body=fragment['functions'].get(name,{})
        start=int(body.get('vram','0'),0);end=start+body.get('size',0)
        if name!=names[event] or event!=site['event'] or pc%4 or not start<=pc<=end-12:
            raise ValueError('Character event ABI changed')
        if len(site['expected'])!=3 or [words.get(pc+i*4) for i in range(3)]!=[int(w,0) for w in site['expected']]:
            raise ValueError('Character site signature changed')
        if event==0 and words[pc]!=0xa7a4004e:raise ValueError('Character hook must precede the header lifetime store')
        if event!=0 and pc!=start:raise ValueError('Character lifecycle hook moved away from entry')
        if any(int(p['vram'],0)==pc for p in patches):
            raise ValueError('Character hook conflicts with an existing owner')
        text='extern void dkr_legacy_character_event(uint8_t*, recomp_context*, unsigned, uint32_t); '+f'dkr_legacy_character_event(rdram, ctx, {event}U, {int(fields[0]["address"],0):#x}U);'
        owners=[h for h in hooks if int(h['beforeVram'],0)==pc]
        if owners:
            expected='extern void dkr_netplay_character_select_ai_seed(uint8_t*, recomp_context*); dkr_netplay_character_select_ai_seed(rdram, ctx);'
            if len(owners)!=1 or event!=1 or owners[0]['function']!='charselect_assign_ai' or owners[0]['text']!=expected:
                raise ValueError('Unreviewed existing character lifecycle owner')
            owners[0]['text']+=' '+text
            owners[0]['reason']+=' Then record the explicitly chosen logical human identities without changing native AI assignment.'
        else:
            hooks.append({'function':name,'beforeVram':hex(pc),'text':text,
                'reason':'Additive per-racer assets before native header ownership; no original character IDs or cache IDs are replaced.'})
    return result


def compose_character_menu(policy,fragment,sections,symbols):
    # Independent menu adapter: the existing per-racer resource ABI stays fixed.
    fields_spec=[('gActivePlayersArray',4),('gCharselectStatus',4),('gPlayersCharacterArray',8),
        ('gCharacterIdSlots',8),('gMenuButtons',20),('gMenuStickX',10),('gMenuStickY',10),
        ('gNumberOfReadyPlayers',4),('gNumberOfActivePlayers',4),('gMenuDelay',4),
        ('sMenuCurrDisplayList',4),('gCurrCharacterSelectData',4),('dDialogueBoxBegin',56),('dDialogueBoxDrawModes',32),('gMenuSoundMasks',16),
        ('gMenuCurrentCharacter',4),('gObjectCount',4),('gFreeListCount',4)]
    names=['menu_character_select_init','menu_character_select_loop','charselect_render_text',
           'charselect_free','charselect_assign_ai','charselect_move','obj_loop_char_select','obj_loop_char_select','sound_play']
    if fragment.get('schema')!=1 or fragment.get('abi')!=3 or fragment.get('revision') not in ('us.v77','us.v80'):
        raise ValueError('Unsupported character-menu adapter')
    fields=fragment.get('fields',[])
    if [(f['name'],f['size']) for f in fields]!=fields_spec:raise ValueError('Incomplete character-menu field ABI')
    for f in fields:
        if symbols.get(f['name'])!={(int(f['address'],0),f['size'])}:raise ValueError('Character-menu symbol changed')
    words={base+i:struct.unpack_from('>I',data,i)[0] for base,data in sections for i in range(0,len(data),4)}
    if set(fragment.get('functions',{}))!=set(names) or len(fragment.get('hooks',[]))!=9:
        raise ValueError('Incomplete character-menu lifecycle')
    for name,body in fragment['functions'].items():
        start,size=int(body['vram'],0),body['size']
        if size<=0 or size%4 or symbols.get(name)!={(start,size)}:raise ValueError('Character-menu function changed')
        try:digest=hashlib.sha256(b''.join(struct.pack('>I',words[p]) for p in range(start,start+size,4))).hexdigest()
        except KeyError as e:raise ValueError('Missing menu function backing') from e
        if digest!=body['sha256']:raise ValueError('Character-menu body changed: '+name)
    result=copy.deepcopy(policy);hooks=result.setdefault('functionHooks',[])
    values=', '.join(f'{int(f["address"],0):#x}U' for f in fields)
    for event,site in enumerate(fragment['hooks']):
        name=site['function'];pc=int(site['vram'],0);at=int(site['expectedAt'],0)
        body=fragment['functions'].get(name,{});start=int(body.get('vram','0'),0);end=start+body.get('size',0)
        if name!=names[event] or site['event']!=event or pc%4 or not start<=pc<end or not start<=at<=end-12:
            raise ValueError('Character-menu event moved')
        if len(site['expected'])!=3 or [words.get(at+4*i) for i in range(3)]!=[int(w,0) for w in site['expected']]:
            raise ValueError('Character-menu site signature changed')
        if event in (0,2) and words[pc]!=0x03e00008:raise ValueError('Menu initialization/render hook must precede the native return')
        if event in (3,4,5,6,8) and pc!=start:raise ValueError('Character-menu lifecycle entry moved')
        if any(int(p['vram'],0)==pc for p in result.get('instructionPatches',[])):raise ValueError('Menu hook overlaps an instruction patch')
        call=f'dkr_legacy_character_menu(rdram, ctx, {event}U, dkr_character_menu_fields)'
        text='{ extern int dkr_legacy_character_menu(uint8_t*, recomp_context*, unsigned, const uint32_t*); '+f'static const uint32_t dkr_character_menu_fields[] = {{ {values} }}; '
        text+=(f'if ({call}) return;' if event in (5,6,8) else call+';')+' }'
        owners=[h for h in hooks if int(h['beforeVram'],0)==pc]
        if owners:
            if len(owners)!=1 or owners[0]['function']!=name:raise ValueError('Ambiguous character-menu hook owner')
            old=owners[0]['text']
            if event==1:
                expected='extern void dkr_netplay_character_select_lock(uint8_t*, recomp_context*); dkr_netplay_character_select_lock(rdram, ctx);'
                if old!=expected:raise ValueError('Unknown collected-input owner')
                owners[0]['text']=old+' '+text
            elif event==4:
                expected='extern void dkr_netplay_character_select_ai_seed(uint8_t*, recomp_context*); dkr_netplay_character_select_ai_seed(rdram, ctx); '
                expected+='extern void dkr_legacy_character_event(uint8_t*, recomp_context*, unsigned, uint32_t); '+f'dkr_legacy_character_event(rdram, ctx, 1U, {int(fields[3]["address"],0):#x}U);'
                if old!=expected:raise ValueError('Logical commit requires the exact existing AI/resource owner')
                owners[0]['text']=text+' '+old
            else:raise ValueError('Unreviewed character-menu owner')
            owners[0]['reason']+=' Compose the separately validated logical custom-character selection without bypassing native launch.'
        else:
            if event==4:raise ValueError('Character-menu adapter requires the resource-commit hook')
            hooks.append({'function':name,'beforeVram':hex(pc),'text':text,'reason':'Additive native-stage selection; original ready/launch and controller mapping remain authoritative.'})
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", required=True, type=Path)
    parser.add_argument("--fragment", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--scene-runtime", action="store_true")
    parser.add_argument("--qualification", action="store_true",
                        help="Add PRIVATE real-menu-load probe; requires DKR_LEGACY_QUALIFICATION build")
    parser.add_argument("--track-menu", type=Path, help="Verified additive native Track Select fragment")
    parser.add_argument("--characters",type=Path,help="Verified additive character resource fragment")
    parser.add_argument("--character-menu",type=Path,help="Verified additive character selection fragment")
    args = parser.parse_args()
    if args.output.resolve() in (args.policy.resolve(), args.fragment.resolve(), args.elf.resolve()):
        raise ValueError("Output must not overwrite an input")
    policy, fragment = json.loads(args.policy.read_text()), json.loads(args.fragment.read_text())
    verify_function_bounds(policy, fragment, elf_functions(args.elf))
    result = compose(policy, fragment, elf_sections(args.elf))
    if args.qualification and not args.scene_runtime:
        raise ValueError("Qualification requires the scene runtime bridge")
    if args.scene_runtime:
        result = compose_scene_runtime(result, fragment, elf_sections(args.elf),
                                       elf_functions(args.elf), args.qualification)
    if args.track_menu:
        if not args.scene_runtime or args.qualification:
            raise ValueError("Track Select requires scene ownership and cannot use the cyclic menu-load probe")
        menu = json.loads(args.track_menu.read_text())
        if menu.get("revision") != fragment.get("revision"):
            raise ValueError("Track-menu and asset revisions differ")
        result = compose_track_menu(result, menu, elf_sections(args.elf), elf_functions(args.elf, (1, 2)))
        from track_select_draw_policy import compose_track_draw
        result = compose_track_draw(result,args.elf,fragment['revision'],elf_sections(args.elf),elf_functions(args.elf,(1,2)))
    if args.characters:
        if not args.scene_runtime:raise ValueError('Character adapter requires boot/scene asset ownership')
        character=json.loads(args.characters.read_text())
        if character.get('revision')!=fragment.get('revision'):raise ValueError('Character and asset revisions differ')
        result=compose_characters(result,character,elf_sections(args.elf),elf_functions(args.elf,(1,2)))
    if args.character_menu:
        menu=json.loads(args.character_menu.read_text())
        if not args.characters or menu.get('revision')!=fragment.get('revision'):raise ValueError('Character menu requires matching resource ownership')
        result=compose_character_menu(result,menu,elf_sections(args.elf),elf_functions(args.elf,(1,2)))
        from legacy_character_presentation_policy import compose_presentation
        result=compose_presentation(result,args.elf,fragment['revision'],elf_sections(args.elf),elf_functions(args.elf,(1,2)))
    from legacy_model_cache_policy import compose_model_cache
    result=compose_model_cache(result,args.elf,fragment['revision'],elf_sections(args.elf),elf_functions(args.elf,(1,2)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # A build artifact, never a mutation of a versioned policy or protected C.
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Verified and composed both PI call sites and all six asset API entries; input policies unchanged.")


if __name__ == "__main__":
    main()
