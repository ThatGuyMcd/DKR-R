"""Compose only diagnostic/UI sources into isolated build outputs."""
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
def compose(rows,stage):
 expected={('DKRPortGame','runtime_ui.cpp'),('DKRNetplayCore','direct_session.cpp'),('DKRNetplayCore','rollback_check.cpp')}
 if len(rows)!=3 or {(r['target'],Path(r['path']).name) for r in rows}!=expected:raise ValueError('Unexpected scoreboard inputs')
 outputs={p.name:p.read_bytes() for p in (AUTH/'native').iterdir() if p.is_file()};swaps=[];includes=[]
 def save(source,text):
  name=source.name
  if name in outputs and outputs[name]!=text.encode():raise ValueError('Ambiguous staged include '+name)
  outputs[name]=text.encode();includes.append({'path':str(source),'new':str(stage/name),'input_sha256':sha(source.read_bytes()),'output_sha256':sha(text.encode())})
 def include(source):
  raw=source.read_text();name=source.name
  if name=='rollback_check.hpp':text=transforms.header(raw)
  elif name=='owner_context.inl':text=transforms.context(raw)
  elif name=='report_ui.inl':return str(stage/'report_ui.inl')
  elif name=='runtime_online_ui.inl':
   text=raw.replace('if (BeginOlDialog(kResults)) {','if (BeginOlScoreboard(kResults)) {')
   if text==raw:raise ValueError('Scoreboard dialog anchor changed')
   text=rewrite(text,source)
  else:return str(source)
  save(source,text);return (stage/name).as_posix()
 def rewrite(text,source):
  def one(m):
   p=Path(m[1]);p=p if p.is_absolute() else source.parent/p
   return '#include "'+include(p.resolve())+'"' if p.is_file() else m[0]
  return INC.sub(one,text)
 for row in rows:
  source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
  if source.name=='rollback_check.cpp':text=transforms.core(text)
  text=rewrite(text,source)
  if source.name=='runtime_ui.cpp':text='#include "'+(stage/'assessment.hpp').as_posix()+'"\n'+text
  outputs[source.name]=text.encode();swaps.append({**row,'new':str(stage/source.name),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
 return swaps,includes,outputs
def main(a):
 stage=a.output.resolve()
 if stage.is_relative_to(ROOT):raise ValueError('Scoreboard outputs must stay outside the checkout')
 swaps,includes,outputs=compose(json.loads(a.inputs.read_text()),stage)
 for name,data in outputs.items():write(stage/name,data)
 for name,value in [('swaps.json',swaps),('includes.json',includes)]:write(stage/name,(json.dumps(value,indent=2)+'\n').encode())
 print('Rollback Lobby Scoreboard staged; diagnostic-only v2 reports; gameplay/guest/renderer unchanged.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
