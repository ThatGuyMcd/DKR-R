"""Stage the approved lobby restoration; checked-out runtime sources stay untouched."""
from pathlib import Path
import argparse,hashlib,json,re,sys
import transforms
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
INC=re.compile(r'^#include "([^"\n]+)"',re.M)
def sha(data):return hashlib.sha256(data).hexdigest()
def write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def main(a):
 stage=a.output.resolve()
 if stage.is_relative_to(ROOT):raise ValueError('Lobby output must stay outside the checkout')
 rows=json.loads(a.inputs.read_text());swaps=[]
 if {Path(r['path']).name for r in rows}!={'runtime_ui.cpp','direct_session.cpp'} or len(rows)!=2:raise ValueError('Unexpected lobby source inputs')
 party=stage.parent/'spectators/party_link.hpp'
 if not party.is_file():raise ValueError('The retained spectator adapter is missing')
 for p in (AUTH/'native').iterdir():
  data=p.read_bytes()
  if p.name=='lobby_adapter.hpp':data=transforms.once(data.decode(),'#include "party_link.hpp"','#include "'+party.as_posix()+'"').encode()
  write(stage/p.name,data)
 includes=[]
 for row in rows:
  source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
  if source.name=='runtime_ui.cpp':
   candidates=[m for m in INC.finditer(text) if Path(m[1]).name=='runtime_online_ui.inl']
   if len(candidates)!=1:raise ValueError('Cannot identify the final online UI include')
   origin=Path(candidates[0][1]);before=origin.read_bytes();after=transforms.online(before.decode().replace('\r\n','\n'),stage)
   write(stage/'runtime_online_ui.inl',after.encode())
   includes.append({'path':str(origin),'new':str(stage/'runtime_online_ui.inl'),'input_sha256':sha(before),'output_sha256':sha(after.encode())})
   text=text[:candidates[0].start()]+'#include "'+(stage/'runtime_online_ui.inl').as_posix()+'"'+text[candidates[0].end():]
   text='#include "'+(stage/'lobby_adapter.hpp').as_posix()+'"\n'+text
  else:text=transforms.direct(text,stage)
  # Rebase only adjacent headers. Staged RT64/compiler include priority is kept.
  def adjacent(match):
   p=source.parent/match[1]
   return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else match[0]
  text=INC.sub(adjacent,text);dest=stage/source.name;write(dest,text.encode())
  swaps.append({'target':row['target'],'path':str(source),'new':str(dest),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
 write(stage/'swaps.json',(json.dumps(swaps,indent=2)+'\n').encode());write(stage/'includes.json',(json.dumps(includes,indent=2)+'\n').encode())
 print('Original lobby shell restored over current spectator identities/actions; no target execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
