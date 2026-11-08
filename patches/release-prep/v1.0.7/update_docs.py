"""Apply reviewed release documentation through the Patch Pipeline.

Original authored sources are recoverable in the release baseline backup.
Historical reports, contributor prose and legal terms are never rewritten.
"""
from pathlib import Path
import hashlib
import json
import re
import sys
import zipfile
sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[2]
BASELINE = Path('F:/DKR Dist dump/v1.0.7-release-20261020/baseline/AUTHORED-SOURCE-BEFORE.zip')

def original(name):
    with zipfile.ZipFile(BASELINE) as archive:
        return archive.read(name).decode('utf-8-sig')

SAVES = '''# Saves and Save Manager

Your normal Adventure now stays with you when you change mods. There is one
personal save with three slots, not a new playthrough for every mod list.
Online and development progress still have their own owners.

## Your personal Adventure

Normal play, character mods, custom tracks and ordinary Custom Magic Codes use
the same personal 512-byte EEPROM. Installing a .dkrmap must not change the
stock EEPROM layout. Added-course records are stored in a separate companion
file, so extra tracks cannot shift your initials or balloon totals.

Older saves under `saves/mods/<fingerprint>/` are kept for recovery. They are
not automatically combined. Open **Save Manager > Backups & Recovery**,
inspect the copy you want and confirm the restore. The previous personal
save is backed up first. Do not delete the old folders to tidy your profile.

## Save Manager

- **Personal save:** manage the three Adventure slots, shared unlocks and records.
- **Edit progress:** choose the intended slot before changing progress. Max Out
  and unlock actions save after confirmation. Manual edits need Apply Changes.
- **Transfer & Paks:** import/export saves, Controller Paks and supported bundles.
- **Online:** inspect the host's separate Adventure and choose progress for hosting.
- **Backups & Recovery:** find older mod-set saves, history and recovery copies.
- **Development:** tools such as the Dev Menu use isolated development progress.

Writes are checked by reading the saved bytes back. A draft opened before
another change cannot overwrite the newer save. Edits/restores are blocked
while an active game or lobby owns that save. Leave the game/lobby first.
Max Out changes stored progress and unlocks; it does not enable every Magic
Code, install mods or bypass the game's current scene/menu lifecycle.

## Online and development saves

The host owns separate online progress under `saves/online/host/`. A client
receives a session copy, not a replacement for their personal Adventure.
Confirmed rollback state is the source of persistent online writes. Speculative
frames and disconnect cleanup must not write into your personal save.
Online Controller Paks remain separate too.

A new development sandbox starts from saved personal progress. Existing
development progress is kept unless you deliberately copy personal progress
again. The Dev Menu does not overwrite your normal Adventure.

## Data locations and portable mode

Windows normally uses `%APPDATA%/DKRPort`. Linux uses `$XDG_CONFIG_HOME/dkr-port`
or `~/.config/dkr-port`. Desktop portable mode uses `dkr-runtime-data` beside
the application. Android uses private app storage; use its export controls.

Portable mode takes effect on the next application launch. It selects another
location; it does not silently move saves, mods or settings. A config override
can also select another profile. Check the selected location before assuming
progress has disappeared. Personal desktop EEPROM is `saves/dkr.us.v77.bin`;
both supported US revisions use the compatible stock layout.

Back up your data before updating. To recover, export and inspect the surviving
copy first, then restore it deliberately. No editor can recreate progress when
no valid copy survives. See [Troubleshooting](TROUBLESHOOTING.md).
'''

MODS = '''# Custom tracks, characters and the Mod Store

Open **Mods / Hacks** to manage tracks, characters and Custom Magic Codes.
The **Mod Store** brings the library into the launcher. Choose grid view or
the detailed browse-and-inspect view, then search/filter the catalogue.

## Install and update

One Install action downloads, verifies and imports a package. Review code
permissions before enabling executable mods. A webpage or incomplete archive
is not a mod package and is not installed as one.

The Store keeps installation receipts, so a managed item can show Installed,
Update or Repair. Duplicate candidates offer a choice rather than silently
creating another copy. Updates replace the selected managed installation;
compatible settings are retained, and changed executable code needs approval.
Same-name matches alone are not proof that two mods are identical.

Use the Store's installed/update pages for managed content. Imports made
outside the Store may need an explicit duplicate/replacement choice. Backups
and interrupted-install recovery protect owned files; do not delete receipts
or recovery folders while diagnosing an installation.

Discord sign-in provides account features, reviews and requests. Moderators
and admins see only the library actions their server-side role allows. A
launcher checkbox cannot grant moderation rights.

## Track and character imports

Legacy patches are extracted into supported native assets. Dedicated behaviour
adapters cover reviewed changes; unknown executable/global ROM edits are not
automatically executed. The importer describes support in Details.

Modern `.dkrmap` tracks can be turned on/off like legacy tracks. ZIP packages
must contain a complete course, including declared music/HD assets. A single
course inside wrapper folders is accepted; ambiguous multi-course archives
need separate imports. See [Custom tracks](CUSTOM_TRACKS.md).

Track Select uses six columns for custom tracks. Custom cups appear below
them when their code mod and required tracks are enabled. Custom character
portraits include supported extracted portrait sources and the in-level
artwork in Fire Mountain and Smokey Castle; that artwork is not the minigame HUD.

Offline mod choices and saves are not replaced by a host's online activation.
The host selects the exact online content/settings and clients consent before
verified preparation. Existing matching installed data can be reused. See
[Online](ONLINE_MULTIPLAYER.md), [Magic Codes](MAGIC_CODES.md) and [Saves](SAVES.md).
'''

MAGIC = '''# Magic Codes and Custom Magic Codes

The original game's Magic Codes and imported **Custom Magic Codes** are two
different things. Original codes keep their normal unlock/gameplay rules.
Custom Magic Codes are executable `.nrm`/`.dkrcode` packages with their own
settings and reviewed game-service access.

Import a package through **Mods / Hacks > Custom Magic Codes** or the Mod Store.
Open Details, review and approve the exact code edition, save its settings and
enable it for the next game. Only approve code from creators you trust. A data
import does not automatically approve code. Revoking permission disables it.

## SDK 2.0

API 2 adds typed racer/object handles, supported live edits and batches, events,
collision queries, checkpoint/route tools, debug drawing, bounded documents,
controller-friendly tool panels and opted-in live settings. API 1 remains
supported; installed API 1 mods do not need a forced conversion.

The SDK is not unrestricted native code or arbitrary ROM execution. Lifecycle,
memory, permissions, budgets and online authority remain enforced. Unsupported
operations need dedicated adapters, not guessed writes into engine internals.
The [Modding Bible](../modding/MODDERS-GUIDE.txt) includes the exact headers.

## Supplied examples

- **Custom Trophy Races:** create cups below Custom Tracks, choose each round's
  track/laps/vehicle, and use lineup or shuffled order. Cups, Lineup and Share
  screens support controller navigation and on-screen text entry. Opening
  Details refreshes the track list. Import/export shares cup definitions,
  not other creators' track assets. Missing tracks still need installing.
- **AI & Boss Difficulty:** tune the supported AI/boss settings. Avoid enabling
  conflicting difficulty mods. The live-settings example is a separate tool.
- **Dev Menu:** public SDK 2 inspection, watches, supported edits/teleport,
  scene tools, pause/step, routes/checkpoints, debug overlays and development
  documents. It uses isolated development progress, not your normal Adventure.
- **ModernCamera++:** optional local presentation presets and live camera tuning.
  VR is unfinished and is not a supported release feature.

Online Custom Magic Codes require Auto / Experimental Rollback and the host's
admitted code/settings. Development tools additionally need explicit lobby
consent and host authority. A local setting cannot secretly turn an ordinary
match into a developer session. See [Online](ONLINE_MULTIPLAYER.md).
'''

LAUNCHER = '''# Launcher and overlay

Pick your ROM and start from **Play**. Desktop portable mode selects a separate
data location on the next launch; it does not move your existing profile.
**Skip intro** can start at the Start Menu or Character Select. The Start Menu
route keeps its normal first sequence and music. **Check for updates** is a
small manual action in Play; background release checks still need your approval
before downloading/installing an update.

**DKR-R Online** contains Host / Quick Join, friends/profile, host settings and
your online-view choices. Hide the lobby code when streaming. Host gameplay
rules are agreed by the session; local presentation choices stay local where
supported. See [Online](ONLINE_MULTIPLAYER.md).

**Mods / Hacks** has tracks, characters, Custom Magic Codes, Mod Store, Track
Lab and import/help tools. Cards stay compact; Details contains the longer
support information and settings. The Store supports both browsing layouts.
See [Mods](LEGACY-MODS-BETA.md) and [Magic Codes](MAGIC_CODES.md).

**Graphics** and **Sound** apply the same settings in launcher and overlay.
Use Escape, F1 or controller Back/View for the overlay. Optional water/reflection
quality and lava glow have separate controls. [Graphics](GRAPHICS.md) explains
their scope and cost. Camera++ is an optional mod, not a forced camera replacement.

**Controls** selects input mappings. Text entry offers an on-screen keyboard;
controller navigation and scrolling are available in supported editors.
Android has a Menu drawer and editable touch controls rather than the desktop
SDL3 helper. Keep access to Menu when hiding racing controls.

**Save Manager** manages personal progress, online progress, development saves,
Paks, transfers and recovery. It does not switch your personal Adventure every
time you change ordinary mods. See [Saves](SAVES.md) before restoring progress.

**Textures** imports/enables packs and sets priorities. **About DKR-R** contains
updates and the support summary above the credits. Support actions can open or
export logs/crash information; review private paths and lobby details before
sharing. Extra Save Location/Connection Details/Reflection Details disclosures
have been removed. Detailed instructions live in the repository docs instead.
'''

def root_readme():
    draft = (AUTH/'README-1.0.7-DRAFT.txt').read_text(encoding='utf-8')
    draft = draft[draft.index('# DKR-R -'):]
    old = original('README.md')
    art = old[old.index('<img '):old.index('DKR-R 1.0.7 brings')]
    draft = draft.replace('Keep the existing project artwork and Discord buttons here.', art.strip())
    draft = draft.replace('Use the final release download links here. Link docs/PLATFORMS.md for the\nactual supported OS/GPU requirements, not guessed minimums.',
        'Downloads: [GitHub Releases](https://github.com/ThatGuyMcd/DKR-R/releases).\nSee [platform support](docs/PLATFORMS.md) before choosing a package.')
    draft = draft.replace('Do not promise independent online high-detail models/ranges until the release\naudit and owner acceptance settle the rolled-back graphics paths.',
        'Independent online maximum-LOD/scenery parity is not a qualified release\nfeature. This pass preserves the accepted rendering code rather than\nreintroducing the rejected graphics changes.')
    draft = draft.replace('- Getting started and platform requirements: docs/README.md and docs/PLATFORMS.md\n- Saves and recovery: docs/SAVES.md\n- Online: docs/ONLINE_MULTIPLAYER.md\n- Mods, the Store and code packages: the current mod and Magic Codes guides\n- Graphics, textures and Camera++: the current graphics/texture guides\n- SDK 2.0 and Modding Bible: the separate SDK release download\n- Full changes: CHANGELOG.md\n\nResolve these to final valid Markdown links when applying this draft.',
        '- [Guide index](docs/README.md) and [platforms](docs/PLATFORMS.md)\n- [Saves and recovery](docs/SAVES.md)\n- [Online](docs/ONLINE_MULTIPLAYER.md)\n- [Mods and Store](docs/LEGACY-MODS-BETA.md), [Magic Codes](docs/MAGIC_CODES.md)\n- [Graphics](docs/GRAPHICS.md), [textures](docs/TEXTURE_PACKS.md)\n- [Modding Bible](modding/MODDERS-GUIDE.txt) and the separate SDK download\n- [Full changes](CHANGELOG.md) and [release checks](RELEASE-VALIDATION.md)\n\nThe game packages do not include an offline Player Guide. Current guides stay\nhere in the repository; modding documentation ships with the separate SDK.')
    draft = draft.replace('Keep the existing contributor credits, licence and asset notices. PR #60\'s', 'Contributor work and credits are retained. PR #60\'s')
    draft = draft.replace('Keep the HDR texture-pack project,\nGolden Balloon HUD reference, Wave Race enhanced-water reference and every\nrequired third-party notice accurately attributed.',
        '`sr.gu` leads the DKR-R HDR Texture Pack Project. Golden Balloon informed\nthe HUD work; Wave Race 64 Recomp provided the enhanced-water reference.\nSee [third-party credits](THIRD_PARTY.md), [licence](LICENSE.md) and the\n[runtime notice](runtime-recomp/COPYING-NOTICE.md).')
    draft += '\nMac and iOS binaries are not included in this release pass. Their contributor\nsource ports remain in the repository. Hardware/gameplay acceptance is recorded\nseparately; a successful compiler is not a guarantee for every device or mod.\n'
    return draft

def change_notes():
    plan = (AUTH/'RELEASE-PLAN.txt').read_text(encoding='utf-8')
    section = plan[plan.index('A. CUSTOM MAGIC CODES AND SDK'):plan.index('\n6. DOCUMENTATION')]
    # Keep the reviewed change ledger, not the editorial instructions after it.
    section = re.sub(r'\nIMPORTANT HOLD:.*?(?=\nF\. CAMERA)', '\nIndependent online maximum-LOD/scenery parity is not qualified for this\nrelease. The accepted rendering paths and fullscreen HUD/nameplates stay.\n', section, flags=re.S)
    section = section[:section.index('\nThe above is the documentation scope')]
    section = section.replace('Verify the deployed backend contract; no new website deployment is assumed.', 'These features use the deployed website backend; its role checks still apply.')
    section = section.replace('All remaining', 'All remaining')
    section = section.replace('Distinguish this from the older built-in Modern camera and preserve migration\n     guidance.', 'Replaces the older camera route with an optional importable package.')
    section = section.replace('Do not announce working VR, tested headsets,\n     standalone Quest output, eye-specific renders or compositor HUD support.', 'VR, standalone Quest and headset compatibility are not release features.')
    section = section.replace('Keep PR #60 nightly/sanitizer/stress/cache tooling and earlier merged build,\n     CI, platform, Track Lab and test-isolation contributions. Attribute actual\n     changes in the correct release: many PR #49-#59 items already shipped in\n     1.0.6 and belong under carried-forward credits, not invented 1.0.7 additions.', 'PR #60 nightly/sanitizer/stress/cache tooling is retained, along with earlier\n     merged CI, platform, Track Lab and test-isolation contributions. PR #49-#59\n     work already shipped in 1.0.6 is carried forward, not counted as new here.')
    return ('DKR-R v1.0.7 - RELEASE NOTES\n20 October 2026\n==========================\n\n'
        'There is a LOT in this update! These are the changes since v1.0.6.\n'
        'Back up your data and keep the complete package together when updating.\n'
        'Everyone in an online lobby needs a matching compatible release.\n\n'+section+
        '\nUPDATING\nUse a manual upgrade from the old 1.0.7+beta packages: their version\n'
        'comparison treats that metadata as the same version as stable 1.0.7.\n'
        'Android updates must retain the existing app/signing identity. Do not\n'
        'uninstall the app to work around an update without exporting your saves.\n\n'
        'NOT INCLUDED\nNo macOS/iOS binary, finished VR or new atmosphere system. The rolled-back\n'
        'independent online high-LOD experiment is not a supported release feature.\n'
        'No ROM, personal save, installed mod library or offline Player Guide is included.\n\n'
        'Build and static package checks are separate from owner gameplay, hardware\n'
        'and multiplayer acceptance. No unmeasured performance gains are promised.\n')

def replace_section(text, start, end, new):
    a = text.index(start); b = text.index(end, a)
    return text[:a]+new+'\n\n'+text[b:]

def apply():
    changes = {}
    def put(name, body):
        path = ROOT/name
        before = hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body.rstrip()+'\n', encoding='utf-8', newline='\n')
        changes[name] = {'before': before, 'after': hashlib.sha256(path.read_bytes()).hexdigest()}
    put('README.md', root_readme())
    put('docs/SAVES.md', SAVES)
    put('docs/LEGACY-MODS-BETA.md', MODS)
    put('docs/MAGIC_CODES.md', MAGIC)
    put('docs/LAUNCHER.md', LAUNCHER)
    notes = change_notes()
    put('docs/PUBLIC-1.0.7-NOTES.txt', notes)
    old = original('CHANGELOG.md')
    old = old.split('\n', 1)[1].lstrip()
    old = old.replace('## 1.0.7 — 15 October 2026', '## 1.0.7 initial release candidate — 15 October 2026 (historical)')
    put('CHANGELOG.md', '# DKR-R changelog\n\n## 1.0.7 — 20 October 2026\n\n'+
        '\n'.join(notes.splitlines()[5:]).split('\nUPDATING\n')[0]+'\n\n## Earlier development records\n\n'+old)
    graphics = original('docs/GRAPHICS.md')
    graphics = replace_section(graphics, 'The optional **Modern camera**', '## Vehicles and scenery',
        'The optional **ModernCamera++** code mod supplies camera presets, local\nprofiles and live F7 tuning. Import/approve it like other Custom Magic Codes.\nCamera presentation and the shadow-interpolation correction are separate from\nshared simulation. See [Camera++](../patches/camera-plus/README.md).\nVR is unfinished and not a supported feature of this release.')
    graphics = replace_section(graphics, 'Beta 5 makes maximum racer detail', 'Normal local 2/3/4-player',
        'The independent experimental online high-LOD/range work was rolled back\nafter graphics and performance regressions. Do not treat parity with offline\nas a qualified release feature. This pass preserves the accepted rendering\npaths/build switches; it does not rework those experiments. Water/reflection\npresentation remains local; shared procedural wave allocation is not\nindependently changed by a client.')
    graphics += '\n## v1.0.7 presentation corrections\n\nOriginal rivers/waterfalls interpolate with enhanced effects off. HD liquid\nframes promote together only when a recognised matching family is resident;\nan incomplete family remains consistently original. Pack reload/eviction\ninvalidates that decision. No extra water GPU pass was added for this fix.\n\nLogo scenes, moving Start Menu names and credits use the selected viewport.\nThe copyright/recompilation text has a translucent rounded backing. Credits\ninclude full-width fades and lower sky, with bounded native readback.\n'
    put('docs/GRAPHICS.md', graphics)
    online = original('docs/ONLINE_MULTIPLAYER.md')
    online = replace_section(online, 'Beta 5 adds local maximum vehicle detail', 'If Custom Trophy Races says',
        'Use the complete v1.0.7 release on all peers. The modern-track music,\nCustom Magic Code startup/Track Select and Big Donut/Moon Station resource\ncorrections are included. Verified matching installed data is reused.\nIndependent high-detail online model/range parity is not qualified in this\nrelease; this pass preserves the accepted graphics paths.')
    online = online.replace('See [beta 3 notes](BETA3-1.0.7.txt)', 'See [release notes](PUBLIC-1.0.7-NOTES.txt)')
    online += '\n## SDK 2 development and saves\n\nAPI 1 packages remain supported. API 2 development needs participant consent\nand host authority; agreed commands execute at confirmed boundaries. No\nunilateral live code reload is permitted online. Host online progress is\nseparate from personal Adventure and development saves. See [Saves](SAVES.md)\nand [Magic Codes](MAGIC_CODES.md).\n'
    put('docs/ONLINE_MULTIPLAYER.md', online)
    index = original('docs/README.md')
    index = index.replace('v1.0.7 beta 5', 'v1.0.7').replace('complete API 1 authoring book', 'complete API 1/API 2 authoring book')
    index = replace_section(index, '## Proposed code-mod API', '## Historical records',
        '## Current code-mod API\n\nThe implemented SDK 2.0 is additive to API 1. Use the\n[Modding Bible](../modding/MODDERS-GUIDE.txt) and exact SDK headers.\n[Modding architecture](MODDING_ARCHITECTURE.md) is the historical design,\nnot a statement that the current SDK is only a proposal.\n\nGame packages contain no offline Player Guide. These guides remain in source;\nthe separate SDK includes the Bible in TXT, HTML and PDF.')
    put('docs/README.md', index)
    platforms = original('docs/PLATFORMS.md')
    platforms = platforms.replace('(15 October 2026)', '(20 October 2026)').replace('APK, code 1070001', 'APK; see final release receipt for versionCode')
    put('docs/PLATFORMS.md', platforms)
    building = original('docs/BUILDING.md')
    a = building.index('For contributor checks')
    put('docs/BUILDING.md', '# Building DKR-R v1.0.7\n\nThe stable version is `1.0.7`. All delivered platforms must use the same\naccepted source/feature ledger. Start with the\n[release pipeline](../patches/release-prep/v1.0.7/BUILDING.txt) for the final\ncomposition and explicit prepared-input contract. Do not substitute an older\npackage or rename a beta executable.\n\n'+building[a:])
    # Keep detailed technical material and contributor commands; attach a
    # precise current contract rather than replacing their original evidence.
    addenda = {
        'docs/CUSTOM_TRACKS.md': 'Modern tracks have library enable/disable controls. Store ZIP imports accept one complete course inside wrapper folders, require declared music/HD resources and reject ambiguous multiple courses. Added-course records live outside stock EEPROM. Cup exports are definitions, not bundled course assets.',
        'docs/TEXTURE_PACKS.md': 'Store-installed packs retain friendly names and managed installation receipts. Reinstall/update targets the selected owned copy. Recognised liquid animation families stay original until all matching HD frames are resident; incomplete families remain consistently original. Pack order and normal Stream/Stall/Preload policy are retained.',
        'docs/TROUBLESHOOTING.md': 'Normal mods share one personal Adventure. Check portable/profile location and Backups & Recovery before restoring. Keep installation receipts/recovery folders. Export logs/support details; report the exact release, ROM revision, course, packs and settings. The accepted HD-water startup fix is included; a hardware-specific problem still needs its own report.',
        'docs/UPDATES.md': 'Manual stable upgrade is required from existing 1.0.7+beta builds: their reader ignores +metadata and compares them equal to 1.0.7. Do not weaken signature checks. Automatic-update metadata is only provided when signed by the existing approved key; the public key alone cannot sign a release. Android must retain the app ID/certificate and increase versionCode. Player packages do not include this offline guide.',
        'docs/HUD-WORKSHOP.md': 'Online My racer fullscreen retains corrected divider/sky and uses the offline-style HUD. Other human racers can show display name and position. Menus/shared scenes retain their layout. This is presentation only, not a change to input ownership or simulation.',
        'docs/ARCHITECTURE.md': 'The release composes Camera++, SDK 2, Store, shared saves and presentation fixes through the Patch Pipeline. API 2 is additive. Personal, confirmed-online and development save ownership are separate. Isolated dependency source patches never edit the checked-out submodules.',
        'docs/DEVELOPMENT.md': 'The final release recipe and prepared-input contract are in patches/release-prep/v1.0.7/BUILDING.txt. The SDK 2 Bible/public headers describe supported tools and API 1 compatibility. Gameplay/hardware acceptance is owner-led; compilation and static inspection are not runtime acceptance.',
        'docs/F3DDKR.md': 'The accepted liquid gate freezes descriptor/dimension/filter selection per renderer workload. It does not change shader passes, guest state or GPU retirement. Renderer/runtime patches are applied only to isolated build copies; protected dependency checkouts remain untouched.',
        'docs/RENDER_SNAPSHOT_ARCHITECTURE.md': 'SDK 2 local tools/camera and coherent HD-liquid selection preserve copied presentation ownership. No independent online terrain/wave allocation or rolled-back high-LOD experiment is advertised. Checkpoint/save persistence stays confirmed-only.',
        'runtime-recomp/README.md': 'Current release: v1.0.7. The complete accepted build uses the release Patch Pipeline, not this base target alone. See ../patches/release-prep/v1.0.7/BUILDING.txt. SDK 2 is additive to API 1; Store, shared personal saves and development isolation are included. Do not edit dependencies or generated guest functions.',
        'scripts/README.md': 'Use patches/release-prep/v1.0.7/release.py and BUILDING.txt for stable composition. Packaging uses explicit runtime/legal allowlists; no offline Player Guide or old playtest handoff notes. Both US revisions, all owned payloads and exact staged inputs are required. Never invoke runtime checks when acceptance is reserved for the owner.',
    }
    for name, text in addenda.items():
        old = original(name)
        heading, _, body = old.partition('\n')
        put(name, heading+'\n\n## v1.0.7 release contract\n\n'+text+'\n\n'+body.lstrip())
    put('VERSION', '1.0.7')
    readme = '# DKR-R v1.0.7\n\nExtract the entire Windows folder and open DKR-R.exe. Keep assets and libexec\nwith it. Linux/Steam Deck: make the AppImage executable and launch it. Android:\ninstall over the existing compatible signed app; back up saves before updating.\n\nBring your own supported US v1.0 or US Rev A/v1.1 ROM. No ROM is included.\nNormal mods use one personal Adventure. Online and development progress stay\nseparate. Older mod-set saves remain available in Backups & Recovery.\n\nRead RELEASE-NOTES.txt for the changes. Guides and support:\nhttps://github.com/ThatGuyMcd/DKR-R\nhttps://discord.com/invite/AMWfXdBjNP\n\nUse a manual upgrade from the old 1.0.7+beta builds. Keep all peers on the\nsame compatible release for Online. SDK 2/Bible/examples are separate downloads.\nNo offline Player Guide is included. Keep all required licence notices.\n'
    put('packaging/RELEASE-README.md', readme)
    (AUTH/'DOCUMENTATION-APPLIED.json').write_text(json.dumps(changes, indent=2)+'\n')
    print('Applied release documentation through Patch Pipeline:', len(changes), 'files')

if __name__ == '__main__': apply()
