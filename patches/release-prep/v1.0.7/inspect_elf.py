"""Inspect ELF architecture, linkage and libc requirements without executing it."""
from pathlib import Path
import argparse
import json
import re
import struct
import subprocess

p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tree',type=Path,required=True)
p.add_argument('--arch',choices=('x86_64','aarch64'),required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
files={};available=set();required={};glibc=set();cpp=set()
system={'libc.so.6','libm.so.6','libdl.so.2','libpthread.so.0','librt.so.1','libresolv.so.2',
    'ld-linux-x86-64.so.2','ld-linux-aarch64.so.1','libvulkan.so.1','libGL.so.1','libEGL.so.1','libGLX.so.0'}
host_policy=Path(__file__).resolve().parent/'linux-host-libraries.txt'
system.update(line.strip() for line in host_policy.read_text().splitlines() if line.strip() and not line.startswith('#'))
for path in sorted(a.tree.rglob('*')):
    if not path.is_file():continue
    with path.open('rb') as stream:header=stream.read(64)
    if header[:4]!=b'\x7fELF':continue
    if header[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',header,18)[0]!=(183 if a.arch=='aarch64' else 62):raise ValueError('Foreign ELF: '+str(path))
    name=path.relative_to(a.tree).as_posix();available.add(path.name)
    dynamic=subprocess.check_output(['readelf','-d',str(path)],text=True)
    versions=subprocess.check_output(['readelf','--version-info',str(path)],text=True)
    needed=re.findall(r'\(NEEDED\).*?\[(.*?)\]',dynamic)
    soname=re.findall(r'\(SONAME\).*?\[(.*?)\]',dynamic)
    available.update(soname)
    needs=versions.split('Version needs section',1)[-1] if 'Version needs section' in versions else ''
    gv=re.findall(r'Name: GLIBC_([0-9.]+)',needs);cv=re.findall(r'Name: GLIBCXX_([0-9.]+)',needs)
    glibc.update(gv);cpp.update(cv)
    files[name]={'needed':needed,'rpath':re.findall(r'\((?:RUNPATH|RPATH)\).*?\[(.*?)\]',dynamic),'glibc':gv,'glibcxx':cv}
    required[name]=needed
missing={name:[lib for lib in libs if lib not in available and lib not in system] for name,libs in required.items()}
missing={name:libs for name,libs in missing.items() if libs}
latest=lambda values:max(values,key=lambda value:tuple(map(int,value.split('.')))) if values else None
host_requirements=sorted({lib for libs in required.values() for lib in libs if lib in system and lib not in available})
report={'execution':'ELF metadata inspection only; no loader/target execution','arch':a.arch,'max_glibc':latest(glibc),'max_glibcxx':latest(cpp),'host_library_policy':'https://github.com/AppImage/pkg2appimage/blob/master/excludelist','required_host_libraries':host_requirements,'missing_non_system_dependencies':missing,'files':files}
a.output.write_text(json.dumps(report,indent=2)+'\n')
print('ELF files:',len(files),'GLIBC:',report['max_glibc'],'GLIBCXX:',report['max_glibcxx'],'unresolved non-system dependencies:',len(missing))
if missing:raise ValueError('Review missing bundled dependencies: '+repr(missing))
