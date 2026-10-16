#!/usr/bin/env python3
"""Compile portable SDK-only Custom Magic Codes. Never runs the resulting mod.

Requires Python 3.11+, Clang/LLD and the project's pinned RecompModTool.
All output goes into a new directory; game code/dependencies remain read-only.
Raw-address mods require a separately reviewed revision-specific authoring kit.
"""
from __future__ import annotations
import argparse
import json
import hashlib
import math
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tomllib
import zipfile

SDK = Path(__file__).resolve().parent
ID = re.compile(r'[a-z0-9_-]{1,96}\Z')


def literal(value):
    if isinstance(value, (str, bool, int)):
        return json.dumps(value, ensure_ascii=True)
    if isinstance(value, float) and math.isfinite(value):
        return repr(value)
    if isinstance(value, list):
        return '[' + ', '.join(literal(v) for v in value) + ']'
    raise ValueError('Unsupported TOML value in mod manifest')


def local(root: Path, value: str) -> Path:
    p = PurePosixPath(value)
    if (not value or p.is_absolute() or any(x in ('', '.', '..') for x in value.split('/')) or
            any(ord(c) < 32 or ord(c) > 126 or c in '\\:*?"<>|' for c in value)):
        raise ValueError('Unsafe project resource path')
    path = root.joinpath(*p.parts)
    for part in (path, *path.parents):
        if part == root:
            break
        if part.is_symlink():
            raise ValueError('Project sources/resources must not be symbolic links')
    if not path.resolve().is_relative_to(root) or not path.is_file():
        raise ValueError(f'Missing or external project file: {value}')
    return path


def unique_object(pairs):
    result = {}
    for k, v in pairs:
        if k in result:
            raise ValueError(f'Duplicate metadata field: {k}')
        result[k] = v
    return result


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('project', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--clang', default='clang')
    p.add_argument('--lld', default='ld.lld')
    p.add_argument('--mod-tool', type=Path, required=True)
    p.add_argument('--reference-kit',type=Path,help='Address-only pinned kit for a single-revision raw mod')
    a = p.parse_args()
    project = a.project.resolve(strict=True)
    source_config = local(project, 'mod.toml')
    config = tomllib.loads(source_config.read_text(encoding='utf-8'))
    manifest = config['manifest']
    if not ID.fullmatch(manifest['id']) or manifest.get('game_id') != 'dkr':
        raise ValueError('Expected a DKR mod manifest with a portable ID')
    if manifest.get('native_libraries') or manifest.get('custom_gamemode'):
        raise ValueError('Native libraries and unmanaged custom game modes are not supported')
    meta_path = local(project, 'dkr.json')
    if meta_path.stat().st_size > 1024 * 1024:
        raise ValueError('DKR metadata exceeds its budget')
    meta = json.loads(meta_path.read_text(encoding='utf-8'), object_pairs_hook=unique_object)
    if meta.get('schema') != 1 or meta.get('api') != 1:
        raise ValueError('Unsupported DKR SDK metadata version')
    reference=SDK/'reference-sdk-only.toml';data_references=[];extra_link=[]
    if a.reference_kit:
        kit=a.reference_kit.resolve(strict=True)
        receipt=json.loads(local(kit,'receipt.json').read_text(encoding='utf-8'),object_pairs_hook=unique_object)
        if receipt.get('schema')!=1 or meta.get('revisions')!=[receipt.get('revision')]:
            raise ValueError('Raw mods must declare exactly the reference-kit revision')
        for name,digest in receipt['files'].items():
            if hashlib.sha256(local(kit,name).read_bytes()).hexdigest()!=digest:raise ValueError('Reference kit was modified')
        reference=local(kit,'functions.toml');data_references=[local(kit,'data.toml').as_posix()]
        extra_link=['-T',str(local(kit,'symbols.ld'))]
    resources = {}
    for name, value in meta.get('resources', {}).items():
        if not ID.fullmatch(name) or value in resources:
            raise ValueError('Invalid or aliased resource')
        resources[value] = local(project, value)
    if len(resources) > 256 or sum(f.stat().st_size for f in resources.values()) > 32 * 1024 * 1024:
        raise ValueError('Resources exceed the portable package budget')
    sources = set()
    for pattern in config['build']['sources']:
        if not isinstance(pattern, str) or '..' in PurePosixPath(pattern).parts or PurePosixPath(pattern).is_absolute():
            raise ValueError('Unsafe source pattern')
        matches = list(project.glob(pattern))
        if not matches:
            raise ValueError(f'No source files match {pattern}')
        for match in matches:
            relative = match.relative_to(project).as_posix()
            source = local(project, relative)
            if source.suffix != '.c' or source.stat().st_size > 1024 * 1024:
                raise ValueError('Only bounded portable C sources are supported')
            sources.add(source)
    if not sources or len(sources) > 256:
        raise ValueError('Source file count exceeds its budget')
    clang, lld = shutil.which(a.clang), shutil.which(a.lld)
    tool = a.mod_tool.resolve(strict=True)
    if not clang or not lld or not tool.is_file():
        raise ValueError('Clang, LLD and the pinned RecompModTool are required')
    output = a.output.resolve()
    if output.exists() or output == project or output in project.parents:
        raise ValueError('Output must be a NEW directory, not an input/ancestor')
    # Upstream Windows tooling uses a PowerShell ZIP command internally. Keep
    # its temporary paths strictly free of shell metacharacters; the compiler
    # invocations themselves always use argument arrays, never shell=True.
    if any(c in str(output) for c in "'`$\r\n"):
        raise ValueError('Choose a tool output path without shell metacharacters')
    output.mkdir(parents=True)
    objects = []
    flags = ['--target=mips-unknown-none', '-march=mips2', '-mabi=32', '-mno-abicalls',
             '-fno-pic', '-G0', '-mfp32', '-mno-odd-spreg', '-ffreestanding', '-fno-builtin',
             '-fno-stack-protector', '-ffp-contract=off', '-O2', '-Wall', '-Wextra', '-Werror',
             '-I' + str(SDK / 'include'), '-I' + str(project)]
    for index, source in enumerate([SDK/'runtime.c',*sorted(sources)]):
        obj = output / f'unit-{index}.o'
        subprocess.run([clang, *flags, '-c', str(source), '-o', str(obj)], check=True)
        objects.append(obj)
    elf = output / 'mod.elf'
    subprocess.run([lld, '-m', 'elf32btsmip', '-static', '--emit-relocs', '-T', str(SDK / 'mod.ld'),
                    *extra_link,'-e', '0', '-o', str(elf), *(str(o) for o in objects)], check=True)
    lines = ['[manifest]']
    for key, value in manifest.items():
        if key != 'config_options':
            if not re.fullmatch(r'[a-z_]+', key):
                raise ValueError('Invalid manifest key')
            lines.append(f'{key} = {literal(value)}')
    for option in manifest.get('config_options', []):
        lines.append('\n[[manifest.config_options]]')
        for key, value in option.items():
            if not re.fullmatch(r'[a-z_]+', key):
                raise ValueError('Invalid option key')
            lines.append(f'{key} = {literal(value)}')
    lines += ['\n[inputs]', 'elf_path = "mod.elf"', 'mod_filename = "tool-output"',
              'func_reference_syms_file = ' + literal(reference.as_posix()),
              'data_reference_syms_files = '+literal(data_references), 'additional_files = []']
    tool_config = output / 'tool.toml'
    tool_config.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    subprocess.run([str(tool), str(tool_config), str(output)], check=True)
    package = output / (manifest['id'] + '.nrm')
    fixed = {'mod.json': output / 'mod.json', 'mod_syms.bin': output / 'mod_syms.bin',
             'mod_binary.bin': output / 'mod_binary.bin', 'dkr.json': meta_path}
    if any(name in fixed for name in resources):
        raise ValueError('A resource cannot replace reserved package metadata')
    with zipfile.ZipFile(package, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, file in sorted({**fixed, **resources}.items()):
            # Portable/reproducible archive metadata, not the developer's
            # platform attributes, usernames or source modification times.
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, file.read_bytes(), compresslevel=6)
    if package.stat().st_size > 32 * 1024 * 1024:
        raise ValueError('Compiled package exceeds its import size limit')
    print(f'Compiled portable code package: {package}')
    print('No game/mod execution or runtime qualification was performed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
