"""Collect and statically inspect the beta 7 startup-fix delivery. No target execution."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import zipfile

sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
sys.path.insert(0,str(AUTH/'sdk'))
from relocations import symbols
import inspect_delivery as inspection

VERSION='1.0.7+beta.7'
MARKER=b'frozen offline EEPROM verified; no mutable route switch'
RETIRED=b'isolated save route did not activate'


def sha(data):return hashlib.sha256(data).hexdigest()


def appimage(path,mod,machine):
    data=path.read_bytes();inspection.elf(data,machine)
    if data[8:11]!=b'AI\x02':raise ValueError('Not a type-2 AppImage')
    header=data[:2*1024*1024];offsets=[];at=0
    while (at:=header.find(b'hsqs',at))>=0:
        if at+96<=len(header):
            values=struct.unpack_from('<5I6H8Q',header,at)
            if values[3] in (65536,131072,262144,1048576) and values[5] in range(1,7) and values[9:11]==(4,0) and values[12]<=len(data)-at:
                offsets.append(at)
        at+=4
    if len(offsets)!=1:raise ValueError('Ambiguous AppImage filesystem')
    image='/mnt/'+path.drive[0].lower()+path.as_posix()[2:]
    def read(member):
        return subprocess.run(['wsl','-d','Ubuntu-24.04','--','unsquashfs','-cat','-o',str(offsets[0]),image,member],
            capture_output=True,check=True).stdout
    runtime=read('usr/bin/DKR-R');inspection.elf(runtime,machine)
    if MARKER not in runtime or RETIRED in runtime:raise ValueError('AppImage still contains the broken startup')
    if read('usr/share/doc/dkr-port/Optional-Mods/DKR-R-Dev-Menu.nrm')!=mod:raise ValueError('AppImage contains the wrong mod edition')
    return {'squashfs_offset':offsets[0],'native_sha256':sha(runtime),'startup_marker':True,'embedded_mod_matches':True}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('stage','products','output','windows-project','android-ninja','linux-ninja','arm-ninja'):
        p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();task=a.products.resolve(strict=True);out=a.output.resolve()
    if out.exists():raise ValueError('Choose a NEW delivery directory')
    configs=[a.windows_project,a.android_ninja,a.linux_ninja,a.arm_ninja];identities=[]
    for path in configs:
        text=html.unescape(path.read_text()).replace('\\"','"')
        ids=dict(re.findall(r'DKR_OWNED_BUILD_ID_V(77|80)="([a-f0-9]{64})"',text))
        if set(ids)!={'77','80'}:raise ValueError('Missing owned identities in '+str(path))
        identities.append(ids)
    if any(ids!=identities[0] for ids in identities):raise ValueError('Platform contract mismatch')
    for name,needle in [('compile-windows.log','DKRPortGame.vcxproj ->'),('compile-linux.log','Linking CXX executable'),
                        ('compile-arm64.log','Linking CXX executable'),('compile-android.log','Linking CXX shared library'),
                        ('package-android.log','BUILD SUCCESSFUL')]:
        text=(task/name).read_text(errors='replace')
        if needle not in text or re.search(r'(^|\n)(FAILED:|.*fatal error:|.*error C[0-9]+:)',text):
            raise ValueError('Incomplete compilation/assembly gate: '+name)
    certificate='b4e82ec59de3b8cdbe2ad1c7e84e0eba271cde79da64aea1811c81aa3b82e262'
    if 'certificate SHA-256 digest: '+certificate not in (task/'android-signature.txt').read_text():
        raise ValueError('Android signing identity changed')
    if "versionCode='1070017'" not in (task/'android-metadata.txt').read_text():raise ValueError('Android versionCode did not advance')
    mod=(task/'mod-compile/dkr_development_menu.nrm').read_bytes()
    with zipfile.ZipFile(task/'mod-compile/dkr_development_menu.nrm') as package:
        manifest=json.loads(package.read('mod.json'));metadata=json.loads(package.read('dkr.json'))
        if manifest['version']!='2.0.1' or metadata['api']!=2 or not metadata['development']:raise ValueError('Incorrect Dev Menu edition')
        sections,relocs=symbols(package.read('mod_syms.bin'),package.read('mod_binary.bin'))
    out.mkdir(parents=True)
    products={
        'Windows-x64.zip':task/'windows-public'/f'DKR-R-{VERSION}-Windows-x64.zip',
        'Linux-x86_64.AppImage':task/'linux-final'/f'DKR-R-{VERSION}-Linux-x86_64.AppImage',
        'Linux-aarch64.AppImage':task/'arm64-final'/f'DKR-R-{VERSION}-Linux-aarch64.AppImage',
        'SteamDeck-x86_64.AppImage':task/'steamdeck-final'/f'DKR-R-{VERSION}-SteamDeck-x86_64.AppImage',
        'SteamDeck-x86_64.tar.gz':task/'steamdeck-final'/f'DKR-R-{VERSION}-SteamDeck-x86_64.tar.gz',
        'Android-arm64.apk':task/'android-final'/f'DKR-R-{VERSION}-Android-arm64.apk'}
    for suffix,source in products.items():shutil.copy2(source,out/f'DKR-R-{VERSION}-Startup-Fix-{suffix}')
    shutil.copy2(task/'mod-compile/dkr_development_menu.nrm',out/'DKR-R-Dev-Menu-2.0.1.nrm')
    shutil.copy2(task/'sdk-final/DKR-R-Modding-SDK-2.0-beta.7.zip',out/'DKR-R-Modding-SDK-2.0-beta.7-Startup-Fix.zip')
    shutil.copy2(AUTH/'STARTUP-FIX-README.txt',out/'README.txt');shutil.copy2(AUTH/'RELEASE-NOTES.txt',out/'RELEASE-NOTES.txt')
    shutil.copy2(task/'mod-compile/relocation-report.json',out/'SDK2-RELOCATION-REPORT.json')
    with zipfile.ZipFile(out/'DKR-R-Dev-Menu-2.0.1-Source.zip','x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        source=a.stage/'sdk/examples/dev-menu'
        for file in sorted(source.rglob('*')):
            if file.is_file() and file.suffix!='.pyc' and '__pycache__' not in file.parts:
                archive.write(file,'dkr-development-menu/'+file.relative_to(source).as_posix())
    records=[];linux_record=None
    for path in sorted(out.iterdir()):
        data=path.read_bytes();record={'file':path.name,'bytes':len(data),'sha256':sha(data)}
        if path.suffix in ('.zip','.apk','.nrm'):
            with zipfile.ZipFile(path) as archive:
                if archive.testzip() is not None:raise ValueError('Archive CRC mismatch')
                if 'Windows-x64' in path.name:
                    runtime=archive.read(next(n for n in archive.namelist() if n.endswith('/libexec/dkr-r/DKR-R-Runtime.exe')))
                    if MARKER not in runtime or RETIRED in runtime:raise ValueError('Windows package is stale')
                if path.suffix=='.apk':
                    runtime=archive.read('lib/arm64-v8a/libDKR-R.so')
                    if MARKER not in runtime or RETIRED in runtime:raise ValueError('APK contains old startup code')
            record.update(inspection.zip_inspect(path,mod))
        elif path.suffix=='.AppImage':
            if 'SteamDeck' in path.name:
                if linux_record is None or record['sha256']!=linux_record['sha256']:raise ValueError('Steam Deck AppImage differs from the x86_64 payload')
                record['same_payload_as_linux_x86_64']=True
            else:
                record.update(appimage(path,mod,183 if 'aarch64' in path.name else 62))
                if 'x86_64' in path.name:linux_record=record
        elif path.name.endswith('.tar.gz'):
            with tarfile.open(path,'r:gz') as archive:
                for member in archive.getmembers():
                    inspection.name_ok(member.name)
                    if (member.issym() or member.islnk()) and (member.linkname.startswith('/') or '..' in Path(member.linkname).parts):raise ValueError('Unsafe tar link')
                runtime=archive.extractfile(next(m for m in archive.getmembers() if m.name.endswith('/usr/bin/DKR-R'))).read()
                if MARKER not in runtime or RETIRED in runtime:raise ValueError('Steam Deck kit has old startup code')
                record['members']=len(archive.getmembers())
        records.append(record)
    report={'schema':1,'version':VERSION,'edition':'Dev Menu Startup Fix','execution':'Compile and static inspection only; no game/mod/unit/replay/multiplayer/headset execution.',
        'engine_identity':(a.stage/'sdk2-engine-identity.txt').read_text().strip(),'owned_ids':identities[0],
        'native_delta':['game_registration.cpp','runtime_save_routing.cpp'],'public_headers_unchanged':True,
        'android_version_code':1070017,'android_certificate_sha256':certificate,'dev_menu_sha256':sha(mod),
        'dev_menu_sections':len(sections),'dev_menu_relocations':len(relocs),'files':records}
    (out/'STATIC-INSPECTION.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (out/'BUILD-RECEIPT.txt').write_text('DKR-R v1.0.7 beta 7 - Dev Menu Startup Fix\n\n'
        'Windows x64, Linux x86_64/ARM64, Steam Deck and Android arm64 compiled and packaged. macOS excluded.\n'
        'Native delta: two first-party source files; public headers and all other native sources unchanged.\n'
        'Unchanged compile paths reused only after both immutable stage receipts and all headers were verified.\n'
        'Patch Pipeline authoring only. No checked-out dependency or generated game-source edit.\n'
        'No save migration, content activation, game/mod execution or Git commit/push performed.\n'
        'Source engine identity: '+report['engine_identity']+'\n'
        'Shared v77 owned ID: '+identities[0]['77']+'\nShared v80 owned ID: '+identities[0]['80']+'\n'
        'These static identities match across all four native CPU configurations.\n'
        'The Android signing certificate matches the previous package; versionCode advanced to 1070017.\n'
        'Gameplay, controller, visual, performance and multiplayer acceptance remain with the user.\n',encoding='utf-8')
    (out/'SHA256SUMS.txt').write_text(''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in sorted(out.iterdir()) if p.is_file()),encoding='ascii')
    print('Completed static startup-fix delivery: '+str(out))


if __name__=='__main__':main()
