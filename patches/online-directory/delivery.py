"""Finalize the unpublished owner delivery from verified client/API artifacts."""
from pathlib import Path
import argparse,hashlib,json,shutil,zipfile

AUTH=Path(__file__).resolve().parent
VERSION='1.0.9-public-lobbies-playtest.1'

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def need(ok,message):
    if not ok:raise ValueError(message)

def main(a):
    root=a.output.resolve()
    need(not (root/'DELIVERY.json').exists(),'Delivery already finalized')
    artifacts={};clients={};evidence={}
    for platform in ('windows','linux'):
        receipt_path=root/platform/'BUILD-RECEIPT.json'
        receipt=json.loads(receipt_path.read_text())
        need(receipt['version']==VERSION and not receipt['published'],'Wrong client version/publication')
        need(receipt['audit']['unchanged_authored_files']==2304,'Authored baseline incomplete')
        artifact=root/platform/receipt['artifact']['name']
        need(sha(artifact)==receipt['artifact']['sha256'],'Client artifact changed: '+platform)
        need(artifact.stat().st_size==receipt['artifact']['bytes'],'Client size changed')
        artifacts[artifact.relative_to(root).as_posix()]=receipt['artifact']
        clients[platform]={'directory_contract':receipt['audit']['directory_contract'],
            'scoreboard_contract':receipt['audit']['retained']['scoreboard_contract'],
            'unchanged_authored_files':receipt['audit']['unchanged_authored_files'],
            'validation':receipt['validation']}
        evidence[receipt_path.relative_to(root).as_posix()]=sha(receipt_path)
    need(clients['windows']['directory_contract']==clients['linux']['directory_contract'],'Cross-platform directory contract mismatch')
    need(clients['windows']['scoreboard_contract']==clients['linux']['scoreboard_contract'],'Cross-platform Scoreboard contract mismatch')
    bundle=root/'api'
    receipt=json.loads((bundle/'BUNDLE-RECEIPT.json').read_text())
    need(not receipt['published'],'API unexpectedly published')
    for name,digest in receipt['files'].items():need(sha(bundle/name)==digest,'API bundle file changed: '+name)
    for name in ('PRIVATE-API/worker-single-file.mjs','ONLINE-WORKER/directory.mjs'):
        need((bundle/name).read_bytes()==(a.tested_api/name).read_bytes(),'Delivered API differs from tested candidate: '+name)
    api_archive=root/'DKR-R-Public-Lobbies-API-Bundle.zip'
    with zipfile.ZipFile(api_archive) as archive:
        need(archive.testzip() is None,'API ZIP CRC failure')
        expected={p.relative_to(bundle).as_posix():p for p in bundle.rglob('*') if p.is_file()}
        need(set(archive.namelist())==set(expected),'API ZIP inventory mismatch')
        for name,path in expected.items():need(archive.read(name)==path.read_bytes(),'API ZIP entry differs: '+name)
    artifacts[api_archive.name]={'name':api_archive.name,'sha256':sha(api_archive),'bytes':api_archive.stat().st_size}
    audit=root/'audit'
    audit.mkdir(exist_ok=True)
    for name,expected_checks in (('api-delivery-default.log',83),('api-delivery-running.log',89),('api-delivery-load.log',3092)):
        source=a.evidence/name
        reports=[json.loads(line) for line in source.read_text().splitlines() if line.startswith('{')]
        need(reports[-1]['checks']==expected_checks and reports[-1]['liveDeployment'] is False,'Backend checks incomplete: '+name)
        target=audit/name;shutil.copy2(source,target)
        evidence[target.relative_to(root).as_posix()]=sha(target)
    regression=a.evidence/'library-regression.log'
    need('PASS: 21 Cloudflare D1 runtime, full Worker entry point and FTP picker checks.' in regression.read_text(),'Library regression incomplete')
    shutil.copy2(regression,audit/regression.name)
    evidence[(audit/regression.name).relative_to(root).as_posix()]=sha(audit/regression.name)
    for name in ('QA.md','DEPLOYMENT.md','PLAYTEST.txt','IMPLEMENTATION.txt'):
        shutil.copy2(AUTH/name,root/name);evidence[name]=sha(root/name)
    evidence['api/BUNDLE-RECEIPT.json']=sha(bundle/'BUNDLE-RECEIPT.json')
    manifest={'schema':1,'version':VERSION,'published':False,'live_deployment_performed':False,
        'native_execution_performed':False,'clients':clients,'artifacts':artifacts,'evidence':evidence,
        'api_identical_to_tested_candidate':True,'backend_checks':{'default':83,'running_spectators':89,'lease_and_load':3092,'library_regression':21},
        'default_flags':{'ONLINE_ENABLED':False,'RUNNING_SPECTATORS_ENABLED':False},
        'owner_acceptance':'Real Discord, native TLS/keyring/controller UI and remote multiplayer playtests remain; see PLAYTEST.txt.'}
    (root/'DELIVERY.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (root/'SHA256SUMS.txt').write_text(''.join(row['sha256']+'  '+name+'\n' for name,row in artifacts.items()))
    (root/'README.md').write_text('''# DKR-R public lobbies playtest 1

This is an unpublished owner-deployed pilot, built through the additive
`patches/online-directory` Patch Pipeline layer over Spectators Playtest 13.
The launcher keeps its current design and retained online repairs. It adds
Public/Friends browsing, unique signed-in accounts online, shared Discord sign-in
and Private/Public hosting. Existing FTP uploads and the website frontend stay
as they are. The service only coordinates presence and admission; the existing
peer connection and rollback workflow continue to run the game.

## Deliverables

- [Owner API bundle](DKR-R-Public-Lobbies-API-Bundle.zip)
- [Windows x64 ZIP](windows/DKR-R-1.0.9-public-lobbies-playtest.1-Windows-x64.zip)
- [Linux x86_64 AppImage](linux/DKR-R-1.0.9-public-lobbies-playtest.1-Linux-x86_64.AppImage)
- [Deployment and rollback instructions](DEPLOYMENT.md)
- [Completed checks and limitations](QA.md)
- [Owner playtest checklist](PLAYTEST.txt)
- [Implementation details](IMPLEMENTATION.txt)
- [Delivery manifest](DELIVERY.json) and [archive hashes](SHA256SUMS.txt)

Deploy the private Cloudflare worker and API with both flags disabled first.
Preserve the current API configuration, secrets, D1 bindings and routes. Compare
any newer live API changes against the pinned local r8 baseline before replacing
its source. No website frontend or FTP upload is needed for this feature. Enable
the normal directory for a small pilot after the library regression checks.
Running spectator entry has its own switch and stays disabled in production
until its separate staging playtest passes.

Both native clients compiled and linked. Static audits retained the accepted
Scoreboard contract and all 2,304 captured authored files. Local backend checks
include real lease expiry, 1,000 sessions, 100 rooms and last-seat contention.
No native game or AppImage was run. Real Discord, controllers and remote
multiplayer acceptance remains the owner's checklist before wider distribution.

The `audit/preliminary-packages` folder contains superseded local build evidence;
only the three artifacts listed above and in DELIVERY.json are the final delivery.
Nothing was deployed live, signed for automatic updating, or uploaded to FTP.
''',encoding='utf-8')
    print(json.dumps({'version':VERSION,'artifacts':artifacts,'clients':clients,'published':False},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('output','evidence','tested-api'):p.add_argument('--'+name,type=Path,required=True)
    main(p.parse_args())
