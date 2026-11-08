"""Seal matching inspected vehicle/graphics playtests with all regression evidence."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys,zipfile
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
VERSION='1.0.10-vehicle-playtest.1'
def need(ok,why):
    if not ok:raise ValueError(why)
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    need(not a.output.exists(),'Choose a fresh delivery folder');receipts={};artifacts={};prepared={}
    for platform in ('windows','linux'):
        folder=a.work/'packages'/platform;receipt=json.loads((folder/'BUILD-RECEIPT.json').read_text());artifact=folder/receipt['artifact']['name']
        need(receipt['version']==VERSION and not receipt['published'] and not receipt['automatic_updates_signed'],'Wrong delivery status')
        need(sha(artifact)==receipt['artifact']['sha256'] and artifact.stat().st_size==receipt['artifact']['bytes'],'Artifact changed')
        checks=json.loads((a.work/platform/'checks/CHECKS.json').read_text())
        need(checks['checks']==7449 and checks['before_exit_code']==1 and checks['after_exit_code']==0,'Car regression missing')
        need(len(checks['legacy'])==9 and len(checks['negative'])==8 and not checks['game_rom_rsp_gpu_or_live_multiplayer_executed'],'Legacy/negative checks incomplete')
        if platform=='linux':need(checks['sanitizer'] and checks['sanitizer']['exit_code']==0,'Linux sanitizer result missing')
        prepared[platform]=checks['prepared_sha256']
        handoff=checks['retained'];need(handoff['handoff_checks']==37 and handoff['concurrent_transfers']==8000,'Graphics handoff regression missing')
        render=handoff['retained'];need({k:v['passed_checks'] for k,v in render['render'].items()}=={'DKRRenderStabilityChecks':30,'DKRLiquidResidencyChecks':23},'Render regressions missing')
        memory=render['retained_memory']
        for rev in ('77','80'):
            need(memory['allocator'][rev]['passed_checks']>=180000 and memory['allocator'][rev]['exit_code']==0,'Allocator regression missing')
            need(memory['allocator']['owned'+rev]['passed_checks']==13,'Owned entry regression missing')
        need(len(memory['retained']['checks'])==7,'Retained online suite incomplete')
        for name,value in memory['retained']['checks'].items():need(value['exit_code']==(1 if name in ('before','race-before') else 0),'Retained regression failed')
        archive=json.loads((a.work/platform/'ARCHIVE-REPORT.json').read_text());need(archive['artifact_sha256']==sha(artifact) and archive['exact_payload_match'] and not archive['game_or_appimage_executed'],'Archive not verified')
        worker=json.loads((a.work/platform/'worker-probe/PROBE-RECEIPT.json').read_text());need(not worker['game_executed'] and not worker['roms_or_mods_loaded'] and worker['online_prepare_batch']==1,'Packaged worker handshake missing')
        receipts[platform]=receipt;artifacts[platform]=artifact
    need(prepared['windows']==prepared['linux'],'Prepared real-package fingerprints differ between platforms')
    peers=json.loads((a.work/'baseline/MEMORY-PEERS.json').read_text())
    for receipt in receipts.values():need(receipt['audit']['rollback_build_ids']==peers,'Presentation compatibility changed canonical peer IDs')
    elf=json.loads((a.work/'linux/ELF-REPORT.json').read_text());need(not elf['missing_non_system_dependencies'],'Linux dependency closure incomplete')
    baseline=json.loads((a.work/'baseline/SOURCE-FILES.json').read_text())
    for row in baseline:need(sha(ROOT/row['path'])==row['sha256'],'Pre-existing source changed: '+row['path'])
    a.output.mkdir(parents=True)
    for platform,artifact in artifacts.items():
        shutil.copy2(artifact,a.output/artifact.name);dest=a.output/'Evidence'/platform;dest.mkdir(parents=True)
        shutil.copy2(a.work/'packages'/platform/'BUILD-RECEIPT.json',dest/'BUILD-RECEIPT.json')
        for path in (a.work/platform/'checks').rglob('*'):
            if path.is_file() and path.suffix in ('.json','.log'):
                target=dest/'checks'/path.relative_to(a.work/platform/'checks');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        for name in ('source-audit.json','ARCHIVE-REPORT.json','ELF-REPORT.json','qualified-build.log'):
            path=a.work/platform/name
            if path.is_file():shutil.copy2(path,dest/name)
        shutil.copytree(a.work/platform/'worker-probe',dest/'worker-probe')
    for name in ('PLAYTEST.txt','PIPELINE.txt'):shutil.copy2(AUTH/name,a.output/name)
    if (a.work/'FINDINGS.txt').is_file():shutil.copy2(a.work/'FINDINGS.txt',a.output/'FINDINGS.txt')
    source_zip=a.output/'PATCH-PIPELINE-SOURCE.zip'
    with zipfile.ZipFile(source_zip,'x',zipfile.ZIP_DEFLATED) as z:
        for folder in (ROOT/'patches/memory-stability',ROOT/'patches/render-stability',ROOT/'patches/graphics-handoff',AUTH):
            for path in sorted(folder.rglob('*')):
                if path.is_file() and '__pycache__' not in path.parts:z.write(path,path.relative_to(ROOT).as_posix())
    with zipfile.ZipFile(source_zip) as z:need(not z.testzip(),'Source ZIP integrity failure')
    git='C:/Program Files/Git/cmd/git.exe' if sys.platform=='win32' else 'git';head=subprocess.check_output([git,'rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    report={'schema':1,'version':VERSION,'base_commit':head,'additive_patch':'patches/vehicle-original-textures',
        'unchanged_preexisting_authored_files':len(baseline),'rollback_build_ids':peers,'includes_render_playtest_2':True,
        'published':False,'signed_auto_update':False,'game_rom_rsp_gpu_or_live_multiplayer_executed':False,
        'parsed_input_sha256':sha(a.work/'inputs/brute-v2.dkrkart'),'prepared_package_sha256':prepared['windows'],
        'artifacts':{name:r['artifact'] for name,r in receipts.items()},
        'limitation':'Real gameplay and visual acceptance remain necessary. The exact prior malformed task was not reproduced with game assets. Enabled custom boost anchors remain unsupported and fail admission clearly.'}
    (a.output/'HANDOFF.json').write_text(json.dumps(report,indent=2)+'\n')
    hashes={path.relative_to(a.output).as_posix():sha(path) for path in a.output.rglob('*') if path.is_file()}
    (a.output/'SHA256SUMS.txt').write_text(''.join(value+'  '+name+'\n' for name,value in sorted(hashes.items())))
    for name,value in hashes.items():need(sha(a.output/name)==value,'Delivery copy changed')
    print('Verified matching private vehicle/graphics handoff:',len(hashes),'files;',a.output)
