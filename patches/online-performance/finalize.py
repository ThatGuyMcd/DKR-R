"""Seal matching test builds and evidence before any later main-branch integration."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1];VERSION='1.0.11-online-playtest.1'
def need(ok,why):
    if not ok:raise ValueError(why)
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('work','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();need(not a.output.exists(),'Choose a fresh handoff folder');receipts={}
    for platform in ('windows','linux'):
        receipt=json.loads((a.work/'packages'/platform/'BUILD-RECEIPT.json').read_text())
        artifact=a.work/'packages'/platform/receipt['artifact']['name'];need(receipt['version']==VERSION and sha(artifact)==receipt['artifact']['sha256'],'Package identity differs')
        archive=json.loads((a.work/platform/'ARCHIVE-REPORT.json').read_text());need(archive['artifact_sha256']==sha(artifact) and archive['exact_payload_match'],'Archive read-back missing')
        core=json.loads((a.work/platform/'checks/CHECKS.json').read_text());need(len(core['fixtures'])==6 and not core['game_rom_rsp_gpu_live_multiplayer_executed'],'Core fixtures incomplete')
        retained=json.loads((a.work/platform/'retained-final/CHECKS.json').read_text());need(len(retained['checks'])==14 and retained['vehicle']['checks']==7449,'Retained checks incomplete')
        if platform=='linux':need(retained['vehicle']['sanitizer']['exit_code']==0,'Linux vehicle sanitizer check missing')
        portrait=json.loads((a.work/platform/'portraits.json').read_text());need(portrait['exit_code']==0 and portrait['passed_checks']>=14,'Portrait regression missing')
        probe=json.loads((a.work/platform/'worker-probe/PROBE-RECEIPT.json').read_text());need(probe['online_prepare_batch']==1 and not probe['game_executed'],'Packaged worker probe missing')
        receipts[platform]=receipt
    need(receipts['windows']['audit']['rollback_build_ids']==receipts['linux']['audit']['rollback_build_ids'],'Windows/Linux peer IDs differ')
    portraits=[json.loads((a.work/p/'installed-portraits.json').read_text()) for p in ('windows','linux')]
    need(portraits[0]==portraits[1] and portraits[0]['count']>0,'Decoded installed portraits differ across platforms')
    need(json.loads((a.work/'linux/ELF-REPORT.json').read_text())['missing_non_system_dependencies']=={},'Linux dependency closure incomplete')
    baseline=json.loads((a.work/'baseline/SOURCE-FILES.json').read_text())
    for row in baseline:need(sha(ROOT/row['path'])==row['sha256'],'Pre-existing source changed before handoff: '+row['path'])
    a.output.mkdir(parents=True)
    for platform,receipt in receipts.items():
        shutil.copy2(a.work/'packages'/platform/receipt['artifact']['name'],a.output/receipt['artifact']['name'])
        dest=a.output/'Evidence'/platform;dest.mkdir(parents=True)
        for name in ('BUILD-RECEIPT.json',):shutil.copy2(a.work/'packages'/platform/name,dest/name)
        for name in ('source-audit.json','ARCHIVE-REPORT.json','ELF-REPORT.json','portraits.json','portraits.log','installed-portraits.json','build-final-r2.log','portrait-fixture-build.log'):
            path=a.work/platform/name
            if path.is_file():shutil.copy2(path,dest/name)
        for name in ('checks','retained-final','worker-probe'):
            source=a.work/platform/name
            for path in source.rglob('*'):
                relative=path.relative_to(source)
                if any(part in {'retention-fixture','variants'} for part in relative.parts):continue
                if path.suffix not in {'.json','.log'} or not path.is_file():continue
                target=dest/name/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
    for name in ('PLAYTEST.txt','PIPELINE.txt','FINDINGS.txt'):shutil.copy2(AUTH/name,a.output/name)
    shutil.copy2(ROOT/'patches/character-library-portraits/PIPELINE.txt',a.output/'CHARACTER-PORTRAITS.txt')
    with zipfile.ZipFile(a.output/'PATCH-PIPELINE-SOURCES.zip','x',zipfile.ZIP_DEFLATED) as z:
        for directory in (AUTH,ROOT/'patches/character-library-portraits'):
            for path in sorted(directory.rglob('*')):
                if path.is_file() and '__pycache__' not in path.parts:z.write(path,path.relative_to(ROOT).as_posix())
    with zipfile.ZipFile(a.output/'PATCH-PIPELINE-SOURCES.zip') as z:need(not z.testzip(),'Source archive CRC failed')
    git='C:/Program Files/Git/cmd/git.exe'
    head=subprocess.check_output([git,'rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    report={'schema':1,'version':VERSION,'source_base_commit':head,'unchanged_preexisting_authored_files':len(baseline),
        'rollback_build_ids':receipts['windows']['audit']['rollback_build_ids'],'artifacts':{p:r['artifact'] for p,r in receipts.items()},
        'game_rom_rsp_gpu_live_multiplayer_executed':False,'published':False,'automatic_updates_signed':False,
        'owner_acceptance':'Fresh and cached joins, full mod selection, stock/custom races, scene changes, spectators and portrait appearance still require real-machine testing.'}
    (a.output/'HANDOFF.json').write_text(json.dumps(report,indent=2)+'\n')
    hashes={p.relative_to(a.output).as_posix():sha(p) for p in a.output.rglob('*') if p.is_file()}
    (a.output/'SHA256SUMS.txt').write_text(''.join(h+'  '+name+'\n' for name,h in sorted(hashes.items())))
    for name,hash in hashes.items():need(sha(a.output/name)==hash,'Delivery copy differs')
    print('Matching Windows/Linux online playtest sealed:',a.output)
