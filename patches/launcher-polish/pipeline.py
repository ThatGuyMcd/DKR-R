"""Compose the launcher cleanup into NEW Patch Pipeline output only."""
from pathlib import Path
import argparse
import json
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
AUTH = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'scripts'))
from camera_plus_pipeline import digest, new_output, replace, verify

FILES = ('runtime_ui.cpp', 'runtime_saves_ui.inl', 'runtime_details_ui.inl')


def authored():
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(AUTH.iterdir()) if p.is_file()}


def transformed(name, text):
    if name == 'runtime_ui.cpp':
        return replace(text, '''    {
        PaddockEnter enter(3);
        DrawPageDisclosure("versions", "ROM versions and compatibility", width, [](float inner) {
            PaddockText(PaddockReading(13.0F, false, 1.55F), PaddockRgb(0xABC0CC),
                        "Legacy patches need the ROM version they were made for. Native tracks don't need patch preparation.", inner);
        });
        PaddockGap(16.0F);
    }
''', '')
    if name == 'runtime_saves_ui.inl':
        return replace(text, '''        PaddockGap(13.0F);
        if (ImGui::CollapsingHeader("Save location")) {
            SavesNote(PathUtf8(info.path), inner, 14.0F);
            if (SavesRaceButton("COPY LOCATION")) SDL_SetClipboardText(PathUtf8(info.path).c_str());
        }
''', '')
    if name == 'runtime_details_ui.inl':
        text = replace(text, 'void DrawAboutSupport(float width) {',
                       (AUTH / 'support_actions.inl').read_text() + '\nvoid DrawAboutSupport(float width) {')
        start = text.index('        const bool stacked = inner <= 520.0F;', text.index('void DrawAboutSupport'))
        end = text.index('        if (!g_support_action_status.empty()) {', start)
        text = text[:start] + '        DrawSupportActionRow(inner);\n' + text[end:]
        text = replace(text, '    if (support) DrawAboutSupport(width);\n', '')
        return replace(text, '    DrawUpdatesCard(width);\n    AboutSectionTitle("Credits", width);',
                       '    DrawUpdatesCard(width);\n    if (support) DrawAboutSupport(width);\n    AboutSectionTitle("Credits", width);')
    raise ValueError('Unreviewed launcher input')


def payloads(baseline):
    return {name: transformed(name, (baseline / 'src/game' / name).read_text()) for name in FILES}


def stage(baseline, output):
    baseline = baseline.resolve(strict=True)
    verify(baseline)
    output = new_output(output, baseline)
    sources = payloads(baseline)
    game = output / 'src/game'
    game.mkdir(parents=True)
    for name, text in sources.items():
        (game / name).write_text(text, newline='\n')
    receipt = {'schema': 1, 'baseline': baseline.relative_to(ROOT).as_posix(),
               'baseline_receipt': digest(baseline / 'camera-receipt.json'), 'authored': authored(),
               'files': {name: digest(game / name) for name in FILES}}
    (output / 'launcher-ui-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    inspect(output)


def inspect(output):
    receipt = json.loads((output / 'launcher-ui-receipt.json').read_text())
    baseline = ROOT / receipt['baseline']
    verify(baseline)
    if receipt['schema'] != 1 or receipt['authored'] != authored():
        raise ValueError('Launcher authorship changed; create a fresh stage')
    if receipt['baseline_receipt'] != digest(baseline / 'camera-receipt.json'):
        raise ValueError('Camera baseline changed')
    game = output / 'src/game'
    if set(receipt['files']) != set(FILES) or {p.name for p in game.iterdir()} != set(FILES):
        raise ValueError('Unexpected launcher stage files')
    for name, expected in payloads(baseline).items():
        path = game / name
        if path.is_symlink() or digest(path) != receipt['files'][name] or path.read_text() != expected:
            raise ValueError('Launcher source differs from reviewed transformation: ' + name)
    print('Launcher cleanup: three staged UI inputs inspected; Camera++ baseline preserved')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('stage', 'verify'))
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.mode == 'stage':
        stage(args.baseline, args.output)
    else:
        inspect(args.output.resolve(strict=True))
