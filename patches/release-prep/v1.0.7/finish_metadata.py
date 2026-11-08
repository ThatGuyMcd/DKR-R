"""Apply stable release metadata and current technical-guide entrypoints."""
from pathlib import Path
import os
import re
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]

def put(name,text):
    (ROOT/name).write_text(text,encoding='utf-8',newline='\n')

path=ROOT/'packaging/android/app/build.gradle'
text=path.read_text(encoding='utf-8').replace('getOrElse(1070010)','getOrElse(1070018)')
put('packaging/android/app/build.gradle',text)
path=ROOT/'packaging/linux/dkr-port.appdata.xml'
text=re.sub(r'    <release version="1\.0\.7"[^>]*(?:/>|>.*?</release>)\n','',path.read_text(encoding='utf-8'),flags=re.S)
text=text.replace('  <releases>','  <releases>\n    <release version="1.0.7" date="2026-10-20"><description><p>Mod Store, SDK 2.0, shared personal saves and presentation fixes.</p></description></release>')
put('packaging/linux/dkr-port.appdata.xml',text)
path=ROOT/'docs/BUILDING.md'
put('docs/BUILDING.md',path.read_text(encoding='utf-8').replace('1.0.7+beta.5','1.0.7'))
path=ROOT/'docs/PLATFORMS.md'
text=path.read_text(encoding='utf-8')
if 'GLIBCXX_3.4.32' not in text:
    text+='\n## Linux ABI requirements for these release files\n\nStatic inspection of both final Linux architectures finds GLIBC 2.38 and\nGLIBCXX_3.4.32 requirements. Install a system libstdc++ providing that symbol\nversion, along with the normal desktop/audio/font stack and a compatible\nVulkan driver. The AppImages do not replace the host C++ runtime, glibc or\ngraphics driver. Core host libraries remain excluded in line with the\n[AppImage library policy](https://github.com/AppImage/pkg2appimage/blob/master/excludelist);\nother required linked libraries are bundled. An older distro needs a\ncompatible source build, not a renamed AppImage.\n'
    put('docs/PLATFORMS.md',text)
path=ROOT/'scripts/stage_magic_code_sdk.py'
text=path.read_text(encoding='utf-8')
text=text.replace("'build.py','runtime.c'","'build.py','tools.py','relocations.py','runtime.c'")
text=text.replace("'.c','.h','.inc'","'.c','.h','.hpp','.inl','.inc'")
put('scripts/stage_magic_code_sdk.py',text)

modules=('patches/android/README.md','patches/camera-plus/README.md',
    'patches/character-portraits/README.md','patches/save-layout/README.md',
    'patches/shared-saves/README.md','patches/vanilla-water/README.md',
    'patches/viewport-flow/README.md','patches/title-text/README.md',
    'patches/credits-viewport/README.md','patches/presentation-polish/README.md',
    'runtime-recomp/tests/legacy_mods/README.md')
for name in modules:
    path=ROOT/name;text=path.read_text(encoding='utf-8')
    heading,_,body=text.partition('\n')
    link=os.path.relpath(AUTH/'BUILDING.txt',path.parent).replace('\\','/')
    note='## Current release composition\n\nFor v1.0.7 use the [release pipeline]('+link+'). The module notes below\nexplain this layer; their dated playtest paths are historical inputs, not the\ncurrent release command. The accepted water/source capsule preserves the\ncomposed behaviour. Dependencies and generated guest checkouts stay read-only.\n'
    if '## Current release composition' not in text:put(name,heading+'\n\n'+note+'\n'+body.lstrip())
print('Stable metadata and technical guide entrypoints updated through Patch Pipeline.')
