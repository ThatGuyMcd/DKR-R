#!/usr/bin/env python3
"""Verify ROM-free Android objects and the linked platform subset."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

OBJECTS = {
    'DKRAndroidCompileCheck': ('android_platform.cpp', 'android_rt64_window.cpp', 'mobile_ui.cpp'),
    'DKRAndroidPlatformLinkCheck': ('android_file_dialog.cpp', 'runtime_portable.cpp', 'runtime_support.cpp'),
}


def elf_architecture(description, kind):
    for name, expected in (('Class', 'ELF64'), ('Machine', 'AArch64'), ('Type', kind)):
        if not re.search(r'^\s*' + name + r':\s+' + expected + r'\b', description, re.MULTILINE):
            raise ValueError(f'Expected {name} {expected}')


def library_contract(description, shared_cpp=False):
    elf_architecture(description, 'DYN')
    alignments = [int(line.split()[-1], 16) for line in description.splitlines()
                  if line.strip().startswith('LOAD ')]
    if not alignments or any(value < 16384 for value in alignments):
        raise ValueError('Every ELF LOAD segment must have at least 16 KB alignment')
    needed = re.findall(r'\(NEEDED\)\s+Shared library: \[([^]]+)\]', description)
    if shared_cpp and not {'libc++_shared.so', 'libSDL2.so'} <= set(needed):
        raise ValueError('Platform subset must link shared libc++ and SDL2')
    note = re.search(r'NT_ANDROID_TYPE_IDENT\s+description data:\s*((?:[0-9a-f]{2} ?)+)', description)
    data = bytes.fromhex(note[1]) if note else b''
    if len(data) != 132:
        raise ValueError('Expected an Android NDK identity note')
    api = int.from_bytes(data[:4], 'little')
    ndk = data[4:68].split(b'\0')[0].decode('ascii')
    build = data[68:].split(b'\0')[0].decode('ascii')
    if api < 28 or (ndk, build) != ('r29', '14206865'):
        raise ValueError(f'Expected API >= 28 and NDK r29 build 14206865, got {api}, {ndk}, {build}')
    return {'architecture': 'AArch64', 'api': api, 'ndk': ndk, 'ndk_build': build,
            'load_alignments': alignments, 'needed': needed}


def verify(build, readelf):
    objects = []
    for target, sources in OBJECTS.items():
        for source in sources:
            matches = list((build / f'CMakeFiles/{target}.dir').rglob(source + '.o'))
            if len(matches) != 1:
                raise ValueError(f'{source}: expected exactly one compiled platform object')
            description = subprocess.check_output([str(readelf), '-h', str(matches[0])], text=True)
            elf_architecture(description, 'REL')
            objects.append(source)
    libraries = {}
    for name, shared_cpp in (('libDKRAndroidPlatformLinkCheck.so', True), ('sdl2/libSDL2.so', False)):
        description = subprocess.check_output([str(readelf), '--wide', '-h', '-l', '-d', '-n', str(build / name)], text=True)
        libraries[name] = library_contract(description, shared_cpp)
    return {'scope': 'Device compilation and platform-subset linking only; no APK or gameplay validation',
            'objects': objects, 'libraries': libraries}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--readelf', type=Path, required=True, help='NDK llvm-readelf executable')
    args = parser.parse_args(argv)
    try:
        report = verify(args.build, args.readelf)
        (args.build / 'device-objects.json').write_text(json.dumps(report, indent=2) + '\n')
        print(f'Verified {len(report["objects"])} arm64 objects and two Android libraries with 16 KB LOAD alignment.')
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'Android compile check: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
