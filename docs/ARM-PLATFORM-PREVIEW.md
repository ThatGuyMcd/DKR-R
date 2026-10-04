# ARM platform preview — 1.0.5 beta 11

This is an initial hardware-qualification port, not a replacement for the
validated Windows/x86-64 Linux releases. No ROM, save, profile, mod bank or
signing key is included. The existing desktop packages remain the rollback.

## Platforms

| Package | Baseline | What has not been established |
| --- | --- | --- |
| Linux aarch64 AppImage | 64-bit ARM Linux, glibc 2.38+, compatible Vulkan driver; built on Ubuntu 24.04 | Real ARM GPU, controller, display-server and performance qualification |
| Android arm64 preview APK | Android 9/API 28+, 64-bit OS, Vulkan 1.2 driver; preview.5 adds touch controls | Adreno/Mali qualification, touch/controller device testing, lifecycle/surface recreation, sustained performance and cross-platform online determinism |

A 64-bit CPU alone is insufficient: some handhelds use a 32-bit Android OS,
or ship GPU drivers missing renderer capabilities. Vulkan version alone does
not guarantee the required features. 32-bit ARM and Android x86 are not built.

The APK is debug-signed for private testing, has a separate application ID
(`io.github.thatguymcd.dkrr.preview`), and stores its files in Android's private
app directory. It is not a signed public release. Uninstalling it removes its
private files; export saves first.

## Implemented boundaries

- Cross-compilation toolchain and multiarch container for Linux ARM64.
- Architecture-checked AppImage packaging. The cross-host `ldd` helper asks
  the ARM loader for dependencies rather than silently calling an ARM binary
  static. The optional AppImage runner is only for the x86 build machine.
- Android SDL Activity and shared-library entry point, with project-owned
  native-window and Storage Access Framework adapters.
- Android-only selection of RT64's window implementation and host shader
  tools, without editing RT64, plume, SDL, N64Recomp or N64ModernRuntime.
- Android launcher uses SDL's accelerated renderer; the existing window
  handoff recreates a Vulkan-capable window before RT64 starts. Its native
  window lookup uses the current window, not a stale hard-coded SDL ID.
- Android app assets are staged into private storage before the native entry
  point. Individual ROM/ZIP/patch imports use the system picker, unique staging
  directories and a 1 GiB copy limit, with no broad-storage permission.
- Save export uses Android's document destination and reports copy failures.
- Mod importer remains an isolated executable installed in Android's native
  library directory, not executable content downloaded to writable storage.
- NDK r29 supplies the standard threading support. Android snapshot
  publication uses standard atomic shared-pointer free functions where libc++
  lacks `atomic<shared_ptr>`. Desktop builds retain the original specialization.
- Android's 128-bit filesystem timestamps no longer rely on unavailable stream
  operators. ROM cache stamps reject out-of-range values; texture cache keys
  retain all timestamp digits.
- Native Android binaries use 16 KiB segment alignment.

## Explicit preview limitations

- No physical Android or ARM Linux device was connected during this build.
  Compiling/signing is not evidence that a particular handheld renders correctly.
- Android uses the in-process SDL2 input backend. The desktop SDL3 helper is not
  packaged for Android. Do not assume handheld gyro/rumble parity yet.
- Android directory-picking is explicitly unavailable in this preview. Use
  file/ZIP imports; no folder URI is misleadingly treated as an ordinary path.
- Preview.5 adds density-aware mobile navigation, finger scrolling and editable
  touchscreen racing controls. See `ANDROID-MOBILE-UI.md`. Launcher touch/mouse
  input and controller navigation still require device verification.
- Android background/resume, screen lock, interruption during a file picker,
  returning to the launcher and the desktop-oriented Restart action are not
  qualified. Close/reopen from Android when a full restart is needed.
- Online behavior across different CPU architectures is not qualified by the
  desktop tests. Test local/offline play before multiplayer testing.

## Build reproduction

All builds use existing patch-pipeline-generated v77/v80 sources. Do not edit
`RecompiledFuncs`, `RecompiledPatches`, or submodule contents. Use separate
build/output directories; package scripts refuse to overwrite a prior package.

Linux cross tools:

```sh
docker build -t dkr-arm64-builder packaging/linux/arm64
# Mount the repository and generated pipeline read-only, /work read-write.
cmake -S /repo/runtime-recomp -B /work/linux-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/repo/runtime-recomp/cmake/LinuxArm64.cmake \
  -DCMAKE_BUILD_TYPE=Release -DDKR_RUNTIME_BUILD_RT64=ON \
  -DDKR_LEGACY_QUALIFICATION=OFF -DNFD_PORTAL=ON -DSDL_X11_XTEST=OFF \
  -DDKR_GENERATED_SOURCE_V77=/pipeline/generated-v77 \
  -DDKR_GENERATED_SOURCE_V80=/pipeline/generated-v80
cmake --build /work/linux-arm64 -j8
ctest --test-dir /work/linux-arm64 -R '^DKR' --output-on-failure -j1
```

Package with `scripts/Package-Linux-AppImage.sh`, `DKR_LINUX_ARCH=aarch64`,
`DKR_LINUX_BUILD_DIR`, a fresh `DKR_APPDIR` and `DKR_APPIMAGE_OUTPUT`, and ARM64
linuxdeploy/plugin binaries. On an x86 multiarch host, put
`packaging/linux/arm64` at the front of PATH for dependency inspection, set
`READELF=aarch64-linux-gnu-readelf` and
`DKR_APPIMAGE_RUNNER=/repo/scripts/Run-Arm64-AppImage.sh`. Extract the ARM64
AppImage plugin with QEMU and point `LINUXDEPLOY_PLUGIN_APPIMAGE` at its ordinary
`usr/bin/linuxdeploy-plugin-appimage` executable; leave its bundled appimagetool
tree intact. Keep the original plugin AppImage outside linuxdeploy's plugin
search directory. Native ARM hosts do not need these emulator workarounds.

Android native configure inputs (use a separate build tree):

```text
-G Ninja
-DCMAKE_TOOLCHAIN_FILE=<NDK-r29>/build/cmake/android.toolchain.cmake
-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28
-DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
-DDKR_RUNTIME_BUILD_RT64=ON -DBUILD_TESTING=OFF
-DDKR_ANDROID_SDL2_SOURCE=<unmodified SDL2-2.32.10 source>
-DDKR_HOST_FILE_TO_C=<host-native RT64 file_to_c executable>
-DDKR_GENERATED_SOURCE_V77=<pipeline>/generated-v77
-DDKR_GENERATED_SOURCE_V80=<pipeline>/generated-v80
```

Build target `DKRPortGame`. On Windows, package with
`scripts/Package-Android-Preview.ps1`, supplying BuildDirectory, SdlSource,
NdkDirectory, a fresh StageDirectory, Gradle and OutputDirectory. Set JAVA_HOME
to a working JDK and ANDROID_HOME to the SDK. Qualification used CMake 3.31.6,
NDK 29.0.14206865, Gradle 8.14.3, AGP 8.11.0 and compile SDK 35.

Source/tool provenance for this run:

- SDL2 2.32.10 from libsdl.org; archive SHA-256
  `5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165`.
- NDK r29 Windows from Google's Android repository; SHA-1
  `ab3bb30fbb9e6903666d60c55d11e78b04e07472`.
- linuxdeploy ARM64 continuous snapshot (07333c6), SHA-256
  `556ab80baa98e600aa80f0dcedfb70bca0e1ce7e9f147fb345be3fcc3e91b2b1`.
- linuxdeploy AppImage plugin ARM64 snapshot, SHA-256
  `ce574719bcf9cc1fb12728d60b17e48cc87d9b6c40f6f48b04cff7d273b5eb24`.

## Device test gate

1. Start without a ROM: launcher appears, controller can navigate, no fullscreen
   black screen. Verify native resolution and sane frame pacing.
2. Import a personally supplied supported ROM through the system picker; test
   cancellation and retry. Confirm the ROM dropdown retains both revisions.
3. Launch each revision. Verify audio, shaders, input, HUD and save/reload.
4. Test a local race for at least ten minutes, then return to the launcher.
5. Export a save, verify the chosen destination, restart and re-import it.
6. Test ZIP texture/mod imports, cancel while loading, repeat launches and check
   storage use. Folder imports should explain the preview limitation.
7. Test background/resume, display rotation policy, lock/unlock and controller
   reconnect. Capture Android logcat and runtime.log for any failure.
8. Only then test online host/client, scene transitions and cross-architecture
   state consistency. Do not label it release-ready until these gates pass.

For Android failures, `adb logcat -d` captures Java/native loader failures that
can happen before runtime.log exists. No ROM, private profile or friend-code
files are needed in a bug report.
