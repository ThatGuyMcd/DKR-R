"""Copy authored spectator policy to an isolated, compile-only build stage."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main(a):
 out=a.output.resolve()
 if out.is_relative_to(ROOT):raise ValueError('Spectator policy output must be outside the checkout')
 out.mkdir(parents=True,exist_ok=True)
 rows=[]
 for source in sorted((AUTH/'native').iterdir()):
  if not source.is_file():continue
  dest=out/'native'/source.name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(source.read_bytes())
  rows.append({'authored':source.relative_to(AUTH).as_posix(),'sha256':sha(source)})
 cmake=AUTH/'core/CMakeLists.txt';(out/'CMakeLists.txt').write_bytes(cmake.read_bytes())
 rows.append({'authored':'core/CMakeLists.txt','sha256':sha(cmake)})
 (out/'SOURCE-RECEIPT.json').write_text(json.dumps({'schema':1,'scope':'unintegrated spectator policy only','sources':rows,'gameplay_executed':False},indent=2)+'\n',encoding='utf-8')
 print('Isolated spectator policy source prepared; no executable or gameplay target:',out)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);main(p.parse_args())
