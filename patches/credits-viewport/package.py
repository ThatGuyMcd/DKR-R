"""Package credits and all accepted title/water beta 10 repairs, without running DKR-R."""
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
    parser.add_argument('--credits-stage', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    pipeline = load('dkr_credits', AUTH / 'pipeline.py')
    pipeline.verify(args.credits_stage)
    title = load('dkr_title_package', ROOT / 'patches/title-text/package.py')
    original_load = title.load
    def nested_load(name, path):
        module = original_load(name, path)
        if name == 'dkr_beta10_package':
            original_features = module.inspect_features
            def features(data):
                original_features(data)
                for marker in (b'full-frame cinematic; borders removed; original scene proportions',
                    b'full-width slides; original font proportions; signed glyphs and spaces',
                    b'signed full-window rectangle and scissor; original opacity',
                    b'lower backdrop widened along camera lateral basis; horizon unchanged'):
                    if marker not in data:
                        raise ValueError('Missing credits repair: ' + repr(marker))
            module.inspect_features = features
            original_notes = module.notes
            def notes(root, platform):
                original_notes(root, platform)
                extra = '''
FULLSCREEN CREDITS UPDATE
Modern + Fit to window expands the credits scene and its fades to the full
viewport. The top, bottom and side borders are removed without stretching the
scene. Moving credit headings/names use the full window width, preserving the
original two fonts, letter sizes, colours, spacing and centred dwell positions.
The music, credits timing, ordering, skip controls and ending text are unchanged.
Accurate and explicitly original-aspect presentation retain their old framing.

Follow-up: credits fades now use a signed full-window scissor as well as a
full-window rectangle. Ordinary full-screen inclusive scissor resets are
normalised. The single-camera lower-sky mesh is widened along its own lateral
basis in decoder-local vertices, preserving horizon height, depth and colour.

Includes the owner-accepted corrected title names, native flowing-water
interpolation, border-free logo intro and all previous beta 10 changes.
Compilation and static package inspection only; visual approval is yours.
'''
                (root / 'RELEASE-NOTES.txt').write_text((root / 'RELEASE-NOTES.txt').read_text() + extra)
                (root / 'FULLSCREEN-CREDITS.txt').write_text(extra.lstrip())
            module.notes = notes
        return module
    title.load = nested_load
    sys.argv = [sys.argv[0]] + remaining
    title.main()
    path = Path(remaining[remaining.index('--output') + 1]) / 'BUILD-RECEIPT.json'
    receipt = json.loads(path.read_text())
    receipt['credits_viewport'] = json.loads((args.credits_stage / 'credits-viewport-receipt.json').read_text())
    receipt['credits_packager_sha256'] = pipeline.sha(Path(__file__))
    path.write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
