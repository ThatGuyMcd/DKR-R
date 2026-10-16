#!/usr/bin/env python3
"""Author additive character-menu hooks from the pinned retail ELF only."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from compose_legacy_mod_policy import elf_functions, elf_sections
from prepare_legacy_menu_fragment import ELFS, reviewed_revision

FIELDS=[('gActivePlayersArray',4),('gCharselectStatus',4),('gPlayersCharacterArray',8),
        ('gCharacterIdSlots',8),('gMenuButtons',20),('gMenuStickX',10),('gMenuStickY',10),
        ('gNumberOfReadyPlayers',4),('gNumberOfActivePlayers',4),('gMenuDelay',4),
        ('sMenuCurrDisplayList',4),('gCurrCharacterSelectData',4),('dDialogueBoxBegin',56),('dDialogueBoxDrawModes',32),('gMenuSoundMasks',16),
        ('gMenuCurrentCharacter',4),('gObjectCount',4),('gFreeListCount',4)]

def prepare(elf,revision):
    if reviewed_revision(elf)!=revision:raise ValueError('Unreviewed character-menu ELF')
    symbols=elf_functions(elf,(1,2))
    words={base+i:struct.unpack_from('>I',data,i)[0] for base,data in elf_sections(elf) for i in range(0,len(data),4)}
    def one(name):
        values=symbols.get(name,set())
        if len(values)!=1:raise ValueError('Missing/ambiguous character-menu symbol '+name)
        return next(iter(values))
    fields=[]
    for name,size in FIELDS:
        address,actual=one(name)
        if actual!=size:raise ValueError(f'{name} has changed size: {actual}, expected {size}')
        fields.append({'name':name,'address':hex(address),'size':size})
    def exit_pc(name):
        begin,size=one(name);sites=[pc for pc in range(begin,begin+size,4) if words[pc]==0x03e00008]
        if len(sites)!=1:raise ValueError('Ambiguous menu exit: '+name)
        return sites[0]
    begin,size=one('menu_character_select_loop');target=one('menu_input')[0]
    sites=[pc+8 for pc in range(begin,begin+size-8,4) if words[pc]==(0x0c000000|((target>>2)&0x3ffffff))]
    if len(sites)!=1:raise ValueError('Ambiguous collected-input boundary')
    definitions=[('menu_character_select_init',exit_pc('menu_character_select_init')),
        ('menu_character_select_loop',sites[0]),('charselect_render_text',exit_pc('charselect_render_text')),
        ('charselect_free',one('charselect_free')[0]),('charselect_assign_ai',one('charselect_assign_ai')[0]),
        ('charselect_move',one('charselect_move')[0]),('obj_loop_char_select',one('obj_loop_char_select')[0])]
    begin,size=one('obj_loop_char_select');target=one('get_player_character')[0]
    cursor_sites=[pc+8 for pc in range(begin,begin+size-8,4) if words[pc]==(0x0c000000|((target>>2)&0x3ffffff))]
    if len(cursor_sites)!=1:raise ValueError('Ambiguous native stage cursor boundary')
    definitions.append(('obj_loop_char_select',cursor_sites[0]))
    definitions.append(('sound_play',one('sound_play')[0]))
    hooks=[];bodies={}
    for event,(name,pc) in enumerate(definitions):
        start,length=one(name);at=min(pc,start+length-12)
        body=b''.join(struct.pack('>I',words[p]) for p in range(start,start+length,4))
        bodies[name]={'vram':hex(start),'size':length,'sha256':hashlib.sha256(body).hexdigest()}
        hooks.append({'function':name,'vram':hex(pc),'event':event,'expectedAt':hex(at),
                      'expected':[hex(words[at+i*4]) for i in range(3)]})
    return {'schema':1,'revision':revision,'abi':3,'fields':fields,'functions':bodies,'hooks':hooks}

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf',type=Path,required=True);p.add_argument('--revision',choices=ELFS,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args(argv)
    payload = json.dumps(prepare(a.elf,a.revision),indent=2)+'\n'
    with a.output.open('x',encoding='utf-8') as f:f.write(payload)
    return 0

if __name__=='__main__':
    raise SystemExit(main())
