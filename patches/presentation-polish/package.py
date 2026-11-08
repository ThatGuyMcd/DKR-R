"""Package final beta 10 presentation repairs without launching any target."""
from pathlib import Path
import argparse
import importlib.util
import json
import shutil
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
    parser.add_argument('--polish-stage', type=Path, required=True)
    parser.add_argument('--credits-stage', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    pipeline = load('dkr_presentation_polish', AUTH / 'pipeline.py')
    pipeline.verify(args.polish_stage)
    credits = load('dkr_credits_viewport', ROOT / 'patches/credits-viewport/pipeline.py')
    credits.verify(args.credits_stage)
    title = load('dkr_title_package', ROOT / 'patches/title-text/package.py')
    original_load = title.load

    def nested_load(name, path):
        module = original_load(name, path)
        if name == 'dkr_beta10_package':
            original_features = module.inspect_features

            def features(data):
                original_features(data)
                for marker in (
                    b'full-frame cinematic; borders removed; original scene proportions',
                    b'full-width slides; original font proportions; signed glyphs and spaces',
                    b'signed full-window rectangle and scissor; original opacity; horizontal raster-edge overlap; native readback bounds',
                    b'lower backdrop widened along camera lateral basis; horizon unchanged',
                    b'rounded black plaque; original SMALLFONT; RECOMPILED BY THATGUYMCD!',
                    b'FOV-aware lower-dome coverage; equator and world projection preserved',
                    b'asynchronous family batch; original optics off',
                    b'symmetric mesh, 68-percent opacity',
                    b'PNG replacement skipped safely; original texture retained',
                    b'scene and mip-upload headroom reserved'):
                    if marker not in data:
                        raise ValueError('Missing presentation repair: ' + repr(marker))
            module.inspect_features = features
            original_notes = module.notes

            def notes(root, platform):
                original_notes(root, platform)
                extra = '''
INTRO / CREDITS / ORIGINAL WATER UPDATE
The credits retain their full-window picture, lower sky and original-font
moving text. Black fades overlap the horizontal screen edges to cover tiny
raster rounding gaps without zooming or stretching the scene. Vertical bounds
stay within the actual framebuffer. Colour/depth readback bookkeeping is
clipped to native bounds separately from the widescreen draw commands.

The spinning-logo intro has a small rounded semi-transparent black background behind its
copyright, plus RECOMPILED BY THATGUYMCD! underneath. Both lines use the game's
original SMALLFONT atlas and share the copyright fade. The bottom sky is
extended for the selected FOV, without changing logo or world projection.
The plaque uses a single symmetric corner mesh at 68% opacity.

The latest credits crash dump identified a negative fade row (-2) converted
to an unsigned readback offset (0xFFFFFFFE). The vertical overscan that caused
it has been removed, and native colour/depth aggregate bounds are constrained
before framebuffer submission. This also avoids oversized readback requests.
The earlier dump also exposed an HD PNG mip upload mapping a failed native
GPU allocation. The optional mip hook retains checks on native allocation
handles, falls back to a guarded single-level upload, or keeps the original
texture if neither can be uploaded. Texture caching reserves more headroom
for the current scene and streaming uploads. Existing mip/texture preferences
are retained; checked-out renderer dependencies remain untouched.

Animated liquid texture families now warm their matching HD Rice images in a
bounded background batch instead of waiting for each frame's first appearance.
This targets the initial HD/original flicker with enhanced water turned off.
Water Off remains free of optical effects, reflections and lava lighting.
Native flow history resets safely when the replacement set changes; the
accepted smooth river/waterfall animation and wrap rules remain included.

Includes all previous beta 10 save, Mod Store, SDK, launcher, title and credits
fixes. Version remains v1.0.7 beta 10. Compilation and static package checks
only; the owner performs gameplay and visual acceptance.

Please check the first few seconds after loading water with your texture pack
and Water Off, both logos, maximum FOV, and a complete credits fade. Compare
windowed/fullscreen and widescreen/ultrawide. Original/Accurate framing remains
available. No ROM, personal save or account data is included.
'''
                (root / 'RELEASE-NOTES.txt').write_text((root / 'RELEASE-NOTES.txt').read_text() + extra)
                (root / 'PRESENTATION-UPDATE.txt').write_text(extra.lstrip())
            module.notes = notes
        return module

    title.load = nested_load
    sys.argv = [sys.argv[0]] + remaining
    title.main()
    path = Path(remaining[remaining.index('--output') + 1]) / 'BUILD-RECEIPT.json'
    receipt = json.loads(path.read_text())
    receipt['credits_viewport'] = json.loads((args.credits_stage / 'credits-viewport-receipt.json').read_text())
    receipt['presentation_polish'] = json.loads((args.polish_stage / 'presentation-polish-receipt.json').read_text())
    receipt['polish_packager_sha256'] = pipeline.sha(Path(__file__))
    receipt['polish_overlay_sha256'] = pipeline.sha(AUTH / 'NativeOverlay.cmake')
    if 'windows' in remaining:
        compiled = Path(remaining[remaining.index('--compiled') + 1]) / 'bin/RelWithDebInfo/libexec/dkr-r'
        image = compiled / 'DKR-R-Runtime.exe'
        pdb = compiled / 'DKR-R-Runtime.pdb'
        if not pdb.is_file():
            raise ValueError('Keep the exact Windows debug symbols privately for future dumps')
        symbols = args.polish_stage.parent / 'symbols' / pipeline.sha(image)
        symbols.mkdir(parents=True, exist_ok=True)
        for source in (image, pdb):
            destination = symbols / source.name
            if destination.exists() and pipeline.sha(destination) != pipeline.sha(source):
                raise ValueError('Refusing to replace archived symbols')
            shutil.copy2(source, destination)
        receipt['private_windows_symbols'] = {'path': str(symbols), 'pdb_sha256': pipeline.sha(pdb)}
    path.write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
