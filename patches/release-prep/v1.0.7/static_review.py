"""Read-only release source/link/privacy inspection, never target/test execution."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]
PRIVATE=re.compile(rb'gh[pousr]_[A-Za-z0-9]{30,}|BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY|AKIA[0-9A-Z]{16}')
EXCLUDED=('extern/','runtime-recomp/RecompiledFuncs/','runtime-recomp/RecompiledPatches/')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    names=subprocess.check_output(['git','-C',str(ROOT),'ls-files','--cached','--others','--exclude-standard','-z']).decode().split('\0')
    records={};issues=[];links=[]
    for name in sorted(set(names)):
        path=ROOT/name
        if not name or name.startswith(EXCLUDED) or not path.is_file() or path.is_symlink():continue
        data=path.read_bytes();records[name]=hashlib.sha256(data).hexdigest()
        if path.suffix.lower() in ('.z64','.v64','.n64','.eep','.mpk','.o2r','.otr','.pdb','.key','.pem','.jks','.keystore','.dmp','.pyc') or data[:4].hex() in ('80371240','37804012','40123780'):
            issues.append({'path':name,'issue':'Private/game/build data in source set'})
        if PRIVATE.search(data):issues.append({'path':name,'issue':'Potential credential; contents intentionally not printed'})
        if path.suffix=='.md' and (name=='README.md' or name.startswith(('docs/','packaging/','patches/'))):
            text=data.decode('utf-8-sig')
            text=re.sub(r'```.*?```','',text,flags=re.S)
            for target in re.findall(r'(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+[^)]*)?\)',text):
                if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:',target) or target.startswith(('#','/')):continue
                file=target.split('#',1)[0]
                if not file or any(c in file for c in '<>{}'):continue
                from urllib.parse import unquote
                if not (path.parent/unquote(file)).exists():links.append({'path':name,'target':target})
    report={'schema':1,'execution':'Read-only source/hash/privacy/link inspection. No game, tests, mod or benchmark execution.',
        'files':records,'issues':issues,'missing_relative_links':links}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    print('Inspected',len(records),'authored/source files;',len(issues),'privacy/asset issues;',len(links),'missing relative links')
    if issues:raise ValueError('Review private/asset issues before committing')

if __name__=='__main__':main()
