"""Reconstruct the unchanged frozen capsule using its reviewed historical bases."""
from pathlib import Path
import argparse,importlib.util,json,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
def main(a):
 if a.output.resolve().is_relative_to(ROOT.resolve()):raise ValueError('Reconstruct into a fresh external capsule, not the checkout')
 pins,_=pipeline.upstream()
 spec=importlib.util.spec_from_file_location('sync_frozen_capsule',ROOT/'patches/release-prep/v1.0.7/capsule.py');base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
 manifest=json.loads((base.DATA/'manifest.json').read_text())
 expected={}
 for entries in manifest['roles'].values():
  for entry in entries.values():
   if 'base' in entry:
    name=entry['base'];value=entry['base_sha256']
    if name in expected and expected[name]!=value:raise ValueError('Conflicting historical base pins')
    expected[name]=value
 original=base.canonical;fallback={}
 for name,wanted in expected.items():
  if pipeline.sha(original(ROOT/name))!=wanted:
   data=pipeline.canonical(pipeline.git('show',pins['base']+':'+name))
   if pipeline.sha(data)!=wanted:raise ValueError('Historical Git blob differs from frozen manifest: '+name)
   fallback[name]=data
 def canonical(path):
  name=path.resolve().relative_to(ROOT.resolve()).as_posix()
  return fallback[name] if name in fallback else original(path)
 base.canonical=canonical
 prepared={key:getattr(a,key.lower()) for key in base.PREPARED}
 base.stage(a.output,prepared)
 print('Unchanged frozen capsule reconstructed; verified historical bases:',len(fallback))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
 for name in ('camera-kernel-v77','camera-kernel-v80','shadow-kernel-v77','shadow-kernel-v80'):p.add_argument('--'+name,type=Path,required=True)
 main(p.parse_args())
