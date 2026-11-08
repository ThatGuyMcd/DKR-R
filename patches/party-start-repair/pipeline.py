"""Final first-party launch/start layer. External generated copies only."""
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
 if len(rows)!=3 or {(r['target'],Path(r['path']).name) for r in rows}!=expected:raise ValueError('Unexpected start source inputs')
 outputs={'start_gate.hpp':(AUTH/'native/start_gate.hpp').read_bytes()};swaps=[]
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
 if stage.is_relative_to(ROOT):raise ValueError('Start output must be external')
 rows=json.loads(a.inputs.read_text());swaps,outputs=compose(rows,stage)
 for name,data in outputs.items():write(stage/name,data)
 write(stage/'swaps.json',(json.dumps(swaps,indent=2)+'\n').encode())
 print('Five-second party start, acknowledgements and initial checkpoint admission staged; no target execution.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
