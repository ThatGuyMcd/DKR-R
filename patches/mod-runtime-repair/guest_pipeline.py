"""Reconcile final init_track viewport count before any track generation."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import sys

sys.dont_write_bytecode = True
AUTH=Path(__file__).resolve().parent
sys.path.insert(0,str(AUTH.parent/'custom-vehicles'))
spec=importlib.util.spec_from_file_location('repair_guest_body',AUTH.parent/'custom-vehicles/guest_pipeline.py')
body_module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(body_module)


def main(args):
    found=0;swaps=[]
    for name in json.loads(args.sources.read_text()):
        path=Path(name)
        if path.suffix!='.c':continue
        text=path.read_text()
        span=body_module.body(text,'init_track')
        if not span:continue
        found+=1
        start,end=span
        body=text[start:end]
        import re
        prologue=re.search(r'// 0x([A-Fa-f0-9]+): addiu\s+\$sp, \$sp, -0x28',body)
        if not prologue:raise ValueError('Unreviewed init_track prologue')
        address=int(prologue.group(1),16)
        addresses={0x800249F0:0x8012350C,0x80024A30:0x80123A8C}
        if address not in addresses:raise ValueError('Unreviewed init_track revision: '+hex(address))
        previous=addresses[address]
        insertion='''
    /* post-mod-init-track=v1: before callbacks have already run, but no
       generated track or renderer may have borrowed a heap cursor yet. */
    const int32_t repair_players=(int32_t)ctx->r6;
    const int32_t repair_previous=MEM_W(0,(int32_t)0x@PREVIOUS@U);
    if(repair_players>=0&&repair_players<4&&repair_previous!=repair_players) {
        recomp_context repair_context=*ctx;
        if(repair_players>repair_previous) {
            extern void dkr_render_heap_reserve(uint8_t*,recomp_context*);
            dkr_render_heap_reserve(rdram,&repair_context);
        }
        repair_context.r4=repair_players;
        alloc_displaylist_heap(rdram,&repair_context);
        repair_context.r4=repair_previous;repair_context.r5=repair_players;
        repair_context.r6=MEM_W(0,(int32_t)0x@PREVIOUS@U);
        extern void dkr_render_heap_sync_notice(uint8_t*,recomp_context*);
        dkr_render_heap_sync_notice(rdram,&repair_context);
    }
'''.replace('@PREVIOUS@',f'{previous:08X}')
        text=text[:start]+insertion+text[start:]
        target=args.output/path.name
        if target.resolve()==path.resolve():raise ValueError('Guest input cannot be overwritten')
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(text,newline='\n')
        swaps.append({'old':str(path.resolve()),'new':str(target.resolve()),'function':'init_track',
                      'input_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                      'output_sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
    if found!=1:raise ValueError('Expected exactly one final init_track body, found '+str(found))
    (args.output/'swaps.json').write_text(json.dumps(swaps,indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    main(parser.parse_args())
