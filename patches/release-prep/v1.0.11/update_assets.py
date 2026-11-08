"""Sign the three finished desktop platform packages using the existing trust key."""
from pathlib import Path
from types import SimpleNamespace
import argparse,importlib.util,json,re,subprocess,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2];VERSION=(ROOT/'VERSION').read_text().strip();TAG='Version'+VERSION
PLATFORMS={'windows-x64':('windows','x86_64',f'DKR-R-{VERSION}-Windows-x64.zip'),
    'linux-x86_64':('linux','x86_64',f'DKR-R-{VERSION}-Linux-x86_64.AppImage'),
    'linux-aarch64':('linux','aarch64',f'DKR-R-{VERSION}-Linux-aarch64.AppImage')}

def base():
    s=importlib.util.spec_from_file_location('release111_trusted_signing',AUTH.parent/'v1.0.9/update_assets.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);m.VERSION=VERSION;m.TAG=TAG
    m.DECK=f'DKR-R-{VERSION}-SteamDeck-x86_64.AppImage';return m

def source(commit):
    def git(*a):return subprocess.check_output(['git',*a],cwd=ROOT)
    if not re.fullmatch('[a-f0-9]{40}',commit):raise ValueError('Use the full release source commit')
    if git('show',commit+':VERSION').decode().strip()!=VERSION:raise ValueError('Wrong committed release version')
    allowed=('patches/lobby-discovery-plan-20261023/','patches/lobby-discovery-plan-20261024/')
    missing=[n.decode() for n in git('ls-files','--others','--exclude-standard','-z').split(b'\0') if n and not n.decode().startswith(allowed)]
    if missing:raise ValueError('Uncommitted release source: '+', '.join(missing[:5]))
    if git('diff','--name-only',commit,'--').strip():raise ValueError('Source differs from release commit')

def qualified(m,packages,commit):
    expected={}
    for platform,(system,arch,name) in PLATFORMS.items():
        label='arm64' if arch=='aarch64' else system
        receipt=json.loads((packages/'Evidence'/(label.upper()+'-BUILD-RECEIPT.json')).read_text())
        a=receipt['artifact']
        if receipt['version']!=VERSION or receipt['platform']!=system or receipt['arch']!=arch or a['name']!=name:
            raise ValueError('Wrong platform package receipt')
        expected[platform]=(name,a['bytes'],a['sha256'])
    m.EXPECTED=expected
    return m.manifest_value(packages,commit)

def main(a):
    m=base();m.openssl();signer=m.load_signer();packages=a.packages.resolve();output=a.output.resolve()
    if a.action=='sign':
        source(a.commit);expected=qualified(m,packages,a.commit)
        private=a.private_key.resolve(strict=True)
        if any(private.is_relative_to(p) for p in (ROOT,packages,output)):raise ValueError('Keep the private key outside source/output')
        signer.create_manifest(SimpleNamespace(private_key=str(private),verification_key=str(ROOT/'packaging/update-public-key.json'),
            version=VERSION,tag=TAG,commit=a.commit,channel='stable',output=str(output),
            artifact=[p+'='+str(packages/v[2]) for p,v in PLATFORMS.items()],minimum_os=[]))
    else:
        value=m.verify_signature(output/'DKR-R-update.json',output/'DKR-R-update.sig')
        if value.get('version')!=VERSION or value.get('tag')!=TAG:raise ValueError('Wrong signed release identity')
        expected=qualified(m,packages,value['commit'])
    manifest=output/'DKR-R-update.json';signature=output/'DKR-R-update.sig'
    value=m.verify_signature(manifest,signature)
    if value!=expected or manifest.read_bytes()!=signer.canonical(expected):raise ValueError('Signed manifest differs from finished packages')
    if a.action=='sign':m.receipt(output,manifest,signature,value)
    print('Verified trusted signature, stable source identity, three platform entries and four package hashes.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=('sign','verify'))
    for n in ('packages','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--commit');p.add_argument('--private-key',type=Path);a=p.parse_args()
    if a.action=='sign' and (not a.commit or not a.private_key):p.error('sign needs --commit and --private-key')
    main(a)
