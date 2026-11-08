"""Static distribution inspection. Does not execute any target binary/mod."""
from pathlib import Path,PurePosixPath
import argparse
import hashlib
import html
import json
import re
import struct
import zipfile
import tarfile
import sys

sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent/'sdk'))
from relocations import symbols as inspect_symbols

VERSION='1.0.7+beta.7'
DENIED={'.z64','.v64','.n64','.eep','.mpk','.sra','.fla','.o2r','.otr','.pdb','.pyc','.dkr-probe','.dkr-bootstrap','.dkr-component'}
def sha(data):return hashlib.sha256(data).hexdigest()
def name_ok(name):
    path=PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or ':' in name or '\\' in name or any(ord(c)<32 for c in name):raise ValueError('Unsafe archive member '+name)
    if path.suffix.lower() in DENIED or '__pycache__' in path.parts:raise ValueError('Private/game/cache data '+name)
def pe(data):
    if data[:2]!=b'MZ':raise ValueError('Missing PE header')
    at=struct.unpack_from('<I',data,60)[0]
    if data[at:at+4]!=b'PE\0\0' or struct.unpack_from('<H',data,at+4)[0]!=0x8664:raise ValueError('Expected Windows x64')
def elf(data,machine):
    if data[:5]!=b'\x7fELF\x02' or struct.unpack_from('<H',data,18)[0]!=machine:raise ValueError('Wrong ELF64 architecture')
def zip_inspect(path,mod):
    with zipfile.ZipFile(path) as archive:
        names=archive.namelist()
        if len(names)!=len(set(n.casefold() for n in names)):raise ValueError('Case-insensitive archive collision')
        exe=[]
        for name in names:
            name_ok(name)
            if name.endswith('.exe'):pe(archive.read(name));exe.append(name)
            if name.endswith('DKR-R-Dev-Menu.nrm') and archive.read(name)!=mod:raise ValueError('Dev Menu edition differs in '+str(path))
        if 'Windows-x64' in path.name:
            if len(exe)!=5 or len([name for name in exe if len(PurePosixPath(name).parts)==2])!=1:raise ValueError('Wrong public/private EXE layout')
            runtime=next(name for name in names if name.endswith('/libexec/dkr-r/DKR-R-Runtime.exe'))
            if VERSION.encode() not in archive.read(runtime):raise ValueError('Windows binary version stamp differs')
            manifest=json.loads(archive.read(next(name for name in names if name.endswith('/assets/updates/install.json'))))
            if manifest['version']!=VERSION or manifest['platform']!='windows-x64':raise ValueError('Install ownership version differs')
        if path.suffix=='.apk':
            libs=[name for name in names if name.startswith('lib/') and name.endswith('.so')]
            if not libs or any(not name.startswith('lib/arm64-v8a/') for name in libs):raise ValueError('APK has wrong ABI set')
            for name in libs:elf(archive.read(name),183)
            if VERSION.encode() not in archive.read('lib/arm64-v8a/libDKR-R.so'):raise ValueError('Android binary version differs')
            if archive.read('assets/Custom-Magic-Codes/DKR-R-Dev-Menu.nrm')!=mod:raise ValueError('APK Dev Menu differs')
        return {'members':len(names),'uncompressed_bytes':sum(i.file_size for i in archive.infolist())}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--directory',type=Path,required=True);p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--windows-registry',type=Path,required=True);p.add_argument('--ninja',type=Path,nargs='+',required=True);a=p.parse_args()
    directory=a.directory.resolve(strict=True);report=directory/'STATIC-INSPECTION.json'
    if report.exists():raise ValueError('Use a new inspection output')
    mod=(directory/'DKR-R-Dev-Menu.nrm').read_bytes()
    with zipfile.ZipFile(directory/'DKR-R-Dev-Menu.nrm') as package:
        meta=json.loads(package.read('dkr.json'));manifest=json.loads(package.read('mod.json'))
        if meta['api']!=2 or not meta['development'] or manifest['id']!='dkr_development_menu':raise ValueError('Wrong Dev Menu metadata')
        inspect_symbols(package.read('mod_syms.bin'),package.read('mod_binary.bin'))
    identities=[]
    for path in [a.windows_registry,*a.ninja]:
        text=html.unescape(path.read_text()).replace('\\"','"')
        pairs=dict(re.findall(r'DKR_OWNED_BUILD_ID_V(77|80)="([a-f0-9]{64})"',text))
        if set(pairs)!={'77','80'}:raise ValueError('Missing owned engine IDs in '+str(path))
        identities.append(pairs)
    if any(value!=identities[0] for value in identities):raise ValueError('Cross-platform owned engine identity mismatch')
    records=[]
    for path in sorted(directory.iterdir()):
        if not path.is_file():continue
        name_ok(path.name);data=path.read_bytes();record={'file':path.name,'bytes':len(data),'sha256':sha(data)}
        if path.suffix in ('.zip','.apk','.nrm'):record.update(zip_inspect(path,mod))
        elif path.suffix=='.AppImage':
            if data[8:11]!=b'AI\x02':raise ValueError('Not a type-2 AppImage')
            elf(data,183 if 'aarch64' in path.name else 62)
        elif path.name.endswith('.tar.gz'):
            with tarfile.open(path,'r:gz') as archive:
                for item in archive.getmembers():
                    name_ok(item.name)
                    if item.issym() or item.islnk():
                        if item.linkname.startswith('/') or '..' in PurePosixPath(item.linkname).parts:raise ValueError('Unsafe tar link')
                record['members']=len(archive.getmembers())
        records.append(record)
    result={'schema':1,'version':VERSION,'execution':'No game/mod/unit/replay/multiplayer/headset execution. Source, compile and package inspection only.',
            'engine_identity':(a.stage/'sdk2-engine-identity.txt').read_text().strip(),'owned_ids':identities[0],
            'dev_menu_sha256':sha(mod),'files':records}
    report.write_text(json.dumps(result,indent=2)+'\n');print('Static package inspection complete. Runtime acceptance is reserved to the user.')
if __name__=='__main__':main()
