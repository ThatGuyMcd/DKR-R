"""Package the compiled Camera++ playtest without launching any game/mod."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from camera_plus_pipeline import new_output,replace,digest

def mod_file(path):
    with zipfile.ZipFile(path) as archive:
        if set(archive.namelist())!={'mod.json','dkr.json','mod_syms.bin','mod_binary.bin','settings.json'}:
            raise ValueError('Unexpected Camera++ package contents')
        if json.loads(archive.read('mod.json'))['id']!='dkr_camera_plus':raise ValueError('Wrong camera mod')
        meta=json.loads(archive.read('dkr.json'))
        if meta['requires'].get('presentation')!=1:raise ValueError('Not a presentation-domain mod')

def windows(base,binaries,mod,output):
    output=new_output(output,base,binaries,mod.parent)
    mod_file(mod)
    stage=output/'DKR-R-1.0.7+beta.5-ModernCamera++-Windows-x64'
    shutil.copytree(base,stage)
    for name in ('DKR-R.exe','libexec/dkr-r/DKR-R-Runtime.exe','libexec/dkr-r/DKR-R-ModWorker.exe',
                 'libexec/dkr-r/DKR-R-Updater.exe','libexec/dkr-r/DKR-R-InputHost.exe',
                 'libexec/dkr-r/SDL2.dll','libexec/dkr-r/SDL3.dll','libexec/dkr-r/dxcompiler.dll','libexec/dkr-r/dxil.dll'):
        source=binaries/name
        if not source.is_file():raise ValueError('Missing compiled image: '+str(source))
        shutil.copyfile(source,stage/name)
    runtime=(stage/'libexec/dkr-r/DKR-R-Runtime.exe').read_bytes()
    if b'[camera++][ready]' not in runtime:raise ValueError('Runtime lacks the Camera++ host')
    optional=stage/'Optional-Mods';optional.mkdir(exist_ok=True)
    shutil.copyfile(mod,optional/'ModernCamera++.nrm')
    shutil.copyfile(Path(__file__).parent/'README.md',stage/'MODERN-CAMERA-README.md')
    subprocess.run([sys.executable,str(ROOT/'scripts/update_release.py'),'prepare-tree','--directory',str(stage),
                    '--version','1.0.7+beta.5','--platform','windows-x64'],check=True)
    denied={'.z64','.v64','.n64','.eep','.mpk','.sra','.fla','.dkr-probe','.dkr-bootstrap','.dkr-component','.pdb'}
    expected={'DKR-R.exe','libexec/dkr-r/DKR-R-Runtime.exe','libexec/dkr-r/DKR-R-ModWorker.exe',
              'libexec/dkr-r/DKR-R-Updater.exe','libexec/dkr-r/DKR-R-InputHost.exe'}
    executables=set()
    for path in stage.rglob('*'):
        if path.is_symlink():raise ValueError('Package must not contain links')
        if not path.is_file():continue
        if path.suffix.lower() in denied:raise ValueError('Prohibited package data: '+str(path))
        if path.suffix.lower()=='.exe':executables.add(path.relative_to(stage).as_posix())
        with path.open('rb') as stream:
            if stream.read(4) in (b'\x80\x37\x12\x40',b'\x37\x80\x40\x12',b'\x40\x12\x37\x80'):
                raise ValueError('ROM header in package')
    if executables!=expected:raise ValueError('Unexpected executable layout')
    archive=output/(stage.name+'.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as package:
        for path in sorted(stage.rglob('*')):
            if path.is_file():package.write(path,path.relative_to(output).as_posix())
    shutil.copyfile(mod,output/'ModernCamera++.nrm')
    print('Created:',archive,'SHA256:',digest(archive))

def linux_script(mod,output):
    output=new_output(output,mod.parent,ROOT/'scripts')
    mod_file(mod);output.parent.mkdir(parents=True,exist_ok=True)
    script=(ROOT/'scripts/Package-Linux-AppImage.sh').read_text()
    script=replace(script,'PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"',
                   'PROJECT_ROOT='+shlex.quote(ROOT.as_posix()))
    insertion='''mkdir -p "${APPDIR}/usr/share/doc/dkr-port/Optional-Mods"
install -m 0644 @MODFILE@ "${APPDIR}/usr/share/doc/dkr-port/Optional-Mods/ModernCamera++.nrm"
install -m 0644 @README@ "${APPDIR}/usr/share/doc/dkr-port/MODERN-CAMERA-README.md"
'''.replace('@MODFILE@',shlex.quote(mod.resolve().as_posix())).replace('@README@',shlex.quote((Path(__file__).parent/'README.md').as_posix()))
    script=replace(script,'collect_linux_dependency_notices "${APPDIR}"',insertion+'collect_linux_dependency_notices "${APPDIR}"')
    output.write_text(script,newline='\n')
    print('Generated packaging-only Linux script:',output)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=('windows','linux-script'))
    p.add_argument('--base',type=Path);p.add_argument('--binaries',type=Path)
    p.add_argument('--mod',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.mode=='windows':windows(a.base.resolve(strict=True),a.binaries.resolve(strict=True),a.mod.resolve(strict=True),a.output)
    else:linux_script(a.mod.resolve(strict=True),a.output)
