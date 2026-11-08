"""Stage PRs 66/68/69 after the accepted lobby pipeline; no target execution."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys
sys.dont_write_bytecode=True
import transforms
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
INC=re.compile(r'^#include "([^"\n]+)"',re.M)

def canonical(data):return data.replace(b'\r\n',b'\n')
def sha(data):return hashlib.sha256(data).hexdigest()
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args])
def write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def upstream():
 pins=json.loads((AUTH/'pins.json').read_text())
 for commit in pins['prs'].values():
  subprocess.run(['git','-C',str(ROOT),'merge-base','--is-ancestor',commit,'HEAD'],check=True)
 result={}
 for name,expected in pins['sources'].items():
  path='runtime-recomp/src/game/'+name
  data=canonical((ROOT/path).read_bytes())
  if sha(data)!=expected or data!=canonical(git('show',pins['upstream']+':'+path)):
   raise ValueError('Reviewed upstream source changed: '+path)
  result[name]=data.decode()
 return pins,result
def rebase(text,source):
 def one(m):
  p=source.parent/m[1]
  return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
 return INC.sub(one,text)

def compose(rows,stage,up):
 expected={('DKRCustomTracksCore','custom_tracks.cpp'),('DKRPortGame','runtime_ui.cpp'),('DKRPortGame','runtime_code_mods.cpp')}
 if len(rows)!=3 or {(r['target'],Path(r['path']).name) for r in rows}!=expected:raise ValueError('Unexpected sync source replacements')
 swaps=[];includes=[];outputs={}
 def include(source,dest,text):
  raw=source.read_bytes();outputs[dest]=text.encode()
  includes.append({'path':str(source),'new':str(dest),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
 for row in rows:
  source=Path(row['path']);raw=source.read_bytes();text=canonical(raw).decode();name=source.name
  if name=='custom_tracks.cpp':text=transforms.tracks(text,up[name])
  elif name=='runtime_ui.cpp':
   original=Path(row['graphics']);header=Path(row['preset_header'])
   include(original,stage/'graphics.inl',transforms.graphics(original.read_text(),up['runtime_graphics_ui.inl']))
   include(header,stage/'mobile_graphics_preset.hpp',transforms.preset_header(header.read_text(),up['mobile_graphics_preset.hpp']))
   text=transforms.once(text,'#include "all_detail_graphics.inl"','#include "'+(stage/'graphics.inl').as_posix()+'"')
   text=transforms.once(text,'#include "mobile_graphics_preset.hpp"','#include "'+(stage/'mobile_graphics_preset.hpp').as_posix()+'"')
  else:
   header=Path(row['dispatch_header']);dest=stage/'magic_code_dispatch.h'
   include(header,dest,transforms.dispatch_header(header.read_text(),up['mods/magic_code_dispatch.h']))
   text=transforms.code_mods(text,up[name],dest)
  text=rebase(text,source);dest=stage/name;outputs[dest]=text.encode()
  swaps.append({'target':row['target'],'path':str(source),'new':str(dest),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
 return swaps,includes,outputs
def main(a):
 stage=a.output.resolve()
 if stage.is_relative_to(ROOT):raise ValueError('Sync output must stay outside the checkout')
 pins,up=upstream();rows=json.loads(a.inputs.read_text());swaps,includes,outputs=compose(rows,stage,up)
 for path,data in outputs.items():write(path,data)
 for leaf,value in [('swaps.json',swaps),('includes.json',includes),('upstream.json',pins)]:write(stage/leaf,(json.dumps(value,indent=2)+'\n').encode())
 print('Reviewed upstream import/preset/TLS regions staged; existing game, guest, renderer and lobby bodies preserved. No target execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
