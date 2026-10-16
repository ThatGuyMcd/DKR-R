# DKR-R platform support

We're keeping Windows and Linux together. Linux releases must include an
AppImage, not just a loose executable. Android and Linux ARM64 have separate
ports, and macOS now has a source/build workflow.

1.0.7 release packaging targets (15 October 2026):

| Platform | Current delivery | Important limits |
|---|---|---|
| Windows x64 | Public ZIP | Use the whole package, including private workers. |
| Linux x86_64 | Public AppImage | Requires glibc 2.38+ and a working compatible Vulkan driver. |
| Linux ARM64 | ARM64 AppImage | Needs glibc 2.38+, compatible 64-bit Linux/Vulkan and physical-device qualification. Cross-build and QEMU checks are not a physical GPU test. |
| Steam Deck | Linux x86_64 AppImage payload | A separate named artifact, not a different engine build. SteamOS/controller integration needs a physical-device check. |
| Android ARM64 | APK, code 1070001 | Android 9+, ARM64 OS and compatible Vulkan features are required. Startup, driver behaviour and sustained performance vary; desktop parity is not established. Existing app ID/signature are retained. |
| macOS | Source/build-kit support | Requires an Apple build host. No newly qualified macOS binary is included in this delivery; integrated Experimental Rollback is currently gated out on Apple. |
| iOS | Merged sideload source port | Contributor validation is recorded separately. No newly built IPA is supplied in this pass. |

Both clean US v1.0 and US Rev A/v1.1 ROMs are supported by the native revision
selection. Owning a 64-bit CPU is not enough if the OS or GPU driver lacks the
required features. There is no 32-bit ARM or Android x86 package in this workflow.

These Linux packages were built with a glibc 2.38 baseline. An AppImage does
not replace the system's glibc or Vulkan driver; an older distribution needs
an appropriate source build rather than a renamed package.

## Android and handhelds

The mobile UI uses a Menu drawer, density-aware text and finger scrolling.
**Controls → Touch Screen** offers editable N64-style touch groups, including
position, size, opacity, visibility and left-handed placement. Menu remains
available when racing controls are hidden. Android uses in-process SDL2 input,
not the desktop SDL3 helper.

Install compatible updates over the existing app where possible. Uninstalling
removes Android's private app data, so export saves first. Use file/ZIP imports
when folder picking is unavailable. APK compilation alone does not certify
Adreno, Mali or a particular handheld.

For a black screen, crash or slowdown, record the device, Android version,
build/version code and scene. Reopen the app and use
**About DKR-R → Support summary → Export Android Startup Logs** if available.
Do not keep retrying a diagnostic action that itself crashes.
The [troubleshooting guide](TROUBLESHOOTING.md) explains what to send.

Android's integrated rollback port does not establish cross-architecture
netplay compatibility: admission still checks architecture and floating-point
mode. Backgrounding Android ends an active experimental match rather than
leaving other racers waiting on a suspended app.

## Building and qualification

Follow [Building DKR-R](BUILDING.md). The older
[ARM build recipes](ARM-PLATFORM-PREVIEW.md) and
[mobile UI implementation report](ANDROID-MOBILE-UI.md) retain their original
preview results; check current toolchain settings before reproducing them.

A platform is not qualified just because it compiles. Check ROM import, both
revisions, rendering, audio, controller/touch input, save reload, return to the
launcher and background/resume on actual hardware. Online testing comes after
ordinary play works. [Release validation](../RELEASE-VALIDATION.md) distinguishes
automated checks from hardware/gameplay acceptance.
