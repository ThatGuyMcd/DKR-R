#!/usr/bin/env python3
"""SDK 2 authoring tools. These commands never execute a game or a code mod."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import zipfile
sys.dont_write_bytecode = True
from relocations import symbols as inspect_symbols


def new_project(args):
    if not re.fullmatch(r'[a-z0-9_-]{1,96}',args.id):raise ValueError('Use a portable lowercase mod ID')
    output=args.output.resolve()
    if output.exists():raise ValueError('Project output must be a NEW directory')
    output.mkdir(parents=True)
    manifest={'schema':1,'api':2,'activation':'restart','online':'deterministic','development':False,
              'revisions':['us.v77','us.v80'],'requires':{'core':1,'events2':2}}
    (output/'dkr.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    extension='cpp' if args.language=='cpp' else 'c'
    body='#include "dkr/development.'+('hpp' if extension=='cpp' else 'h')+'"\n'
    if extension=='cpp':body+='extern "C" {\n'
    body+='''DKR_CALLBACK(on_boot) void boot(void) {
    if(dkr_sdk_version()!=2)return;
    dkr_log("SDK 2 package initialized.");
}
DKR_CALLBACK(on_frame) void frame(void) {
    /* Deterministic game logic goes here. Use copied UI actions for edits;
       never use a host clock, filesystem or physical input as game state. */
}
'''
    if extension=='cpp':body+='}\n'
    (output/f'main.{extension}').write_text(body,encoding='utf-8')
    (output/'mod.toml').write_text(f'''[manifest]
id = {json.dumps(args.id)}
version = "1.0.0"
display_name = {json.dumps(args.name or args.id)}
description = "Portable DKR-R SDK 2 Custom Magic Code."
authors = ["Your name"]
game_id = "dkr"
minimum_recomp_version = "1.0.7"
dependencies = []

[build]
sources = ["main.{extension}"]
''',encoding='utf-8')
    print('Created SDK 2 '+args.language+' project: '+str(output))


def inspect(args):
    path=args.package.resolve(strict=True)
    if path.stat().st_size>32*1024*1024:raise ValueError('Package exceeds the 32 MiB portable budget')
    with zipfile.ZipFile(path) as archive:
        entries=archive.infolist();names=[entry.filename for entry in entries]
        if len(entries)>512 or len(set(name.casefold() for name in names))!=len(names):raise ValueError('Duplicate or excessive archive members')
        if sum(entry.file_size for entry in entries)>64*1024*1024:raise ValueError('Expanded package exceeds 64 MiB')
        for entry in entries:
            parts=PurePosixPath(entry.filename)
            if parts.is_absolute() or '..' in parts.parts or '\\' in entry.filename or ':' in entry.filename:raise ValueError('Unsafe archive path')
        meta=json.loads(archive.read('dkr.json'));manifest=json.loads(archive.read('mod.json'))
        sections,relocations=inspect_symbols(archive.read('mod_syms.bin'),archive.read('mod_binary.bin'))
        print(json.dumps({'id':manifest.get('id'),'name':manifest.get('display_name'),'version':manifest.get('version'),
            'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'api':meta.get('api'),'development':meta.get('development',False),
            'online':meta.get('online'),'revisions':meta.get('revisions'),'requires':meta.get('requires',{}),
            'resources':meta.get('resources',{}),'files':len(entries),'sections':len(sections),'relocations':len(relocations)},indent=2))
    print('Static metadata and symbol/relocation inspection only. This is not runtime qualification or a trust guarantee.')


def source_map(args):
    elf=args.elf.resolve(strict=True);output=args.output.resolve()
    if output.exists():raise ValueError('Source-map output must be a NEW file')
    nm=shutil.which(args.nm);symbolizer=shutil.which(args.symbolizer)
    if not nm or not symbolizer:raise ValueError('llvm-nm and llvm-symbolizer are required for source maps')
    result=subprocess.run([nm,'--defined-only','--numeric-sort','--print-size',str(elf)],capture_output=True,text=True,check=True)
    functions=[]
    for line in result.stdout.splitlines():
        found=re.fullmatch(r'([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([TtWw])\s+(.+)',line)
        if found:functions.append((int(found[1],16),int(found[2],16),found[4]))
    if len(functions)>16384:raise ValueError('Source map exceeds the function budget')
    resolved=subprocess.run([symbolizer,'--obj='+str(elf),'--output-style=JSON','--no-inlines'],
        input=''.join(hex(address)+'\n' for address,_,_ in functions),capture_output=True,text=True,check=True)
    rows=[json.loads(line) for line in resolved.stdout.splitlines() if line.strip()]
    if len(rows)!=len(functions):raise ValueError('Symbolizer did not return one result per function')
    mapped=[]
    for (address,size,name),entry in zip(functions,rows):
        symbols=entry.get('Symbol',[]);symbol=symbols[0] if symbols else {}
        file=symbol.get('FileName','')
        # Kits build with /mod and /sdk prefix mappings. Refuse to publish a
        # developer's local absolute path accidentally.
        if file and not file.startswith(('/mod/','/sdk/')):file=PurePosixPath(file.replace('\\','/')).name
        mapped.append({'name':name,'elf_address':address,'size':size,'file':file,'line':symbol.get('Line',0),'column':symbol.get('Column',0)})
    document={'schema':2,'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'functions':mapped}
    if not any(item['line'] for item in mapped):raise ValueError('The ELF has no source lines. Build with --source-map or -gline-tables-only.')
    encoded=json.dumps(document,indent=2)+'\n'
    if len(encoded.encode())>1024*1024:raise ValueError('Source map exceeds 1 MiB')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(encoded,encoding='utf-8')
    print('Created source map: '+str(output))


def main():
    parser=argparse.ArgumentParser(description=__doc__);commands=parser.add_subparsers(dest='command',required=True)
    new=commands.add_parser('new');new.add_argument('--id',required=True);new.add_argument('--name');new.add_argument('--language',choices=('c','cpp'),default='c');new.add_argument('--output',type=Path,required=True)
    read=commands.add_parser('inspect');read.add_argument('package',type=Path)
    source=commands.add_parser('source-map');source.add_argument('elf',type=Path);source.add_argument('--output',type=Path,required=True);source.add_argument('--nm',default='llvm-nm');source.add_argument('--symbolizer',default='llvm-symbolizer')
    args=parser.parse_args();{'new':new_project,'inspect':inspect,'source-map':source_map}[args.command](args)


if __name__=='__main__':main()
