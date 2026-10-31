# DKR-R v1.0.12 platforms

This release includes Windows, Linux and Steam Deck. Both Linux architectures
have AppImages. Keep the full package together, including its helpers and notices.

| Platform | Download | Requirements |
|---|---|---|
| Windows x64 | `DKR-R-1.0.12-Windows-x64.zip` | Windows 10 or newer and a compatible Vulkan driver. Extract the whole ZIP. |
| Linux x86_64 | `DKR-R-1.0.12-Linux-x86_64.AppImage` | glibc 2.38, GLIBCXX_3.4.32 and a compatible Vulkan driver. |
| Linux ARM64 | `DKR-R-1.0.12-Linux-aarch64.AppImage` | 64-bit Linux and Vulkan, with the same library requirements. Physical GPU testing remains separate. |
| Steam Deck | `DKR-R-1.0.12-SteamDeck-x86_64.AppImage` | The same payload as Linux x86_64. Device, controller and gyro checks need a Deck. |
| Android | No v1.0.12 build | Android releases are no longer supported because performance is not good enough across devices. |
| macOS / iOS | No v1.0.12 build | Skipped for this release. Earlier Apple downloads keep their original version numbers. |

Both supported US ROM revisions remain available: v1.0 and Rev A/v1.1. Online
still checks architecture, build and floating-point compatibility. Having a
download for two architectures does not mean every combination can race together.

An AppImage does not replace the system's glibc, C++ runtime or graphics driver.
Older distributions need a compatible source build. Required non-system linked
libraries are included; the normal desktop, audio and font stack must be present.

Apple host/device compile checks are still useful for source development. They
do not produce qualified v1.0.12 downloads, and the current integrated Rollback
backend remains gated off on Apple. Old packages must not be renamed or put in
the v1.0.12 update manifest.

Existing Android installations and source instructions remain available. Skipping
Android releases does not require deleting saves or uninstalling the old app.
The historical application ID and signing requirements remain relevant to any
future compatible Android build.

See [Building](BUILDING.md) and [release validation](../RELEASE-VALIDATION.md).
Compilation and automated checks are recorded separately from actual rendering,
controller, audio, Steam Deck and online playtesting.
