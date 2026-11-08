"""Package the composed water fix using the accepted beta 10 packager."""
from pathlib import Path
import argparse
import importlib.util
import json
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def main():
    p=argparse.ArgumentParser(add_help=False);p.add_argument('--water-stage',type=Path,required=True)
    a,remaining=p.parse_known_args()
    pipeline=load('dkr_water_animation',AUTH/'pipeline.py');pipeline.verify(a.water_stage)
    package=load('dkr_beta10_package',ROOT/'patches/shared-saves/package.py')
    original_features=package.inspect_features
    def features(data):
        original_features(data)
        if b'independent vanilla flow interpolation; optics remain optional' not in data:raise ValueError('Water animation fix is missing from compiled image')
    package.inspect_features=features
    original_notes=package.notes
    def notes(root,platform):
        original_notes(root,platform)
        extra='\nVANILLA RIVERS AND WATERFALLS\nFlowing-water texture interpolation no longer depends on enhanced water being\nenabled. Water Off keeps the original appearance and does not enable rays,\nwakes or lava glow. Modern presentation is required for interpolated frames.\nThe development-save correction is included. No gameplay testing was run.\n'
        (root/'RELEASE-NOTES.txt').write_text((root/'RELEASE-NOTES.txt').read_text()+extra)
        (root/'WATER-ANIMATION-BETA10.txt').write_text(extra.lstrip())
    package.notes=notes
    sys.argv=[sys.argv[0]]+remaining
    package.main()
    at=remaining.index('--output');out=Path(remaining[at+1])
    path=out/'BUILD-RECEIPT.json';receipt=json.loads(path.read_text())
    receipt['water_animation']=json.loads((a.water_stage/'water-animation-receipt.json').read_text())
    receipt['water_packager_sha256']=package.sha(Path(__file__))
    path.write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':main()
