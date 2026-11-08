"""Allowlisted stable game packages. No game, helper or AppImage execution."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]
VERSION='1.0.7'
DENIED={'.z64','.v64','.n64','.eep','.mpk','.sra','.fla','.o2r','.otr','.pdb','.key','.pem','.jks','.dmp','.pyc'}
TROPHY_UPDATE='6f900a1005c892e4b86a48a3e4bacb7a1e01a7364680ebdd61af954cd94cf154'

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def copy(source,target):
    if not source.is_file():raise ValueError('Missing explicit release input: '+str(source))
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)

def tree(source,target):
    for path in source.rglob('*'):
        if path.is_symlink() and not path.resolve(strict=True).is_relative_to(source.resolve()):
            raise ValueError('Input link escapes explicit source root: '+str(path))
    shutil.copytree(source,target,symlinks=True)

def image(path,machine,version=False):
    data=path.read_bytes()
    if machine=='windows':
        if data[:2]!=b'MZ':raise ValueError('Not PE: '+str(path))
        at=struct.unpack_from('<I',data,0x3c)[0]
        if data[at:at+4]!=b'PE\0\0' or struct.unpack_from('<H',data,at+4)[0]!=0x8664:raise ValueError('Wrong PE architecture')
    elif data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0]!=(183 if machine=='aarch64' else 62):
        raise ValueError('Wrong ELF architecture: '+str(path))
    if version:
        if VERSION.encode() not in data and VERSION.encode('utf-16le') not in data:raise ValueError('Stable version missing')
        if b'1.0.7+beta.10' in data or '1.0.7+beta.10'.encode('utf-16le') in data:raise ValueError('Stale beta executable')
    return data

WATER_FEATURE_MARKER=None

def features(data, water_marker=b'workload-atomic promotion'):
    for marker in (b'complete-family selection',WATER_FEATURE_MARKER or water_marker,
        b'personal EEPROM committed and read-back verified',b'one-personal-save',
        b'The mod installation registry needs recovery; it was not replaced.',
        b'The track package contains more than one course. Import each .dkrmap separately.',
        b'frozen offline EEPROM verified; no mutable route switch'):
        if marker not in data:raise ValueError('Missing accepted feature: '+repr(marker))
    for marker in (b'DKR_REPLAY_CAPTURE_FILE',b'DKR_UPDATE_TEST_VERIFIER',b'DKR_WATER_TEST_MAP'):
        if marker in data:raise ValueError('Private fixture found')

def scan(root):
    records={}
    for path in sorted(root.rglob('*')):
        name=path.relative_to(root).as_posix()
        if path.is_symlink():
            if path.readlink().is_absolute() or not path.resolve(strict=True).is_relative_to(root.resolve()):raise ValueError('Unsafe package link')
            records[name]={'link':path.readlink().as_posix()};continue
        if not path.is_file():continue
        if path.suffix.lower() in DENIED or path.name.lower() in ('imgui.ini','device.bin'):raise ValueError('Private/non-runtime file: '+name)
        if 'custom-magic-codes' in {s.lower() for s in path.relative_to(root).parts}:
            if path.name!='dkr_custom_trophies.nrm' or sha(path)!=TROPHY_UPDATE:raise ValueError('Only the pinned runtime trophy updater belongs here')
        if {s.lower() for s in path.relative_to(root).parts}&{'.git','saves','logs','online-profile','optional-mods','__pycache__'}:
            raise ValueError('Private/authoring data in game package: '+name)
        if 'player guide' in name.lower() or path.suffix.lower() in ('.html','.pdf'):raise ValueError('Offline guide must not ship')
        with path.open('rb') as stream:
            if stream.read(4).hex() in ('80371240','37804012','40123780'):raise ValueError('ROM header found')
        records[name]={'sha256':sha(path),'bytes':path.stat().st_size}
    return records

def ownership(root,platform):
    spec=importlib.util.spec_from_file_location('release_ownership',ROOT/'scripts/update_release.py')
    rules=importlib.util.module_from_spec(spec);spec.loader.exec_module(rules)
    if platform=='windows-x64':
        subprocess.run([sys.executable,'-B',str(ROOT/'scripts/update_release.py'),'prepare-tree','--directory',str(root),'--version',VERSION,'--platform',platform],check=True)
    else:
        names=[]
        for path in root.rglob('*'):
            if path.is_dir() and not path.is_symlink():continue
            name=path.relative_to(root).as_posix()
            if not rules.safe_path(name) or rules.protected(name):raise ValueError('Unsafe owned path')
            names.append(name)
        name='assets/updates/install.json'
        if name not in names:names.append(name)
        path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(rules.canonical({'schema':1,'version':VERSION,'platform':platform,'files':sorted(names)}))

def notes(root,label):
    text=(ROOT/'packaging/RELEASE-README.md').read_text(encoding='utf-8').replace('# DKR-R v1.0.7','DKR-R v1.0.7 - '+label)
    (root/'README.txt').write_text(text,encoding='utf-8')
    copy(ROOT/'docs/PUBLIC-1.0.7-NOTES.txt',root/'RELEASE-NOTES.txt')

def trophy(source,target):
    if source is None or sha(source)!=TROPHY_UPDATE:raise ValueError('Provide the exact admitted trophy updater, not a repackaged SDK sample')
    copy(source,target/'Custom-Magic-Codes/dkr_custom_trophies.nrm')

def windows(a):
    root=a.output/('DKR-R-'+VERSION+'-Windows-x64');root.mkdir(parents=True)
    # No blanket template copy: only runtime art and required legal material.
    tree(a.compiled/'assets',root/'assets')
    tree(a.template/'ThirdPartyLicenses',root/'ThirdPartyLicenses')
    for name,source in [('LICENSE.md',ROOT/'LICENSE.md'),('THIRD_PARTY.md',ROOT/'THIRD_PARTY.md'),('COPYING-NOTICE.md',ROOT/'runtime-recomp/COPYING-NOTICE.md')]:copy(source,root/name)
    names=['DKR-R.exe']+['libexec/dkr-r/'+n for n in ('DKR-R-Runtime.exe','DKR-R-InputHost.exe','DKR-R-ModWorker.exe','DKR-R-Updater.exe','SDL2.dll','SDL3.dll','dxcompiler.dll','dxil.dll')]
    for name in names:
        image(a.compiled/name,'windows',name in ('DKR-R.exe','libexec/dkr-r/DKR-R-Runtime.exe'))
        copy(a.compiled/name,root/name)
    features((root/'libexec/dkr-r/DKR-R-Runtime.exe').read_bytes())
    trophy(a.trophy,root)
    notes(root,'Windows x64');ownership(root,'windows-x64');records=scan(root)
    archive=a.output/(root.name+'.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(root.rglob('*')):
            if path.is_file():z.write(path,root.name+'/'+path.relative_to(root).as_posix())
    with zipfile.ZipFile(archive) as z:
        if z.testzip():raise ValueError('ZIP CRC failed')
    return root,archive,records

def linux(a):
    root=a.output/('DKR-R-'+VERSION+'-Linux-'+a.arch+'.AppDir');root.mkdir(parents=True)
    tree(a.template/'usr/lib',root/'usr/lib')
    for name in ('applications','icons','metainfo'):
        if (a.template/'usr/share'/name).is_dir():tree(a.template/'usr/share'/name,root/'usr/share'/name)
    copy(ROOT/'packaging/linux/dkr-port.appdata.xml',root/'usr/share/metainfo/dkr-port.appdata.xml')
    for path in (root/'usr/share/applications').glob('*.desktop'):
        text=re.sub(r'^X-AppImage-Version=.*$', 'X-AppImage-Version='+VERSION,path.read_text(),flags=re.M)
        path.write_text(text)
    # Dependency copyright/licence directories remain; player/SDK docs do not.
    for path in (a.template/'usr/share/doc').iterdir():
        if path.name!='dkr-port':tree(path,root/'usr/share/doc'/path.name)
    legal=root/'usr/share/doc/dkr-port'
    for name in ('licenses','third-party'):
        if (a.template/'usr/share/doc/dkr-port'/name).is_dir():tree(a.template/'usr/share/doc/dkr-port'/name,legal/name)
    for name,source in [('LICENSE.md',ROOT/'LICENSE.md'),('THIRD_PARTY.md',ROOT/'THIRD_PARTY.md'),('COPYING-NOTICE.md',ROOT/'runtime-recomp/COPYING-NOTICE.md'),('WAVE-RACE-WATER-LICENSE.txt',ROOT/'patches/enhanced-water/LICENSE-WAVE-RACE.txt')]:copy(source,legal/name)
    for name in ('AppRun','dkr-r.png','.DirIcon'):
        path=a.template/name
        if path.is_symlink(): (root/name).symlink_to(path.readlink())
        elif path.exists():copy(path,root/name)
    for path in a.template.glob('*.desktop'):
        text=path.read_text();text=re.sub(r'^X-AppImage-Version=.*$', 'X-AppImage-Version='+VERSION,text,flags=re.M)
        (root/path.name).write_text(text)
    tree(a.compiled/'assets',root/'usr/bin/assets')
    pairs=[(a.compiled/'DKR-R',root/'usr/bin/DKR-R')]
    for name in ('DKR-R-ModWorker','DKR-R-Updater','DKR-R-InputHost','libSDL3.so.0'):
        pairs.append((a.compiled/'libexec/dkr-r'/name,root/'usr/libexec/dkr-r'/name))
    for source,target in pairs:
        image(source,a.arch,target.name=='DKR-R');copy(source,target);target.chmod(0o755)
    subprocess.run(['patchelf','--set-rpath','$ORIGIN/../lib',str(root/'usr/bin/DKR-R')],check=True)
    for name in ('DKR-R-ModWorker','DKR-R-Updater'):
        subprocess.run(['patchelf','--set-rpath','$ORIGIN/../../lib',str(root/'usr/libexec/dkr-r'/name)],check=True)
    subprocess.run(['patchelf','--set-rpath','$ORIGIN',str(root/'usr/libexec/dkr-r/DKR-R-InputHost')],check=True)
    features((root/'usr/bin/DKR-R').read_bytes());notes(root,'Linux '+a.arch)
    trophy(a.trophy,root/'usr/bin')
    platform='linux-aarch64' if a.arch=='aarch64' else 'linux-x86_64'
    ownership(root,platform);records=scan(root)
    original=image(a.runtime_image,a.arch)
    if original[8:11]!=b'AI\x02':raise ValueError('Not type-2 AppImage')
    offsets=[m.start() for m in re.finditer(b'hsqs',original[:4*1024*1024]) if subprocess.run(['unsquashfs','-s','-o',str(m.start()),str(a.runtime_image)],capture_output=True).returncode==0]
    if len(offsets)!=1:raise ValueError('Ambiguous AppImage runtime')
    squash=a.output/(root.stem+'.squashfs')
    subprocess.run(['mksquashfs',str(root),str(squash),'-noappend','-comp','zstd','-processors','3','-no-progress','-root-owned'],check=True)
    artifact=a.output/('DKR-R-'+VERSION+'-Linux-'+a.arch+'.AppImage')
    with artifact.open('xb') as out,squash.open('rb') as payload:out.write(original[:offsets[0]]);shutil.copyfileobj(payload,out)
    artifact.chmod(0o755)
    subprocess.run(['unsquashfs','-s','-o',str(offsets[0]),str(artifact)],check=True)
    return root,artifact,records

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
    for name in ('compiled','template','output','evidence'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arch',choices=('x86_64','aarch64'),default='x86_64');p.add_argument('--runtime-image',type=Path)
    p.add_argument('--trophy',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a NEW output directory')
    a.output.mkdir(parents=True)
    root,artifact,records=globals()[a.platform](a)
    defs=(a.evidence/'DKRExperimentalOwnedRegistry-COMPILE_DEFINITIONS.txt').read_text()
    ids=dict(re.findall(r'DKR_OWNED_BUILD_ID_V(77|80)="([a-f0-9]{64})"',defs))
    if set(ids)!={'77','80'}:raise ValueError('Missing owned IDs')
    receipt={'schema':1,'version':VERSION,'platform':a.platform,'arch':a.arch,'rollback_build_ids':ids,
        'artifact':{'name':artifact.name,'sha256':sha(artifact),'bytes':artifact.stat().st_size},'files':records,
        'offline_player_guide':False,'automatic_updates_signed':False,'validation':'Compilation and static package inspection only. Owner gameplay/hardware acceptance remains separate.',
        'release_source_manifest_sha256':sha(AUTH/'source-capsule-r2/manifest.json'),
        'accepted_water_sources':json.loads((AUTH/'source-capsule-r2/manifest.json').read_text())['roles']['LIQUID_STREAMING']}
    (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
    (a.output/'SHA256SUMS.txt').write_text(sha(artifact)+'  '+artifact.name+'\n')
    print('Stable release packaged without target execution:',artifact)

if __name__=='__main__':main()
