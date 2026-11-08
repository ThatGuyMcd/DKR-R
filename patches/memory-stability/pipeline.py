"""Counted source composition; existing allocator and generated inputs stay read-only."""
from pathlib import Path
import argparse, hashlib, json, re, sys
sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
VERSION = '1.0.10-memory-playtest.1'

def sha(data): return hashlib.sha256(data).hexdigest()
def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != data: path.write_bytes(data)
def once(text, before, after):
    if text.count(before) != 1: raise ValueError('Memory stability anchor changed: '+before[:100])
    return text.replace(before, after, 1)

def compose(rows, output):
    if len(rows)!=4 or {(r['target'],Path(r['path']).name) for r in rows}!={
        ('DKRPortGame','runtime_legacy_mods.cpp'),('DKRPortGame','runtime_hud_layout.cpp'),
        ('DKRExperimentalOwnedGameV77','probe_native.c'),('DKRExperimentalOwnedGameV80','probe_native.c')}:
        raise ValueError('Expected resource callback, HUD diagnostic and both owned fallback units')
    outputs={'primary_heap.h':(AUTH/'native/primary_heap.h').read_bytes()}; swaps=[]
    for row in rows:
        source=Path(row['path']); raw=source.read_bytes(); text=raw.decode().replace('\r\n','\n')
        if source.name=='runtime_legacy_mods.cpp':
            text=once(text, '    const auto end=dkr::mods::legacy_heap_end(std::uint32_t(ctx->r15),dkr::runtime::legacy::guest_memory_size(),\n        bool(dkr::runtime::legacy::session.load()));',
                '    const auto admitted=dkr::mods::legacy_heap_end(std::uint32_t(ctx->r15),dkr::runtime::legacy::guest_memory_size(),\n        bool(dkr::runtime::legacy::session.load()));\n'
                '    const auto end=dkr_primary_heap_end(admitted,dkr::runtime::legacy::guest_memory_size());')
            text=once(text,'        std::fprintf(stderr,"[legacy][memory] offline custom-session pool uses existing 8 MiB RDRAM; stock/online pool unchanged\\n");',
                '        std::fprintf(stderr,"[memory][primary-pool] end=0x%08X mapped=%zu; deterministic residency headroom; deferred frees unchanged\\n",end,dkr::runtime::legacy::guest_memory_size());')
            text='#include "'+(output/'primary_heap.h').as_posix()+'"\n'+text
        elif source.name=='runtime_hud_layout.cpp':
            text='#include "'+(ROOT/'runtime-recomp/src/game/mods/legacy_memory_safety.h').as_posix()+'"\n'+text
            text=once(text,'    std::fprintf(stderr, "[hud][asset-load-failed]',
                '    const dkr_memory_view view{rdram,0x800000U,v80?80U:77U};\n'
                '    const auto heap=dkr_memory_measure(view);\n'
                '    std::fprintf(stderr,"[hud][heap] sprite=%u free=%u largest=%u slots=%u/%u queue=%u peak=%u valid=%d\\n",\n'
                '        sprite,heap.free_bytes,heap.largest_free,heap.used_slots,heap.max_slots,heap.queue,heap.high_water,heap.valid);\n\n'
                '    std::fprintf(stderr, "[hud][asset-load-failed]')
        else:
            text=once(text,'    if(dkr_probe_mod_dispatch(name,ram,context,0,0,0,0,&mod_result))return (int)mod_result;',
                '    if(dkr_probe_mod_dispatch(name,ram,context,0,0,0,0,&mod_result))return (int)mod_result;\n'
                '    // No asset world (vanilla/code-only): same known-pool policy.\n'
                '    // A handled custom admission above always keeps precedence.\n'
                '    if(strcmp(name,"dkr_legacy_heap_capacity")==0) {\n'
                '        context->r15=(int32_t)dkr_primary_heap_end((uint32_t)context->r15,0x800000U);\n'
                '        return 0;\n'
                '    }')
            text='#include "'+(output/'primary_heap.h').as_posix()+'"\n'+text
            # Only the exact entry prefix runs in the no-service fixture. All
            # remaining native operations are outside this narrow regression.
            prefix=text.split('int dkr_probe_native(const char* name, uint8_t* ram, struct recomp_context* context) {',1)[1].split('    /* modern_camera.cpp:',1)[0]
            entry='#include "recomp.h"\n#include <string.h>\n#include "'+(output/'primary_heap.h').as_posix()+'"\n'
            entry+='void dkr_probe_checkpoint(void);\nint dkr_probe_mod_dispatch(const char*,uint8_t*,recomp_context*,const uint64_t*,unsigned,const uint32_t*,unsigned,uint64_t*);\n'
            entry+='int dkr_memory_owned_heap_entry(const char* name,uint8_t* ram,recomp_context* context) {'+prefix+'\n return 0;\n}\n'
            outputs['owned-entry'+row['target'][-2:]+'.c']=entry.encode()
        # Preserve relative include resolution after moving the translation unit.
        def include(m):
            name=Path(m[1]); path=name if name.is_absolute() else source.parent/name
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        text=re.sub(r'^#include "([^"\n]+)"',include,text,flags=re.M)
        name=row['target']+'/'+source.name; outputs[name]=text.encode()
        swaps.append({**row,'new':str(output/name),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
    return swaps,outputs

ALLOCATOR=('mempool_init_main','mempool_init','mempool_alloc','mempool_alloc_safe',
    'mempool_slot_find','mempool_slot_assign','mempool_slot_clear','mempool_get_pool',
    'mempool_free','mempool_free_addr','mempool_free_queue','mempool_free_queue_clear','mempool_free_timer')

def extract(rows, output):
    # Execute only the original allocator bodies on synthetic RAM. Code-mod
    # dispatch is intentionally inactive; no ROM bytes or game assets are read.
    found={}; receipts=[]
    for row in rows:
        source=Path(row['path']); text=source.read_text().replace('\r\n','\n'); rev=row['revision']
        for name in ALLOCATOR:
            matches=list(re.finditer(r'(?:static void dkr_code_body_|RECOMP_FUNC void )'+name+r'\(uint8_t\* rdram, recomp_context\* ctx\) \{',text))
            if not matches: continue
            original=[m for m in matches if m[0].startswith('static')]
            if original: matches=original
            if len(matches)!=1: raise ValueError('Ambiguous allocator fixture '+name)
            start=matches[0].end(); depth=1
            for tok in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\{|\}',text[start:]):
                if tok[0]=='{':depth+=1
                if tok[0]=='}':depth-=1
                if not depth:break
            else:raise ValueError('Unbalanced allocator function')
            body=text[start:start+tok.start()]
            key=(rev,name)
            if key in found:raise ValueError('Duplicate final allocator '+name)
            found[key]=body;receipts.append({'revision':rev,'name':name,'input':str(source),'input_sha256':sha(source.read_bytes()),'body_sha256':sha(body.encode())})
    for rev in (77,80):
        if {n for r,n in found if r==rev}!=set(ALLOCATOR):raise ValueError('Allocator fixture closure incomplete')
        data='#include "recomp.h"\n#include "funcs.h"\n'
        for name in ALLOCATOR:data+='\nRECOMP_FUNC void '+name+'(uint8_t* rdram, recomp_context* ctx) {'+found[rev,name]+'}\n'
        write(output/f'allocator{rev}.c',data.encode())
    write(output/'allocator-inputs.json',(json.dumps(receipts,indent=2)+'\n').encode())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--allocators',type=Path);a=p.parse_args();out=a.output.resolve()
    if out.is_relative_to(ROOT):raise ValueError('All generated outputs must be outside the checkout')
    rows,outputs=compose(json.loads(a.inputs.read_text()),out)
    for name,data in outputs.items():write(out/name,data)
    write(out/'swaps.json',(json.dumps(rows,indent=2)+'\n').encode())
    if a.allocators:extract(json.loads(a.allocators.read_text()),out)
    print('Primary heap headroom and bounded HUD evidence staged; allocator and deferred frees unchanged.')
