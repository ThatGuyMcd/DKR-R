"""Package HD-liquid selection over the accepted surgical beta 10 package."""
from pathlib import Path
import argparse
import importlib.util
import json
import shutil
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def main():
    p=argparse.ArgumentParser(add_help=False);p.add_argument('--liquid-stage',type=Path,required=True)
    args,remaining=p.parse_known_args()
    pipeline=load('liquid_pipeline',AUTH/'pipeline.py');pipeline.verify(args.liquid_stage)
    parent=load('liquid_performance_package',ROOT/'patches/performance-surgical/package.py')
    def instrument(loader):
        def nested(name,path):
            module=loader(name,path)
            if name=='dkr_beta10_package':
                original_features=module.inspect_features
                def features(data):
                    original_features(data)
                    for marker in (b'workload-atomic promotion',b'complete-family selection'):
                        if marker not in data:raise ValueError('Missing HD water repair: '+repr(marker))
                module.inspect_features=features
                original_notes=module.notes
                def notes(root,platform):
                    original_notes(root,platform)
                    shutil.copy2(AUTH/'OWNER-NOTES.txt',root/'HD-WATER-LOADING.txt')
                    with (root/'RELEASE-NOTES.txt').open('a') as stream:
                        stream.write('\nHD WATER LOADING FIX\nSee HD-WATER-LOADING.txt. Transparent animation families switch together only after upload.\n')
                module.notes=notes
            elif hasattr(module,'load'):module.load=instrument(module.load)
            return module
        return nested
    parent.load=instrument(parent.load)
    sys.argv=[sys.argv[0]]+remaining;parent.main()
    receipt_path=Path(remaining[remaining.index('--output')+1])/'BUILD-RECEIPT.json'
    receipt=json.loads(receipt_path.read_text())
    receipt['liquid_streaming']=json.loads((args.liquid_stage/'liquid-streaming-receipt.json').read_text())
    receipt['liquid_packager_sha256']=pipeline.sha(AUTH/'package.py')
    receipt['liquid_overlay_sha256']=pipeline.sha(AUTH/'NativeOverlay.cmake')
    receipt['acceptance']='Compilation and static package/source inspection only. Owner HD water cold-load and visual acceptance pending.'
    receipt_path.write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':main()
