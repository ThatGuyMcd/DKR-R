"""Static camera-renderer feasibility audit. Does not execute guest functions.

Rendering the original whole render_scene is prohibited: it advances waves,
colour cycles, fog and weather. Inspect the smaller per-view roots, enumerate
their transitive code/data needs and identify indirect/native-service escapes
before admitting a renderer-only CPU closure.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_replay_probe import DEF, CALL, strip

ROOTS = ('render_level_geometry_and_objects', 'skydome_render',
         'trackbg_render_gradient', 'viewport_main', 'mtx_world_origin')
FORBIDDEN = ('audio', 'sound', 'music', 'rumble', 'rand', 'alloc', 'free',
             'dma', 'read_rom', 'load_asset', 'obj_loop', 'update_racer',
             'waves_update', 'weather_update', 'on_frame', 'code_event')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--root',action='append',default=[])
    args = parser.parse_args()
    source = args.generated.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() or not (ROOT / 'build').resolve() in output.parents:
        raise ValueError('Choose a NEW report in the workspace build directory')
    functions = {}
    for path in sorted(source.glob('*.c')):
        text = path.read_text()
        matches = list(DEF.finditer(text))
        for i, match in enumerate(matches):
            if match[1] in functions:
                raise ValueError('Ambiguous render function: ' + match[1])
            body = text[match.start():matches[i + 1].start() if i + 1 < len(matches) else len(text)]
            functions[match[1]] = (path.name, body)
    roots=tuple(args.root) if args.root else ROOTS
    work = list(roots)
    visited = {}
    missing = set()
    for name in roots:
        if name not in functions:
            missing.add(name)
    while work:
        name = work.pop()
        if name in visited or name not in functions:
            continue
        file, body = functions[name]
        calls = set(CALL.findall(strip(body)))
        child = sorted(calls.intersection(functions) - {name})
        external = sorted(calls.difference(functions) - {'if', 'for', 'while', 'switch', 'sizeof', 'return', 'void'})
        indirect = sorted(c for c in calls if 'LOOKUP' in c or 'lookup' in c or 'indirect' in c)
        visited[name] = {'file': file, 'sha256': hashlib.sha256(body.encode()).hexdigest(),
                         'calls': child, 'external': external, 'indirect': indirect,
                         'requires_review': any(part in name.lower() for part in FORBIDDEN)}
        work += child
    report = {'schema': 1, 'source': source.as_posix(), 'roots': roots,
              'function_count': len(visited), 'missing_roots': sorted(missing),
              'functions': dict(sorted(visited.items())),
              'review_required': sorted(k for k, v in visited.items() if v['requires_review']),
              'indirect_routes': {k: v['indirect'] for k, v in visited.items() if v['indirect']},
              'execution_performed': False, 'renderer_capabilities_proven': False}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'functions'}, indent=2))


if __name__ == '__main__':
    main()
