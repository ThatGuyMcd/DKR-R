"""Seal the signed, checked release handoff and its GitHub upload instructions."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2]
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('work','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();ev=a.output/'Evidence'
    subprocess.run([sys.executable,'-B',str(AUTH/'update_assets.py'),'verify','--packages',str(a.output),'--output',str(a.output)],check=True)
    manifest=json.loads((a.output/'DKR-R-update.json').read_text())
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    if manifest['commit']!=commit:raise ValueError('Release signature and source HEAD differ')
    for platform in ('windows','linux','arm64'):
        receipt=json.loads((ev/(platform.upper()+'-BUILD-RECEIPT.json')).read_text())
        probe=a.work/platform/'packaged-worker-probe/PROBE-RECEIPT.json'
        result=json.loads(probe.read_text())
        worker='DKR-R-ModWorker.exe' if platform=='windows' else 'DKR-R-ModWorker'
        matched=[v['sha256'] for n,v in receipt['files'].items() if n.endswith('/'+worker)]
        if matched!=[result['worker_sha256']] or result['online_prepare']!=1 or result['online_prepare_batch']!=1:
            raise ValueError('Packaged worker probe does not match: '+platform)
        shutil.copy2(probe,ev/(platform.upper()+'-WORKER-PROBE.json'))
    for name in ('tooling-windows-native.log','tooling-linux-native.log','update-signing-tests.log'):
        path=a.work/'evidence'/name
        if not path.is_file() or b'FAILED' in path.read_bytes() or b'Traceback' in path.read_bytes():raise ValueError('Tooling evidence failed: '+name)
        shutil.copy2(path,ev/name)
    for case in ('normal','corrupt-stage','interrupted','migrate-layout'):
        path=a.work/'windows'/('updater-'+case+'.log')
        if not path.is_file() or b'PASS:' not in path.read_bytes() or b'Traceback' in path.read_bytes():raise ValueError('Windows update qualification failed: '+case)
        shutil.copy2(path,ev/path.name)
    (a.output/'UPLOAD.txt').write_text('DKR-R v1.0.10 - GitHub upload\n\n'
        'Release title: DKR-R v1.0.10 Release Build\nTag: Version1.0.10\nSource commit: '+commit+'\n\n'
        'Create a draft release at that exact source commit. Upload these files:\n'
        'DKR-R-1.0.10-Windows-x64.zip\nDKR-R-1.0.10-Linux-x86_64.AppImage\n'
        'DKR-R-1.0.10-Linux-aarch64.AppImage\nDKR-R-1.0.10-SteamDeck-x86_64.AppImage\n'
        'DKR-R-update.json\nDKR-R-update.sig\nSHA256SUMS.txt\n\n'
        'Paste RELEASE-NOTES.txt as the release description. The changelog TXT can\n'
        'also be attached. Evidence is supplied for review; private source backups\n'
        'and signing keys must stay outside GitHub release assets.\n\n'
        'Check the uploaded package hashes and exact tag/names. Finish the final\n'
        'device/multiplayer smoke test and publish as a stable release. Publishing\n'
        'makes the update discoverable. Do not edit/recompress signed packages.\n')
    with (a.output/'VALIDATION.txt').open('a') as f:
        f.write('\nPASS: Existing trusted-key Ed25519 signature and exact final package hashes.\n'
            'PASS: Windows signed-package install, corrupt-stage rejection, interrupted\n'
            '      recovery and flat-layout migration in isolated temporary profiles.\n'
            '      Actual helpers/runtime ROM-free self-tests ran; user data was not used.\n'
            'PASS: All three packaged ModWorkers report online and batch capabilities.\n'
            'PASS: Windows and native-filesystem Linux contributor suites and signer tests.\n'
            'Release source commit: '+commit+'\n')
    paths=[p for p in sorted(a.output.rglob('*')) if p.is_file() and p.name!='SHA256SUMS.txt']
    (a.output/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(a.output).as_posix()+'\n' for p in paths))
    for p in paths:
        if not p.is_file():raise ValueError('Release file vanished')
    print('Signed release handoff sealed:',len(paths),'files;',a.output)
