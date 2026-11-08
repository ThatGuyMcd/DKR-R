"""Package first-party flow/intro repairs without executing DKR-R."""
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
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    p=argparse.ArgumentParser(add_help=False)
    p.add_argument('--presentation-stage',type=Path,required=True)
    p.add_argument('--water-stage',type=Path,required=True)
    a,remaining=p.parse_known_args()
    pipeline=load('dkr_viewport_flow',AUTH/'pipeline.py')
    pipeline.verify(a.presentation_stage)
    water=load('dkr_vanilla_flow',ROOT/'patches/vanilla-water/pipeline.py')
    water.verify(a.water_stage)
    package=load('dkr_beta10_package',ROOT/'patches/shared-saves/package.py')
    original_features=package.inspect_features
    def features(data):
        original_features(data)
        for marker in (b'native triangle UV history and fractional scroll phase',b'manual-faces=%llu held-faces=%llu',
            b'full-frame picture; copyright text retained; logo proportions preserved'):
            if marker not in data:raise ValueError('Missing presentation repair: '+repr(marker))
    package.inspect_features=features
    original_notes=package.notes
    def notes(root,platform):
        original_notes(root,platform)
        extra='''
FULLSCREEN WATER / INTRO UPDATE
Original flowing water now carries native per-triangle UV history directly
through the existing GPU interpolation channel, independently of the generic
transform/UV matcher. Scene/time boundaries clear history safely. The authored
texture-wrap units and scroll objects' fractional ST remainder are retained.
A lightweight cadence log reports real presentation, manual UV and held-source
counts to distinguish animation-source holds from interpolation loss.
Water Off does
not enable optical effects, rays or lava lighting. Modern mode is required.

Modern + Fit to window now expands the rotating-logo intro to full width and
height, removing all four borders. Copyright text is kept in its original
position over the picture. Logos and text are not stretched. Accurate/original
aspect retains the original framing.

Version remains v1.0.7 beta 10. All earlier beta 10 fixes are included.
Compilation and static inspection only. Gameplay/visual approval is yours.
Please compare fullscreen/windowed waterfalls first, then check both logos.
'''
        (root/'RELEASE-NOTES.txt').write_text((root/'RELEASE-NOTES.txt').read_text()+extra)
        (root/'FULLSCREEN-WATER-AND-INTRO.txt').write_text(extra.lstrip())
    package.notes=notes
    sys.argv=[sys.argv[0]]+remaining
    package.main()
    at=remaining.index('--output')
    path=Path(remaining[at+1])/'BUILD-RECEIPT.json'
    receipt=json.loads(path.read_text())
    receipt['viewport_flow']=json.loads((a.presentation_stage/'viewport-flow-receipt.json').read_text())
    receipt['water_animation']=json.loads((a.water_stage/'water-animation-receipt.json').read_text())
    receipt['presentation_packager_sha256']=package.sha(Path(__file__))
    path.write_text(json.dumps(receipt,indent=2)+'\n')

if __name__=='__main__':main()
