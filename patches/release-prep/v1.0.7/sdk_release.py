"""Publish the exact corrected SDK sources and current Bible, separately from the game."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import zipfile
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]
ALLOWED={'.py','.c','.h','.hpp','.inc','.inl','.ld','.toml','.json','.txt'}

def copy_sources(source,target):
    for path in sorted(source.rglob('*')):
        if not path.is_file() or '__pycache__' in path.parts or path.suffix=='.pyc':continue
        if path.is_symlink() or path.suffix.lower() not in ALLOWED:raise ValueError('Unexpected SDK source: '+str(path))
        out=target/path.relative_to(source);out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,out)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','bible','mod','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--trophy',type=Path)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a NEW SDK output')
    kit=a.output/'DKR-R-Modding-SDK-2.0';copy_sources(a.source,kit)
    readme=(ROOT/'patches/sdk2/SDK-README.txt').read_text(encoding='utf-8')
    for name in ('README.txt','SDK.txt','USER-GUIDE.txt','IMPLEMENTATION-STATUS.txt'):(kit/name).write_text(readme,encoding='utf-8')
    for extension in ('txt','html','pdf'):
        name='DKR-R-Modding-Bible-SDK-2.0.'+extension;shutil.copy2(a.bible/name,kit/name)
    shutil.copy2(a.bible/'DKR-R-Modding-Bible-SDK-2.0.txt',kit/'MODDERS-GUIDE.txt')
    shutil.copy2(a.bible/'DKR-R-Modding-Bible-SDK-2.0.html',kit/'MODDERS-GUIDE.html')
    shutil.copy2(ROOT/'LICENSE.md',kit/'LICENSE.txt')
    shutil.copy2(ROOT/'docs/PUBLIC-1.0.7-NOTES.txt',kit/'RELEASE-NOTES.txt')
    shutil.copy2(a.mod,kit/'DKR-R-Dev-Menu-2.0.1.nrm')
    if a.trophy:
        if hashlib.sha256(a.trophy.read_bytes()).hexdigest()!='6f900a1005c892e4b86a48a3e4bacb7a1e01a7364680ebdd61af954cd94cf154':raise ValueError('Incorrect pinned trophy updater')
        shutil.copy2(a.trophy,kit/'dkr_custom_trophies.nrm')
    # The public repo now exposes the same SDK 2 builder/headers/examples as
    # the release download, instead of an obsolete API-1-only authoring kit.
    if a.source.resolve()!=(ROOT/'modding').resolve():copy_sources(a.source,ROOT/'modding')
    for name in ('SDK.txt','USER-GUIDE.txt','IMPLEMENTATION-STATUS.txt'):(ROOT/'modding'/name).write_text(readme,encoding='utf-8')
    (ROOT/'modding/MODDERS-GUIDE.txt').write_text((a.bible/'DKR-R-Modding-Bible-SDK-2.0.txt').read_text(encoding='utf-8').rstrip()+'\n',encoding='utf-8',newline='\n')
    records={path.relative_to(kit).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(kit.rglob('*')) if path.is_file()}
    archive=a.output/'DKR-R-Modding-SDK-2.0-v1.0.7.zip'
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name in records:z.write(kit/name,kit.name+'/'+name)
    with zipfile.ZipFile(archive) as z:
        if z.testzip():raise ValueError('SDK archive CRC failure')
    (a.output/'SDK-FILES.json').write_text(json.dumps(records,indent=2)+'\n')
    print('Current SDK 2 source and separate release kit:',archive)

if __name__=='__main__':main()
