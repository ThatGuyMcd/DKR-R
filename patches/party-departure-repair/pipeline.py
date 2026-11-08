"""Stage party-specific departure handling without modifying checked-out sources."""
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
 expected={('DKRNetplayCore','direct_session.cpp'),('DKRPortGame','experimental_runtime.cpp'),('DKRPortGame','party_runtime.cpp')}
 if len(rows)!=3 or {(r['target'],Path(r['path']).name) for r in rows}!=expected:raise ValueError('Unexpected departure inputs')
 outputs={'departure.hpp':(AUTH/'native/departure.hpp').read_bytes()};swaps=[]
 for row in rows:
  source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
  text={'direct_session.cpp':transforms.direct,'experimental_runtime.cpp':transforms.runtime,'party_runtime.cpp':transforms.party_runtime}[source.name](text)
  def adjacent(m):
   p=source.parent/m[1]
   return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
  text=INC.sub(adjacent,text);outputs[source.name]=text.encode()
  swaps.append({**row,'new':str(stage/source.name),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
 return swaps,outputs
def main(a):
 stage=a.output.resolve()
 if stage.is_relative_to(ROOT):raise ValueError('Departure outputs must be external')
 swaps,outputs=compose(json.loads(a.inputs.read_text()),stage)
 for name,data in outputs.items():write(stage/name,data)
 write(stage/'swaps.json',(json.dumps(swaps,indent=2)+'\n').encode())
 print('Acknowledged client close, unified party retirement and bounded liveness staged; no target execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
