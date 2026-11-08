"""Stage lobby stability above the retained music and online repair pipeline."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys

sys.dont_write_bytecode = True
import transforms

AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
INC = re.compile(r'^#include "([^"\n]+)"', re.M)
PAIRS = {('DKRLegacyImportLibrary', 'online_mod_sync.cpp'),
         ('DKRLegacyImportLibrary', 'online_mod_profile.cpp'),
         ('DKRNetplayCore', 'direct_session.cpp'),
         ('DKRPortGame', 'runtime_ui.cpp'), ('DKRPortGame', 'game_main.cpp'), ('DKRPortGame', 'runtime_stubs.cpp')}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def compose(rows, output, task_source):
    if len(rows) != len(PAIRS) or {(r['target'], Path(r['path']).name) for r in rows} != PAIRS:
        raise ValueError('Expected the six final first-party stability sources')
    game = next(Path(r['path']).parent.parent for r in rows if Path(r['path']).name == 'online_mod_sync.cpp')
    outputs = {p.name: p.read_bytes().replace(b'\r\n', b'\n') for p in (AUTH / 'native').iterdir() if p.is_file()}
    if task_source.name != 'sp.cpp' or task_source.parent.name != 'src' or task_source.parent.parent.name != 'librecomp':
        raise ValueError('Expected the unchanged SDK task entrypoint for delegation')
    outputs['sdk_task_source.hpp'] = ('#pragma once\n#define DKR_STABILITY_ORIGINAL_SP "'+task_source.resolve().as_posix()+'"\n').encode()
    swaps, includes = [], []
    staged = {}

    def rewrite(text, source):
        def one(match):
            name = Path(match[1])
            if name.name in outputs:
                return '#include "' + (output / name.name).as_posix() + '"'
            path = name if name.is_absolute() else source.parent / name
            if not path.is_file() and (name.name == 'online_mod_sync_link.hpp' or source.parent == AUTH / 'native'):
                path = next((p for p in (game / name, game / 'netplay' / name.name, game / 'mods' / name.name) if p.is_file()), path)
            if name.name in ('runtime_online_mods_ui.inl', 'online_mod_sync_link.hpp'):
                if not path.is_file():
                    raise ValueError('Cannot resolve final stability include: ' + str(name))
                key = str(path.resolve())
                if key not in staged:
                    raw = path.read_bytes()
                    text = raw.decode().replace('\r\n', '\n')
                    text = transforms.mod_ui(text) if name.name.endswith('.inl') else transforms.link(text)
                    text = rewrite(text, path)
                    outputs[name.name] = text.encode()
                    staged[key] = (output / name.name).as_posix()
                    includes.append({'path': str(path), 'new': staged[key], 'input_sha256': sha(raw), 'output_sha256': sha(text.encode())})
                return '#include "' + staged[key] + '"'
            return '#include "' + path.resolve().as_posix() + '"' if path.is_file() else match[0]
        return INC.sub(one, text)

    for name in tuple(outputs):
        outputs[name] = rewrite(outputs[name].decode(), AUTH / 'native' / name).encode()
    for row in rows:
        source = Path(row['path'])
        if any(p.lower() in ('extern', 'recompiledfuncs', 'recompiledpatches') for p in source.parts):
            raise ValueError('A protected source cannot be a stability input')
        raw = source.read_bytes()
        text = raw.decode().replace('\r\n', '\n')
        transform = {'online_mod_sync.cpp': transforms.sync, 'online_mod_profile.cpp': transforms.profile,
                     'direct_session.cpp': transforms.direct, 'game_main.cpp': transforms.main,
                     'runtime_stubs.cpp': transforms.stubs}.get(source.name)
        if transform:
            text = transform(text)
        elif source.name == 'runtime_ui.cpp':
            text = '#include "diagnostics.hpp"\n' + text
        text = rewrite(text, source)
        outputs[source.name] = text.encode()
        swaps.append({**row, 'new': str(output / source.name), 'input_sha256': sha(raw), 'output_sha256': sha(text.encode())})
    if {Path(h['path']).name for h in includes} != {'runtime_online_mods_ui.inl', 'online_mod_sync_link.hpp'}:
        raise ValueError('Both final UI/transport adapter includes must be composed')
    # Retain the real transfer/cache tests and add repair fault cases. These
    # use synthetic bytes in an exclusively reserved fixture; no ROM or mods.
    test_source = ROOT / 'runtime-recomp/tests/online_mod_transfer_tests.cpp'
    test = '#include "cache_repair.hpp"\n' + test_source.read_text()
    test = transforms.once(test, '        std::filesystem::remove_all(cache); // exclusively created test fixture', '''        using dkr::lobby_stability::verify_or_repair_download;
        check(verify_or_repair_download(cache,"payloads",descriptor,{}));
        check(!verify_or_repair_download(cache,"partials",invalid,{}));
        check(!std::filesystem::exists(cache/"partials"/invalid.digest));
        check(cached_payload(cache/"payloads",descriptor));
        write_new_file(cache/"payloads"/invalid.digest,Bytes{1,2,3});
        check(!verify_or_repair_download(cache,"payloads",invalid,{}));
        check(!std::filesystem::exists(cache/"payloads"/invalid.digest));
        check(cached_payload(cache/"payloads",descriptor));
        const auto incomplete=cache/"partials"/invalid.digest;write_new_file(incomplete,Bytes{1,2,3});
        check(!verify_or_repair_download(cache,"partials",invalid,{}));
        check(read_file(incomplete,16)==Bytes({1,2,3}));
        std::stop_source cancelled;cancelled.request_stop();
        rejects([&]{verify_or_repair_download(cache,"partials",invalid,cancelled.get_token());});
        check(read_file(incomplete,16)==Bytes({1,2,3}));
        auto traversal=invalid;traversal.digest="../outside";
        rejects([&]{verify_or_repair_download(cache,"partials",traversal,{});});
#ifndef _WIN32
        const auto link_target=cache/"partials"/sha256(bytes("symlink-target"));
        std::filesystem::create_symlink(cache/"payloads"/hash,link_target);
        auto linked=descriptor;linked.digest=sha256(bytes("symlink-target"));
        rejects([&]{verify_or_repair_download(cache,"partials",linked,{});});
        check(cached_payload(cache/"payloads",descriptor));
#endif
        std::filesystem::remove_all(cache); // exclusively created test fixture''')
    outputs['transfer_checks.cpp'] = rewrite(test, test_source).encode()
    return swaps, includes, outputs


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--task-source', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.is_relative_to(ROOT):
        raise ValueError('Generated stability output must stay outside the checkout')
    swaps, includes, outputs = compose(json.loads(args.inputs.read_text()), output, args.task_source)
    for name, data in outputs.items():
        write(output / name, data)
    for name, rows in [('swaps.json', swaps), ('includes.json', includes)]:
        write(output / name, (json.dumps(rows, indent=2) + '\n').encode())
    write(output / 'task-adapter.json', (json.dumps({'target':'librecomp','path':str(args.task_source.resolve()),
        'new':str(output/'task_submission.cpp'),'input_sha256':sha(args.task_source.read_bytes()),
        'output_sha256':sha(outputs['task_submission.cpp']),'mode':'delegate unchanged source; scoped owned bootstrap audio submission'},indent=2)+'\n').encode())
    print('Lobby stability staged over current music/online fixes; no protected source edits.')
