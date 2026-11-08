DKR-R v1.0.7 beta 7 playtest - Dev Menu Startup Fix
Modding SDK 2.0 / DKR-R Dev Menu

Windows: extract the full ZIP, then launch DKR-R.exe. Keep libexec and assets
beside it. The runtime/helpers are private application files, not extra launch
shortcuts. Do not copy only the EXE out of the folder.

Linux: make the AppImage executable, then launch it. Steam Deck can use the
Linux x86_64 AppImage or the unpacked Steam Deck kit's START-DKR-R.sh. Add that
launcher as a non-Steam game if you want it in Gaming Mode. Linux ARM64 needs
an ARM64 Linux machine with the required graphics support; it is not Android.

Android: install the arm64 APK using the existing application identity. Its
version code is 1070017. Back up important data before changing playtest builds.
macOS is not included, as requested.

Supply your own supported, legally obtained Game Pak. No ROMs or player saves
are included. Existing mods remain optional. This does not enable every sample
or change your installed mod selection automatically.

To try the Dev Menu, import DKR-R-Dev-Menu.nrm in MODS / HACKS. Open its Details,
review/approve the code, enable it and start a game. Open Custom Magic Code
Tools from the overlay or press F2. Tools has shortcut settings, including an
optional Back/Select + Y controller chord. Only approve code you trust.

This app fixes the development-save startup stop and includes Dev Menu 2.0.1,
which fixes the original package's import failure. Keep the frozen development
save route; do not disable save protection or change an ordinary save to use it.
Everyone playing online needs this same Startup Fix application edition.

For online tools use matching beta 7 builds on every peer, Auto/Experimental
Rollback and Development lobby enabled before hosting. All racers consent
before readying. Clients request shared actions; the host approves or grants
authority. Local camera/debug display remains local. Development progress is
isolated/temporary; do not use the Dev Menu for an ordinary progression run.

The SDK ZIP has the complete Dev Menu source and Modding Bible in TXT, HTML
and PDF. The PDF has linked contents and was rendered/reviewed. The guide
explains both APIs, settings, behaviour/hooks, every public header, online
commands, source maps, reload rules and the supported/protected tool limits.

Please start with one code mod and an ordinary course, then check your own
installed set. Useful playtest coverage: ordinary save unchanged; scene return;
stale inspector target; edit/undo; pause/step/resume; CPU freeze/reset; clone/
delete; checkpoint movement; document import rejection; camera handoff and
shadows; host/client approval; an online scene change and a completed race.
Report the app version, ROM revision, package edition/digest and first useful
runtime error. Keep the matching logs and crash dump if a failure occurs.

Compilation, source/ABI and package inspections are complete only where the
build receipt says so. No game, mod, unit test, replay, headset or multiplayer
session was executed by the build agent. Runtime acceptance belongs to you.
This is a playtest, not a claim of bug-free or unrestricted live editing.
