"""Build/package the startup hotfix over the frozen 1.0.7 source composition."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import shutil
import sys

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
BASE = ROOT / 'patches/release-prep/v1.0.7'
VERSION = '1.0.8'
CODE = 1080001


def module(name):
    spec = importlib.util.spec_from_file_location('hotfix_' + name, BASE / (name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def put(name, text):
    (ROOT / name).write_text(text, encoding='utf-8', newline='\n')


NOTES = '''DKR-R v1.0.8 - STARTUP HOTFIX

This fixes a Windows startup failure that could stop the game before the
graphics even started, with mods both on and off.

Windows was being asked to commit 4 GiB of memory when only 512 MiB needed
to be usable. We now reserve the full address range and only commit that
usable part. The guard region stays protected. This saves 3.5 GiB of Windows
commit allowance without shrinking the game's memory or limiting your mods.

If Windows still cannot allocate memory, the log now tells us which step
failed, the Windows error number and the available memory allowance.
The old logs did not contain that error number, so they cannot tell us the
exact RAM/page-file state on the affected PC.

All the 1.0.7 features and the accepted HD-water fix are retained. Linux and
Android keep their existing memory mapping. No save reset or mod reinstall
is needed. Back up your saves and keep the complete package together.

Windows x64, Linux x86_64/ARM64 AppImages, Steam Deck and Android ARM64.
No macOS/iOS package. Android updates retain the existing app/signing identity.
SDK 2.0 and existing API 1/2 mods remain supported; there is no SDK/API change.
Use matching complete builds on every machine for Online.

Linux requirements are unchanged: glibc 2.38+, GLIBCXX_3.4.32 and a compatible
desktop/audio/font/Vulkan stack. Required legal notices are included. No ROM,
personal saves, installed mod library or offline Player Guide is included.

Compilation and package/source inspection are separate from gameplay checks.
The owner verifies startup on the affected PC and the physical devices.
Automatic-update signatures still require the existing private release key.
'''


def prepare():
    # Publication is authored here, not by editing protected/generated source.
    pipeline = AUTH / 'pipeline.py'
    exec(compile(pipeline.read_text(), str(pipeline), 'exec'), {'__file__': str(pipeline)})
    put('VERSION', VERSION + '\n')
    path = ROOT / 'README.md'
    text = path.read_text(encoding='utf-8')
    heading = '## DKR-R v1.0.7'
    if '## DKR-R v1.0.8' not in text:
        text = text.replace(heading, '''## DKR-R v1.0.8

This one fixes a nasty Windows startup issue. The game could stop before it
even reached the graphics, with mods on or off. Windows was being asked to
commit 4 GiB of memory instead of the 512 MiB we actually use. That is fixed,
the guard region is still protected, and your saves/mods do not need reset.
If memory really is unavailable, the log now gives us the Windows error too.
All the 1.0.7 additions below are still here.

''' + heading, 1)
    put('README.md', text)
    path = ROOT / 'CHANGELOG.md'
    text = path.read_text(encoding='utf-8')
    if '## 1.0.8 ' not in text:
        text = text.replace('# DKR-R changelog\n', '''# DKR-R changelog

## 1.0.8 — 20 October 2026

- Fix Windows startup committing the 4 GiB guard/address reservation.
- Commit only the existing 512 MiB usable RDRAM; leave 3.5 GiB guarded.
- Log the failed allocation phase, Windows error, requested sizes and memory
  availability instead of the old generic allocation message.
- Preserve the accepted 1.0.7 water/source composition, saves, SDK and mods.
- Rebuild Windows, Linux x86_64/ARM64, Steam Deck and Android ARM64.
- Keep the existing Android signing identity and use version code 1080001.
''', 1)
    put('CHANGELOG.md', text)
    put('docs/PUBLIC-1.0.8-NOTES.txt', NOTES)
    path = ROOT / 'packaging/RELEASE-README.md'
    put('packaging/RELEASE-README.md', path.read_text(encoding='utf-8').replace('# DKR-R v1.0.7', '# DKR-R v1.0.8'))
    path = ROOT / 'packaging/android/app/build.gradle'
    put('packaging/android/app/build.gradle', path.read_text(encoding='utf-8').replace('getOrElse(1070018)', 'getOrElse(1080001)'))
    path = ROOT / 'packaging/linux/dkr-port.appdata.xml'
    text = path.read_text(encoding='utf-8')
    if 'version="1.0.8"' not in text:
        text = text.replace('  <releases>', '  <releases>\n    <release version="1.0.8" date="2026-10-20"><description><p>Windows startup memory allocation hotfix.</p></description></release>')
    put('packaging/linux/dkr-port.appdata.xml', text)
    path = ROOT / 'docs/BUILDING.md'
    text = path.read_text(encoding='utf-8')
    if '## v1.0.8 startup hotfix' not in text:
        text += '\n## v1.0.8 startup hotfix\n\nUse [the startup hotfix pipeline](../patches/startup-memory/BUILDING.txt).\nIt composes the same frozen 1.0.7 inputs, adds only the isolated runtime-memory\npatch and compiles the actual 1.0.8 version. Android version code is 1080001.\n'
    put('docs/BUILDING.md', text)
    print('v1.0.8 metadata published through the Patch Pipeline.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('prepare', 'configure', 'build', 'package', 'android-recipe'))
    p.add_argument('--build', type=Path); p.add_argument('--log', type=Path)
    p.add_argument('--capsule', type=Path); p.add_argument('--cmake', default='cmake')
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--platform', choices=('windows', 'linux')); p.add_argument('--arch', default='x86_64')
    for name in ('compiled', 'template', 'output', 'evidence', 'runtime-image', 'trophy', 'products'):
        p.add_argument('--' + name, type=Path)
    a = p.parse_args()
    if a.action == 'prepare':
        prepare()
    elif a.action in ('configure', 'build'):
        release = module('release'); release.VERSION = VERSION
        if a.action == 'configure':
            release.configure(a.build, a.log, a.cmake, a.capsule)
        else:
            release.compile_build(a.build, a.log, a.cmake, a.jobs)
    elif a.action == 'package':
        package = module('package_release'); package.VERSION = VERSION
        def notes(root, label):
            (root / 'README.txt').write_text((ROOT / 'packaging/RELEASE-README.md').read_text(encoding='utf-8').replace('# DKR-R v1.0.8', 'DKR-R v1.0.8 - ' + label), encoding='utf-8')
            shutil.copy2(ROOT / 'docs/PUBLIC-1.0.8-NOTES.txt', root / 'RELEASE-NOTES.txt')
        package.notes = notes
        argv = [a.platform]
        for key in ('compiled', 'template', 'output', 'evidence', 'trophy'):
            argv += ['--' + key, str(getattr(a, key))]
        if a.platform == 'linux':
            argv += ['--arch', a.arch, '--runtime-image', str(a.runtime_image)]
        sys.argv = [str(BASE / 'package_release.py'), *argv]
        package.main()
    else:
        original = BASE / 'prepare_android.py'
        sys.argv = [str(original), '--output', str(a.output), '--products', str(a.products)]
        exec(compile(original.read_text(encoding='utf-8'), str(original), 'exec'), {'__file__': str(original), '__name__': '__main__'})
        text = a.output.read_text(encoding='utf-8').replace("-eq '1.0.7'", "-eq '1.0.8'").replace('docs/PUBLIC-1.0.7-NOTES.txt', 'docs/PUBLIC-1.0.8-NOTES.txt')
        a.output.write_text(text, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
