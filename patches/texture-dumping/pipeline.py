"""Receipt-checked texture observation and shared UI; isolated build copies only."""
from pathlib import Path
import argparse
import hashlib
import json

AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Texture dump anchor changed: '+old[:100])
    return text.replace(old,new)
def main(a):
    if a.output.resolve().is_relative_to(ROOT):raise ValueError('Choose an isolated build outside the checkout')
    rows=json.loads(a.inputs.read_text());swaps=[]
    a.output.mkdir(parents=True,exist_ok=True)
    for name in ('texture_capture_bridge.hpp','texture_dump.hpp','texture_decode.hpp','texture_gallery.inl','texture_dump.cpp','texture_dump_ui.inl'):
        (a.output/name).write_bytes((AUTH/'native'/name).read_bytes())
    for row in rows:
        path=Path(row['path']);original=path.read_bytes();text=original.decode().replace('\r\n','\n')
        if path.name=='runtime_ui.cpp':
            text=once(text,'#include "runtime_ui.hpp"','#include "runtime_ui.hpp"\n#include "texture_dump.hpp"')
            text=once(text,'#include "runtime_details_ui.inl"','#define DrawTexturesPage DrawTexturesPageWithoutDump\n#include "runtime_details_ui.inl"\n#undef DrawTexturesPage\n#include "texture_dump_ui.inl"')
            text=once(text,'    ReleaseModStoreTextures();','    ReleaseTextureDumpTextures();\n    ReleaseModStoreTextures();')
        elif path.name=='rt64_rdp_tmem.cpp':
            text=once(text,'#include "rt64_rdp_tmem.h"','#include "rt64_rdp_tmem.h"\n#include "texture_capture_bridge.hpp"')
            text=once(text,'uint32_t tlut, uint64_t &alias) {','uint32_t tlut, uint64_t &alias, char* capturedIdentity=nullptr) {')
            text=once(text,'            alias = riceNativeAlias(identityBuffer);','            if(capturedIdentity)std::snprintf(capturedIdentity,64,"%s",identityBuffer);\n            alias = riceNativeAlias(identityBuffer);')
            anchor='''        // Dump memory contents into a file if the process is active.
        if (!state->dumpingTexturesDirectory.empty()) {
            dumpTexture(nativeHash, state, loadTile, width, height, tlut);
        }'''
            observe='''        // Observation is independent of upload/cache misses and replacement
        // selection. No guest, GPU lifetime, replacement or simulation state is changed.
        if (const auto sink=dkr::texture_capture::observer.load(std::memory_order_acquire)) {
          const auto gate=dkr::texture_capture::gate.load(std::memory_order_acquire);
          if(gate&&gate(nativeHash)) {
            dkr::texture_capture::Tile tile{};
            tile.native_hash=nativeHash;
            const auto alias=riceAliasMap.find(nativeHash);
            if(alias!=riceAliasMap.end())tile.rice_alias=alias->second;
            uint64_t observedAlias=0;
            if(!calculateRiceAlias(state,loadTile,tlut,observedAlias,tile.rice_name)||observedAlias!=tile.rice_alias)
                tile.rice_name[0]=0; // Do not invent Rice names from reused source memory.
            tile.width=width;tile.height=height;tile.tlut=tlut;
            tile.fmt=loadTile.fmt;tile.siz=loadTile.siz;tile.line=loadTile.line;
            tile.tmem=loadTile.tmem;tile.palette=loadTile.palette;
            tile.cms=loadTile.cms;tile.cmt=loadTile.cmt;tile.masks=loadTile.masks;tile.maskt=loadTile.maskt;
            tile.shifts=loadTile.shifts;tile.shiftt=loadTile.shiftt;
            tile.uls=loadTile.uls;tile.ult=loadTile.ult;tile.lrs=loadTile.lrs;tile.lrt=loadTile.lrt;
            sink(tile,TMEM);
          }
        }
'''
            text=once(text,anchor,observe+anchor)
        else:raise ValueError('Unexpected dump source: '+str(path))
        out=a.output/path.name;out.write_text(text,encoding='utf-8',newline='\n')
        swaps.append({**row,'new':str(out.resolve()),'input_sha256':sha(original),'output_sha256':sha(text.encode())})
    (a.output/'dump-swaps.json').write_text(json.dumps(swaps,indent=2)+'\n')
    (a.output/'dump-receipt.json').write_text(json.dumps({'schema':1,'policy':'Patch Pipeline only; original source inputs read-only; no guest or save changes','files':{p.name:sha(p.read_bytes()) for p in a.output.iterdir() if p.is_file() and p.name!='dump-receipt.json'}},indent=2)+'\n')
    print('Texture dumping composed over accepted vehicle build; no game/mod/test execution.')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
