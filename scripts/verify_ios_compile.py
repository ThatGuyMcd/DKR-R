#!/usr/bin/env python3
"""Verify the ROM-free compile check produced arm64 iPhoneOS objects."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

SOURCES = ('ios_platform.mm', 'ios_metal_layer.mm', 'ios_file_dialog.mm',
           'mobile_ui.cpp', 'runtime_portable.cpp', 'runtime_support.cpp')


def inspect_object(path):
    architecture = subprocess.check_output(['xcrun', 'lipo', '-archs', str(path)], text=True).strip()
    if architecture != 'arm64':
        raise ValueError(f'{path}: expected only arm64, got {architecture}')
    description = subprocess.check_output(['xcrun', 'vtool', '-show-build', str(path)], text=True)
    fields = dict(re.findall(r'^\s*(platform|minos|sdk)\s+(\S+)\s*$', description, re.MULTILINE))
    if fields.get('platform') != 'IOS' or fields.get('minos') != '18.0' or fields.get('sdk') in (None, '0.0'):
        raise ValueError(f'{path}: expected iPhoneOS device object targeting iOS 18.0, got {fields}')
    return {'architecture': architecture, **fields}


def verify(build):
    records = []
    for source in SOURCES:
        matches = list((build / 'CMakeFiles/DKRIosCompileCheck.dir').rglob(source + '.o'))
        if len(matches) != 1:
            raise ValueError(f'{source}: expected exactly one compiled platform object, found {len(matches)}')
        records.append({'source': source, **inspect_object(matches[0])})
    sdl_objects = list((build / 'sdl2/CMakeFiles/SDL2-static.dir').rglob('SDL_uikitappdelegate.m.o'))
    if len(sdl_objects) != 1:
        raise ValueError('Expected compiled SDL2 UIKit application delegate')
    records.append({'source': 'SDL_uikitappdelegate.m', **inspect_object(sdl_objects[0])})
    archive = build / 'libDKRIosCompileCheck.a'
    architecture = subprocess.check_output(['xcrun', 'lipo', '-archs', str(archive)], text=True).strip()
    if architecture != 'arm64':
        raise ValueError(f'{archive}: expected arm64 validation archive')
    return {'scope': 'Device compilation only; no application linking, ROM execution, or IPA validation',
            'objects': records}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    args = parser.parse_args(argv)
    try:
        report = verify(args.build)
        (args.build / 'device-objects.json').write_text(json.dumps(report, indent=2) + '\n')
        print(f'Verified {len(report["objects"])} arm64 iPhoneOS device objects; compilation only.')
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'iOS compile check: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
