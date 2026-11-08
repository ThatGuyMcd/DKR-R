"""Record completed compile/package gates and SHA-256 sums; no execution."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import re

p=argparse.ArgumentParser(description=__doc__);p.add_argument('task',type=Path);p.add_argument('--directory',type=Path);a=p.parse_args();task=a.task.resolve(strict=True);output=a.directory.resolve(strict=True) if a.directory else task/'delivery'
gates={
    'compile-windows.log':['DKRPortGame.vcxproj ->','DKRLegacyModWorker.vcxproj ->'],
    'compile-linux.log':['Linking CXX executable bin/Release/DKR-R'],
    'compile-arm64.log':['Linking CXX executable bin/Release/DKR-R'],
    'compile-android.log':['Linking CXX shared library libDKR-R.so'],
    'package-android-sdk.log':['BUILD SUCCESSFUL'],
}
for name,needles in gates.items():
    text=(task/name).read_text(errors='replace')
    if any(needle not in text for needle in needles):raise ValueError('Incomplete compile/package gate: '+name)
    if re.search(r'(^|\n)(FAILED:|.*(?:fatal error:|error C[0-9]+:|error:))',text):raise ValueError('Compile failure in gate: '+name)
report=json.loads((output/'STATIC-INSPECTION.json').read_text())
for item in report['files']:
    path=output/item['file']
    if path.stat().st_size!=item['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:raise ValueError('Artifact changed after inspection: '+path.name)
certificates=[]
for name in ('android-signature.txt','android-signature-previous.txt'):
    text=(task/name).read_text(errors='replace')
    line=next(line for line in text.splitlines() if 'certificate SHA-256 digest:' in line)
    certificates.append(line.partition('certificate SHA-256 digest:')[2].strip())
if not certificates[0] or certificates[0]!=certificates[1]:raise ValueError('Android signing identity differs')
receipt=output/'BUILD-RECEIPT.txt';sums=output/'SHA256SUMS.txt'
if receipt.exists() or sums.exists():raise ValueError('Finalize only once into this delivery')
lines=['DKR-R v1.0.7 beta 7 playtest - build/package evidence','18 October 2026','',
       'Windows x64, Linux x86_64, Linux ARM64, Steam Deck and Android arm64 compiled/packaged.',
       'macOS excluded at the explicit request of the user. Linux/Deck include AppImages.',
       'No game, mod, unit test, replay, multiplayer or headset execution by the agent.',
       'Gameplay/controller/visual/performance/multiplayer acceptance remains with the user.','',
       'Native stage: '+(task/'stage').as_posix(),
       'Strict Patch Pipeline receipt verified; checked-out protected sources retained.',
       'All authored work is under patches/sdk2. No Git commit/push performed.',
       'API 1 trophy/difficulty samples compiled with byte-identical ZIP member contents.',
       'Dev Menu API 2 and optional C++ scaffold/source-map package compiled, not executed.',
       'Windows launcher compiles with extended COFF sections (/bigobj).',
       'Version/build arguments were quoted and final payload versions inspected.',
       'Android APK: existing application ID, version 1.0.7+beta.7, versionCode 1070016, arm64-v8a.',
       'Android public signing certificate SHA-256: '+certificates[0],
       'PDF: 64 pages; Poppler rendered/layout reviewed; no out-of-safe-page-bound text.',
       'No ROMs, player saves, Python caches or private qualification images in inspected archives.',
       'One public Windows EXE; runtime/input/import/update helpers remain in libexec/dkr-r.','',
       'Source-only SDK engine identity: '+report['engine_identity'],
       'Shared v77 owned ID: '+report['owned_ids']['77'],
       'Shared v80 owned ID: '+report['owned_ids']['80'],'',
       'Both owned IDs match Windows, Linux x86_64, Linux ARM64 and Android configurations.',
       'This static equality is not a runtime multiplayer interoperability test.','',
       'Guide and release notes document actual supported/protected typed capabilities.',
       'Additional editor coverage: admitted character/model/texture/cup/audio resources;',
       'scene/segment/triangle/AI-link/spawn/trigger inspection; bounded debug geometry;',
       'native checkpoint patch documents; entrance restarts; conservative lap rules;',
       'local screenshot HUD controls; favourites/watches/workspace files/command completion;',
       'opted-in cross-mod live numeric/enum settings and the live AI/boss example.',
       'Protected lifecycle boundaries: no live vehicle/character ID-byte swap, arbitrary',
       'AI-link topology, spawn-table creation or structural/moving-mesh collision mutation.',
       'Cup/extended-lap ownership remains with the admitted trophy/race-laps adapter.',
       'Existing expert game symbols/hooks remain; unrestricted safe live editing is not claimed.','',
       'Completed compile/assembly gates:']
lines += ['  '+name for name in gates]
layout=(task/'bible-layout.log').read_text()
if 'Pages: 64' not in layout or 'out-of-safe-page-bound characters: 0' not in layout:raise ValueError('PDF layout gate missing')
compatibility=json.loads((task/'API1-COMPATIBILITY.json').read_text())
if not compatibility.get('same_zip_member_contents'):raise ValueError('API 1 compatibility inspection missing')
receipt.write_text('\n'.join(lines)+'\n',encoding='utf-8')
files=[path for path in sorted(output.iterdir()) if path.is_file()]
sums.write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n' for path in files),encoding='ascii')
print('Finalized',len(files),'files plus SHA256SUMS.txt. No target execution.')
