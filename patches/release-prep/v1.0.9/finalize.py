"""Collect the complete unpublished stable release with cross-platform receipts."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,zipfile
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def need(ok,message):
    if not ok:raise ValueError(message)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('work','output','windows-binaries'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();need(not a.output.exists(),'Choose a new final release directory')
    contracts={};audits={};artifacts={};linux_requirements={}
    for platform in ('windows','linux','arm64','android'):
        evidence=a.work/'evidence'/platform
        if platform=='android':source=json.loads((evidence/'SOURCE-INSPECTION.json').read_text())
        else:
            source=json.loads((a.work/'packages'/platform/'BUILD-RECEIPT.json').read_text())['audit']
            (evidence/'SOURCE-INSPECTION.json').write_text(json.dumps(source,indent=2)+'\n')
        need(source['version']=='1.0.9' and source['unchanged_authored_files']==2343,'Incomplete source baseline: '+platform)
        audits[platform]={'directory_contract':source['directory_contract'],
            'scoreboard_contract':source['retained']['scoreboard_contract'],'unchanged_authored_files':source['unchanged_authored_files']}
        definitions=(evidence/'DKRExperimentalOwnedRegistry-COMPILE_DEFINITIONS.txt').read_text()
        ids=dict(re.findall(r'DKR_OWNED_BUILD_ID_V(77|80)="([a-f0-9]{64})"',definitions))
        need(set(ids)=={'77','80'},'Missing online identities: '+platform);contracts[platform]=ids
    need(all(value==contracts['windows'] for value in contracts.values()),'Mixed native cross-platform online identities')
    need(all(value==audits['windows'] for value in audits.values()),'Mixed accepted online/Scoreboard contracts')
    for platform in ('linux','arm64'):
        elf=json.loads((a.work/'evidence'/platform/'ELF-INSPECTION.json').read_text())
        need(not elf['missing_non_system_dependencies'],'Missing linked dependency: '+platform)
        linux_requirements[platform]={'glibc':elf['max_glibc'],'glibcxx':elf['max_glibcxx'],'host_libraries':elf['required_host_libraries']}
    apk=json.loads((a.work/'evidence/android/APK-INSPECTION.json').read_text())
    need(apk['version']=='1.0.9' and apk['version_code']==1090001,'Wrong APK release version')
    need(apk['matches_published_1_0_6_and_beta_7'],'Android signer continuity missing')
    need(apk.get('release_build_debuggable') is False and apk.get('elf_load_alignments'),'Android Release/16 KB checks missing')
    for platform in ('windows','linux','arm64'):
        receipt=json.loads((a.work/'packages'/platform/'BUILD-RECEIPT.json').read_text())
        need(receipt['version']=='1.0.9' and not receipt['published'],'Wrong desktop package version')
        source=a.work/'packages'/platform/receipt['artifact']['name']
        need(sha(source)==receipt['artifact']['sha256'],'Desktop artifact changed: '+platform)
        artifacts[source.name]=source
    android=a.work/'packages/android/DKR-R-1.0.9-Android-arm64.apk'
    need(sha(android)==apk['artifact']['sha256'],'Inspected APK changed');artifacts[android.name]=android
    with zipfile.ZipFile(artifacts['DKR-R-1.0.9-Windows-x64.zip']) as archive:
        native={'windows':archive.read('DKR-R-1.0.9-Windows-x64/libexec/dkr-r/DKR-R-Runtime.exe')}
    for platform,arch in (('linux','x86_64'),('arm64','aarch64')):
        native[platform]=(a.work/'packages'/platform/('DKR-R-1.0.9-Linux-'+arch+'.AppDir/usr/bin/DKR-R')).read_bytes()
    with zipfile.ZipFile(android) as archive:native['android']=archive.read('lib/arm64-v8a/libDKR-R.so')
    for platform,data in native.items():
        for revision,identity in contracts[platform].items():need(identity.encode() in data,'Linked native online identity missing: '+platform+'/'+revision)
    artifacts['DKR-R-1.0.9-SteamDeck-x86_64.AppImage']=artifacts['DKR-R-1.0.9-Linux-x86_64.AppImage']
    metadata=json.loads((a.work/'evidence/METADATA-CHANGES.json').read_text())
    need(set(metadata)=={'VERSION','packaging/RELEASE-README.md','packaging/linux/dkr-port.appdata.xml'},'Unexpected release metadata scope')
    baseline=json.loads((a.work/'baseline/SOURCE-FILES.json').read_text())
    for row in baseline:
        expected=metadata[row['path']]['after'] if row['path'] in metadata else row['sha256']
        need(sha(ROOT/row['path'])==expected,'Approved source changed: '+row['path'])
    a.output.mkdir(parents=True);records={}
    for name,source in artifacts.items():
        target=a.output/name;shutil.copy2(source,target);need(sha(target)==sha(source),'Delivery copy changed')
        records[name]={'bytes':target.stat().st_size,'sha256':sha(target)}
    shutil.copy2(AUTH/'RELEASE-NOTES.txt',a.output/'RELEASE-NOTES.txt')
    (a.output/'README.txt').write_text('''DKR-R v1.0.9 Release Build

Windows x64: extract the entire ZIP and open DKR-R.exe. Keep assets/libexec.
Linux x86_64/ARM64: use the matching AppImage and make it executable.
Steam Deck: its AppImage is the exact Linux x86_64 payload.
Android ARM64: install as an update to the existing compatible signed app.
Version name 1.0.9; version code 1090001; the existing app/signer is retained.
Back up saves before updating; do not uninstall to force an update.

Use matching complete 1.0.9 builds on all Online peers. Public discovery needs
the owner-deployed API service and Discord sign-in. Existing peer transport and
rollback remain in use. Existing website/FTP uploads are unchanged. Running
spectator entry remains under its separate service flag.

Bring your own supported US v1.0 or US Rev A/v1.1 ROM. No ROM, personal saves,
installed mods, credentials, SDK, offline guide or debug symbols are included.
Required legal notices are retained. See RELEASE-NOTES.txt and VALIDATION.txt.
Apple source compile checks do not supply macOS/iOS release packages here.

These are local release files. Nothing has been published or uploaded by the
build pipeline. Automatic-update signatures are not included. Native compilation
and static package inspection do not establish physical-device gameplay results.
''',encoding='utf-8')
    with (a.output/'README.txt').open('a',encoding='utf-8') as stream:
        stream.write('\nLinux runtime requirements (static inspection):\n')
        for platform,requirements in linux_requirements.items():
            stream.write(platform+': GLIBC '+str(requirements['glibc'])+'+, GLIBCXX_'+str(requirements['glibcxx'])+' and the normal desktop/audio/font stack plus a compatible Vulkan driver.\n')
        stream.write('The AppImages do not replace the host C++ runtime, glibc or graphics driver.\n')
    (a.output/'VALIDATION.txt').write_text('''v1.0.9 Release Build validation

All four native targets compiled and linked: Windows x64, Linux x86_64,
Linux aarch64, Android arm64-v8a. The Steam Deck artifact matches Linux x86_64.
Source audits retain the owner-accepted directory and Scoreboard contracts.
Native v77/v80 online engine identities agree across all four platforms.
The 2,343-file snapshot is preserved except for three declared metadata files.
Submodules, RecompiledFuncs and RecompiledPatches were not authored in place.

Windows PE/ZIP and Linux ELF/AppImage/SquashFS packages are statically inspected.
Both Linux dependency reports have no unresolved non-system dependency.
Android APK metadata, native architecture, ZIP integrity and update signer were
verified against previous accepted APKs. Required legal notices are included.

The owner reported the preceding public-lobby pilot works as intended. This is
not a claim that this exact release was played on every platform. No game,
launcher, mod, native test executable or AppImage was run by this release pass.
Hardware/controller/touch, native TLS/keyring and cross-platform gameplay checks
remain with the owner. See private receipts for exact build inputs and ABI needs.
''',encoding='utf-8')
    for name in ('README.txt','RELEASE-NOTES.txt','VALIDATION.txt'):
        records[name]={'bytes':(a.output/name).stat().st_size,'sha256':sha(a.output/name)}
    (a.output/'SHA256SUMS.txt').write_text(''.join(r['sha256']+'  '+n+'\n' for n,r in sorted(records.items())))
    report={'schema':1,'version':'1.0.9','label':'v1.0.9 Release Build','files':records,'platforms':audits,
        'rollback_build_ids':contracts['windows'],'android_version_code':1090001,'android_signer_sha256':apk['signer_sha256'],'linux_requirements':linux_requirements,
        'published':False,'automatic_updates_signed':False,'native_execution_performed':False,'metadata_changes':metadata,
        'release_authoring_sha256':{p.relative_to(AUTH).as_posix():sha(p) for p in sorted(AUTH.rglob('*')) if p.is_file() and '__pycache__' not in p.parts}}
    (a.work/'evidence/DELIVERY.json').write_text(json.dumps(report,indent=2)+'\n')
    symbols=a.work/'private-symbols/windows';symbols.mkdir(parents=True,exist_ok=True)
    for relative in ('DKR-R.exe','libexec/dkr-r/DKR-R-Runtime.exe','libexec/dkr-r/DKR-R-ModWorker.exe','libexec/dkr-r/DKR-R-InputHost.exe','libexec/dkr-r/DKR-R-Updater.exe'):
        source=a.windows_binaries/relative
        for path in (source,source.with_suffix('.pdb')):
            if path.is_file():shutil.copy2(path,symbols/path.name)
    print(json.dumps({'release':str(a.output),'version':'1.0.9','artifacts':records,'rollback_build_ids':contracts['windows'],'published':False},indent=2))
