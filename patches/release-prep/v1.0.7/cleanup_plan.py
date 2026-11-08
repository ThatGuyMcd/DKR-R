"""List obsolete local output before recoverable relocation; no source deletion."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]

def access(path):
    value=str(path)
    return Path('\\\\?\\'+value) if os.name=='nt' and not value.startswith('\\\\?\\') else path

def sha(path):
    h=hashlib.sha256()
    with access(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cache',type=Path,nargs='+',required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Never overwrite a removal list')
    tracked=set(subprocess.check_output(['git','-C',str(ROOT),'ls-files','-z']).decode().split('\0'))
    keep=set()
    for cache in a.cache:
        text=cache.read_text(encoding='utf-8',errors='replace').replace('\\','/')
        keep.update(re.findall(r'(?:C:/DKRPort|/mnt/c/DKRPort)/build/([^/;\r\n"<>]+)',text,re.I))
    targets=[];holds=[]
    candidates=list((ROOT/'build').iterdir())
    candidates += [ROOT/'squashfs-root',ROOT/'build-logs',AUTH/'source-capsule']
    for path in sorted(candidates):
        if not path.exists():continue
        name=path.relative_to(ROOT).as_posix()
        if path.parent==ROOT/'build' and path.name in keep:
            holds.append({'path':name,'reason':'Referenced by a current platform CMake cache/prepared-input contract'});continue
        if any(n==name or n.startswith(name+'/') for n in tracked):
            holds.append({'path':name,'reason':'Contains tracked source; not an output-only cleanup target'});continue
        if path.is_symlink() or path.is_junction():
            holds.append({'path':name,'reason':'Top-level link requires separate review'});continue
        entries=[];unsafe=False
        for base,dirs,files in os.walk(access(path),followlinks=False):
            if '.git' in dirs or '.git' in files:
                unsafe=True;dirs.clear();break
            for leaf in list(dirs):
                child=Path(str(Path(base)/leaf).removeprefix('\\\\?\\'))
                if leaf=='.git' or access(child).is_symlink() or access(child).is_junction():unsafe=True;dirs.remove(leaf)
            if unsafe:dirs.clear();break
            for leaf in files:
                child=Path(str(Path(base)/leaf).removeprefix('\\\\?\\'))
                if child.is_symlink():unsafe=True;continue
                try:entries.append({'path':child.relative_to(ROOT).as_posix(),'bytes':access(child).stat().st_size,'sha256':sha(child)})
                except OSError:unsafe=True;break
            if unsafe:dirs.clear();break
        if path.is_file():entries=[{'path':name,'bytes':path.stat().st_size,'sha256':sha(path)}]
        if unsafe or any('/.git/' in x['path'] or x['path'].endswith('/.git') for x in entries):
            holds.append({'path':name,'reason':'Contains links or an embedded dependency checkout; protected/retained'});continue
        targets.append({'path':name,'source':str(path.resolve()),'files':entries,'bytes':sum(e['bytes'] for e in entries),
            'reason':'Superseded local build/audit output; final native source is reconstructed by source-capsule-r2. No current cache references this top-level output.'})
        print('Inventoried:',name,len(entries),'files',flush=True)
    report={'schema':1,'source_root':str(ROOT),'backup_root':'F:/DKR Dist dump/v1.0.7-release-20261020/obsolete-output',
        'cache_inputs':[str(x) for x in a.cache],'retained_build_roots':sorted(keep),'targets':targets,'holds':holds}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    lines=['DKR-R v1.0.7 - RECOVERABLE OUTPUT RELOCATION LIST','Nothing on this list is permanently deleted.','']
    for t in targets:lines.append(t['path']+' | '+str(len(t['files']))+' files | '+str(t['bytes'])+' bytes')
    lines += ['','HOLD / RETAINED:']+[x['path']+' | '+x['reason'] for x in holds]
    a.output.with_suffix('.txt').write_text('\n'.join(lines)+'\n')
    print('Listed before any move:',len(targets),'locations;',sum(len(t['files']) for t in targets),'files;',sum(t['bytes'] for t in targets),'bytes')
    print('Retained prepared roots:',sorted(keep));print(a.output.with_suffix('.txt'))

if __name__=='__main__':main()
