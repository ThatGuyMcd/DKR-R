"""Stage diagnostic-only sources outside the checkout; never execute targets."""
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
def rebase(text,source):
 def one(m):
  p=source.parent/m[1]
  return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
 return INC.sub(one,text)
def compose(rows,stage):
 if len(rows)!=2 or {(r['target'],Path(r['path']).name) for r in rows}!={('DKRPortGame','runtime_ui.cpp'),('DKRNetplayCore','direct_session.cpp')}:raise ValueError('Unexpected diagnostic source inputs')
 outputs={p.name:p.read_bytes() for p in (AUTH/'native').iterdir() if p.is_file()};swaps=[];includes=[]
 # The report needs the existing dialog action types, defined below the
 # connection panel in the retained UI. Stage its definition at that point.
 ui=outputs['connection_ui.inl'].decode();a=ui.index('void DrawOlRollbackResults(');b=ui.index('void DrawOlConnection(',a)
 outputs['report_ui.inl']=ui[a:b].encode()
 outputs['connection_ui.inl']=(ui[:a]+'void DrawOlRollbackResults(const OnlineFrame&);\n'+ui[b:]).encode()
 def save_include(source,name,after):
  outputs[name]=after.encode();includes.append({'path':str(source),'new':str(stage/name),'input_sha256':sha(source.read_bytes()),'output_sha256':sha(after.encode())})
 for row in rows:
  source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
  if source.name=='direct_session.cpp':text=transforms.direct(text,stage)
  else:
   text=transforms.include(stage,'rollback_check.hpp')+text
   matches=[m for m in INC.finditer(text) if Path(m[1]).name=='runtime_online_ui.inl']
   if len(matches)!=1:raise ValueError('Ambiguous online UI include')
   m=matches[0];origin=Path(m[1]);online=origin.read_text();online=transforms.online(online,stage)
   for name,change in [('lobby_context.inl',transforms.context),('lobby_controls.inl',transforms.controls)]:
    picks=[p for p in INC.finditer(online) if Path(p[1]).name==name]
    if len(picks)!=1:raise ValueError('Ambiguous retained lobby include: '+name)
    pick=picks[0];path=Path(pick[1]);save_include(path,name,change(path.read_text()))
    online=online[:pick.start()]+transforms.include(stage,name).rstrip()+online[pick.end():]
   save_include(origin,'runtime_online_ui.inl',online)
   text=text[:m.start()]+transforms.include(stage,'runtime_online_ui.inl').rstrip()+text[m.end():]
  text=rebase(text,source);outputs[source.name]=text.encode()
  swaps.append({'target':row['target'],'path':str(source),'new':str(stage/source.name),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
 return swaps,includes,outputs
def main(a):
 stage=a.output.resolve()
 if stage.is_relative_to(ROOT):raise ValueError('Output must stay outside the checkout')
 rows=json.loads(a.inputs.read_text());swaps,includes,outputs=compose(rows,stage)
 for name,data in outputs.items():write(stage/name,data)
 for name,value in [('swaps.json',swaps),('includes.json',includes)]:write(stage/name,(json.dumps(value,indent=2)+'\n').encode())
 print('Rollback-only connection diagnostics staged. Game/guest/renderer and rollback algorithms unchanged. No target execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
