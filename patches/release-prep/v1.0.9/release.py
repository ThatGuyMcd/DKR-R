"""v1.0.9 Release Build orchestration; author only through this Patch Pipeline."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,struct,subprocess,sys,zipfile
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]
VERSION='1.0.9'
ANDROID_CODE=1090001

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def prepare(baseline,evidence):
    rows=json.loads((baseline/'SOURCE-FILES.json').read_text())
    changed={}
    for row in rows:
        path=ROOT/row['path']
        if sha(path)!=row['sha256']:raise ValueError('Approved source changed before release: '+row['path'])
    version=(ROOT/'VERSION').read_text().strip()
    if version!='1.0.8':raise ValueError('Unexpected pre-release metadata version: '+version)
    desired={'VERSION':'1.0.9\n'}
    path=ROOT/'packaging/RELEASE-README.md'
    text=path.read_text(encoding='utf-8')
    if text.count('# DKR-R v1.0.8')!=1:raise ValueError('Release README anchor changed')
    desired['packaging/RELEASE-README.md']=text.replace('# DKR-R v1.0.8','# DKR-R v1.0.9 Release Build',1)
    path=ROOT/'packaging/linux/dkr-port.appdata.xml';text=path.read_text(encoding='utf-8')
    if text.count('  <releases>')!=1 or 'version="1.0.9"' in text:raise ValueError('AppStream metadata anchor changed')
    desired['packaging/linux/dkr-port.appdata.xml']=text.replace('  <releases>',
        '  <releases>\n    <release version="1.0.9" date="2026-10-24"><description><p>Public online lobbies, spectators, vehicles, achievements and online reliability updates.</p></description></release>',1)
    for name,text in desired.items():
        path=ROOT/name;before=sha(path);path.write_text(text,encoding='utf-8',newline='\n')
        changed[name]={'before':before,'after':sha(path)}
    evidence.mkdir(parents=True,exist_ok=True)
    (evidence/'METADATA-CHANGES.json').write_text(json.dumps(changed,indent=2)+'\n')
    for row in rows:
        if row['path'] in changed:row['sha256']=changed[row['path']]['after'];row['bytes']=(ROOT/row['path']).stat().st_size
    (evidence/'SOURCE-FILES-AFTER-METADATA.json').write_text(json.dumps(rows,indent=2)+'\n')
    print('Stable metadata applied through the release pipeline; three declared metadata files, native sources unchanged.')

def main(a):
    if a.action=='prepare':prepare(a.baseline,a.evidence)
    elif a.action=='configure':
        m=load('stable109_configure',ROOT/'patches/release-prep/v1.0.7/release.py')
        command=[a.cmake,'-S',str(ROOT/'runtime-recomp'),'-B',str(a.build),
            '-DDKR_RELEASE_VERSION:STRING='+VERSION,'-DCMAKE_PROJECT_INCLUDE='+str(AUTH/'ReleaseOverlay.cmake'),
            '-DDKR_RELEASE_EVIDENCE='+str(a.log.parent),'-DDKR_RELEASE_CAPSULE='+str(a.capsule)]
        command+=['-DDKR_'+name+'_STAGE='+str(a.capsule/'roles'/name) for name in m.STAGES]
        command+=['-DDKR_SDK2_REUSE_STAGE='+str(a.capsule/'roles/SDK2_BASE'),'-DDKR_SHARED_SAVES_REUSE_STAGE=','-DDKR_SHARED_SAVES_SOURCE_STAGE=']
        command+=['-DDKR_CAMERA_KERNEL_V'+str(revision)+'='+str(a.capsule/('prepared/CAMERA_KERNEL_V'+str(revision))) for revision in (77,80)]
        command+=['-DDKR_'+flag+'_QUALIFICATION=OFF' for flag in ('EXPERIMENTAL_RENDER','REPLAY','ANDROID_RENDER','WATER','TASK','LEGACY')]
        for definition in a.define:
            if not definition.startswith('DKR_VISUAL_MODEL_GENERATED_V') or '=' not in definition:raise ValueError('Unexpected extra build definition')
            command+=['-D'+definition]
        a.log.parent.mkdir(parents=True,exist_ok=True)
        with a.log.open('w') as stream:subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT,check=True)
        print('Configured actual stable version through retained patch layers:',a.build)
    elif a.action=='android-recipe':
        script=ROOT/'patches/release-prep/v1.0.7/prepare_android.py'
        sys.argv=[str(script),'--output',str(a.output),'--products',str(a.products)]
        exec(compile(script.read_text(),str(script),'exec'),{'__file__':str(script),'__name__':'__main__'})
        text=a.output.read_text()
        # The stable metadata notes are explicit, rather than inheriting stale
        # 1.0.8 notes from the original preview recipe.
        anchor="$notices = Join-Path $assets 'notices'"
        if text.count(anchor)!=1:raise ValueError('Android notices anchor changed')
        text=text.replace(anchor,anchor,1)
        marker='Copy-Item -LiteralPath (Join-Path $projectRoot \'packaging/licenses\')'
        at=text.index(marker)
        text=text[:at]+"Copy-Item -LiteralPath '"+(AUTH/'RELEASE-NOTES.txt').as_posix()+"' -Destination (Join-Path $notices 'RELEASE-NOTES.txt')\nCopy-Item -LiteralPath '"+(ROOT/'packaging/RELEASE-README.md').as_posix()+"' -Destination (Join-Path $notices 'README.txt')\n"+text[at:]
        a.output.write_text(text,encoding='utf-8',newline='\n')
    else:
        script=ROOT/'patches/release-prep/v1.0.7/inspect_apk.py'
        text=script.read_text().replace('1.0.7','1.0.9').replace('1070018',str(ANDROID_CODE))
        sys.argv=[str(script)]
        for name in ('apk','published','beta','tools','output'):sys.argv+=['--'+name,str(getattr(a,name))]
        exec(compile(text,str(script),'exec'),{'__file__':str(script),'__name__':'__main__'})
        badging=subprocess.check_output([str(a.tools/'aapt.exe'),'dump','badging',str(a.apk)],text=True)
        if 'application-debuggable' in badging:raise ValueError('The release APK is debuggable')
        alignments={}
        with zipfile.ZipFile(a.apk) as archive:
            for name in archive.namelist():
                if not name.startswith('lib/arm64-v8a/') or not name.endswith('.so'):continue
                data=archive.read(name);start=struct.unpack_from('<Q',data,32)[0]
                size,count=struct.unpack_from('<HH',data,54);loads=[]
                for index in range(count):
                    at=start+size*index
                    if struct.unpack_from('<I',data,at)[0]==1:loads.append(struct.unpack_from('<Q',data,at+48)[0])
                if not loads or min(loads)<16384:raise ValueError('Native library lacks 16 KB load alignment: '+name)
                alignments[name]=loads
        report=json.loads(a.output.read_text());report['release_build_debuggable']=False;report['elf_load_alignments']=alignments
        a.output.write_text(json.dumps(report,indent=2)+'\n')
        print('Release APK is non-debuggable; every packaged native library has at least 16 KB PT_LOAD alignment.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('prepare','configure','android-recipe','inspect-apk'))
    for name in ('baseline','evidence','build','log','capsule','output','products','apk','published','beta','tools'):p.add_argument('--'+name,type=Path)
    p.add_argument('--cmake',default='cmake');p.add_argument('--define',action='append',default=[]);main(p.parse_args())
