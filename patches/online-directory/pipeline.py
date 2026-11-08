"""Append directory/account/UI sources to the accepted final Patch Pipeline."""
from pathlib import Path
import argparse,hashlib,json,re,sys
sys.dont_write_bytecode=True
import transforms
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
INC=re.compile(r'^#include "([^"\n]+)"',re.M)
def sha(raw):return hashlib.sha256(raw).hexdigest()
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)
def compose(rows,stage):
    expected={('DKRPortGame','runtime_ui.cpp'),('DKRPortGame','mod_store.cpp'),('DKRNetplayCore','direct_session.cpp')}
    if len(rows)!=3 or {(r['target'],Path(r['path']).name) for r in rows}!=expected:raise ValueError('Unexpected directory source inputs')
    outputs={p.name:p.read_bytes() for p in (AUTH/'native').iterdir() if p.is_file()};swaps=[];includes=[]
    seen={}
    capsule_dirs=[]
    for row in rows:
        for match in INC.finditer(Path(row['path']).read_text(encoding='utf-8')):
            path=Path(match[1])
            if path.name=='mod_store.hpp' and path.is_absolute() and path.exists():capsule_dirs.append(path.parent)
    def rewrite(text,source):
        def one(match):
            path=Path(match[1]);path=path if path.is_absolute() else source.parent/path
            name=path.name
            if name not in ('runtime_online_ui.inl','invite_ui.inl','mod_store_ui.inl','store_account_page.inl'):return match[0]
            if not path.exists():
                path=next((directory/name for directory in capsule_dirs if (directory/name).exists()),path)
            if not path.exists():raise ValueError('Cannot resolve retained UI include '+name)
            key=str(path.resolve())
            if key in seen:return '#include "'+seen[key]+'"'
            text=path.read_text(encoding='utf-8')
            if name=='runtime_online_ui.inl':text=transforms.online_ui(text,stage)
            elif name=='invite_ui.inl':text=transforms.invite(text)
            elif name=='store_account_page.inl':text=transforms.account_ui(text)
            text=rewrite(text,path)
            if name in outputs and outputs[name]!=text.encode():raise ValueError('Ambiguous staged include '+name)
            outputs[name]=text.encode();seen[key]=(stage/name).as_posix()
            includes.append({'path':str(path),'new':str(stage/name),'input_sha256':sha(path.read_bytes()),'output_sha256':sha(text.encode())})
            return '#include "'+seen[key]+'"'
        return INC.sub(one,text)
    for row in rows:
        source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
        # Resolve the retained relative store-account include against the source
        # capsule via its unchanged parent before rewriting its include graph.
        text=rewrite(text,source)
        if source.name=='direct_session.cpp':text=transforms.direct(text,stage)
        elif source.name=='mod_store.cpp':text=transforms.store(text,stage)
        else:text=transforms.runtime_ui(text,stage)
        outputs[source.name]=text.encode();swaps.append({**row,'new':str(stage/source.name),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
    return swaps,includes,outputs
def main(a):
    stage=a.output.resolve()
    if stage.is_relative_to(ROOT):raise ValueError('Native generated outputs must stay outside the checkout')
    swaps,includes,outputs=compose(json.loads(a.inputs.read_text()),stage)
    for name,data in outputs.items():write(stage/name,data)
    for name,rows in [('swaps.json',swaps),('includes.json',includes)]:write(stage/name,(json.dumps(rows,indent=2)+'\n').encode())
    print('Online directory staged after Scoreboard; existing game/runtime repairs retained.')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
