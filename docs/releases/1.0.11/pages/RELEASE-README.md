# DKR-R v1.0.11 Release Build

This update focuses on the reported memory issues and missing HUD elements.
I've also worked on queued graphics data and water textures, and added support
for Syeo's updated custom cars. The launcher design and Online fixes stay.

Windows: extract the whole ZIP and open DKR-R.exe. Keep assets and libexec
with it. Linux and Steam Deck: make the AppImage executable and launch it.
Linux needs glibc 2.38, GLIBCXX_3.4.32 and a compatible Vulkan driver.

Bring your own supported US v1.0 or US Rev A/v1.1 ROM. No ROM is included.
Your saves, mods and settings can stay. Everyone in an Online lobby should
use the complete v1.0.11 package. Read RELEASE-NOTES.txt for the changes.

Windows x64, Linux x86_64/ARM64 and Steam Deck are included. Android, macOS
and iOS are skipped. Updated cars keep their original artwork; enabled custom
boost anchors are not supported yet. Incomplete HD water animations can stay
vanilla until the pack supplies every frame.

Downloads and support:
https://github.com/ThatGuyMcd/DKR-R
https://discord.com/invite/AMWfXdBjNP

Auto updates need both DKR-R-update.json and DKR-R-update.sig, signed with
the existing release key. Do not alter packages or updater files after signing.
The SDK and Modding Bible remain separate downloads. Keep the licence notices.
