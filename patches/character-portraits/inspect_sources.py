"""Static source/provenance inspection only. Does not execute game/mod code."""
from pathlib import Path
import argparse,contextlib,hashlib,json,re,sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import character_portrait_pipeline as pipeline
from save_layout_pipeline import DEF

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('task',type=Path);p.add_argument('baseline',type=Path)
a=p.parse_args()
with contextlib.redirect_stdout(sys.stderr):pipeline.verify_native(a.task/'native')

def functions(folder):
    out={}
    for file in folder.glob('*.c'):
        text=file.read_text();matches=list(DEF.finditer(text))
        for i,m in enumerate(matches):
            if m[1] in out:raise ValueError('Duplicate CPU definition')
            end=matches[i+1].start() if i+1<len(matches) else len(text)
            out[m[1]]=text[m.start():end]
    return out

report={'inspection':'Static source comparison only; no game/mod/test execution',
        'native_changed_files':sorted(pipeline.PINS),'revisions':{}}
for revision in (77,80):
    old=a.baseline/('generated-v'+str(revision));new=a.task/('generated-v'+str(revision))
    before,after=functions(old),functions(new)
    if before.keys()!=after.keys():raise ValueError('Unexpected CPU function added/removed')
    changed=[n for n in before if before[n]!=after[n]]
    if changed!=['obj_loop_characterflag']:raise ValueError('Unrelated CPU changes: '+str(changed))
    inserted=(pipeline.AUTH/'world_portrait.inc').read_text()+'\n'
    if after[changed[0]].replace(inserted,'')!=before[changed[0]]:
        raise ValueError('Original world sign instructions were changed')
    # Static ABI guard: uint32 guest addresses must not enter MEM_* directly.
    # This is source inspection, not game/mod execution or a gameplay test.
    for pointer in ('portrait_cell','portrait_draw'):
        if 'MEM_W(0,(gpr)(int32_t)'+pointer+')' not in inserted:
            raise ValueError('Portrait guest pointer lacks MIPS sign extension: '+pointer)
        if re.search(r'MEM_\w+\([^,]+,\s*'+pointer+r'\s*\)',inserted):
            raise ValueError('Unsigned portrait pointer passed directly to MEM_*')
    for file in old.rglob('*'):
        if file.is_file() and file.name!='funcs_29.c' and (new/file.relative_to(old)).read_bytes()!=file.read_bytes():
            raise ValueError('Unrelated payload file changed: '+file.name)
    with contextlib.redirect_stdout(sys.stderr):receipt=pipeline.owned_receipt(new,revision)
    owned=json.loads((a.task/('owned-v'+str(revision))/'manifest.json').read_text())
    if owned['character_portrait_pipeline']!=receipt:raise ValueError('Portrait provenance absent from rollback contract')
    for option in ('runtime_symbol_isolation','audio_symbol_isolation','checked_inline_memory','owned_mod_services','full_scenes'):
        if not owned[option]:raise ValueError('Required rollback safety gate is missing: '+option)
    report['revisions'][str(revision)]={'cpu_function_count':len(after),'changed_guest_functions':changed,
        'portrait_pointer_abi':'Two explicit sign-extended gpr reads; aligned KSEG0 pointer guards',
        'native_overlay_sha256':receipt['native_sha256'],'checked_rollback_functions':len(owned['guest_functions'])}

patterns=[r'Owned v77 rollback contract: ([0-9a-f]{64})',r'Owned v80 rollback contract: ([0-9a-f]{64})',
          r'Owned rollback contract: ([0-9a-f]{64})']
contracts=[]
for platform in ('Windows','Linux'):
    text=(a.task/(platform+'-configure.log')).read_text(encoding='utf-8-sig')
    contracts.append([re.findall(pattern,text)[-1] for pattern in patterns])
if contracts[0]!=contracts[1]:raise ValueError('Cross-platform rollback identities differ')
report['matching_rollback_contracts']=dict(zip(('v77','v80','combined'),contracts[0]))
print(json.dumps(report,indent=2))
