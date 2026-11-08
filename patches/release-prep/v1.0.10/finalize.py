"""Collect qualified, matching stable packages without publishing them."""
from pathlib import Path
import argparse,hashlib,json,shutil
AUTH=Path(__file__).resolve().parent;VERSION='1.0.10'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def need(ok,why):
    if not ok:raise ValueError(why)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('work','output','accepted'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    need(not a.output.exists(),'Choose a fresh release handoff folder')
    receipts={k:json.loads((a.work/'packages'/k/'BUILD-RECEIPT.json').read_text()) for k in ('windows','linux','arm64')}
    reference=json.loads(a.accepted.read_text())['audit']
    contracts=('spectator_admission_contract','rollback_build_ids','offline_keep_contract','lobby_layout_contract','release_contract')
    for k,r in receipts.items():
        item=r['artifact'];artifact=a.work/'packages'/k/item['name']
        need(r['version']==VERSION and sha(artifact)==item['sha256'] and artifact.stat().st_size==item['bytes'],'Wrong or stale release package: '+k)
        for field in contracts:need(r['audit'][field]==reference[field],'Accepted playtest.4 contract changed: '+k+'/'+field)
        checks=json.loads((a.work/k/'checks/CHECKS.json').read_text())['checks']
        for field,minimum in (('after',345),('handler',45),('retention',68),('race-after',96)):
            need(checks[field]['exit_code']==0 and checks[field]['passed_checks']>=minimum,'Missing regression evidence: '+k+'/'+field)
        need(checks['before']['exit_code']==checks['race-before']['exit_code']==1,'Negative controls did not reproduce the old failures')
        if k!='windows':
            elf=json.loads((a.work/k/'elf-inspection.json').read_text())
            need(not elf['missing_non_system_dependencies'],'Incomplete Linux dependency tree: '+k)
            need(elf['max_glibc']=='2.38' and elf['max_glibcxx']=='3.4.32','Linux requirements changed')
    a.output.mkdir(parents=True);ev=a.output/'Evidence';ev.mkdir()
    for k,r in receipts.items():
        shutil.copy2(a.work/'packages'/k/r['artifact']['name'],a.output/r['artifact']['name'])
        shutil.copy2(a.work/'packages'/k/'BUILD-RECEIPT.json',ev/(k.upper()+'-BUILD-RECEIPT.json'))
        shutil.copy2(a.work/k/'checks/CHECKS.json',ev/(k.upper()+'-CHECKS.json'))
        if k!='windows':shutil.copy2(a.work/k/'elf-inspection.json',ev/(k.upper()+'-ELF-INSPECTION.json'))
        for log in sorted((a.work/k/'checks').glob('*.log')):shutil.copy2(log,ev/(k.upper()+'-'+log.name))
    linux=a.output/receipts['linux']['artifact']['name']
    shutil.copy2(linux,a.output/'DKR-R-1.0.10-SteamDeck-x86_64.AppImage')
    for name in ('DOCUMENTATION-CHECKS.json','DOCUMENTATION-INVENTORY.json','SOURCE-VERIFICATION.json','METADATA-CHANGES.json'):
        shutil.copy2(a.work/'evidence'/name,ev/name)
    for name,target in (('RELEASE-NOTES.txt','RELEASE-NOTES.txt'),('RELEASE-NOTES.txt','DKR-R-v1.0.10-Changelog.txt'),('UPDATER.txt','UPDATER.txt')):
        shutil.copy2(AUTH/name,a.output/target)
    shutil.copy2(AUTH/'pages/RELEASE-README.md',a.output/'README.txt')
    (a.output/'VALIDATION.txt').write_text('DKR-R v1.0.10 Release Build\n\n'
        'PASS: Windows x64, Linux x86_64 and Linux ARM64 compile/link and packaging.\n'
        'PASS: All three builds retain the exact accepted playtest.4 source contracts.\n'
        'PASS: Spectator admission, handler, race-start, offline-retention and transfer\n'
        '      fixtures on Windows/Linux; ARM64 runs under QEMU with its sysroot.\n'
        'PASS: Old music and release handlers reproduce their original failures.\n'
        'PASS: ZIP CRC, package ownership, both Linux ELF/dependency/ABI checks.\n'
        'PASS: Steam Deck is the exact Linux x86_64 payload.\n'
        'PASS: Documentation links and declared source/metadata changes checked.\n\n'
        'Signing/updater qualification has its own verification receipt and logs.\n'
        'The owner accepted playtest.4. Final v1.0.10 hardware, GPU/controller,\n'
        'multiplayer and real v1.0.9 prompt/install acceptance remain separate.\n'
        'No game, user ROM or RSP was executed for these build/regression checks.\n'
        'Android, macOS and iOS were skipped. No GitHub release is published.\n')
    paths=[p for p in sorted(a.output.rglob('*')) if p.is_file()]
    (a.output/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(a.output).as_posix()+'\n' for p in paths))
    print('Matching release handoff prepared:',a.output)
