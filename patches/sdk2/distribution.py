"""Package SDK2 in new distribution trees. Never runs a game or self-test."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tarfile
import zipfile

ROOT=Path(__file__).resolve().parents[2]
AUTH=Path(__file__).resolve().parent
VERSION='1.0.7+beta.7'
DENIED={'.z64','.v64','.n64','.eep','.mpk','.sra','.fla','.o2r','.otr','.dkr-probe','.dkr-component','.dkr-bootstrap','.pdb'}

def fresh(path):
    path=path.resolve()
    if path.exists():raise ValueError('Choose a NEW output: '+str(path))
    path.mkdir(parents=True);return path

def copy(source,target):
    if not source.is_file():raise ValueError('Missing build product: '+str(source))
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)

def scan(root):
    files=[]
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            resolved=path.resolve(strict=True)
            if not resolved.is_relative_to(root):raise ValueError('Link escapes package: '+str(path))
            continue
        if not path.is_file():continue
        if path.suffix.lower() in DENIED:raise ValueError('Game/private data in package: '+str(path))
        with path.open('rb') as stream:
            if stream.read(4) in (b'\x80\x37\x12\x40',b'\x37\x80\x40\x12',b'\x40\x12\x37\x80'):raise ValueError('ROM header found')
        files.append({'path':path.relative_to(root).as_posix(),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    return files

def zip_tree(root,output):
    if output.exists():raise ValueError('Archive exists: '+str(output))
    with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path in sorted(root.rglob('*')):
            if path.is_file():archive.write(path,path.relative_to(root.parent).as_posix())

def kit(args):
    output=fresh(args.output);target=output/'DKR-R-Modding-SDK-2.0';shutil.copytree(args.stage/'sdk',target,ignore=shutil.ignore_patterns('__pycache__','*.pyc','.DS_Store'))
    for name in ('SDK.txt','USER-GUIDE.txt','IMPLEMENTATION-STATUS.txt'):
        copy(AUTH/'SDK-README.txt',target/name)
    copy(AUTH/'SDK-README.txt',target/'README.txt');copy(ROOT/'LICENSE.md',target/'LICENSE.txt')
    for source in args.bible.glob('DKR-R-Modding-Bible-SDK-2.0.*'):
        if source.suffix in ('.txt','.html','.pdf'):copy(source,target/source.name)
    copy(args.bible/'DKR-R-Modding-Bible-SDK-2.0.txt',target/'MODDERS-GUIDE.txt')
    copy(args.bible/'DKR-R-Modding-Bible-SDK-2.0.html',target/'MODDERS-GUIDE.html')
    copy(args.mod,target/'DKR-R-Dev-Menu.nrm')
    for source in args.examples:copy(source,target/source.name)
    copy(AUTH/'RELEASE-NOTES.txt',target/'RELEASE-NOTES.txt')
    records=scan(target);(output/'SDK-FILES.json').write_text(json.dumps(records,indent=2)+'\n')
    zip_tree(target,output/'DKR-R-Modding-SDK-2.0-beta.7.zip');print(target)

def windows(args):
    output=fresh(args.output);target=output/('DKR-R-'+VERSION+'-Windows-x64')
    shutil.copytree(args.base,target,ignore=shutil.ignore_patterns('imgui.ini','Custom-Magic-Codes'))
    for name in ('DKR-R.exe','libexec/dkr-r/DKR-R-Runtime.exe','libexec/dkr-r/DKR-R-ModWorker.exe','libexec/dkr-r/DKR-R-Updater.exe',
                 'libexec/dkr-r/DKR-R-InputHost.exe','libexec/dkr-r/SDL2.dll','libexec/dkr-r/SDL3.dll','libexec/dkr-r/dxcompiler.dll','libexec/dkr-r/dxil.dll'):
        copy(args.binaries/name,target/name)
    if VERSION.encode() not in (target/'libexec/dkr-r/DKR-R-Runtime.exe').read_bytes():raise ValueError('Compiled Windows version differs from beta 7')
    shutil.copytree(args.kit,target/'Custom-Magic-Codes')
    copy(args.mod,target/'Optional-Mods/DKR-R-Dev-Menu.nrm');copy(AUTH/'RELEASE-NOTES.txt',target/'RELEASE-NOTES.txt')
    copy(AUTH/'SDK-README.txt',target/'SDK-2.0-README.txt')
    copy(AUTH/'PLAYTEST-README.txt',target/'README.txt')
    subprocess.run([sys.executable,str(ROOT/'scripts/update_release.py'),'prepare-tree','--directory',str(target),'--version',VERSION,'--platform','windows-x64'],check=True)
    files=scan(target);(output/'PACKAGE-FILES.json').write_text(json.dumps(files,indent=2)+'\n')
    zip_tree(target,output/(target.name+'.zip'));print(target)

def linux(args):
    target=args.output.resolve()
    if target.exists():raise ValueError('AppDir exists')
    shutil.copytree(args.base,target,symlinks=True,ignore=shutil.ignore_patterns('Custom-Magic-Codes','imgui.ini'))
    copy(args.binaries/'DKR-R',target/'usr/bin/DKR-R')
    if VERSION.encode() not in (target/'usr/bin/DKR-R').read_bytes():raise ValueError('Compiled Linux version differs from beta 7')
    for name in ('DKR-R-ModWorker','DKR-R-Updater','DKR-R-InputHost','libSDL3.so.0'):
        copy(args.binaries/'libexec/dkr-r'/name,target/'usr/libexec/dkr-r'/name)
    subprocess.run(['patchelf','--set-rpath','$ORIGIN/../lib',str(target/'usr/bin/DKR-R')],check=True)
    for name in ('DKR-R-ModWorker','DKR-R-Updater'):
        subprocess.run(['patchelf','--set-rpath','$ORIGIN/../../lib',str(target/'usr/libexec/dkr-r'/name)],check=True)
    subprocess.run(['patchelf','--set-rpath','$ORIGIN',str(target/'usr/libexec/dkr-r/DKR-R-InputHost')],check=True)
    for path in target.glob('*.desktop'):
        text=path.read_text();text='\n'.join('X-AppImage-Version='+VERSION if line.startswith('X-AppImage-Version=') else line for line in text.splitlines())+'\n';path.write_text(text)
    docs=target/'usr/share/doc/dkr-port';shutil.copytree(args.kit,docs/'Custom-Magic-Codes')
    copy(args.mod,docs/'Optional-Mods/DKR-R-Dev-Menu.nrm');copy(AUTH/'RELEASE-NOTES.txt',docs/'RELEASE-NOTES.txt')
    copy(AUTH/'SDK-README.txt',docs/'SDK-2.0-README.txt')
    copy(AUTH/'PLAYTEST-README.txt',docs/'README.txt')
    # Inspect ELF headers without executing either target architecture.
    expected='AArch64' if args.arch=='aarch64' else 'Advanced Micro Devices X86-64'
    for path in (target/'usr/bin/DKR-R',target/'usr/libexec/dkr-r/DKR-R-ModWorker',target/'usr/libexec/dkr-r/DKR-R-InputHost',target/'usr/libexec/dkr-r/DKR-R-Updater'):
        header=subprocess.check_output(['readelf','-h',str(path)],text=True)
        if expected not in header:raise ValueError('Wrong ELF architecture: '+str(path))
    scan(target);print(target)

def appimage(args):
    if args.output.exists():raise ValueError('AppImage exists')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.base.open('rb') as stream:header=stream.read(2*1024*1024)
    if header[:4]!=b'\x7fELF' or header[8:11]!=b'AI\x02':raise ValueError('Expected type-2 AppImage runtime')
    offsets=[];start=0
    while (at:=header.find(b'hsqs',start))>=0:
        start=at+4
        if at+96>len(header):continue
        values=struct.unpack_from('<5I6H8Q',header,at)
        if values[3] in (65536,131072,262144,1048576) and values[5] in range(1,7) and values[9]==4 and values[10]==0 and values[12]<=args.base.stat().st_size-at:offsets.append(at)
    if len(offsets)!=1:raise ValueError('Could not uniquely locate the admitted AppImage squashfs')
    machine=struct.unpack_from('<H',header,18)[0]
    if machine!=(183 if args.arch=='aarch64' else 62):raise ValueError('Wrong runtime architecture')
    image=args.output.with_suffix('.squashfs')
    if image.exists():raise ValueError('Squashfs output exists')
    subprocess.run(['mksquashfs',str(args.tree),str(image),'-noappend','-comp','gzip','-b','131072','-root-owned','-processors','3'],check=True)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('xb') as dest,image.open('rb') as source:
        dest.write(header[:offsets[0]]);shutil.copyfileobj(source,dest)
    args.output.chmod(0o755);print('Packaged AppImage, no target execution:',args.output)

def android_script(args):
    if args.output.exists():raise ValueError('Script exists')
    text=(ROOT/'scripts/Package-Android-Preview.ps1').read_text()
    text=text.replace('$projectRoot = Split-Path $PSScriptRoot -Parent',"$projectRoot = '"+ROOT.as_posix()+"'")
    a=text.index('$codeSdkArguments = ');b=text.index('$libraries = @(',a)
    text=text[:a]+"Copy-Item -LiteralPath '"+args.kit.resolve().as_posix()+"' -Destination (Join-Path $assets 'Custom-Magic-Codes') -Recurse\n"+text[b:]
    text=text.replace("$variant = if ($ReleaseBuild)","Copy-Item -LiteralPath '"+(AUTH/'RELEASE-NOTES.txt').as_posix()+"' -Destination (Join-Path $notices 'RELEASE-NOTES.txt')\n$variant = if ($ReleaseBuild)")
    if args.products:
        # Source stays in the repository; Gradle's generated products and clean
        # target stay in a new task-owned directory on the spacious build drive.
        build_root=args.products.resolve()
        if build_root.exists():raise ValueError('Choose a NEW external Gradle output directory')
        replacement="@versionArguments --init-script '"+(AUTH/'android_startup_init.gradle').as_posix()+"' '-DdkrStartupBuildRoot="+build_root.as_posix()+"' :app:clean $assembleTask --no-daemon"
        if text.count('@versionArguments :app:clean $assembleTask --no-daemon')!=1:raise ValueError('Android Gradle assembly anchor changed')
        text=text.replace('@versionArguments :app:clean $assembleTask --no-daemon',replacement)
        old='Join-Path $projectRoot "packaging/android/app/build/outputs/apk/$variant/app-$variant.apk"'
        if text.count(old)!=1:raise ValueError('Android APK product anchor changed')
        text=text.replace(old,"Join-Path '"+(build_root/'app').as_posix()+"' \"outputs/apk/$variant/app-$variant.apk\"")
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(text,newline='\n');print(args.output)

def steamdeck(args):
    output=fresh(args.output);target=output/('DKR-R-'+VERSION+'-SteamDeck-x86_64');shutil.copytree(args.tree,target,symlinks=True)
    for name in ('START-DKR-R.sh','STEAM-DECK-README.txt'):copy(args.base/name,target/name)
    (target/'START-DKR-R.sh').chmod(0o755);copy(AUTH/'RELEASE-NOTES.txt',target/'RELEASE-NOTES.txt')
    copy(args.mod,target/'DKR-R-Dev-Menu.nrm')
    copy(AUTH/'PLAYTEST-README.txt',target/'README.txt')
    with tarfile.open(output/(target.name+'.tar.gz'),'x:gz',compresslevel=6) as archive:archive.add(target,arcname=target.name)
    # Steam Deck also has an AppImage as requested; same qualified CPU target.
    copy(args.image,output/('DKR-R-'+VERSION+'-SteamDeck-x86_64.AppImage'))
    print(output)

def delivery(args):
    output=fresh(args.output);task=args.products or args.stage.parent
    products={
        'Windows-x64.zip':task/'windows-versioned'/('DKR-R-'+VERSION+'-Windows-x64.zip'),
        'Linux-x86_64.AppImage':task/'linux-final'/('DKR-R-'+VERSION+'-Linux-x86_64.AppImage'),
        'Linux-aarch64.AppImage':task/'arm64-final'/('DKR-R-'+VERSION+'-Linux-aarch64.AppImage'),
        'SteamDeck-x86_64.AppImage':task/'steamdeck-final'/('DKR-R-'+VERSION+'-SteamDeck-x86_64.AppImage'),
        'SteamDeck-x86_64.tar.gz':task/'steamdeck-final'/('DKR-R-'+VERSION+'-SteamDeck-x86_64.tar.gz'),
        'Android-arm64.apk':task/'android-final'/('DKR-R-'+VERSION+'-Android-arm64.apk')}
    for source in products.values():copy(source,output/source.name)
    copy(args.kit.parent/'DKR-R-Modding-SDK-2.0-beta.7.zip',output/'DKR-R-Modding-SDK-2.0-beta.7.zip')
    copy(args.mod,output/'DKR-R-Dev-Menu.nrm')
    for name in ('DKR-R-Modding-Bible-SDK-2.0.txt','DKR-R-Modding-Bible-SDK-2.0.html','DKR-R-Modding-Bible-SDK-2.0.pdf'):copy(args.bible/name,output/name)
    copy(AUTH/'PLAYTEST-README.txt',output/'README.txt');copy(AUTH/'RELEASE-NOTES.txt',output/'RELEASE-NOTES.txt')
    import tomllib
    mod_version=tomllib.loads((args.stage/'sdk/examples/dev-menu/mod.toml').read_text())['manifest']['version']
    with zipfile.ZipFile(output/('DKR-R-Dev-Menu-'+mod_version+'-Source.zip'),'x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        source=args.stage/'sdk/examples/dev-menu'
        for file in sorted(source.rglob('*')):
            if file.is_file() and file.suffix!='.pyc' and '__pycache__' not in file.parts:archive.write(file,'dkr-development-menu/'+file.relative_to(source).as_posix())
    print('Consolidated beta-7 playtest delivery:',output)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=('kit','windows','linux','appimage','android-script','steamdeck','delivery'))
    for name in ('base','binaries','stage','kit','bible','mod','tree','image','products'):p.add_argument('--'+name,type=Path)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--arch',choices=('x86_64','aarch64'));p.add_argument('--examples',type=Path,nargs='*',default=[])
    a=p.parse_args();globals()[a.mode.replace('-','_')](a)
if __name__=='__main__':main()
