"""Compose Store vehicle support into isolated first-party build copies."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
import transforms
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
INC=re.compile(r'^#include "([^"\n]+)"',re.M)
def sha(raw):return hashlib.sha256(raw).hexdigest()
def write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def main(a):
 out=a.output.resolve()
 if out.is_relative_to(ROOT):raise ValueError('Vehicle Store stage must stay outside the checkout')
 rows=json.loads(a.inputs.read_text())
 install_source=next(Path(row['path']) for row in rows if Path(row['path']).name=='mod_store_install.cpp')
 for p in (AUTH/'native').iterdir():
  if p.is_file():
   data=p.read_bytes()
   if p.name=='store_vehicle_adapter.hpp':
    data=transforms.once(data.decode(),'#include "mod_store_install.hpp"',
        '#include "'+install_source.with_suffix('.hpp').as_posix()+'"').encode()
   write(out/p.name,data)
 includes=[];cache={}
 def resolve(name,origin,dirs):
  p=Path(name)
  if p.is_absolute():return p if p.is_file() else None
  if name=='vehicle_store_ui.inl':return out/name
  for root in [origin.parent]+dirs:
   p=root/name
   if p.is_file():return p.resolve()
  return None
 def compose(text,origin,dirs):
  def replace(m):
   p=resolve(m[1],origin,dirs)
   if not p or p.suffix!='.inl':return m[0]
   if p==out/'vehicle_store_ui.inl':return '#include "'+p.as_posix()+'"'
   if p in cache:return '#include "'+cache[p].as_posix()+'"' if cache[p]!=p else m[0]
   raw=p.read_bytes();before=raw.decode().replace('\r\n','\n')
   after=compose(transforms.inl(p.name,before),p,dirs)
   if after==before:cache[p]=p;return m[0]
   dest=out/'includes'/sha(p.name.encode()+raw)[:12]/p.name;cache[p]=dest
   def adjacent(child):
    q=resolve(child[1],p,[])
    return '#include "'+q.as_posix()+'"' if q else child[0]
   after=INC.sub(adjacent,after);write(dest,after.encode())
   includes.append({'path':str(p),'new':str(dest),'input_sha256':sha(raw),'output_sha256':sha(dest.read_bytes())})
   return '#include "'+dest.as_posix()+'"'
  return INC.sub(replace,text)
 swaps=[]
 functions={'mod_store.cpp':transforms.service,'mod_store_install.cpp':transforms.installer,'runtime_ui.cpp':transforms.ui}
 for row in rows:
  origin=Path(row['path'])
  if origin.name not in functions:continue
  raw=origin.read_bytes();text=functions[origin.name](raw.decode().replace('\r\n','\n'))
  dirs=[Path(p) for p in row['includes'] if '$<' not in p and Path(p).is_dir()]
  if origin.name=='runtime_ui.cpp':text=compose(text,origin,dirs)
  else:text='#include "'+(out/'store_vehicle_adapter.hpp').as_posix()+'"\n'+text
  def adjacent(m):
   p=resolve(m[1],origin,[])
   return '#include "'+p.as_posix()+'"' if p else m[0]
  text=INC.sub(adjacent,text)
  dest=out/origin.name;write(dest,text.encode())
  swaps.append({'target':row['target'],'path':str(origin),'new':str(dest),'input_sha256':sha(raw),'output_sha256':sha(dest.read_bytes())})
 if len(swaps)!=3:raise ValueError('Expected final Store service, installer and launcher sources')
 write(out/'swaps.json',(json.dumps(swaps,indent=2)+'\n').encode())
 write(out/'includes.json',(json.dumps(includes,indent=2)+'\n').encode())
 print('Vehicle Store download, importer, receipts and UI staged; no game/test execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
