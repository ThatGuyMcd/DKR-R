"""Collect only release uploads and retain private evidence/symbols separately."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import zipfile
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--windows-binaries',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Never replace an existing release folder')
    receipts={key:json.loads((a.work/'packages'/folder/'BUILD-RECEIPT.json').read_text()) for key,folder in
        [('windows','distribution-windows'),('linux','distribution-linux'),('arm64','distribution-arm64')]}
    expected=json.loads((AUTH/'ACCEPTED-WATER-BASELINE.json').read_text())['rollback_build_ids']
    for key,receipt in receipts.items():
        if receipt['version']!='1.0.7' or receipt['rollback_build_ids']!=expected:raise ValueError('Mixed release/engine contract: '+key)
    for key in ('android','linux','arm64'):
        if key=='android':
            report=json.loads((a.work/'evidence/android/APK-INSPECTION.json').read_text())
            if report['version']!='1.0.7' or report['version_code']!=1070018 or not report['matches_published_1_0_6_and_beta_7']:raise ValueError('APK identity not inspected')
        else:
            report=json.loads((a.work/'evidence'/key/'ELF-INSPECTION.json').read_text())
            if report['missing_non_system_dependencies']:raise ValueError('Unresolved dependency')
    source=json.loads((a.work/'evidence/SOURCE-REVIEW-FINAL.json').read_text())
    if source['issues'] or source['missing_relative_links']:raise ValueError('Source inspection needs repair')
    apk_ids=(a.work/'evidence/android/DKRExperimentalOwnedRegistry-COMPILE_DEFINITIONS.txt').read_text()
    if any(value not in apk_ids for value in expected.values()):raise ValueError('Android owned IDs differ')
    a.output.mkdir(parents=True)
    mapping={
        'DKR-R-1.0.7-Windows-x64.zip':a.work/'packages/distribution-windows/DKR-R-1.0.7-Windows-x64.zip',
        'DKR-R-1.0.7-Linux-x86_64.AppImage':a.work/'packages/distribution-linux/DKR-R-1.0.7-Linux-x86_64.AppImage',
        'DKR-R-1.0.7-Linux-aarch64.AppImage':a.work/'packages/distribution-arm64/DKR-R-1.0.7-Linux-aarch64.AppImage',
        'DKR-R-1.0.7-SteamDeck-x86_64.AppImage':a.work/'packages/distribution-linux/DKR-R-1.0.7-Linux-x86_64.AppImage',
        'DKR-R-1.0.7-Android-arm64.apk':a.work/'packages/distribution-android/DKR-R-1.0.7-Android-arm64.apk',
        'DKR-R-Modding-SDK-2.0-v1.0.7.zip':a.work/'sdk/distribution/DKR-R-Modding-SDK-2.0-v1.0.7.zip',
        'track_lab.zip':a.work/'track-lab/track_lab.zip',
    }
    records={}
    for name,path in mapping.items():
        target=a.output/name;shutil.copy2(path,target)
        if sha(path)!=sha(target):raise ValueError('Delivery copy changed')
        records[name]={'bytes':target.stat().st_size,'sha256':sha(target)}
    if records['DKR-R-1.0.7-SteamDeck-x86_64.AppImage']['sha256']!=records['DKR-R-1.0.7-Linux-x86_64.AppImage']['sha256']:raise ValueError('Deck payload changed')
    shutil.copy2(ROOT/'docs/PUBLIC-1.0.7-NOTES.txt',a.output/'RELEASE-NOTES.txt')
    (a.output/'README.txt').write_text('DKR-R v1.0.7 RELEASE FILES\n\nChoose the package for your system. Windows: extract the whole ZIP.\nLinux/Steam Deck: make the AppImage executable. Android: install over the\nexisting compatible app; keep a save backup and do not uninstall to update.\n\nThe SDK 2.0 ZIP and Track Lab addon are separate modder downloads, not\nrequired game files. The SDK includes the Modding Bible in TXT/HTML/PDF.\nNo offline Player Guide, ROM, personal save or installed mod library is included.\n\nThe Deck AppImage is the exact Linux x86_64 payload. No macOS/iOS binaries.\nAll online peers need a compatible complete release and supported ROM revision.\nUse manual upgrades from older 1.0.7+beta packages.\n\nGameplay/hardware acceptance remains with the owner. No game/tests were run\nby the build agent. Automatic-update signatures are not included: the existing\nprivate release signing key is still required; no trust key was replaced.\n',encoding='utf-8')
    with (a.output/'README.txt').open('a',encoding='utf-8') as stream:
        stream.write('\nLinux/Deck requirements: glibc 2.38+, a system libstdc++ providing\nGLIBCXX_3.4.32, the desktop/audio/font libraries and a compatible Vulkan driver.\nThe AppImage does not replace the host C++ runtime or graphics driver.\n')
    for name in ('README.txt','RELEASE-NOTES.txt'):records[name]={'bytes':(a.output/name).stat().st_size,'sha256':sha(a.output/name)}
    (a.output/'SHA256SUMS.txt').write_text(''.join(row['sha256']+'  '+name+'\n' for name,row in sorted(records.items())))
    (a.work/'evidence/DELIVERY.json').write_text(json.dumps({'version':'1.0.7','files':records,'rollback_build_ids':expected,'automatic_update_signed':False,'gameplay_acceptance':'Owner only; pending for this exact stable package.'},indent=2)+'\n')
    symbols=a.work/'private-symbols/windows';symbols.mkdir(parents=True)
    for name in ('DKR-R.exe','libexec/dkr-r/DKR-R-Runtime.exe','libexec/dkr-r/DKR-R-InputHost.exe','libexec/dkr-r/DKR-R-ModWorker.exe','libexec/dkr-r/DKR-R-Updater.exe'):
        for path in (a.windows_binaries/name,(a.windows_binaries/name).with_suffix('.pdb')):
            if path.is_file():shutil.copy2(path,symbols/path.name)
    print('All platform release uploads collected:',a.output)

if __name__=='__main__':main()
