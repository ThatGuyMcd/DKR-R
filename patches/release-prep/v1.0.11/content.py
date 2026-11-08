"""Current release prose, applied only through the snapshotted metadata pipeline."""
from pathlib import Path
AUTH=Path(__file__).resolve().parent
CURRENT=('README.md','docs/README.md','docs/BUILDING.md','docs/DEVELOPMENT.md',
 'docs/ONLINE_MULTIPLAYER.md','docs/PUBLIC_LOBBIES.md','docs/TROUBLESHOOTING.md',
 'docs/LAUNCHER.md','docs/SAVES.md','docs/GRAPHICS.md','docs/SOUND.md',
 'docs/CONTROLLERS.md','docs/ROM_SETUP.md','docs/TEXTURE_PACKS.md',
 'docs/LEGACY-MODS-BETA.md','docs/MAGIC_CODES.md','docs/VEHICLES.md',
 'docs/ACHIEVEMENTS.md','docs/MOD_LIBRARY_ADMIN.md','docs/ARCHITECTURE.md',
 'docs/CUSTOM_TRACKS.md','docs/F3DDKR.md','docs/HUD-WORKSHOP.md',
 'docs/RENDER_SNAPSHOT_ARCHITECTURE.md','docs/SPECTATORS.md',
 'runtime-recomp/README.md','packaging/MACOS-BUILD-README.md',
 'docs/PLATFORMS.md','docs/UPDATES.md')
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Release document anchor changed: '+old[:90])
    return text.replace(old,new,1)
def build(z):
    def read(n):return z.read(n).decode('utf-8').replace('\r\n','\n')
    result={n:read(n).replace('1.0.10','1.0.11') for n in CURRENT}
    for n in ('scripts/README.md','tools/blender/README.md','runtime-recomp/tests/legacy_mods/README.md'):
        result[n]=read(n).replace('1.0.9','1.0.11')
    for n in ('modding/USER-GUIDE.txt','modding/SDK.txt','modding/MODDERS-GUIDE.txt','modding/IMPLEMENTATION-STATUS.txt'):
        result[n]=read(n).replace('1.0.9','1.0.11')
    result['scripts/README.md']=result['scripts/README.md'].replace(
        'patches/release-prep/v1.0.11/documentation/pipeline.py --help',
        'patches/release-prep/v1.0.11/release.py --help').replace(
        'Never invoke runtime checks when acceptance is reserved for the owner.',
        'Run only the named synthetic fixtures and ROM-free helper/updater checks; gameplay acceptance remains separate.')
    for n in z.namelist():
        if not n.startswith('patches/') or not n.endswith('/README.md') or 'source-capsule' in n:continue
        text=read(n)
        if '## Current release composition' not in text:continue
        old='For v1.0.7 use the [release pipeline](../release-prep/v1.0.7/BUILDING.txt).'
        if old not in text:continue
        new='For v1.0.11 use the [release pipeline](../release-prep/v1.0.11/BUILDING.txt).'
        if n=='patches/android/README.md':new='Android is excluded from v1.0.11. The [desktop release pipeline](../release-prep/v1.0.11/BUILDING.txt) does not produce an APK.'
        result[n]=once(text,old,new)
    result['VERSION']='1.0.11\n'
    text=result['README.md'];start=text.index('This update is mainly');end=text.index('DKR-R is a native',start)
    text=text[:start]+("This update is mainly about the memory issues and missing HUD elements\n"
        "people have been reporting. I've also worked on queued graphics data,\n"
        "water textures and support for Syeo's updated custom cars. The launcher\n"
        "design and the Online fixes from v1.0.10 stay as they were.\n\n"
        "Your saves, mods and settings can stay. Please use the complete v1.0.11\n"
        "package on every machine in an Online lobby.\n\n")+text[end:]
    result['README.md']=text
    notes=(AUTH/'RELEASE-NOTES.txt').read_text(encoding='utf-8')
    result['docs/PUBLIC-1.0.11-NOTES.txt']=notes;result['docs/DKR-R-v1.0.11-Changelog.txt']=notes
    text=read('CHANGELOG.md');title=text.split('\n',1)[0]+'\n'
    entry=("\n## 1.0.11 — 25 October 2026\n\n"
        "This update is mainly about the memory issues and missing HUD elements\n"
        "people have reported. I've kept the launcher design and the v1.0.10\n"
        "Online fixes in place.\n\n"
        "- Added expansion RAM headroom to the normal game heap, keeping its upper guard.\n"
        "- Added safer allocation failure handling and useful HUD memory diagnostics.\n"
        "- Protected graphics data before it waits in the game scheduler.\n"
        "- Kept queued water animations consistent, with bounded cache recovery.\n"
        "- Added the larger textures and explicit driver seating in updated .dkrkart cars.\n"
        "- Updated the guides, Windows/Linux builds and signed auto update files.\n\n"
        "Windows x64, Linux x86_64/ARM64 and Steam Deck are included. Android,\n"
        "macOS and iOS are skipped. Saves and installed mods can stay.\n\n")
    result['CHANGELOG.md']=once(text,title,title+entry)
    result['docs/README.md']=once(result['docs/README.md'],
        '[v1.0.11 release notes](PUBLIC-1.0.11-NOTES.txt), [v1.0.9 notes]',
        '[v1.0.11 release notes](PUBLIC-1.0.11-NOTES.txt), [v1.0.10 notes](PUBLIC-1.0.10-NOTES.txt), [v1.0.9 notes]')
    text=result['docs/BUILDING.md']
    text=once(text,'and its `ReleaseOverlay.cmake`. This includes the accepted playtest.4 fixes',
        'and its `ReleaseOverlay.cmake`. This includes the memory, render, scheduler\nhandoff and updated vehicle repairs, above the retained playtest 4 fixes')
    result['docs/BUILDING.md']=text
    text=result['runtime-recomp/README.md']
    result['runtime-recomp/README.md']=once(text,'target alone: directory, Scoreboard, party/spectator repairs, vehicles,',
        'target alone: guarded heap, renderer ownership, water residency, updated\nvehicle exports, directory, Scoreboard, party/spectator repairs, vehicles,')
    text=result['docs/VEHICLES.md']
    text=once(text,'## Online and managed installations',
        '## Updated vehicle exports\n\n'
        "Syeo's `original-resolution-v1` cars are supported in v1.0.11, including\n"
        'Brute v2. Import the original `.dkrkart`; you do not need to rename it or\n'
        'shrink its images. Larger PNGs are prepared as small material tiles once\n'
        'during import, keeping their original pixels and the native memory limits.\n'
        'This can add triangles. Exports exceeding the preparation budget still\n'
        'receive an error rather than raising the renderer limits.\n\n'
        'The explicit revision 1 driver seating contract uses raw native model\n'
        'anchors. Older exports keep their existing placement rules. Source files,\n'
        'archive hashes and installed copies are not rewritten by this support.\n\n'
        'Disabled boost anchors use the native effect position. Enabled custom\n'
        'boost anchors and unknown explicit revisions are not supported yet and\n'
        'are rejected with a specific error. Custom hovercrafts and planes remain\n'
        'unsupported.\n\n## Online and managed installations')
    result['docs/VEHICLES.md']=text
    text=result['docs/GRAPHICS.md']
    text=once(text,'## Textures and sound',
        '## Graphics and memory fixes in v1.0.11\n\n'
        "I've given the normal game heap its missing expansion RAM headroom,\n"
        'while retaining the upper guard and existing allocation lifetimes.\n'
        'Allocation failures and missing HUD resources now have clearer logs.\n'
        'Queued graphics data is copied before it waits in the game scheduler,\n'
        'and the renderer keeps its own decode workspace. These changes do not\n'
        'alter the launcher. The renderer changes do not alter online simulation.\n\n'
        'Water animations retain their selection while frames are queued. Shared\n'
        'frames use one consistent decision, and complete animations can request\n'
        'evicted cache entries again, with bounded asynchronous retries. A pack\n'
        'replacing only some frames stays consistently vanilla for that animation.\n'
        'For example, one replacement out of 16 ripple frames is incomplete.\n'
        'The update does not fill in missing artwork or modify your texture pack.\n\n'
        'These fixes do not remove GPU, guest memory or mod complexity limits.\n'
        'If a problem remains, keep the logs and report the exact scene and packs.\n\n## Textures and sound')
    result['docs/GRAPHICS.md']=text
    text=result['docs/TROUBLESHOOTING.md']
    text=once(text,'## ROM rejected',
        '## Missing HUD elements, memory reports or rejected graphics\n\n'
        'Use the full v1.0.11 package first. Keep `runtime.log` and\n'
        '`runtime-previous.log`, with the track, ROM revision, mods, texture packs\n'
        'and action immediately before the problem. For Online, include the host\n'
        'and affected client or spectator logs from the same attempt.\n\n'
        'The normal game heap now uses the admitted expansion RAM headroom.\n'
        '`[memory][primary-pool]` and `[hud][heap]` help identify the actual heap\n'
        'state. More headroom does not make arbitrarily large mods safe.\n\n'
        'Malformed graphics are still rejected. The scheduler handoff repair\n'
        'closes an earlier memory ownership gap, but the exact previously logged\n'
        'malformed task was not reproduced with game assets during preparation.\n'
        'Keep any new command/provenance reports; do not disable those checks.\n\n'
        'Water staying vanilla can mean an incomplete HD animation. All frames\n'
        'need matching replacements before that family can use HD consistently.\n'
        'For `unsupported car texture profile`, use the original updated export\n'
        'with v1.0.11. Unknown format revisions or enabled custom boost anchors\n'
        'still need exporter/runtime support. See [Vehicles](VEHICLES.md).\n\n## ROM rejected')
    result['docs/TROUBLESHOOTING.md']=text
    text=result['docs/RENDER_SNAPSHOT_ARCHITECTURE.md']
    start=text.index('Modern presentation uses');end=text.index('## Decoded renderer stage',start)
    text=text[:start]+('## Scheduler and submission ownership\n\n'
        'The graphics-handoff Patch Pipeline captures a completed 8 MiB RDRAM\n'
        'image through the existing `gfxtask_run_xbus` callback, before the\n'
        'scheduler message. This protects the earlier wait before host RSP\n'
        'submission. The lease transfers at `submit_rsp_task`, avoiding a second\n'
        'normal-path capture. FIFO matching checks RAM owner, physical root and\n'
        'native command count. It never searches by a recycled root alone.\n\n'
        'Pending capture is bounded to four images. Invalid capture, mismatch\n'
        'or overflow disables early matching until session reset, retaining the\n'
        'accepted submission-copy fallback. Suppressed tasks retire their leases;\n'
        'session generations reject captures that cross reset. Active workers\n'
        'retain their images. SP/DP completion and guest freeing are unchanged.\n\n'
        'The immutable task image is separate from the reusable private decode\n'
        'workspace. Packed-vertex translation uses the upper private arena rather\n'
        'than the original game heap. RT64 readback and decode writes cannot\n'
        'alter live simulation RAM or the immutable submitted image.\n\n'
        'Producer capture also protects Accurate presentation; semantic transform\n'
        'sidecars remain Modern-only. Historical replay suppresses producer side\n'
        'effects. No competing GPU queue, guest command format or gameplay state\n'
        'is added. Malformed command guards and limited provenance reports remain.\n\n')+text[end:]
    result['docs/RENDER_SNAPSHOT_ARCHITECTURE.md']=text
    text=result['docs/F3DDKR.md']
    text=once(text,"The decoder reads an owned 8 MiB task snapshot. It may use the snapshot's\nreserved translation workspace, but it never writes renderer data back to live\nsimulation RDRAM. The runtime's snapshot and SP-completion patches let the CPU\nrecycle live display-list buffers while queued graphics still own their input.\nThis lifetime mechanism supports the HLE decoder rather than replacing it.",
        'A completed task captures an immutable 8 MiB image before the game\nscheduler queue. Its lease transfers at host submission. The decoder uses\na separate private workspace, with packed-vertex translation outside the\ngame heap. Decode/readback writes cannot change the immutable source or live\nsimulation RDRAM. The existing SP/DP completion and guest lifetimes remain.\nMalformed commands still stop safely and can report limited provenance.\nThis ownership mechanism supports the HLE decoder rather than replacing it.')
    result['docs/F3DDKR.md']=text
    text=result['docs/ARCHITECTURE.md']
    text=once(text,'The release composes the Online directory, Scoreboard, party/spectator repairs, vehicles, achievements and texture tools',
        'The release composes guarded game-heap headroom, immutable scheduler handoff, private decode isolation, water residency and updated vehicle preparation above the Online directory, Scoreboard, party/spectator repairs, vehicles, achievements and texture tools')
    result['docs/ARCHITECTURE.md']=text
    result['docs/UPDATES.md']=result['docs/UPDATES.md'].replace('Check a v1.0.9 notice,','Check a v1.0.10 notice,')
    for name,target in (('RELEASE-README.md','packaging/RELEASE-README.md'),
        ('RELEASE-VALIDATION.md','RELEASE-VALIDATION.md'),('DOCUMENTATION-1.0.11.md','docs/DOCUMENTATION-1.0.11.md')):
        result[target]=(AUTH/'pages'/name).read_text(encoding='utf-8')
    result['docs/history/RELEASE-VALIDATION-1.0.10.md']=read('RELEASE-VALIDATION.md').replace(
        '(docs/history/RELEASE-VALIDATION-1.0.9.md)','(RELEASE-VALIDATION-1.0.9.md)')
    text=read('packaging/linux/dkr-port.appdata.xml')
    result['packaging/linux/dkr-port.appdata.xml']=once(text,'  <releases>',
        '  <releases>\n    <release version="1.0.11" date="2026-10-25"><description><p>Memory and HUD safeguards, queued graphics ownership, water texture consistency and updated custom car support.</p></description></release>')
    return result
