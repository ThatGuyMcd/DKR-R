#!/usr/bin/env python3
"""Patch Pipeline: derive address-only SDK bindings from a pinned ELF.

No ROM bytes or original implementations are distributed. Inputs are read-only;
output is always a new directory. The receipt pins the full ELF and symbol list.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def symbols(path: Path):
    data=path.read_bytes()
    if data[:6]!=b'\x7fELF\x01\x02':raise ValueError('Expected big-endian ELF32')
    start=struct.unpack_from('>I',data,32)[0]
    stride,count=struct.unpack_from('>HH',data,46)
    if stride!=40 or count>4096 or start+stride*count>len(data):raise ValueError('Invalid ELF directory')
    headers=[struct.unpack_from('>10I',data,start+i*stride) for i in range(count)]
    found={}
    for h in headers:
        if h[1]!=2:continue
        if h[9]!=16 or h[5]%16 or h[4]+h[5]>len(data) or h[6]>=count:raise ValueError('Invalid symbols')
        strings=headers[h[6]];names=data[strings[4]:strings[4]+strings[5]]
        for at in range(h[4],h[4]+h[5],16):
            name,address,size,info,_,section=struct.unpack_from('>IIIBBH',data,at)
            kind=info&15
            if kind not in (1,2) or not size or not 0x80000400<=address<0x80800000 or size>0x80800000-address:continue
            if section>=count or not(headers[section][2]&2):continue
            end=names.find(b'\0',name)
            if end<name:raise ValueError('Unterminated symbol')
            label=names[name:end].decode('ascii')
            if not re.fullmatch(r'[A-Za-z_]\w*',label):continue
            entry=(address,size,kind)
            if label in found and found[label]!=entry:raise ValueError('Ambiguous symbol '+label)
            found[label]=entry
    if len(found)<1000:raise ValueError('Unexpectedly incomplete DKR symbol table')
    return found


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf',type=Path,required=True)
    p.add_argument('--revision',type=int,choices=(77,80),required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    if out.exists() or out in a.elf.resolve().parents:raise ValueError('A NEW output directory is required')
    values=symbols(a.elf);out.mkdir(parents=True)
    rows=['// Generated address-only SDK bindings; see receipt.json.']
    rows += [f'{{"{name}",0x{address:08X}U,{size}U,{kind==2 and "true" or "false"}}},'
        for name,(address,size,kind) in sorted(values.items())]
    (out/'game_symbols.inc').write_text('\n'.join(rows)+'\n',encoding='utf-8')
    funcs=['[[section]]','name = ".main"','rom = 0x1000','vram = 0x80000400','size = 0x7FFC00','functions = [']
    funcs += [f'{{ name = "{name}", vram = 0x{address:08X}, size = 0x{size:X} }},'
        for name,(address,size,kind) in sorted(values.items()) if kind==2]
    funcs += [']']
    (out/'functions.toml').write_text('\n'.join(funcs)+'\n',encoding='utf-8')
    objects=['[[section]]','name = ".main"','rom = 0x1000','vram = 0x80000400','size = 0x7FFC00','symbols = [']
    objects += [f'{{ name = "{name}", vram = 0x{address:08X} }},'
        for name,(address,_,kind) in sorted(values.items()) if kind==1]
    (out/'data.toml').write_text('\n'.join(objects+["]"])+'\n',encoding='utf-8')
    # Data may be absolute addresses in this non-relocatable retail namespace.
    # The explicit revision declaration is mandatory for raw-address packages.
    (out/'symbols.ld').write_text('\n'.join(f'{name} = 0x{address:08X};'
        for name,(address,_,_) in sorted(values.items()))+'\n',encoding='utf-8')
    receipt=dict(schema=1,revision=f'us.v{a.revision}',elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),
        files={name:hashlib.sha256((out/name).read_bytes()).hexdigest() for name in ('game_symbols.inc','functions.toml','data.toml','symbols.ld')})
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(f'Address-only SDK kit: {len(values)} symbols -> {out}')


if __name__=='__main__':main()
