"""Stable release metadata and builds through the accepted Patch Pipeline."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, subprocess, sys, zipfile
sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[2]
VERSION = (ROOT/'VERSION').read_text().strip()
EXCLUDED = ('.wrangler/', 'extern/', 'runtime-recomp/RecompiledFuncs/',
            'runtime-recomp/RecompiledPatches/', 'patches/lobby-discovery-plan-20261023/',
            'patches/lobby-discovery-plan-20261024/')

def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f, 'sha256').hexdigest()

def load(name, path):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def files():
    raw=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=ROOT)
    return sorted({n.decode() for n in raw.split(b'\0') if n and not n.decode().startswith(EXCLUDED)
                   and (ROOT/n.decode()).is_file()})

def snapshot(out):
    if out.exists():raise ValueError('Choose a fresh source snapshot directory')
    out.mkdir(parents=True);rows=[]
    with zipfile.ZipFile(out/'AUTHORED-SOURCE-BEFORE.zip','x',zipfile.ZIP_DEFLATED) as z:
        for name in files():
            p=ROOT/name
            if p.is_symlink():raise ValueError('Unexpected source link: '+name)
            if p.suffix.lower() in {'.pem','.key','.p12','.pfx','.jks','.keystore','.z64','.v64','.n64','.eep','.mpk'}:
                raise ValueError('Private material in source set: '+name)
            z.write(p,name);rows.append({'path':name,'sha256':sha(p),'bytes':p.stat().st_size})
    with zipfile.ZipFile(out/'AUTHORED-SOURCE-BEFORE.zip') as z:
        if z.testzip():raise ValueError('Source snapshot CRC failed')
        for r in rows:
            if hashlib.sha256(z.read(r['path'])).hexdigest()!=r['sha256']:raise ValueError('Snapshot hash differs')
    (out/'SOURCE-FILES.json').write_text(json.dumps(rows,indent=2)+'\n')
    for name,args in {'GIT-HEAD.txt':['rev-parse','HEAD'],'GIT-STATUS.txt':['status','--short'],
                      'SUBMODULES.txt':['submodule','status','--recursive']}.items():
        (out/name).write_bytes(subprocess.check_output(['git',*args],cwd=ROOT))
    print('Recoverable source snapshot verified:',len(rows),'files')

def desired(baseline):
    content=load('release111_documents',AUTH/'content.py')
    with zipfile.ZipFile(baseline/'AUTHORED-SOURCE-BEFORE.zip') as z:
        if z.read('VERSION').decode().strip()!='1.0.10':
            raise ValueError('Metadata preparation requires the original v1.0.10 baseline. A committed v1.0.11 checkout should configure/build without reapplying metadata.')
        return content.build(z)

def apply(baseline,evidence):
    texts=desired(baseline);before=json.loads((baseline/'SOURCE-FILES.json').read_text())
    expected={r['path']:r['sha256'] for r in before}
    ledger=evidence/'METADATA-CHANGES.json'
    prior=json.loads(ledger.read_text()) if ledger.exists() else {}
    for name,text in texts.items():
        if name.startswith(EXCLUDED) or not (name in {'VERSION','.gitignore','packaging/linux/dkr-port.appdata.xml'}
            or Path(name).suffix in {'.md','.txt'}):raise ValueError('Outside release document boundary: '+name)
        p=ROOT/name;old=prior.get(name,{}).get('after',expected.get(name))
        if p.exists() and sha(p)!=old:raise ValueError('Source changed outside release pipeline: '+name)
    changes={}
    for name,text in texts.items():
        data=text.replace('\r\n','\n').encode();p=ROOT/name
        if expected.get(name)==hashlib.sha256(data).hexdigest():continue
        p.parent.mkdir(parents=True,exist_ok=True)
        if not p.is_file() or p.read_bytes()!=data:p.write_bytes(data)
        changes[name]={'before':expected.get(name),'after':sha(p)}
    evidence.mkdir(parents=True,exist_ok=True)
    ledger.write_text(json.dumps(changes,indent=2)+'\n');verify(baseline,evidence)

def verify(baseline,evidence):
    changes=json.loads((evidence/'METADATA-CHANGES.json').read_text())
    expected=desired(baseline)
    for name,text in expected.items():
        if (ROOT/name).read_bytes()!=text.replace('\r\n','\n').encode():raise ValueError('Stale release document: '+name)
    rows=json.loads((baseline/'SOURCE-FILES.json').read_text());unchanged=0
    for r in rows:
        name=r['path']
        # This layer is the explicit authoring boundary; it contains no game code.
        if name.startswith('patches/release-prep/v1.0.11/'):continue
        check=changes[name]['after'] if name in changes else r['sha256']
        if sha(ROOT/name)!=check:raise ValueError('Undeclared pre-existing source change: '+name)
        if name not in changes:unchanged+=1
    after=[{'path':n,'sha256':sha(ROOT/n),'bytes':(ROOT/n).stat().st_size} for n in files()]
    (evidence/'SOURCE-FILES-AFTER-METADATA.json').write_text(json.dumps(after,indent=2)+'\n')
    (evidence/'SOURCE-VERIFICATION.json').write_text(json.dumps({'unchanged_preexisting_files':unchanged,
        'declared_document_metadata_changes':len(changes),'protected_source_edits':False},indent=2)+'\n')
    print('Release documents verified; unchanged pre-existing files:',unchanged)

def configure(a):
    base=load('release111_configure',ROOT/'patches/release-prep/v1.0.7/release.py')
    base.AUTH=AUTH;base.VERSION=VERSION
    base.configure(a.build,a.log,a.cmake,a.capsule)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('snapshot','apply','verify','configure'))
    for n in ('output','baseline','evidence','build','capsule','log'):p.add_argument('--'+n,type=Path)
    p.add_argument('--cmake',default='cmake');a=p.parse_args()
    if a.action=='snapshot':snapshot(a.output)
    elif a.action=='configure':configure(a)
    else:globals()[a.action](a.baseline,a.evidence)
