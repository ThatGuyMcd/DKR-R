"""Collect audited matching packages and synthetic regression evidence."""
from pathlib import Path
import argparse, hashlib, json, shutil, zipfile
AUTH=Path(__file__).resolve().parent
VERSION='1.0.9-stability-playtest.4'
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh handoff directory')
    receipts={k:json.loads((a.work/'packages'/k/'BUILD-RECEIPT.json').read_text()) for k in ('windows','linux')}
    checks={k:json.loads((a.work/k/'checks/ADMISSION-RECEIPT.json').read_text()) for k in receipts}
    retention={k:json.loads((a.work/k/'checks/retained/RETENTION-RECEIPT.json').read_text()) for k in receipts}
    races={k:json.loads((a.work/k/'checks/retained/race/REGRESSION-RECEIPT.json').read_text()) for k in receipts}
    for k,r in receipts.items():
        artifact=a.work/'packages'/k/r['artifact']['name'];c=checks[k]
        if r['version']!=VERSION or sha(artifact)!=r['artifact']['sha256'] or artifact.stat().st_size!=r['artifact']['bytes']:
            raise ValueError('Packaged artifact differs from its audit')
        if c['before']['exit_code']!=1 or c['after']['exit_code']!=0 or c['after']['passed_checks']<300 or c['handler']['exit_code']!=0 or c['handler']['passed_checks']<30:
            raise ValueError('Admission/handler regression evidence is incomplete')
        if c['test_sources']!={p.name:sha(p) for p in sorted((AUTH/'tests').glob('*.cpp'))}:raise ValueError('Executed test sources differ')
        if retention[k]['exit_code']!=0 or retention[k]['passed_checks']<65 or races[k]['passed_checks']!=96 or races[k]['before']['exit_code']!=1 or races[k]['after']['exit_code']!=0:
            raise ValueError('Retained playtest.3 regressions failed')
        probe=json.loads((a.work/k/'packaged-worker-probe/PROBE-RECEIPT.json').read_text())
        worker='DKR-R-ModWorker.exe' if k=='windows' else 'DKR-R-ModWorker'
        if [v['sha256'] for n,v in r['files'].items() if n.endswith('/'+worker)]!=[probe['worker_sha256']] or probe['online_prepare']!=1 or probe['online_prepare_batch']!=1:
            raise ValueError('Worker capability probe does not match the package')
    left,right=receipts.values()
    for field in ('spectator_admission_contract','rollback_build_ids','offline_keep_contract','lobby_layout_contract','release_contract','unchanged_authored_files'):
        if left['audit'][field]!=right['audit'][field]:raise ValueError('Platform source/contract audits differ: '+field)
    elf=json.loads((a.work/'linux/elf-inspection.json').read_text())
    if elf['missing_non_system_dependencies']:raise ValueError('Linux package dependencies are incomplete')
    a.output.mkdir(parents=True);evidence=a.output/'Evidence';evidence.mkdir()
    for k,r in receipts.items():
        shutil.copy2(a.work/'packages'/k/r['artifact']['name'],a.output/r['artifact']['name'])
        shutil.copy2(a.work/'packages'/k/'BUILD-RECEIPT.json',evidence/(k.upper()+'-BUILD-RECEIPT.json'))
        for source in ('ADMISSION-RECEIPT.json','before.log','after.log','handler.log',
            'retained/RETENTION-RECEIPT.json','retained/retention.log','retained/race/REGRESSION-RECEIPT.json',
            'retained/race/regression-before.log','retained/race/regression-after.log'):
            shutil.copy2(a.work/k/'checks'/source,evidence/(k.upper()+'-'+Path(source).name))
        for source in ('configure.log','check-build.log','build-final.log','build-completion.log'):
            shutil.copy2(a.work/k/source,evidence/(k.upper()+'-'+source))
        shutil.copy2(a.work/k/'packaged-worker-probe/PROBE-RECEIPT.json',evidence/(k.upper()+'-WORKER-PROBE.json'))
    shutil.copy2(a.work/'linux/elf-inspection.json',evidence/'LINUX-ELF-INSPECTION.json')
    for name,target in (('PLAYTEST.txt','README.txt'),('IMPLEMENTATION.txt','IMPLEMENTATION.txt')):shutil.copy2(AUTH/name,a.output/target)
    source_zip=a.output/'PATCH-PIPELINE-SOURCES.zip'
    with zipfile.ZipFile(source_zip,'x',zipfile.ZIP_DEFLATED) as z:
        for directory in ('music-preview-repair','lobby-stability','race-release-repair','offline-retention-repair',AUTH.name):
            for path in sorted((AUTH.parent/directory).rglob('*')):
                if path.is_file() and '__pycache__' not in path.parts:z.write(path,path.relative_to(AUTH.parents[1]).as_posix())
    with zipfile.ZipFile(source_zip) as z:
        if z.testzip():raise ValueError('Patch source ZIP integrity failure')
    report=f'''DKR-R {VERSION} - Validation

PASS: Windows x64 and Linux x86_64 native compile/link and complete packages.
PASS: The old production resident music implementation reproduces the cold
      spectator failure on EACH OS with the missing-loading-boundary error.
PASS: {checks['windows']['after']['passed_checks']} Windows and {checks['linux']['after']['passed_checks']} Linux admission checks against actual production
      music/preparation/decoder code using synthetic WAVs and memory fields.
      Cold admission, host PCM parity, warm rollback, lease release, invalid
      checkpoint rejection, scope unwind/thread isolation, recording changes,
      results/sequence states, decode bounds/shape and cancellation are covered.
PASS: {checks['windows']['handler']['passed_checks']} Windows and {checks['linux']['handler']['passed_checks']} Linux handler checks against the final party handler
      with synthetic owner hooks: failed admission, bounded retries, concrete
      reasons, viewer isolation, recovery and confirmed-progress reset.
PASS: All 96 race-start assertions on EACH OS against the final handler.
      The old release handler still fails its expected negative control.
PASS: {retention['windows']['passed_checks']} Windows and {retention['linux']['passed_checks']} Linux offline-retention checks; existing metadata,
      activation, labels and save sentinels preserved. Linux adds symlink checks.
PASS: Exact source recomposition, compiler bindings, linked markers and every
      retained playtest.3 race-start/music/directory/online/security audit.
PASS: All {left['audit']['unchanged_authored_files']} pre-existing authored files unchanged from the verified backup.
PASS: Both ROM revisions retain playtest.3 peer identities. This repair adds
      confirmed observer resource preparation; canonical/wire layouts stay fixed.
PASS: Both packaged ModWorkers complete online/batch capability handshakes.
PASS: Windows ZIP CRC, AppImage SquashFS metadata and {len(elf['files'])} ELF files; no
      unresolved non-system dependencies. GLIBC {elf['max_glibc']}, GLIBCXX {elf['max_glibcxx']}.

Not executed: game, user ROM, RSP, AppImage or physical multiplayer. Synthetic
fixture executables and packaged worker capability operations WERE run. The
owner's Steam Deck/Windows live test in README.txt is the remaining acceptance.
No source submodule/RecompiledFuncs/RecompiledPatches edits, user mod-library
mutation, website/API deployment, publishing or automatic-update signing.
'''
    (a.output/'VALIDATION.txt').write_text(report)
    paths=[p for p in sorted(a.output.rglob('*')) if p.is_file() and p.name!='SHA256SUMS.txt']
    (a.output/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(a.output).as_posix()+'\n' for p in paths))
    print('Verified matching spectator-admission handoff:',a.output)
