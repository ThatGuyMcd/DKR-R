#!/usr/bin/env python3
"""Stage authored SDK files and an optional explicit sample; never activate code."""
import argparse
import json
from pathlib import Path
import shutil
import zipfile
from render_magic_code_guide import render as render_guide

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--example',type=Path)
    p.add_argument('--difficulty-example',type=Path)
    a=p.parse_args();root=Path(__file__).resolve().parents[1];sdk=root/'modding'
    if a.output.exists():raise ValueError('Choose a new SDK staging directory')
    sources=[sdk/name for name in ('SDK.txt','USER-GUIDE.txt','MODDERS-GUIDE.txt','build.py','runtime.c','mod.ld','reference-sdk-only.toml')]
    for name in ('include','examples','reference-us-v77','reference-us-v80'):
        sources+=sorted(f for f in (sdk/name).rglob('*') if f.is_file())
    for source in sources:
        if source.is_symlink() or source.suffix not in ('.txt','.py','.c','.h','.inc','.ld','.toml','.json'):
            raise ValueError('SDK contains an unexpected source type')
        target=a.output/source.relative_to(sdk);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
    shutil.copyfile(root/'LICENSE.md',a.output/'LICENSE.txt')
    render_guide(sdk,a.output/'MODDERS-GUIDE.html')
    for example,identity in ((a.example,'dkr_custom_trophies'),(a.difficulty_example,'dkr_ai_boss_difficulty')):
        if not example:continue
        if not example.is_file() or example.is_symlink() or example.stat().st_size>32*1024*1024:raise ValueError('Invalid example package')
        with zipfile.ZipFile(example) as package:
            expected={'dkr.json','mod.json','mod_syms.bin','mod_binary.bin'}
            if len(package.infolist())!=4 or set(package.namelist())!=expected:raise ValueError('Only source-only examples are allowed')
            meta=json.loads(package.read('mod.json'))
            if meta.get('id')!=identity or meta.get('game_id')!='dkr':raise ValueError('Unexpected example identity')
        shutil.copyfile(example,a.output/(identity+'.nrm'))
    print('Staged SDK sources and optional manually importable mods. No activation or execution.')

if __name__=='__main__':main()
