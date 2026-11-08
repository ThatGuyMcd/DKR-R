"""Seal inspected, matching private playtest packages and their evidence."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys,zipfile
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
VERSION='1.0.10-render-playtest.2'
def need(ok,why):
    if not ok:raise ValueError(why)
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    need(not a.output.exists(),'Choose a fresh handoff folder');artifacts={};receipts={}
    for platform in ('windows','linux'):
        folder=a.work/'packages'/platform;receipt=json.loads((folder/'BUILD-RECEIPT.json').read_text());artifact=folder/receipt['artifact']['name']
        need(receipt['version']==VERSION and not receipt['published'] and not receipt['automatic_updates_signed'],'Wrong package status')
        need(sha(artifact)==receipt['artifact']['sha256'] and artifact.stat().st_size==receipt['artifact']['bytes'],'Artifact changed')
        checks=json.loads((a.work/platform/'checks/CHECKS.json').read_text())
        need(not checks['game_or_rom_or_rsp_executed'] and checks['handoff_checks']==37 and checks['concurrent_transfers']==8000,'Handoff regression missing')
        render=checks['retained'];need({k:v['passed_checks'] for k,v in render['render'].items()}=={'DKRRenderStabilityChecks':30,'DKRLiquidResidencyChecks':23},'Render regressions missing')
        memory=render['retained_memory']
        for rev in ('77','80'):
            need(memory['allocator'][rev]['passed_checks']>=180000 and memory['allocator'][rev]['exit_code']==0,'Allocator regression missing')
            need(memory['allocator']['owned'+rev]['passed_checks']==13,'Owned entry regression missing')
        need(len(memory['retained']['checks'])==7,'Retained online suite incomplete')
        for name,value in memory['retained']['checks'].items():need(value['exit_code']==(1 if name in ('before','race-before') else 0),'Retained regression failed')
        archive=json.loads((a.work/platform/'ARCHIVE-REPORT.json').read_text());need(archive['artifact_sha256']==sha(artifact) and archive['exact_payload_match'] and not archive['game_or_appimage_executed'],'Archive not verified')
        worker=json.loads((a.work/platform/'worker-probe/PROBE-RECEIPT.json').read_text());need(not worker['game_executed'] and not worker['roms_or_mods_loaded'] and worker['online_prepare_batch']==1,'Packaged worker handshake missing')
        receipts[platform]=receipt;artifacts[platform]=artifact
    peers=json.loads((a.work/'baseline/MEMORY-PEERS.json').read_text())
    for receipt in receipts.values():need(receipt['audit']['rollback_build_ids']==peers,'Presentation handoff changed canonical peer IDs')
    elf=json.loads((a.work/'linux/ELF-REPORT.json').read_text());need(not elf['missing_non_system_dependencies'],'Linux dependency closure incomplete')
    baseline=json.loads((a.work/'baseline/SOURCE-FILES.json').read_text())
    for row in baseline:need(sha(ROOT/row['path'])==row['sha256'],'Pre-existing source changed: '+row['path'])
    a.output.mkdir(parents=True)
    for platform,artifact in artifacts.items():
        shutil.copy2(artifact,a.output/artifact.name)
        dest=a.output/'Evidence'/platform;dest.mkdir(parents=True)
        shutil.copy2(a.work/'packages'/platform/'BUILD-RECEIPT.json',dest/'BUILD-RECEIPT.json')
        for path in (a.work/platform/'checks').rglob('*'):
            if path.is_file() and path.suffix in ('.json','.log'):
                target=dest/'checks'/path.relative_to(a.work/platform/'checks');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        for name in ('source-audit.json','ARCHIVE-REPORT.json','ELF-REPORT.json','qualified-build.log'):
            path=a.work/platform/name
            if path.is_file():shutil.copy2(path,dest/name)
        shutil.copytree(a.work/platform/'worker-probe',dest/'worker-probe')
    for name in ('PLAYTEST.txt','PIPELINE.txt'):shutil.copy2(AUTH/name,a.output/name)
    diagnostics=a.output/'Evidence/diagnostics';diagnostics.mkdir(parents=True)
    for name in ('handoff-epoch.log','SANITIZERS.json'):
        path=a.work/'sanitizers'/name
        need(path.is_file(),'Final sanitizer evidence missing: '+name);shutil.copy2(path,diagnostics/name)
    for name in ('COPY-COST.log','FINDINGS.txt'):
        if (a.work/name).is_file():shutil.copy2(a.work/name,diagnostics/name)
    source_zip=a.output/'PATCH-PIPELINE-SOURCE.zip'
    with zipfile.ZipFile(source_zip,'x',zipfile.ZIP_DEFLATED) as z:
        for folder in (ROOT/'patches/memory-stability',ROOT/'patches/render-stability',AUTH):
            for path in sorted(folder.rglob('*')):
                if path.is_file() and '__pycache__' not in path.parts:z.write(path,path.relative_to(ROOT).as_posix())
    with zipfile.ZipFile(source_zip) as z:need(not z.testzip(),'Source ZIP integrity failure')
    git='C:/Program Files/Git/cmd/git.exe' if sys.platform=='win32' else 'git'
    head=subprocess.check_output([git,'rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    report={'schema':1,'version':VERSION,'base_commit':head,'additive_patch':'patches/graphics-handoff',
        'unchanged_preexisting_authored_files':len(baseline),'rollback_build_ids':peers,
        'published':False,'signed_auto_update':False,'game_rom_rsp_gpu_or_live_multiplayer_executed':False,
        'artifacts':{name:r['artifact'] for name,r in receipts.items()},
        'limitation':'The scheduler ownership gap is closed and synthetically reproduced. The exact latest malformed-task incident still requires repeated gameplay acceptance; the original guard and provenance remain.'}
    (a.output/'HANDOFF.json').write_text(json.dumps(report,indent=2)+'\n')
    hashes={path.relative_to(a.output).as_posix():sha(path) for path in a.output.rglob('*') if path.is_file()}
    (a.output/'SHA256SUMS.txt').write_text(''.join(value+'  '+name+'\n' for name,value in sorted(hashes.items())))
    for name,value in hashes.items():need(sha(a.output/name)==value,'Handoff copy changed')
    print('Verified private handoff:',len(hashes),'files;',a.output)
