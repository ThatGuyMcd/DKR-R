# ARM preview validation — 2026-09-21

Source baseline: `ab141cb` plus the uncommitted project-owned ARM adapters.
Version: `1.0.5-beta.11`. No protected dependency or generated-source files
were edited. Existing desktop release packages were not replaced.

Artifacts and logs are in `G:/DiddykongWorkFolder/arm-port-20260921`.
The initial preview is retained in `packages/final` as a rollback.
The corrected handoff is `packages/android-preview2`; other staging directories
are intermediate build attempts.

## Results

- Linux ARM64 native executable, input helper, mod helper and SDL3: built.
- Existing DKR tests under QEMU: **74/74 passed on the serial validation run**.
- Atomic snapshot native and Android-compatible implementations under QEMU:
  **2/2 passed**, including concurrent publication and retained ownership.
- First parallel QEMU run: 73/74. `DKRDirectSession` failed the realtime-attempt
  count assertion at test line 1302. An immediate isolated retry also failed;
  a later diagnostic run and the subsequent unmodified serial suite passed.
  This timing-sensitive failure is recorded, not silently counted as a pass.
  It has not been reproduced/diagnosed on physical ARM hardware. No network
  timing limits or assertions were weakened to pass the port.
- Packaged AppImage: mod-worker self-test, SDL3 self-test, save-accessory test
  and two live input-backend switching round trips passed under emulation.
- AppImage staging: prohibited game-data and ROM-header scan passed.
- Android native library and isolated importer: built with NDK r29.
- Android Java/manifest/DEX/APK: assembled successfully.
- APK signature: v2 verified; private-test debug certificate, not release key.
- APK zip alignment: passed with 16 KiB page check.
- All four Android native ELF payloads: AArch64, LOAD alignment `0x4000`.
- APK ABI: `arm64-v8a`; min SDK 28, target SDK 35; Vulkan 1.2 requirement.
- Native entry `SDL_main` and the two file/platform JNI entry points exported.
- Android package staging: no ROM/save extensions or N64 ROM headers found.
- Physical device launch, graphics, controllers and online qualification:
  **not performed — no Android/ARM Linux device connected**.
- Windows desktop regression build/tests: **76/76 passed** (before the
  Android startup follow-up below).

## Final package SHA-256

```text
6ed5a47e98a25165976c7fae6d9c8ee3a95cf4863ab06755765a43e7e790ab6a  DKR-R-1.0.5-beta.11-Android-arm64-preview.apk
a40855bb10221822ab2c0a6c33415ae5f63871932e9a96d8ffdf2038200feef9  DKR-R-1.0.5-beta.11-Linux-aarch64.AppImage
```

See `ARM-PLATFORM-PREVIEW.md` for build reproduction, explicit feature limits
and the device-test gate. These artifacts are not evidence of universal
Android handheld compatibility or release readiness.

## Android startup follow-up (preview.2)

The user tested preview.1 on Honor Magic V5 / Magic OS 10: the launcher and
ROM import work, but Start closes the application. No device crash log was
available and no phone is connected to the development machine.

Source-proven failure path (not yet confirmed by a device stack trace):

1. `RT64Renderer` passed `detectDataPath=true` to RT64.
2. `UserPaths::detectDataPath` uses HOME, or `getpwuid(getuid())->pw_dir` on Linux.
3. Android's [Bionic app passwd implementation](https://android.googlesource.com/platform/bionic/+/master/libc/bionic/grp_pwd.cpp)
   returns `/data` for application UIDs. Neither this directory nor an inherited
   `/` HOME is the application's writable private directory.
4. RT64's constructor calls `checkDirectoryCreated`; this uses throwing
   filesystem operations on the derived `.dkr-port` directory. There is no
   surrounding exception handler in the graphics thread's constructor call.
5. The launcher's own files use `getFilesDir()`, so launcher success does not
   validate the renderer's independent storage discovery.

Correction: Android only now sets `detectDataPath=false` and provides
`SDL_AndroidGetInternalStoragePath()/config/rt64` through RT64's existing public
configuration. The directory is prepared with error-code filesystem operations;
an unavailable native surface/storage returns a renderer setup failure instead
of explicitly throwing. No dependency files, guest patches, saves, gameplay,
network code or desktop renderer configuration are modified by this follow-up.

Diagnostics: retain Android's fatal-signal handler (the old `_exit` handler
suppressed tombstones); record uncaught exception text and GPU stdout in the
runtime log. About → Support summary → Export Android Startup Logs uses SAF to
save an opt-in ZIP containing only the explicit runtime/renderer log allowlist,
device/build description, and this application's recent Android exit records
and available traces. No ROMs, save files, profile/private-key files or settings
are collected. Logs/traces can still include paths/session data; review before
sharing. Collection is on a worker thread with bounded log/trace sizes.

The isolated mod helper also now has `$ORIGIN` RUNPATH to find its packaged
libc++ library; this is independent of the unmodded renderer startup failure.

Physical game launch remains a user-test gate, not a claimed pass.

### Follow-up verification

- Android native rebuild and APK assembly succeeded. The APK reports
  `1.0.5-beta.11-android-preview.2`, version code `1050012`, and uses the same
  preview signing certificate/package ID, allowing an in-place upgrade.
- Final APK signature v2 and `zipalign -c -P 16 4` passed. Staged helper
  `DT_RUNPATH` was explicitly checked and contains `$ORIGIN`.
- Private renderer-directory test passed on Windows and ARM64/QEMU: missing,
  empty and relative paths rejected; existing renderer files retained across
  repeated starts; non-directory obstruction reported without an exception.
- Windows rebuild and **77/77 DKR tests passed**.
- ARM64 Linux rebuilt; new AppImage and its packaging smoke checks passed
  (mod helper, SDL3 helper, virtual accessory and two live input round trips).
- Android lint did **not** pass: 48 errors/57 warnings, with the errors in the
  unmodified SDL2 Java sources (Bluetooth/microphone permission checks and a
  receiver flag). No errors were reported in the project-owned Java changes.
  These were not suppressed or presented as a clean Android qualification.
- No physical-device game launch has yet been verified for preview.2.

Final corrected artifacts (`packages/android-preview2`) SHA-256:

```text
74a664b208a29b4debbcf29f4581d06fe3daeab6c4d3e8cce73b0f378fb05de8  DKR-R-1.0.5-beta.11-Android-arm64-preview.apk
d9721b7087d74d7e270713333648fd845f5a2008c9c3b4649a3511dd44c83fcf  DKR-R-1.0.5-beta.11-Linux-aarch64.AppImage
```

Phone test: install over the previous preview without uninstalling, launch the
same imported ROM, and check the title/game scene. If it exits, reopen once and
choose About → Support summary → Export Android Startup Logs. Save the ZIP
and review it before sharing. The previous run's runtime log is retained for
one subsequent launch, and Android exit records/traces are best-effort.

## Android black-screen follow-up (preview.3, 2026-09-22)

Device: Honor Magic V5, MBH-N49, Android 16/API 36, Magic OS 10. The user
confirmed preview.2 plays title music continuously but displays no game image.
The supplied `DKR-R-Android-startup.zip` contains a 1 MiB previous-log tail with
repeated `vkQueueSubmit ... 0xFFFFFFFC` (VK_ERROR_DEVICE_LOST), fence timeouts
(`0x2`) and query-not-ready (`0x1`). Current-run logs describe the reopened
launcher. All three exit records say `[REMOVE TASK] remove task`; they are not
native crash traces. The first GPU failure and renderer setup were missing.

### Narrow correction

- The pinned renderer requests `B8G8R8A8_UNORM` for both the swapchain and the
  final VI presentation pipelines. Its Vulkan swapchain constructor requires
  an exact supported surface format. Android guarantees RGBA8, not BGRA8;
  see the primary reference in `patches/android/README.md`.
- An Android-only build-stage patch now uses `R8G8B8A8_UNORM` at both sites.
  The inspector already takes its attachment format from the swapchain.
- Only staged copies of those two source files change. Dependency checkout
  hashes remain unchanged. Windows/Linux still compile their original sources;
  no generated guest functions or networking/gameplay logic changed in this pass.
- Exported logs up to 1 MiB remain whole. Larger logs retain the first 256 KiB
  and last 768 KiB in separately named `.head`/`.tail` entries, plus byte-range
  metadata. This preserves startup evidence without increasing log-data limits.

Device loss is confirmed; the format defect is independently confirmed in the
source. The clipped log does NOT prove the format mismatch caused this phone's
first GPU error. Physical-device retesting remains mandatory.

### Verification

- Android native build and APK assembly passed. Preview.3 has version code
  `1050013` and the same package ID/signing certificate as preview.2.
- APK signature verification and `zipalign -c -P 16 4` passed.
- Host-JDK snapshot tests passed: missing, empty, small, exact-limit,
  over-limit and simulated 5 MiB error-flood logs, including byte-for-byte
  checks of exported head/tail and preservation of the source file.
- Windows rebuilt and all **77/77 DKR tests passed**.
- ARM64 Linux rebuilt. **76/77 tests passed** under QEMU. DKRDirectSession
  fails at `direct_session_tests.cpp:1302`, the exact realtime-send attempt
  assertion, including on a separate rerun. This does not exercise the Android
  renderer patch. Do not claim a clean ARM64 online qualification; the test
  was not weakened and online code was not changed to accommodate it.
- Linux ARM AppImage packaging/smoke checks passed, including the mod helper,
  virtual accessories and two live SDL input-backend round trips.
- Previously documented upstream SDL2 Java lint findings remain unresolved;
  successful APK assembly is not a claim of lint or physical-device qualification.

Artifacts are in `G:/DiddykongWorkFolder/arm-port-20260921/packages/android-preview3`.
The preview.2 packages remain untouched as rollback artifacts.

```text
a4ff47be2a9f8c074766688d0c613e14501f15e99a2cedf587b34c5ff3125b04  DKR-R-1.0.5-beta.11-Android-arm64-preview.apk
d1c6c7a007369aa7daf50b05b6ec8705bf6f93b79e8ad252fc0eb0faa7e192e2  DKR-R-1.0.5-beta.11-Linux-aarch64.AppImage
```

Install the APK over preview.2 without uninstalling, then start the imported
ROM and check the title visuals. If still black, close/reopen once and export
startup diagnostics before another launch; the new head capture should retain
the initial renderer messages and first failure.

## Android mobile/lifecycle follow-up (preview.4)

The user confirmed preview.3 now launches and plays on Honor Magic V5, but
reports visual glitches/slowdown and crashes when taking screenshots or screen
recordings. No new screenshot, timings or crash export was available for this
pass. Preview.3 is retained as the known-playing rollback build.

### Evidence and changes

1. **Desktop-scale workload:** `ApplyConfig` uses WindowIntegerScale for Auto;
   RT64 rounds swapchain height up to a multiple of 240. A 1200-pixel-high
   surface therefore renders at 5x, versus 2x for the mobile preset (6.25 times
   as many internal pixels at the same aspect). Modern defaults also match
   display refresh and use 16x anisotropic filtering. These are cost factors,
   not measured proof of this phone's bottleneck.
2. **Explicit mobile preset:** Android Graphics now offers an opt-in button
   choosing Modern, Original2x (480p), 60 FPS, no MSAA/supersampling, 4x AF.
   No automatic settings migration/reset occurs. Aspect/HUD/framebuffer
   precision/scenery choices are retained from the Modern profile. AF still
   follows the existing next-game-launch behavior. This lowers workload at a
   resolution-quality tradeoff; it is not an engine optimization or guarantee
   of desktop performance at identical quality settings.
3. **Lifecycle defects:** the SDL2 event pump tried to manage an EGL context
   for externally managed Vulkan; its surface-change callback also lacked an
   OpenGL-window guard, and surface destruction waited for EGL backup even on
   Vulkan. The scoped external-context hint and checked SDL build-stage patch
   address these concrete defects. See `patches/android/README.md` for source
   details and the SDL reference. No SDL/RT64 dependency checkout was edited.
4. **Diagnostics without screen capture:** Android now logs effective graphics
   configuration, five-second samples of existing presentation/simulation/VI
   counters, and relevant SDL lifecycle events. Metrics are observational and
   do not alter the simulation clock or renderer synchronization. They do not
   constitute a GPU profiler or identify artifacts by themselves.

### Qualification and remaining work

- Android native rebuild and Gradle APK assembly succeeded.
- Windows and Linux ARM64/QEMU each passed **78/78 DKR tests** this run,
  including mobile preset values, repeated application and preservation of
  aspect, HUD, precision, API and window mode. The earlier QEMU DirectSession
  failures remain recorded above; a later passing run does not erase them.
- Linux ARM AppImage packaging and helper/input smoke checks passed.
- SDL Android source SHA-256 before/after is identical:
  `4fff263fb1e8343f100603fcfebea4fa376b8bf9dd73d3fc7c917ead3ffa6bc5`.
- The final preview.4 artifacts are under `packages/android-preview4-final`;
  the earlier `packages/android-preview4` APK was an intermediate candidate
  before the SDL callback correction and should not be distributed.
- Visual glitches remain unclassified. No evidence supports claiming they
  are fixed, or that capture crashes/performance parity have been verified on
  the phone. The user was asked for a text description and opt-in diagnostic
  ZIP, not another screenshot attempt.
- No changes were made to guest patches, gameplay, netplay, HUD positioning,
  shader precision, driver choice, texture assets, or desktop graphics defaults.

Device check: install preview.4 over preview.3, select Graphics → Apply mobile
60 FPS preset, start the same ROM, and compare ordinary play without recording.
If problems persist, reopen once and export startup logs before further runs.

## Android mobile launcher and touch controls (preview.5)

See `ANDROID-MOBILE-UI.md` for scope, implementation, limitations and the device
acceptance checklist. No dependency checkout, generated recomp source or game
HUD patch was edited for this pass.

- Windows full suite: 80/80 passed (121.01 seconds).
- Linux ARM64/QEMU full suite: 79/80 passed (40.03 seconds). DirectSession
  failed the previously recorded timing assertion at line 1302,
  `transport->realtime_attempts == 1U`. No claim of a clean ARM online suite.
- Following the final editor-scroll/undo fixes, targeted mobile policy, UI and
  graphics tests passed 3/3 on both platforms.
- Android native compilation and Gradle assembly succeeded with NDK r29 and
  Java 21. The initially selected older Android Studio Java directory lacked
  `jvm.cfg`; assembly was rerun using the working Android Studio1 JBR.
- APK v2 signature verified and `zipalign -c -P 16 4` passed. It remains a
  debug-signed preview, not a production signing identity.
- APK metadata: `1.0.5-beta.11-android-preview.5`, code `1050015`, API 28 minimum.
- Linux ARM AppImage packaging, release-tree scans, helper checks, virtual pak
  and two SDL2/SDL3 input-switch round trips passed.
- `git diff --check` passed. No physical Android device was attached.

Final packages: `G:/DiddykongWorkFolder/arm-port-20260921/packages/android-preview5-mobile`

SHA-256:

```text
f0bb4fd6638351d6c2dc752cd978589f6836e25776bd10853712fe5062159519  DKR-R-1.0.5-beta.11-Android-arm64-preview.apk
276086e13984440e05a09782892192158effd43c8b98292cbf0d927d1e73014f  DKR-R-1.0.5-beta.11-Linux-aarch64.AppImage
```

The previous preview.4 packages and pre-change source copies are retained in
`G:/DiddykongWorkFolder/arm-port-20260921/mobile-ui-baseline-20260922`.
The device acceptance pass must verify the phone's safe-area/keyboard behavior,
scrolling, control placement, simultaneous touches, saved layouts and lifecycle.
