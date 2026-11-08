"""Seal a private test handoff only after both platform packages and checks pass."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys,zipfile
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
VERSION='1.0.10-memory-playtest.1'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh handoff folder')
    receipts={};checks={};artifacts={}
    for platform in ('windows','linux'):
        receipt=json.loads((a.work/'packages'/platform/'BUILD-RECEIPT.json').read_text())
        if receipt['version']!=VERSION or receipt['published'] or receipt['automatic_updates_signed']:raise ValueError('Wrong handoff status')
        artifact=a.work/'packages'/platform/receipt['artifact']['name']
        if sha(artifact)!=receipt['artifact']['sha256'] or artifact.stat().st_size!=receipt['artifact']['bytes']:raise ValueError('Artifact changed')
        check=json.loads((a.work/platform/'checks/CHECKS.json').read_text())
        if check['game_or_rom_or_rsp_executed']:raise ValueError('Unexpected execution scope')
        for rev in ('77','80'):
            if check['allocator'][rev]['exit_code'] or check['allocator'][rev]['passed_checks']<180000:raise ValueError('Memory regression missing')
            if check['allocator']['owned'+rev]['passed_checks']!=13:raise ValueError('Owned regression missing')
        if len(check['retained']['checks'])!=7:raise ValueError('Retained checks incomplete')
        for name,data in check['retained']['checks'].items():
            if data['exit_code']!=(1 if name in ('before','race-before') else 0):raise ValueError('Retained regression failed')
        receipts[platform]=receipt;checks[platform]=check;artifacts[platform]=artifact
        worker=json.loads((a.work/platform/'worker-probe/PROBE-RECEIPT.json').read_text())
        if worker['game_executed'] or worker['roms_or_mods_loaded'] or worker['online_prepare_batch']!=1:raise ValueError('Worker qualification failed')
        archive=json.loads((a.work/platform/'ARCHIVE-REPORT.json').read_text())
        if archive['artifact_sha256']!=receipt['artifact']['sha256'] or not archive['exact_payload_match'] or archive['game_or_appimage_executed']:raise ValueError('Archive qualification failed')
    if receipts['windows']['audit']['rollback_build_ids']!=receipts['linux']['audit']['rollback_build_ids']:raise ValueError('Platform peers do not match')
    elf=json.loads((a.work/'linux/ELF-REPORT.json').read_text())
    if elf['missing_non_system_dependencies']:raise ValueError('AppImage dependencies incomplete')
    unchanged=0
    for row in json.loads((a.work/'baseline/SOURCE-FILES.json').read_text()):
        if sha(ROOT/row['path'])!=row['sha256']:raise ValueError('Pre-existing source changed: '+row['path'])
        unchanged+=1
    a.output.mkdir(parents=True)
    for platform,artifact in artifacts.items():
        shutil.copy2(artifact,a.output/artifact.name)
        evidence=a.output/'Evidence'/platform;evidence.mkdir(parents=True)
        shutil.copy2(a.work/'packages'/platform/'BUILD-RECEIPT.json',evidence/'BUILD-RECEIPT.json')
        # Keep evidence, not the synthetic mod trees created by the retention
        # fixture (their content-addressed paths can exceed Windows MAX_PATH).
        for group in ('','retained'):
            source=a.work/platform/'checks'/group;target=evidence/'checks'/group
            target.mkdir(parents=True,exist_ok=True)
            for path in sorted(source.iterdir()):
                if path.is_file() and path.suffix in ('.log','.json'):shutil.copy2(path,target/path.name)
        for name in ('verified-build.log','ELF-REPORT.json','ARCHIVE-REPORT.json'):
            source=a.work/platform/name
            if source.is_file():shutil.copy2(source,evidence/name)
        if (a.work/platform/'worker-probe').is_dir():shutil.copytree(a.work/platform/'worker-probe',evidence/'worker-probe')
    for name in ('PLAYTEST.txt','DIAGNOSIS.txt','PIPELINE.txt'):shutil.copy2(AUTH/name,a.output/name)
    source_zip=a.output/'PATCH-PIPELINE-SOURCE.zip'
    with zipfile.ZipFile(source_zip,'x',zipfile.ZIP_DEFLATED) as z:
        for source in sorted(AUTH.rglob('*')):
            if source.is_file() and '__pycache__' not in source.parts:z.write(source,source.relative_to(ROOT).as_posix())
    with zipfile.ZipFile(source_zip) as z:
        if z.testzip():raise ValueError('Source ZIP verification failed')
    git='C:/Program Files/Git/cmd/git.exe' if sys.platform=='win32' else 'git'
    head=subprocess.check_output([git,'rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    report={'schema':1,'version':VERSION,'base_commit':head,'uncommitted_additive_patch':'patches/memory-stability',
        'unchanged_preexisting_authored_files':unchanged,'rollback_build_ids':receipts['windows']['audit']['rollback_build_ids'],
        'published':False,'signed_auto_update':False,'game_rom_rsp_or_live_multiplayer_executed':False,
        'artifacts':{k:v['artifact'] for k,v in receipts.items()},'validation':'Synthetic allocator and entry fixtures on both platforms, retained regression suite, exact source/compiled bindings, linked guards and allowlisted package inspection. Real gameplay acceptance pending.'}
    (a.output/'HANDOFF.json').write_text(json.dumps(report,indent=2)+'\n')
    files=[p for p in sorted(a.output.rglob('*')) if p.is_file()]
    (a.output/'SHA256SUMS.txt').write_text(''.join(sha(f)+'  '+f.relative_to(a.output).as_posix()+'\n' for f in files))
    print('Matching memory playtest handoff verified:',a.output,'; unchanged authored files:',unchanged)
