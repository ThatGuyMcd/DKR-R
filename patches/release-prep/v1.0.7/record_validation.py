"""Record actual release evidence; never convert compilation into gameplay acceptance."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work',type=Path,required=True);a=p.parse_args()
delivery=json.loads((a.work/'evidence/DELIVERY.json').read_text())
source=json.loads((a.work/'evidence/SOURCE-REVIEW-FINAL.json').read_text())
journal_path=Path('F:/DKR Dist dump/v1.0.7-release-20261020/obsolete-output/MOVE-JOURNAL.json')
journal=json.loads(journal_path.read_text(encoding='utf-8-sig'))
if len(journal)!=249 or not all(x['verified'] for x in journal):raise ValueError('Cleanup is not fully verified')
for name in ('windows','android','linux','arm64'):
    defs=(a.work/'evidence'/name/'DKRExperimentalOwnedRegistry-COMPILE_DEFINITIONS.txt').read_text()
    if any(value not in defs for value in delivery['rollback_build_ids'].values()):raise ValueError('Mixed per-revision engine IDs')
history=AUTH/'history/RELEASE-VALIDATION-before-release.txt'
if not history.exists():
    history.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile('F:/DKR Dist dump/v1.0.7-release-20261020/baseline/AUTHORED-SOURCE-BEFORE.zip') as backup:
        history.write_bytes(backup.read('RELEASE-VALIDATION.md'))
lines=['# v1.0.7 release build record','','20 October 2026. This records this exact stable delivery, not earlier test counts.','',
    '## What was done','',
    '- Windows x64, Linux x86_64, Linux ARM64 and Android ARM64 compiled and linked.',
    '- Steam Deck uses the exact final Linux x86_64 AppImage bytes.',
    '- PE/ELF/APK versions, architectures, archive integrity, linked feature markers and ownership membership inspected.',
    '- Android: application ID `io.github.thatguymcd.dkrr.preview`, code `1070018`, API 28+, ARM64. Its signer matches both the published 1.0.6 APK (code 1060002) and the last beta (1070017). The existing distributed debug certificate was retained; no signing identity was replaced.',
    '- Both Linux architectures require GLIBC 2.38 and GLIBCXX_3.4.32. Host C++/graphics/audio/font libraries are intentionally external under the documented AppImage policy; no unexplained linked dependency remains.',
    '- Source privacy/asset and relative-link inspection: '+str(len(source['files']))+' files, zero flagged issues and zero missing checked relative links.',
    '- The SDK 2 kit matches the corrected builder/public headers and keeps API 1 support. The updated 64-page Bible was rendered and visually reviewed; static text bounds reported zero overflow.',
    '- No offline Player Guide or full SDK is inside the game packages. Required legal notices remain. Desktop packages retain only the small checksum-pinned trophy updater needed by its existing UI action.',
    '- Accepted water bytes were reconstructed into the release capsule. The owner-accepted beta ZIP remains unchanged. Fresh reconstruction and Windows configuration succeeded after old staging output was archived.',
    '- Protected submodules, RecompiledFuncs and RecompiledPatches were not edited. All new authoring is in the Patch Pipeline; generated documentation/SDK publication is applied by that pipeline.',
    '', '## Online identity','']
for revision,identity in delivery['rollback_build_ids'].items():lines.append('- US revision '+revision+': `'+identity+'` on Windows, Linux x86_64/ARM64 and Android.')
lines += ['', '## What was NOT run or claimed','',
    'No game, launcher, code mod, CTest, unit suite, replay, multiplayer match or performance benchmark was executed for this delivery. Compiler/toolchain probes and archive/document/source inspection are not gameplay acceptance.',
    '', 'The owner accepted the HD-water startup correction on the preceding Windows package. That is not blanket acceptance of this exact stable package on every platform. Actual GPU, controller/touch, saves, mod combinations, cross-platform online, background/resume and sustained-performance checks remain with the owner.',
    '', 'Independent online maximum-LOD/scenery parity remains unqualified. Existing baseline graphics source/build switches were preserved, not reintroduced as a newly fixed feature. VR and the withdrawn atmosphere system are not release features. Apple source ports are retained, but no macOS/iOS binary was built.',
    '', '## Cleanup and recovery','',
    str(len(journal))+' obsolete output locations, '+str(sum(x['files'] for x in journal))+' files, '+str(sum(x['bytes'] for x in journal))+' bytes were moved and destination-hash verified. Nothing was permanently deleted.',
    '', 'Backup: `F:/DKR Dist dump/v1.0.7-release-20261020/obsolete-output`. BEFORE-MOVE.json, REMOVE-LIST.txt and MOVE-JOURNAL.json record exact recovery paths. The earlier source snapshot and first cleanup remain separate. Active prepared inputs, protected checkouts and link-containing output trees were retained.',
    '', '## Updating and publishing','',
    'Existing 1.0.7+beta version readers compare equal to stable 1.0.7: upgrade those copies manually. Automatic-update signatures were not generated because the existing private release signing key has not been located. The public trust anchor was not replaced. Manual release assets are complete.',
    '', 'The source is prepared on a local release branch. No push to main, tag, GitHub release or asset upload was performed in this build pass. Owner gameplay acceptance and publishing authority remain separate.',
    '', '## Final upload hashes','']
for name,row in sorted(delivery['files'].items()):lines.append('- `'+name+'`: `'+row['sha256']+'` ('+str(row['bytes'])+' bytes).')
lines += ['', '## Private evidence','',
    'Build/configuration, APK signer, ELF dependency, source/link, capsule and delivery records: `'+a.work.as_posix()+'/evidence`. Exact Windows EXE/PDB pairs are under its private-symbols folder, not in release downloads.',
    '', 'The preceding validation text is retained unchanged in [historical evidence](patches/release-prep/v1.0.7/history/RELEASE-VALIDATION-before-release.txt). Those old results do not become new results here.','']
(ROOT/'RELEASE-VALIDATION.md').write_text('\n'.join(lines),encoding='utf-8',newline='\n')
print('Current stable validation record written; historical evidence preserved.')
