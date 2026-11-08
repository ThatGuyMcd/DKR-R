"""Release documentation authored here, applied against the verified snapshot."""
from pathlib import Path
AUTH=Path(__file__).resolve().parent

def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Document anchor changed: '+old[:100])
    return text.replace(old,new,1)

def section(text,start,end,new):
    a,b=text.index(start),text.index(end)
    return text[:a]+new+text[b:]

def build(z):
    def read(n):return z.read(n).decode('utf-8').replace('\r\n','\n')
    result={'VERSION':'1.0.10\n'}
    current=('docs/README.md','docs/BUILDING.md','docs/DEVELOPMENT.md','docs/ONLINE_MULTIPLAYER.md',
        'docs/PUBLIC_LOBBIES.md','docs/TROUBLESHOOTING.md','docs/LAUNCHER.md','docs/SAVES.md',
        'docs/GRAPHICS.md','docs/SOUND.md','docs/CONTROLLERS.md','docs/ROM_SETUP.md',
        'docs/TEXTURE_PACKS.md','docs/LEGACY-MODS-BETA.md','docs/MAGIC_CODES.md','docs/VEHICLES.md',
        'docs/ACHIEVEMENTS.md','docs/MOD_LIBRARY_ADMIN.md','docs/ARCHITECTURE.md',
        'docs/CUSTOM_TRACKS.md','docs/F3DDKR.md','docs/HUD-WORKSHOP.md','docs/RENDER_SNAPSHOT_ARCHITECTURE.md',
        'runtime-recomp/README.md')
    for n in current:result[n]=read(n).replace('1.0.9','1.0.10')
    text=read('README.md').replace('1.0.9','1.0.10')
    text=section(text,'Public lobbies now live','DKR-R is a native',
        "This update is mainly about getting Online into a much better state. I've\n"
        "fixed problems joining modded lobbies, races getting stuck at the start,\n"
        "and spectators getting a black screen during custom races. I've also\n"
        "tidied up Open Lobbies while keeping the current launcher design.\n\n"
        "Public lobbies, custom cars, achievements, texture tools and the existing\n"
        "modding features are all still here. There is no need to reset your saves\n"
        "or reinstall your mods.\n\n")
    text=once(text,'4. Android: back up your saves, then install the compatible signed APK over\n   your existing app. Uninstalling can erase app data.\n5. Add your original ROM',
        '4. Add your original ROM')
    text=section(text,'The v1.0.10 build set contains','Accurate keeps',
        'v1.0.10 is available for Windows x64, Linux x86_64, Linux ARM64 and Steam\n'
        'Deck. Both Linux architectures have AppImages. Linux needs glibc 2.38,\n'
        'GLIBCXX_3.4.32 and a compatible Vulkan driver.\n\n'
        "Android releases are no longer supported. I'm leaving Android out until\n"
        'performance is good enough. There are no new macOS or iOS downloads in\n'
        'this release. See [platform support](docs/PLATFORMS.md) for the details.\n\n')
    result['README.md']=text
    notes=(AUTH/'RELEASE-NOTES.txt').read_text(encoding='utf-8')
    result['docs/PUBLIC-1.0.10-NOTES.txt']=notes
    result['docs/DKR-R-v1.0.10-Changelog.txt']=notes
    changelog=read('CHANGELOG.md');title=changelog.split('\n',1)[0]+'\n'
    result['CHANGELOG.md']=once(changelog,title,
        title+'\n## 1.0.10 — 25 October 2026\n\n'+
        "This update is mainly about Online stability. I've kept the current launcher\n"
        "design and brought the accepted playtest.4 fixes into the release.\n\n"
        '- Fixed modded lobby preparation getting stuck and improved verified cache reuse.\n'
        '- Added clearer join progress, Retry Join and Copy Join Diagnostics.\n'
        '- Fixed races failing to start when spectators were in the lobby.\n'
        '- Fixed spectators getting a black screen and no audio during custom races.\n'
        '- Fixed matching downloaded mods conflicting with existing offline copies.\n'
        '- Tidied up Open Lobbies, keeping more space for the lobby list.\n'
        '- Kept the earlier Track Select custom-music fix and existing online safeguards.\n'
        '- Updated the guides, Windows/Linux packages and signed updater metadata.\n\n'
        'Windows x64, Linux x86_64/ARM64 and Steam Deck are included. Android is no\n'
        'longer supported; no new macOS/iOS downloads are included. Use matching\n'
        'complete v1.0.10 packages for Online. Saves and installed mods can stay.\n\n')
    text=result['docs/README.md']
    text=once(text,'[v1.0.10 release notes](PUBLIC-1.0.10-NOTES.txt), [v1.0.8 notes]',
        '[v1.0.10 release notes](PUBLIC-1.0.10-NOTES.txt), [v1.0.9 notes](PUBLIC-1.0.9-NOTES.txt), [v1.0.8 notes]')
    text=once(text,'The v1.0.10 build set contains Windows x64, Linux x86_64/ARM64 AppImages,\nSteam Deck and Android ARM64. Published v1.0.8 macOS/iOS packages exist; new\nApple packages were not built in this pass.',
        'v1.0.10 includes Windows x64, Linux x86_64/ARM64 AppImages and Steam Deck.\nAndroid releases are no longer supported. macOS and iOS are skipped in this\nrelease; their older downloads keep their own version numbers.')
    text=text.replace('v1.0.10 packages, Linux ABI, Android identity and separate Apple release coverage.',
        'v1.0.10 packages, Linux requirements and excluded platforms.')
    result['docs/README.md']=text
    text=result['docs/BUILDING.md']
    text=text.replace('Windows/Linux/ARM64/Android','Windows/Linux/ARM64').replace('Windows/Linux/Android Public Build','Windows/Linux Public Build')
    text=text.replace('Older ARM/Android caches','Older ARM caches')
    text=section(text,'## Linux ARM64 and Android','## Custom Magic Codes SDK',
        '## Linux ARM64\n\n'
        'Use the reviewed Linux ARM64 toolchain/sysroot and separate compiled\n'
        'outputs. Compose both normal and owned payloads above, with matching\n'
        'visual-model inputs, then configure through the v1.0.10 release overlay.\n'
        'The older [ARM recipes](ARM-PLATFORM-PREVIEW.md) describe the initial\n'
        'cross-build setup; their preview versions and results stay historical.\n\n'
        'The release package.py uses --arch aarch64, matching ARM64 libraries and\n'
        'an inspected ARM64 AppImage outer runtime. Linux ARM64 includes the\n'
        'desktop runtime and its input/import/updater helpers. Steam Deck uses\n'
        'Linux x86_64. QEMU checks do not replace physical GPU/audio/input tests.\n\n'
        '## Android source work\n\n'
        'Android releases are no longer supported and there is no v1.0.10 APK.\n'
        'The new release overlay rejects Android. Historical source recipes and\n'
        'the v1.0.9 Android adapter remain available for development, with their\n'
        'original application identity, signing and version-code requirements.\n'
        'Do not change those numbers to claim a new tested release.\n\n')
    text=once(text,'1.0.10 and code 1090001 while retaining the startup correction.',
        '1.0.10 for Windows/Linux while retaining the startup correction. Android\nrelease metadata is not changed by this release.')
    text=once(text,'Release runtime/gameplay acceptance is owner-led. Do not launch a game, mod,\ntest executable or AppImage while preparing these packages. The native build\nand static audits are distinct from acceptance on physical hardware.',
        'Release gameplay acceptance is owner-led. Do not launch a game, user ROM,\nRSP or AppImage during package preparation. The explicitly named synthetic\nregression fixtures and ROM-free updater/worker checks are recorded separately\nfrom gameplay and acceptance on physical hardware.')
    text=once(text,'Build only the runtime/helper targets; do not invoke CTest or\nthe executables.',
        'Build the runtime/helpers and named ROM-free\nregression targets. Run only the selected synthetic/updater checks; do not\nrun a whole game/ROM qualification suite as a packaging side effect.')
    text=once(text,'and its `ReleaseOverlay.cmake`. It composes the accepted Online directory,',
        'and its `ReleaseOverlay.cmake`. This includes the accepted playtest.4 fixes\nfor mod joining, offline copies, race starts and spectator music, plus the Online directory,')
    result['docs/BUILDING.md']=text
    text=result['docs/ONLINE_MULTIPLAYER.md']
    text=once(text,'## Host a lobby',
        '## Joining with mods\n\n'
        'The Session mods dialog shows what is happening, how long it has taken,\n'
        'and how much data was downloaded or reused. A busy host can queue your\n'
        'request. Temporary transfers can be repaired up to five times; if a join\n'
        'fails, use **Retry Join** or **Copy Join Diagnostics**. Verified completed\n'
        'downloads can be reused when you retry. A first join with lots of mods\n'
        'can still take longer while they are downloaded and prepared.\n\n'
        'You can leave **Keep downloaded mods for offline play** enabled. Matching\n'
        'installed copies are reused, and missing selected content is kept as\n'
        'inactive entries after lobby admission. Your existing names, enabled\n'
        'mods and saves stay as they are. A real content conflict is reported\n'
        'without replacing the existing files or stopping lobby entry.\n\n'
        'In Open Lobbies, **Join as** and **Browse lobbies** sit under your account\n'
        'on wide windows. **Before you race** sits below **Your online view**. On\n'
        'narrow windows, the lobby filters remain above the list.\n\n## Host a lobby')
    result['docs/ONLINE_MULTIPLAYER.md']=text
    text=read('docs/SPECTATORS.md')
    text=once(text,'## Swap a racing place',
        '## Custom tracks and music\n\n'
        'v1.0.10 includes the playtest.4 spectator fix. A spectator now prepares\n'
        "the selected track's verified music when joining the confirmed race\n"
        'state. This fixes the black screen and missing audio that could last\n'
        'until the results screen on a fresh spectator machine.\n\n'
        'A spectator that fails to recover five times without making playback\n'
        'progress stops with the actual reason. The racers carry on. If it still\n'
        "happens, send the spectator's runtime.log and the host log from the same\n"
        'race, including the track and mod selection. Changing cameras does not\n'
        'repair a checkpoint or missing music resource.\n\n## Swap a racing place')
    result['docs/SPECTATORS.md']=text
    text=result['docs/TROUBLESHOOTING.md']
    text=once(text,'## ROM rejected',
        '## Modded joins, race starts and spectator black screens\n\n'
        'Check that everyone has the complete v1.0.10 package first. This release\n'
        'includes the accepted playtest.4 fixes. Keep the downloaded offline-mod\n'
        'option enabled if you want those copies; matching installed content no\n'
        'longer causes the old identity-conflict warning. Genuine conflicts and\n'
        'damaged files are still reported without replacing your offline mods.\n\n'
        'If joining fails, use **Retry Join** and **Copy Join Diagnostics** in\n'
        'Session mods. Send the full host and affected client/spectator logs from\n'
        'the same attempt. Include the track, enabled mods, role and whether the\n'
        'problem happened on first join, race start or a later track change.\n'
        "Please don't delete your whole mod library or turn off safeguards.\n\n"
        'A spectator error after five failed recoveries includes the reason. The\n'
        'log also records `[spectator][music]`, `[spectator][admission]` and\n'
        '`[spectator][playback]`. A race-start stall needs both racing logs too.\n\n## ROM rejected')
    result['docs/TROUBLESHOOTING.md']=text
    for filename in ('PLATFORMS.md','UPDATES.md','RELEASE-README.md','RELEASE-VALIDATION.md','DOCUMENTATION-1.0.10.md'):
        target={'RELEASE-README.md':'packaging/RELEASE-README.md','RELEASE-VALIDATION.md':'RELEASE-VALIDATION.md'}.get(filename,'docs/'+filename)
        result[target]=(AUTH/'pages'/filename).read_text(encoding='utf-8')
    result['docs/history/RELEASE-VALIDATION-1.0.9.md']=read('RELEASE-VALIDATION.md')
    text=read('packaging/MACOS-BUILD-README.md')
    text=once(text,'No new Apple package is part of the Windows/Linux/Android v1.0.9 build set.',
        'macOS and iOS are skipped in the v1.0.10 Windows/Linux release set.')
    text=text.replace('v1.0.9','v1.0.10')
    result['packaging/MACOS-BUILD-README.md']=text
    text=read('packaging/linux/dkr-port.appdata.xml')
    result['packaging/linux/dkr-port.appdata.xml']=once(text,'  <releases>',
        '  <releases>\n    <release version="1.0.10" date="2026-10-25"><description><p>Modded lobby, race start and spectator fixes, offline mod copies and a tidier Open Lobbies screen.</p></description></release>')
    result['.gitignore']=read('.gitignore')+'\n# Local Cloudflare development state\n/.wrangler/\n'
    return result
