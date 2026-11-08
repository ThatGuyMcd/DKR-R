"""Package compiled beta 10 images without launching any target program."""
from pathlib import Path
import argparse
import hashlib
import html
import importlib.util
import json
import re
import shutil
import struct
import subprocess
import sys
import zipfile
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.7+beta.10'
DENIED={'.z64','.v64','.n64','.eep','.mpk','.sra','.fla','.o2r','.otr','.pdb','.dkr-probe','.dkr-bootstrap','.dkr-component'}

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def inspect_image(path,windows=False,version=False):
    data=path.read_bytes()
    if windows:
        if data[:2]!=b'MZ':raise ValueError('Not a PE image: '+str(path))
        at=struct.unpack_from('<I',data,0x3c)[0]
        if data[at:at+4]!=b'PE\0\0' or struct.unpack_from('<H',data,at+4)[0]!=0x8664:raise ValueError('Not Windows x64')
        if version and VERSION.encode('utf-16le') not in data:raise ValueError('Stale Windows version: '+str(path))
    else:
        if data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0]!=62:raise ValueError('Not Linux x86_64: '+str(path))
        if version and VERSION.encode() not in data:raise ValueError('Stale Linux version')
    return data

def inspect_features(data):
    for marker in (b'personal EEPROM committed and read-back verified',
        b'Your save changed since this editor was opened.',b'one-personal-save',
        b'Custom records have an invalid format; previous records were kept.',
        b'MAX OUT ENTIRE SAVE',b'BACKUPS & RECOVERY',b'USE FOR HOSTING',
        b'new sandbox seeded from saved progress',b'COPY PERSONAL SAVE TO DEVELOPMENT',b'[save][selected] owner=%s',
        b'frozen offline EEPROM verified; no mutable route switch',
        b'The track package contains more than one course. Import each .dkrmap separately.',
        b'This listing has unsaved changes. Keep editing, or discard them before leaving.'):
        if marker not in data:raise ValueError('Missing beta 10 or accepted beta 9 feature: '+repr(marker))
    for marker in (b'DKR_REPLAY_CAPTURE_FILE',b'DKR_UPDATE_TEST_VERIFIER',b'DKR_WATER_TEST_MAP'):
        if marker in data:raise ValueError('Private execution fixture in public image')

def inspect_tree(root):
    result={}
    for p in sorted(root.rglob('*')):
        if p.is_symlink() and not p.resolve().is_relative_to(root.resolve()):raise ValueError('Escaping symlink: '+str(p))
        if not p.is_file():continue
        if {part.lower() for part in p.relative_to(root).parts}&{'saves','logs','online-profile','rom-cache','dkr-runtime-data','.git'}:
            raise ValueError('Private data directory in package: '+str(p))
        if p.name.lower()=='device.bin' or p.suffix.lower() in {'.key','.pem'}:raise ValueError('Private key/account material in package')
        if p.suffix.lower() in DENIED:raise ValueError('Private/game data in package: '+str(p))
        with p.open('rb') as f:
            if f.read(4).hex() in ('80371240','37804012','40123780'):raise ValueError('ROM found in package')
        result[p.relative_to(root).as_posix()]=sha(p)
    return result

def notes(root,platform):
    shutil.copy2(AUTH/'RELEASE-NOTES.txt',root/'RELEASE-NOTES.txt')
    shutil.copy2(AUTH/'RELEASE-NOTES.txt',root/'SAVE-MANAGER-BETA10.txt')
    text=f'DKR-R v1.0.7 beta 10 - {platform}\n\n'
    text+=('Extract the entire folder and launch DKR-R.exe. Keep assets and libexec in place.\n' if platform=='Windows x64' else 'Make the AppImage executable and launch it.\n')
    text+='Bring your own supported original ROM. No ROM or save is included.\n\n'
    text+='Normal play and mods now share one personal Adventure. Older mod-set saves\nare kept in Save Manager > Backups & Recovery. Choose the progress you want\nto keep; DKR-R does not merge or delete those originals.\n\n'
    text+='Max Out and Unlock actions save when confirmed. Manual edits still use Apply.\nOnline and development saves remain separate. All online peers need beta 10.\n\n'
    text+='Read SAVE-MANAGER-BETA10.txt before testing. Keep an external data backup.\nCompilation and static packaging inspection only; gameplay acceptance is yours.\n'
    (root/'README.txt').write_text(text,encoding='utf-8')
    (root/'README.md').write_text('# DKR-R v1.0.7 beta 10\n\n'+text.split('\n\n',1)[1]+'\n',encoding='utf-8')

def zip_tree(directory,path):
    with zipfile.ZipFile(path,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(directory.rglob('*')):
            if p.is_file():z.write(p,directory.name+'/'+p.relative_to(directory).as_posix())
    with zipfile.ZipFile(path) as z:
        if z.testzip():raise ValueError('Archive CRC mismatch')

def build_ids(path):
    text=html.unescape(path.read_text(errors='replace')).replace('\\"','"')
    ids=dict(re.findall(r'DKR_OWNED_BUILD_ID_V(77|80)="([0-9a-f]{64})"',text))
    if set(ids)!={'77','80'}:raise ValueError('Missing per-revision rollback build IDs: '+str(path))
    return ids

def appdir_receipt(root):
    # AppImages replace one immutable bundle, not individual installation files.
    # Retain the accepted AppDir's internal launch/icon links, and apply the
    # first-party updater's ownership rules to every packaged entry.
    spec=importlib.util.spec_from_file_location('dkr_release_ownership',ROOT/'scripts/update_release.py')
    rules=importlib.util.module_from_spec(spec);spec.loader.exec_module(rules)
    owned=[]
    for path in sorted(root.rglob('*')):
        if path.is_dir() and not path.is_symlink():continue
        name=path.relative_to(root).as_posix()
        if not rules.safe_path(name) or rules.protected(name):raise ValueError('Unsafe AppDir ownership: '+name)
        if path.is_symlink() and (path.readlink().is_absolute() or not path.resolve(strict=True).is_relative_to(root.resolve())):
            raise ValueError('Unsafe AppDir link: '+name)
        owned.append(name)
    receipt=root/'assets/updates/install.json'
    if 'assets/updates/install.json' not in owned:owned.append('assets/updates/install.json')
    receipt.parent.mkdir(parents=True,exist_ok=True)
    receipt.write_bytes(rules.canonical({'schema':1,'version':VERSION,'platform':'linux-x86_64','files':sorted(owned)}))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('platform',choices=('windows','linux'))
    for name in ('stage','template','compiled','output','build-log','build-config'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--runtime-image',type=Path)
    a=p.parse_args()
    subprocess.run([sys.executable,'-B',str(AUTH/'pipeline.py'),'verify','--output',str(a.stage)],check=True)
    if a.output.exists():raise ValueError('Choose a NEW delivery directory')
    if a.output.resolve().is_relative_to(a.template.resolve()):raise ValueError('Delivery cannot replace its template')
    inspect_tree(a.template)
    log=a.build_log.read_text(encoding='utf-8-sig',errors='replace')
    if a.platform=='windows':
        if 'DKRPortGame.vcxproj ->' not in log or re.search(r'error (?:C\d|LNK\d|MSB\d)',log):raise ValueError('Windows compile/link did not complete')
        name='DKR-R-'+VERSION+'-Windows-x64'
    else:
        if 'Linking CXX executable bin/Release/DKR-R' not in log or 'FAILED:' in log:raise ValueError('Linux compile/link did not complete')
        name='DKR-R-'+VERSION+'-Linux-x86_64.AppDir'
    a.output.mkdir(parents=True)
    root=a.output/name;shutil.copytree(a.template,root,symlinks=a.platform=='linux')
    binaries={}
    if a.platform=='windows':
        compiled=a.compiled/'bin/RelWithDebInfo'
        replacements=['DKR-R.exe']+['libexec/dkr-r/'+n for n in ('DKR-R-Runtime.exe','DKR-R-InputHost.exe','DKR-R-ModWorker.exe','DKR-R-Updater.exe','SDL2.dll','SDL3.dll','dxcompiler.dll','dxil.dll')]
        for relative in replacements:
            src=compiled/relative;inspect_image(src,True,relative in ('DKR-R.exe','libexec/dkr-r/DKR-R-Runtime.exe'))
            shutil.copy2(src,root/relative);binaries[relative]=sha(src)
        inspect_features((root/'libexec/dkr-r/DKR-R-Runtime.exe').read_bytes())
        notes(root,'Windows x64')
        subprocess.run([sys.executable,'-B',str(ROOT/'scripts/update_release.py'),'prepare-tree','--directory',str(root),'--version',VERSION,'--platform','windows-x64'],check=True)
        files=inspect_tree(root)
        expected={x for x in replacements if x.endswith('.exe')}
        if {x for x in files if x.endswith('.exe')}!=expected:raise ValueError('Unexpected executable set')
        artifact=a.output/(name+'.zip');zip_tree(root,artifact)
    else:
        compiled=a.compiled/'bin/Release'
        for src,relative in ((compiled/'DKR-R','usr/bin/DKR-R'),
            (compiled/'libexec/dkr-r/DKR-R-ModWorker','usr/libexec/dkr-r/DKR-R-ModWorker'),
            (compiled/'libexec/dkr-r/DKR-R-Updater','usr/libexec/dkr-r/DKR-R-Updater')):
            inspect_image(src,False,relative=='usr/bin/DKR-R')
            target=root/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,target);target.chmod(0o755);binaries[relative]=sha(src)
        inspect_features((root/'usr/bin/DKR-R').read_bytes());notes(root,'Linux x86_64')
        appdir_receipt(root)
        files=inspect_tree(root)
        original=inspect_image(a.runtime_image)
        if original[8:11]!=b'AI\x02':raise ValueError('Expected type-2 AppImage runtime')
        offsets=[]
        for match in re.finditer(b'hsqs',original[:4*1024*1024]):
            offset=match.start()
            if subprocess.run(['unsquashfs','-s','-o',str(offset),str(a.runtime_image)],capture_output=True).returncode==0:offsets.append(offset)
        if len(offsets)!=1:raise ValueError('Ambiguous AppImage payload')
        squash=a.output/('DKR-R-'+VERSION+'-Linux-x86_64.squashfs')
        subprocess.run(['mksquashfs',str(root),str(squash),'-noappend','-comp','zstd','-processors','4','-no-progress'],check=True)
        artifact=a.output/('DKR-R-'+VERSION+'-Linux-x86_64.AppImage')
        with artifact.open('xb') as out:
            out.write(original[:offsets[0]])
            with squash.open('rb') as source:shutil.copyfileobj(source,out)
        artifact.chmod(0o755)
        subprocess.run(['unsquashfs','-s','-o',str(offsets[0]),str(artifact)],check=True)
    report={'schema':1,'version':VERSION,'platform':a.platform,'validation':'Compilation and static package inspection only. No target execution.',
        'authored_changes':'Patch Pipeline only; dependencies, checked-out runtime and existing generated game sources untouched',
        'source_receipt':json.loads((a.stage/'shared-saves-receipt.json').read_text()),'rollback_build_ids':build_ids(a.build_config),
        'build_log_sha256':sha(a.build_log),'packager_sha256':sha(Path(__file__)),'binaries':binaries,'files':files,
        'artifact':{'file':artifact.name,'sha256':sha(artifact),'bytes':artifact.stat().st_size},'automatic_update_signed':False}
    (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(report,indent=2)+'\n')
    (a.output/'SHA256SUMS.txt').write_text(sha(artifact)+'  '+artifact.name+'\n')
    print('Packaged without application execution: '+str(artifact))

if __name__=='__main__':main()
