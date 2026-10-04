# Android startup regression correction — build 1050023

## Confirmed failure

The supplied `DKR-R-Android-startup (1).zip` identifies versionCode 1050022 on
HONOR MBH-N49 / Android 16 / Adreno 830. Its native SHA-256 matches the delivered
library. All three tombstones show `SIGSEGV`, address `0xe0`, on the graphics
startup thread. The ELF build ID matches the retained unstripped symbols:
`2611cafe11751936fdc70f5b927c816794e47317`.

Symbolized call chain:

1. `VulkanCommandList::copyTextureRegion`, relative PC `0x10b75a0`.
2. `android_graphics::qualify_color_target`, `0xdf4ec4`.
3. `RT64::Application::setup`, `0xdf35e0`.
4. `RT64Renderer` construction and the graphics thread.

The exact instruction loads from `[x8 + 0xe0]`, with `x8 = 0`. The new probe
requested an image-to-buffer copy. This Plume Vulkan implementation handles
buffer-to-image explicitly and otherwise assumes image-to-image. The buffer
destination has no texture pointer. The crash occurs while recording commands,
before the image-copy command reaches the GPU. It is not a low-performance-phone
diagnosis. Buffer qualification already passed in the supplied runtime log.

## Narrow correction

- Remove the colour probe and its startup call entirely. No replacement image
  readback feature is introduced as part of this emergency correction.
- Keep the existing two-pattern buffer-only check, including its real completion
  fences and mapped-memory visibility handling.
- Add an Android-only checked patch validating the copy direction and required
  pointers before the Vulkan code dereferences them. Existing buffer-to-image
  uploads and image-to-image copies are unchanged. Unsupported/invalid calls
  retain the existing terminal graphics-failure reporting path, rather than
  crashing, silently skipping a copy, or pretending work completed. This path
  reports to Android UI and requires app restart; it is not renderer recovery.
- Append a diagnostic stage for unsupported texture copies without renumbering
  existing stages.
- Raise Android versionCode from 1050022 to **1050023**. Keep versionName
  `1.0.5-beta.12`, package ID, signing identity, DKR-R adaptive icon, storage and
  graphics preferences unchanged. Install over the existing application.

No direct edits to RT64, N64ModernRuntime, other dependency checkouts,
RecompiledFuncs or RecompiledPatches. CMake selects a checked copy of the Vulkan
source. No online, gameplay, save, HUD, custom-mod, water, mipmap, touch-layout or
surface-lifecycle changes in this correction. No Docker recovery attempted.

## Recovery evidence

The last pre-regression APK (1050021) and dirty-source snapshot remain at
`G:/DiddykongWorkFolder/android-compat-20260927/baseline/`.

The failed 1050022 APK, its unstripped symbols, the supplied crash ZIP and copies
of files about to be modified were retained before editing at
`G:/DiddykongWorkFolder/android-startup-correction-20260927/before/`.
The failed artifact is diagnostic evidence, **not** a recommended rollback.

## Verification

- Regression source tests were run before the correction and failed on the
  unsafe startup probe / missing guard. Afterward all three pass, including
  actually applying the new patch to a temporary source copy.
- Copy-contract test checks 256 type/pointer combinations and the exact failed
  image-to-buffer direction. The two supported directions remain accepted.
- An explicit Vulkan GPU test invokes the actual surviving Android startup
  helper, not a substitute: three worker lifetimes, two patterns each. This is
  desktop Vulkan coverage, not a physical Android driver qualification.
- Final build/test/package results and SHA-256 values accompany the delivery.

Completed checks for this candidate:

- Android arm64 Release native build and APK assembly succeeded; versionCode
  1050023, unchanged signing certificate, DKR-R icon, arm64 ABI and 16 KiB ZIP
  alignment verified. The packaged library has no colour-probe symbol/message
  and contains the buffer check and copy-contract guard.
- Windows and Linux x64 each passed **87/87 project CTest tests**, run serially.
  An earlier unfiltered concurrent run also selected upstream benchmarks and
  failed the direct-session handoff assertion under that combined load. This
  is not dismissed as proof of network stability: the isolated project suites
  subsequently passed, and no online code was modified for this Android fix.
- The real buffer-only GPU helper passed three worker lifetimes / six pattern
  copies on both desktop Vulkan platforms. The follow-up test enables capture
  before device creation to cover the newly reported ordering, but still does
  not cover Android Activity, Java metrics or physical phone drivers.
- A visible isolated Windows Vulkan/Modern 60-FPS smoke completed 1,178 graphics
  tasks in 40 seconds and shut down cleanly. The first invocation had the wrong
  expected API number in the test command; logs showed a clean Vulkan run, and
  the corrected invocation passed. Neither run is an Android benchmark.
- Windows ZIP and Linux x64 AppImage packaging/helper tests succeeded. A first
  Linux package with stale internal beta.5 branding was withheld and preserved
  outside delivery; the final package was rebuilt with beta.12 branding.

## New device feedback: performance capture remains unqualified

**Follow-up:** the user subsequently supplied `DKR-R-Android-startup (4).zip`.
Its two 1050023 crashes were symbolized as a separate VI initialization fault.
See [the 1050024 investigation and correction](ANDROID-RECORDING-STARTUP-20260927.md)
for the evidence, staged fixes, test results and remaining phone checks. The
paragraphs below preserve the state of knowledge before that ZIP arrived.

During final packaging the user reported better gameplay performance but still
unacceptable Android frame drops, then clarified a separate crash: recording can
be started in the launcher, but launching gameplay with it active crashes. The
installed build code and a fresh trace have not yet been supplied. Do not reuse
the older 1050022 colour-probe tombstones as evidence for this new failure.

The JNI entry points for begin/end capture are exported in the candidate library.
The launcher start action enables bounded native counters and schedules Java
memory/thermal samples; it does not run the removed colour probe. Source review
also found Java sampling/start operations without error containment. That is a
diagnostic robustness gap, **not an established cause** of this reported crash.
Desktop startup profiling and unit tests cannot distinguish a Java exception,
native fault, driver failure or startup stall on the phone.

Do not ask the user to reproduce using recording again. The next evidence is a
normal Startup Logs export after reopening, without enabling performance capture.
It includes the prior process exit/trace when Android supplies it. Symbolize it
against its exact native hash/build ID, fix the identified recording/launch path,
then validate launcher-armed capture and gameplay capture before relying on the
recorder for Android optimization measurements. No speculative scheduling,
framebuffer or lifecycle correction has been added in response to this report.

This APK is therefore **not a fully qualified Android performance release**.

There is no attached Android device. No physical-device startup or FPS result is
claimed by compilation, desktop tests, or APK verification.

## Performance qualification after startup

The target remains stable frame pacing and reduced CPU/GPU cost on Android, but
this crash correction is not a measured FPS improvement or proof of desktop
parity. Mobile GPUs, drivers and thermal limits differ. Keep the performance
recorder and existing low-load presets available without resetting user settings.

The user agreed to test on Honor and provide a 60-second performance report, but
that request is now suspended until the recording crash above is resolved:

1. Update in place, confirm startup and gameplay, then repeat launch/resume.
2. Record a slow track with the current settings using About → Support → Record
   performance report; return the exported Startup Logs ZIP.
3. Compare the same scene under a controlled 60-FPS target, stock textures,
   1x versus 2x resolution, and cold versus warm play. Do not change unrelated
   variables between samples.
4. Use present-interval tails, CPU-side regions, readback volume, GPU history and
   thermal/memory samples to choose the next isolated optimisation. Validate
   identical visuals and gameplay before broadening the device test matrix.

No removal of synchronization/readbacks, speculative shader changes, or blanket
driver-feature bypass is justified by the startup crash evidence.
