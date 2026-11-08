"""Collect verified desktop packages, retention checks and retained race checks."""
from pathlib import Path
import argparse,hashlib,json,shutil,zipfile
AUTH=Path(__file__).resolve().parent
VERSION='1.0.9-stability-playtest.3'
def sha(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--packages',default='packages-final');p.add_argument('--checks',default='checks-final');a=p.parse_args()
 if a.output.exists():raise ValueError('Choose a fresh handoff folder')
 receipts={k:json.loads((a.work/a.packages/k/'BUILD-RECEIPT.json').read_text()) for k in ('windows','linux')}
 checks={k:json.loads((a.work/k/a.checks/'RETENTION-RECEIPT.json').read_text()) for k in receipts}
 races={k:json.loads((a.work/k/a.checks/'race/REGRESSION-RECEIPT.json').read_text()) for k in receipts}
 left,right=receipts.values()
 for k,r in receipts.items():
  artifact=a.work/a.packages/k/r['artifact']['name']
  if r['version']!=VERSION or sha(artifact)!=r['artifact']['sha256'] or artifact.stat().st_size!=r['artifact']['bytes']:raise ValueError('Artifact differs from its audit')
  if checks[k]['exit_code']!=0 or checks[k]['passed_checks']<65 or races[k]['passed_checks']!=96:raise ValueError('Regression checks missing')
  if races[k]['before']['exit_code']!=1 or races[k]['after']['exit_code']!=0:raise ValueError('Retained race controls failed')
  probe=json.loads((a.work/k/'packaged-worker-probe/PROBE-RECEIPT.json').read_text())
  worker='DKR-R-ModWorker.exe' if k=='windows' else 'DKR-R-ModWorker'
  worker_hashes=[value['sha256'] for name,value in r['files'].items() if name.endswith('/'+worker)]
  if worker_hashes!=[probe['worker_sha256']] or probe['online_prepare']!=1 or probe['online_prepare_batch']!=1:
   raise ValueError('Capability probe does not match the packaged worker')
  if checks[k]['test_source_sha256']!=sha(AUTH/'tests/retention_tests.cpp'):raise ValueError('Retention test source differs from the executed fixture')
 for field in ('rollback_build_ids','offline_keep_contract','lobby_layout_contract','unchanged_authored_files'):
  if left['audit'][field]!=right['audit'][field]:raise ValueError('Platform source/contract checks differ')
 elf=json.loads((a.work/'linux/elf-inspection.json').read_text())
 if elf['missing_non_system_dependencies']:raise ValueError('Linux dependencies are incomplete')
 a.output.mkdir(parents=True);evidence=a.output/'Evidence';evidence.mkdir()
 for k,r in receipts.items():
  name=r['artifact']['name'];shutil.copy2(a.work/a.packages/k/name,a.output/name)
  shutil.copy2(a.work/a.packages/k/'BUILD-RECEIPT.json',evidence/(k.upper()+'-BUILD-RECEIPT.json'))
  for file in ('RETENTION-RECEIPT.json','retention.log','race/REGRESSION-RECEIPT.json','race/regression-before.log','race/regression-after.log'):
   shutil.copy2(a.work/k/a.checks/file,evidence/(k.upper()+'-'+Path(file).name))
  shutil.copy2(a.work/k/'build-final.log',evidence/(k.upper()+'-build.log'))
  if (a.work/k/'build-completion.log').exists():shutil.copy2(a.work/k/'build-completion.log',evidence/(k.upper()+'-build-completion.log'))
  shutil.copy2(a.work/k/'build-layout.log',evidence/(k.upper()+'-build-layout.log'))
  shutil.copy2(a.work/k/'build-layout-completion.log',evidence/(k.upper()+'-build-layout-completion.log'))
  shutil.copy2(a.work/k/'packaged-worker-probe/PROBE-RECEIPT.json',evidence/(k.upper()+'-WORKER-PROBE.json'))
 shutil.copy2(a.work/'linux/elf-inspection.json',evidence/'LINUX-ELF-INSPECTION.json')
 for source,target in (('PLAYTEST.txt','README.txt'),('IMPLEMENTATION.txt','IMPLEMENTATION.txt')):shutil.copy2(AUTH/source,a.output/target)
 source_zip=a.output/'PATCH-PIPELINE-SOURCES.zip'
 with zipfile.ZipFile(source_zip,'x',zipfile.ZIP_DEFLATED) as z:
  for directory in ('music-preview-repair','lobby-stability','race-release-repair',AUTH.name):
   for path in sorted((AUTH.parent/directory).rglob('*')):
    if path.is_file() and '__pycache__' not in path.parts:z.write(path,path.relative_to(AUTH.parents[1]).as_posix())
 with zipfile.ZipFile(source_zip) as z:
  if z.testzip():raise ValueError('Source bundle ZIP integrity failure')
 report=f'''DKR-R {VERSION} - Validation

PASS: Windows x64 and Linux x86_64 native compile/link and complete packages.
PASS: Windows {checks['windows']['passed_checks']} and Linux {checks['linux']['passed_checks']} synthetic filesystem
      retention checks against the actual production helper/catalogue code.
PASS: The previous catalogue preflight rejects the synthetic renamed/repacked
      duplicate; new retention succeeds, keeps the selected missing mod only,
      and preserves installed labels, activation, metadata and save sentinels.
PASS: Repeated joins are file no-ops; unrelated conflicting entries are not
      copied; true semantic conflicts and corrupt blobs fail; cancellation,
      revisions, concurrent keep jobs and unsafe paths are covered. Linux
      includes an additional symlink rejection check.
PASS: All 96 party-runtime regression assertions on EACH OS; the unchanged
      old release handler still fails the expected first post-load release.
PASS: Source recomposition, compiled bindings, linked features and every
      retained race-start/music/directory/scoreboard/online/security audit.
PASS: Open Lobbies layout is compiled in both native launchers. Existing
      filter/control IDs and actions are retained; wide and narrow layouts
      are source-audited. Visual/gamepad acceptance remains a manual check.
PASS: All {left['audit']['unchanged_authored_files']} pre-existing authored files unchanged.
PASS: Identical peer identifiers; local storage and presentation keep the
      playtest.2 simulation and wire contract unchanged.
PASS: Both PACKAGED ModWorker online/batch capability handshakes without ROMs.
PASS: Windows ZIP CRC and AppImage SquashFS metadata. {len(elf['files'])} ELF files,
      no unresolved non-system dependencies; GLIBC {elf['max_glibc']}, GLIBCXX {elf['max_glibcxx']}.

Not executed: game, user ROM, RSP, AppImage, real multiplayer or offline mod
gameplay. The synthetic fixture executables and worker capability operations
WERE run. Use README.txt for the manual test. Queue/retry/bootstrap policies
are unchanged and source-audited; their earlier suites were not rerun here.

No website/API/FTP deployment, release publishing or automatic-update signing.
No existing user mod library was modified during development or testing.
'''
 (a.output/'VALIDATION.txt').write_text(report)
 files=[*(a.output/r['artifact']['name'] for r in receipts.values()),source_zip]
 (a.output/'SHA256SUMS.txt').write_text(''.join(sha(path)+'  '+path.name+'\n' for path in files))
 print('Verified offline-retention handoff:',a.output)
