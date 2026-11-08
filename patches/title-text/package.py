"""Package the title text repair together with every accepted beta 10 fix."""
from pathlib import Path
import argparse
import importlib.util
import json
import sys
sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--title-stage', type=Path, required=True)
    parser.add_argument('--presentation-stage', type=Path, required=True)
    parser.add_argument('--water-stage', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    title = load('dkr_title_text', AUTH / 'pipeline.py')
    title.verify(args.title_stage)
    viewport = load('dkr_viewport_flow', ROOT / 'patches/viewport-flow/pipeline.py')
    viewport.verify(args.presentation_stage)
    water = load('dkr_vanilla_flow', ROOT / 'patches/vanilla-water/pipeline.py')
    water.verify(args.water_stage)
    package = load('dkr_beta10_package', ROOT / 'patches/shared-saves/package.py')
    original_features = package.inspect_features
    def features(data):
        original_features(data)
        for marker in (b'native triangle UV history and fractional scroll phase',
            b'manual-faces=%llu held-faces=%llu',
            b'full-frame picture; copyright text retained; logo proportions preserved',
            b'full-width travel; unclipped signed glyphs; original proportions and colour trails'):
            if marker not in data:
                raise ValueError('Missing accepted presentation repair: ' + repr(marker))
    package.inspect_features = features
    original_notes = package.notes
    def notes(root, platform):
        original_notes(root, platform)
        extra = '''
TITLE CHARACTER NAMES / FULL-WIDTH UPDATE
The names and coloured trails in the opening title cinematic now travel across
the full viewport in Modern + Fit to window. Letter widths and heights stay
unchanged. Recover the original atlas coordinates at the former left edge,
then clip at the actual window edges rather than the old 4:3 rectangle.
Accurate and original-aspect framing retain the original presentation.

Includes the accepted native per-triangle waterfall/river UV interpolation and
border-free spinning-logo intro, with copyright text retained. All other beta
10 save, Mod Store, SDK and launcher changes remain included.

Version remains v1.0.7 beta 10. Compilation and static package inspection only.
No application, replay or gameplay tests were run. Visual approval is yours.
'''
        (root / 'RELEASE-NOTES.txt').write_text((root / 'RELEASE-NOTES.txt').read_text() + extra)
        (root / 'FULL-WIDTH-TITLE-TEXT.txt').write_text(extra.lstrip())
    package.notes = notes
    sys.argv = [sys.argv[0]] + remaining
    package.main()
    receipt_path = Path(remaining[remaining.index('--output') + 1]) / 'BUILD-RECEIPT.json'
    receipt = json.loads(receipt_path.read_text())
    receipt['title_text'] = json.loads((args.title_stage / 'title-text-receipt.json').read_text())
    receipt['viewport_flow'] = json.loads((args.presentation_stage / 'viewport-flow-receipt.json').read_text())
    receipt['water_animation'] = json.loads((args.water_stage / 'water-animation-receipt.json').read_text())
    receipt['presentation_packager_sha256'] = package.sha(Path(__file__))
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
