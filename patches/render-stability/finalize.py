"""Seal matching, inspected private render playtests with reproducible evidence."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys,zipfile
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
VERSION='1.0.10-render-playtest.1'
def need(ok,message):
    if not ok:raise ValueError(message)
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    need(not a.output.exists(),'Choose a fresh handoff folder')
    receipts={};artifacts={}
    for platform in ('windows','linux'):
        folder=a.work/'packages'/platform;receipt=json.loads((folder/'BUILD-RECEIPT.json').read_text());artifact=folder/receipt['artifact']['name']
        need(receipt['version']==VERSION and not receipt['published'] and not receipt['automatic_updates_signed'],'Wrong package status')
        need(sha(artifact)==receipt['artifact']['sha256'] and artifact.stat().st_size==receipt['artifact']['bytes'],'Artifact changed')
        check=json.loads((a.work/platform/'checks/CHECKS.json').read_text())
        need(not check['game_or_rom_or_rsp_executed'] and check['concurrent_workloads']==4000,'Wrong test execution scope')
        need({name:value['passed_checks'] for name,value in check['render'].items()}=={'DKRRenderStabilityChecks':30,'DKRLiquidResidencyChecks':23},'Render checks incomplete')
        memory=check['retained_memory']
        for rev in ('77','80'):
            need(memory['allocator'][rev]['passed_checks']>=180000 and memory['allocator'][rev]['exit_code']==0,'Retained allocator checks missing')
            need(memory['allocator']['owned'+rev]['passed_checks']==13,'Owned entry checks missing')
        need(len(memory['retained']['checks'])==7,'Retained online suite incomplete')
        for name,value in memory['retained']['checks'].items():
            need(value['exit_code']==(1 if name in ('before','race-before') else 0),'Retained regression failed')
        archive=json.loads((a.work/platform/'ARCHIVE-REPORT.json').read_text())
        need(archive['artifact_sha256']==sha(artifact) and archive['exact_payload_match'] and not archive['game_or_appimage_executed'],'Archive not verified')
        worker=json.loads((a.work/platform/'worker-probe/PROBE-RECEIPT.json').read_text())
        need(not worker['game_executed'] and not worker['roms_or_mods_loaded'] and worker['online_prepare_batch']==1,'Packaged worker not qualified')
        receipts[platform]=receipt;artifacts[platform]=artifact
    need(receipts['windows']['audit']['rollback_build_ids']==receipts['linux']['audit']['rollback_build_ids'],'Platform peer identities differ')
    previous=json.loads((a.work/'baseline/MEMORY-PEERS.json').read_text())
    need(receipts['windows']['audit']['rollback_build_ids']==previous,'Presentation repair changed canonical peer IDs')
    elf=json.loads((a.work/'linux/ELF-REPORT.json').read_text());need(not elf['missing_non_system_dependencies'],'AppImage dependencies missing')
    source=json.loads((a.work/'baseline/SOURCE-FILES.json').read_text())
    for row in source:need(sha(ROOT/row['path'])==row['sha256'],'Pre-existing source changed: '+row['path'])
    a.output.mkdir(parents=True)
    for platform,artifact in artifacts.items():
        shutil.copy2(artifact,a.output/artifact.name);dest=a.output/'Evidence'/platform;dest.mkdir(parents=True)
        shutil.copy2(a.work/'packages'/platform/'BUILD-RECEIPT.json',dest/'BUILD-RECEIPT.json')
        for group in ('','memory','memory/retained'):
            source_dir=a.work/platform/'checks'/group;target=dest/'checks'/group;target.mkdir(parents=True,exist_ok=True)
            for path in sorted(source_dir.iterdir()):
                if path.is_file() and path.suffix in ('.json','.log'):shutil.copy2(path,target/path.name)
        for name in ('source-audit.json','ELF-REPORT.json','ARCHIVE-REPORT.json','verified-build.log'):
            source_path=a.work/platform/name
            if source_path.is_file():shutil.copy2(source_path,dest/name)
        shutil.copytree(a.work/platform/'worker-probe',dest/'worker-probe')
    for name in ('PLAYTEST.txt','PIPELINE.txt'):shutil.copy2(AUTH/name,a.output/name)
    diagnostic=a.output/'Evidence/diagnostics';diagnostic.mkdir(parents=True)
    shutil.copy2(a.work/'COPY-COST.log',diagnostic/'COPY-COST.log')
    shutil.copy2(a.work/'baseline/BEFORE-WATER-PROBE.log',diagnostic/'BEFORE-WATER-PROBE.log')
    for path in (a.work/'sanitizers').glob('*.log'):shutil.copy2(path,diagnostic/('SANITIZERS-'+path.name))
    source_zip=a.output/'PATCH-PIPELINE-SOURCE.zip'
    with zipfile.ZipFile(source_zip,'x',zipfile.ZIP_DEFLATED) as z:
        for directory in (ROOT/'patches/memory-stability',AUTH):
            for path in sorted(directory.rglob('*')):
                if path.is_file() and '__pycache__' not in path.parts:z.write(path,path.relative_to(ROOT).as_posix())
    with zipfile.ZipFile(source_zip) as z:need(not z.testzip(),'Source ZIP corrupted')
    git='C:/Program Files/Git/cmd/git.exe' if sys.platform=='win32' else 'git'
    head=subprocess.check_output([git,'rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    handoff={'schema':1,'version':VERSION,'base_commit':head,'additive_patch':'patches/render-stability',
        'unchanged_preexisting_authored_files':len(source),'rollback_build_ids':previous,'presentation_only':True,
        'published':False,'signed_auto_update':False,'game_rom_gpu_rsp_or_live_multiplayer_executed':False,
        'artifacts':{name:r['artifact'] for name,r in receipts.items()},
        'unresolved':'The original writer/lifetime behind three malformed graphics tasks is not proven. Bounded provenance is included; a graphics-root-cause fix is not claimed.'}
    (a.output/'HANDOFF.json').write_text(json.dumps(handoff,indent=2)+'\n')
    files=[path for path in sorted(a.output.rglob('*')) if path.is_file()]
    (a.output/'SHA256SUMS.txt').write_text(''.join(sha(path)+'  '+path.relative_to(a.output).as_posix()+'\n' for path in files))
    print('Matching render playtests verified:',a.output,'; pre-existing files unchanged:',len(source))
