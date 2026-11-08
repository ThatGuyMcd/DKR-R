"""Final surgical lobby link layer; external staging only, no execution."""
from pathlib import Path
import argparse,hashlib,json,re,sys
sys.dont_write_bytecode=True
import transforms
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
INC=re.compile(r'^#include "([^"\n]+)"',re.M)
def sha(data):return hashlib.sha256(data).hexdigest()
def write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def adjacent(text,source):
 def one(m):
  p=source.parent/m[1]
  return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
 return INC.sub(one,text)
def compose(rows,stage):
 expected={('DKRPortGame','runtime_ui.cpp'),*(('DKRNetplayCore',n) for n in ('direct_session.cpp','party_link.cpp','lobby_adapter.cpp','rollback_check.cpp'))}
 if len(rows)!=5 or {(r['target'],Path(r['path']).name) for r in rows}!=expected:raise ValueError('Unexpected lobby repair source inputs')
 swaps=[];includes=[];outputs={}
 def include(source,name,text):
  outputs[name]=text.encode();includes.append({'path':str(source),'new':str(stage/name),'input_sha256':sha(source.read_bytes()),'output_sha256':sha(text.encode())})
 for row in rows:
  source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
  if source.name=='runtime_ui.cpp':
   picks=[m for m in INC.finditer(text) if Path(m[1]).name=='runtime_online_ui.inl']
   if len(picks)!=1:raise ValueError('Ambiguous final online UI')
   pick=picks[0];original=Path(pick[1]);online=original.read_text()
   contexts=[m for m in INC.finditer(online) if Path(m[1]).name=='lobby_context.inl']
   if len(contexts)!=1:raise ValueError('Ambiguous lobby context')
   context=contexts[0];origin=Path(context[1]);include(origin,'lobby_context.inl',transforms.context(origin.read_text()))
   online=online[:context.start()]+'#include "'+(stage/'lobby_context.inl').as_posix()+'"'+online[context.end():]
   include(original,'runtime_online_ui.inl',online)
   text=text[:pick.start()]+'#include "'+(stage/'runtime_online_ui.inl').as_posix()+'"'+text[pick.end():]
  else:text={'direct_session.cpp':transforms.direct,'party_link.cpp':transforms.party_link,'lobby_adapter.cpp':transforms.adapter,'rollback_check.cpp':transforms.check}[source.name](text)
  text=adjacent(text,source);outputs[source.name]=text.encode()
  swaps.append({'target':row['target'],'path':str(source),'new':str(stage/source.name),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
 return swaps,includes,outputs
def main(a):
 stage=a.output.resolve()
 if stage.is_relative_to(ROOT):raise ValueError('Repair output must be external')
 rows=json.loads(a.inputs.read_text());swaps,includes,outputs=compose(rows,stage)
 for name,data in outputs.items():write(stage/name,data)
 for name,value in [('swaps.json',swaps),('includes.json',includes)]:write(stage/name,(json.dumps(value,indent=2)+'\n').encode())
 print('Lobby slot/launch serialization, promoted nonce ownership and native registry lifetimes repaired; no target execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
