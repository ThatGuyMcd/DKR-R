# DKR-R v1.0.14 platforms

Keep each complete package together, including helpers, assets and notices.
The [release validation record](../RELEASE-VALIDATION.md) records what was
actually built, tested under emulation and checked on hardware.

| Platform | Release download / status | Requirements and scope |
| --- | --- | --- |
| Windows x64 | `DKR-R-1.0.14-Windows-x64.zip` | Windows 10 or newer with a compatible graphics driver. Extract the full ZIP and use its top-level launcher. |
| Linux x86_64 | `DKR-R-1.0.14-Linux-x86_64.AppImage` | Compatible Vulkan driver and the recorded glibc/C++ runtime floors. Make the AppImage executable. |
| Steam Deck | `DKR-R-1.0.14-SteamDeck-x86_64.AppImage` | Identical application payload to Linux x86_64. Test Gaming Mode and Desktop Mode separately. |
| Linux ARM64 | `DKR-R-1.0.14-Linux-aarch64.AppImage` | 64-bit ARM Linux, Vulkan and the recorded system-library floors. QEMU CPU tests do not qualify a physical GPU. |
| macOS Apple Silicon | No v1.0.14 download | Source/host checks remain useful; the integrated Rollback backend is gated on Apple. Older signed/notarised releases retain their original scope. |
| macOS Intel | No v1.0.14 download | Host checks are separate from a complete game build and public packaging qualification. |
| iOS ARM64 | No v1.0.14 download | Device-adapter compilation is separate from a signed, runnable and device-tested IPA. |
| Android ARM64 | No v1.0.14 download | Current Android releases remain unsupported because performance is not good enough across devices. Historical APKs keep their original version/signing identity. |
| Windows ARM64 / 32-bit targets | No qualified v1.0.14 download | A recognised updater platform name does not establish a supported game build. |

An AppImage does not replace the system graphics driver, glibc or C++ runtime.
ELF inspection of both final v1.0.14 Linux packages confirms glibc 2.38 and
GLIBCXX_3.4.32 as the required floors, with no missing non-system dependencies. Older
distributions may need a compatible source build. Required non-system linked
libraries are bundled; the desktop/audio/font stack must still be available.

Linux requests launcher GPU acceleration automatically. If it fails, a logged
software fallback remains. The log identifies the SDL renderer and, for OpenGL,
the rasterizer. llvmpipe/softpipe indicates software graphics; it is not evidence
of Steam Deck AMD hardware acceleration. See [launcher performance](TROUBLESHOOTING.md#linux-and-steam-deck-launcher-performance).

Both supported US ROM revisions are linked into the application. Online still
checks architecture, build, floating-point mode, ROM and session content. Two
downloads for different architectures are not a promise of unrestricted cross-play.

Existing Android/Apple users do not need to uninstall their application or delete
saves. Excluded platforms must not receive a differently labelled desktop package
through the updater. See [Building](BUILDING.md) and [Updates](UPDATES.md).
