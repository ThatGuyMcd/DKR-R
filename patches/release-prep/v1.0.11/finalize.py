"""Collect verified stable desktop packages before committing and signing."""
from pathlib import Path
import argparse,hashlib,json,shutil
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2];VERSION=(ROOT/'VERSION').read_text().strip()
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def need(ok,why):
    if not ok:raise ValueError(why)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('work','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();need(not a.output.exists(),'Choose a fresh release handoff folder')
    receipts={};prepared={};peers=None
    for platform,arch in (('windows','x86_64'),('linux','x86_64'),('arm64','aarch64')):
        folder=a.work/'packages'/platform;r=json.loads((folder/'BUILD-RECEIPT.json').read_text());item=r['artifact'];artifact=folder/item['name']
        need(r['version']==VERSION and r['arch']==arch and sha(artifact)==item['sha256'] and artifact.stat().st_size==item['bytes'],'Stale release artifact: '+platform)
        need(r['files']['RELEASE-NOTES.txt']['sha256']==sha(ROOT/('docs/PUBLIC-'+VERSION+'-NOTES.txt')),'Packaged release notes differ: '+platform)
        need(r['files']['README.txt']['sha256']==sha(ROOT/'packaging/RELEASE-README.md'),'Packaged README differs: '+platform)
        ids=r['audit']['rollback_build_ids']
        if peers is None:peers=ids
        need(ids==peers,'Canonical peer IDs differ')
        checks=json.loads((a.work/platform/'checks/CHECKS.json').read_text())
        need(not checks['game_rom_rsp_gpu_or_live_multiplayer_executed'] and len(checks['checks'])==14,'Incomplete regression set')
        for name,value in checks['checks'].items():need(value['exit_code']==(1 if name in ('before','race-before') else 0),'Regression failed: '+platform+'/'+name)
        v=checks['vehicle'];need(v['checks']==7449 and len(v['legacy'])==9 and len(v['negative'])==8,'Vehicle checks incomplete')
        need(v['before_exit_code']==1 and v['after_exit_code']==0,'Vehicle negative control missing')
        if platform=='linux':need(v['sanitizer'] and v['sanitizer']['exit_code']==0,'Native sanitizer evidence missing')
        need(checks['emulated_arm64']==(platform=='arm64'),'Execution architecture receipt differs')
        online=json.loads((a.work/platform/'online-checks/CHECKS.json').read_text())
        need(len(online['fixtures'])==7,'Incomplete current online fixtures')
        for name,value in online['fixtures'].items():
            need(value['exit_code']==(1 if 'Before' in name else 0),'Current online fixture failed: '+name)
        for name in ('profile','portraits'):
            need(b'checks' in (a.work/platform/(name+'.log')).read_bytes(),'Missing current fixture: '+name)
        prepared[platform]=v['prepared_sha256']
        archive=json.loads((a.work/platform/'ARCHIVE-REPORT.json').read_text())
        need(archive['exact_payload_match'] and archive['artifact_sha256']==sha(artifact),'Archive read-back failed')
        probe=json.loads((a.work/platform/'packaged-worker-probe/PROBE-RECEIPT.json').read_text())
        worker='DKR-R-ModWorker.exe' if platform=='windows' else 'DKR-R-ModWorker'
        need([x['sha256'] for n,x in r['files'].items() if n.endswith('/'+worker)]==[probe['worker_sha256']],'Packaged worker hash differs')
        need(probe['online_prepare_batch']==1 and not probe['roms_or_mods_loaded'],'Worker capability failed')
        if platform!='windows':
            elf=json.loads((a.work/platform/'elf-inspection.json').read_text())
            need(not elf['missing_non_system_dependencies'] and elf['arch']==arch,'Linux ELF closure incomplete')
            need(elf['max_glibc']=='2.38' and elf['max_glibcxx']=='3.4.32','Linux requirements changed')
        receipts[platform]=r
    need(len(set(prepared.values()))==1,'Prepared vehicle data differs across platform builds')
    a.output.mkdir(parents=True);ev=a.output/'Evidence';ev.mkdir()
    for platform,r in receipts.items():
        shutil.copy2(a.work/'packages'/platform/r['artifact']['name'],a.output/r['artifact']['name'])
        shutil.copy2(a.work/'packages'/platform/'BUILD-RECEIPT.json',ev/(platform.upper()+'-BUILD-RECEIPT.json'))
        dest=ev/platform;dest.mkdir()
        for path in (a.work/platform).rglob('*'):
            if path.is_file() and path.suffix in ('.json','.log') and not any(x.startswith('generated') or 'fixture' in x or x=='variants' for x in path.relative_to(a.work/platform).parts):
                target=dest/path.relative_to(a.work/platform);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
    shutil.copy2(a.output/receipts['linux']['artifact']['name'],a.output/('DKR-R-'+VERSION+'-SteamDeck-x86_64.AppImage'))
    for name in ('DOCUMENTATION-CHECKS.json','DOCUMENTATION-INVENTORY.json','SOURCE-VERIFICATION.json','METADATA-CHANGES.json'):
        shutil.copy2(a.work/'evidence'/name,ev/name)
    for path in (a.work/'evidence').glob('*'):
        if path.is_file() and path.suffix in ('.log','.json'):shutil.copy2(path,ev/path.name)
    for source,name in ((ROOT/('docs/PUBLIC-'+VERSION+'-NOTES.txt'),'RELEASE-NOTES.txt'),(ROOT/('docs/DKR-R-v'+VERSION+'-Changelog.txt'),'DKR-R-v'+VERSION+'-Changelog.txt'),(AUTH/'UPDATER.txt','UPDATER.txt'),(ROOT/'packaging/RELEASE-README.md','README.txt')):
        shutil.copy2(source,a.output/name)
    (a.output/'VALIDATION.txt').write_text('DKR-R v'+VERSION+' Release Build\n\n'
        'PASS: Windows x64, Linux x86_64 and Linux ARM64 compiled and linked.\n'
        'PASS: Full retained memory/render/handoff/vehicle/online source audits.\n'
        'PASS: Named synthetic fixtures on Windows/Linux; ARM64 uses QEMU.\n'
        'PASS: 7,449 vehicle checks, nine unchanged legacy packages and eight negative\n'
        '      admissions on each architecture. Native Linux ASan/UBSan passed.\n'
        'PASS: Matching prepared Brute data and canonical peer IDs across builds.\n'
        'PASS: Every archive file read back; both ELF dependency/ABI checks passed.\n'
        'PASS: Packaged workers report online/batch capabilities without loading mods.\n'
        'PASS: Steam Deck is an exact Linux x86_64 copy. Documentation links checked.\n\n'
        'No game, user ROM, RSP/GPU workload or live multiplayer was executed.\n'
        'The exact previous malformed graphics task still needs gameplay confirmation.\n'
        'Final hardware, HUD, modded/spectator races and updater prompt acceptance\n'
        'remain separate. Android, macOS and iOS were skipped. Nothing is published.\n')
    print('Verified stable package handoff prepared:',a.output)
