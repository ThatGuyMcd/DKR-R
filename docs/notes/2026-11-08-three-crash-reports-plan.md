# Three v1.0.14 crash reports: findings and proposed repairs

Investigated on 8 November 2026. The original findings and repair plan follow.
Implementation progress is recorded below. The three original crashes have not
been reproduced; the graphics-startup and Linux gameplay causes remain open.
Existing menu repair changes were left intact.

## Evidence supplied

The three ZIPs were read directly without executing or importing their contents.
Each contains `manifest.json`, `runtime-previous.log`, and `runtime.log` only.
None contains a `.dmp`, core, binary, matching symbol file, or mod package.
All manifests and crashed-session logs identify version 1.0.14. The manifests
say `rawDump: false` and `dumpStatus: Not requested or not available`; that does
not distinguish a declined dump from an unavailable one.

| Report prefix | Crashed log | Current log | Confirmed failure |
| --- | ---: | ---: | --- |
| `7c20333a` | 505 lines | Empty | Windows exception `0xC0000094` |
| `13a00366` | 118 lines | Empty | Windows exception `0xC0000005`, read at `0x2D00F0` |
| `4afc79bf` | 1,810 lines | 13 startup lines | Linux signal 11 |

An empty current log does not prove a second startup crash. The previous log is
the crashed session; collection timing can leave the new log empty or partial.
Current-version symbols cannot safely be substituted for the missing build
identification of an older executable.

## 7c20333a: Windows shell / file-dialog layout path

`runtime-previous.log:468` records integer division by zero at a DLL address.
Frames 474-505 include `DllCanUnloadNow`, repeated
`DirectUI::Element::_FlushLayout`, `DirectUI::Element::EndDefer`, `DUserSendEvent`,
`CreateGadget`, window procedures and subclass handlers. There is no game-launch
request, runtime thread start, or RT64 handoff in this log. It was still in the
launcher after several texture-pack imports/refreshes, reaching 15 packs.

This strongly points to a Windows shell/dialog layout failure. It does not prove
which picker was open, which DLL owns the fault, or whether a shell extension,
thumbnail/preview handler, DPI change, or application-side misuse triggered it.
The invalid-ROM-header inspection earlier in the session is not enough to link
that file to the crash. The log does not show a division by zero in the game's
simulation or custom character renderer.

Proposed repair:

1. Add bounded operation breadcrumbs around the shared picker boundary: picker
   kind, open/close/cancel/error, owning window and thread, DPI and dimensions.
   Never log the selected user's full path.
2. Audit COM initialization/lifetime, owner window validity, filter storage and
   serialization of dialogs. Exercise importing ROMs, mods, textures, images and
   saves, not just one picker.
3. Isolate native shell dialogs in a small helper process using a bounded result
   protocol. A crashing shell extension should return a picker error to DKR-R
   rather than terminate the launcher. Preserve selection, cancellation and
   Unicode paths. Do not resume execution after a fatal shell exception inside
   the game process.
4. Test repeated open/cancel/import, directories containing broken previews,
   multiple DPI settings and window resize/minimize while the dialog is open.

The helper is containment, not proof of the original trigger. Any confirmed
in-process picker misuse must also be corrected.

## 13a00366: graphics presentation / hook path during game startup

The launcher presents normally with accelerated SDL Direct3D. The user requests
an offline launch at about 131.85 seconds. RT64 setup completes, the guest entry
starts, one texture pack is accepted, audio opens and the in-game overlay attaches.
The final normal lines announce the shared starting/syncing presentation and
decode isolation. Line 88 then records an access violation reading `0x2D00F0`.

The stack includes `OpenShimInterface`, `D3D12GetInterface`,
`DXGIReportAdapterConfiguration`, `IcdPresentBuffers`, `dummy_debug_proc` and
`OW_Negotiate`. This is evidence for the graphics/presentation path, including
possible third-party hooking. The report lacks module names and driver versions;
it does not establish the driver vendor, identify an overlay product, or prove
that an external hook caused the crash. Large export displacements are not
trustworthy source-function names without matching symbols.

Proposed repair:

1. Record the selected API, adapter and driver version, relevant module IDs and
   the exact failing presentation phase. Keep machine/user identities out.
2. Audit launcher-renderer destruction before RT64 starts, startup-screen and
   overlay resource ownership, swapchain changes, texture retirement and fences.
   Exercise fresh and repeated launches, minimized windows and resize during the
   handoff. Do not remove synchronization to hide the symptom.
3. Reproduce Direct3D 12 versus Vulkan, with a clean profile and with the same
   texture-pack setup, then with external capture/overlay hooks present/absent.
   Change one variable at a time. Do not automatically disable users' software.
4. Use graphics validation in a diagnostic build and retain the matching symbols.
   Repair the proven ownership/lifetime or API violation. If failure is external,
   provide a clear next-launch backend choice; do not catch an access violation
   and try to carry on with corrupted renderer state.

## 4afc79bf: Linux gameplay crash, exact function lost

The AppImage uses hardware-accelerated OpenGL in the launcher. Offline game
startup succeeds with four custom tracks installed and six active texture packs.
Many scene transitions occur. The final recorded scene generation is 39 and its
map ID is 28. The reviewed retail level enum identifies map 28 as Frosty Village;
scene generation 39 is not map ID 39. The last cadence report is roughly 30 guest
tasks and 60 presentations per second. Signal 11 appears at line 1803.

The executable frames at 1804, 1806 and 1807 are completely replaced by
`[redacted]`. Remaining libc and libstdc++ frames do not identify the failing
application function or owning thread. There is no fault PC, accessed address,
module offset or core. It is not possible to identify an exact bad instruction
from this archive. The last logged map and water cadence are context, not proof
that Frosty Village, water, custom tracks or texture packs caused the fault.
There is no logged out-of-memory event.

There is also a confirmed earlier loader problem: host GVFS/GIO modules cannot
resolve `g_variant_builder_init_static` from the libraries available in the
AppImage process. Multiple file-picker interactions emit this warning, and the
game subsequently runs. This deserves a packaging correction, but the log does
not connect it to the later segmentation fault.

Proposed repair:

1. Preserve application module offsets and build IDs in Linux reports, and record
   the fault PC/address and thread context using a crash-safe mechanism. Keep
   local core/dump collection subject to the existing consent choices.
2. Fix AppImage GTK/GLib/GIO module compatibility as one coherent dependency set,
   or move the native picker to a helper with a coherent system environment.
   Do not broadly clear loader variables or disable desktop portals as a blanket
   workaround. Verify actual pickers on supported distributions and Steam Deck.
3. Reproduce repeated hub/race/track-select transitions ending in Frosty Village
   on Linux: stock assets first, then each texture pack and custom track separately,
   then the reported combination. Include window resizing because the log shows
   output dimensions changing during the session. Preserve users' original saves.
4. Use a matching-symbol build, targeted sanitizers and existing allocation,
   display-list bounds and asynchronous-resource diagnostics. Change the code
   only after the fault has been localized. Run the same transition tests on
   Windows to catch shared lifetime errors.

## Confirmed report-quality defects to fix first

These defects are visible in the current first-party code, independently of the
unknown crash triggers:

- `runtime-recomp/src/game/game_main.cpp` calculates the logged RVA by subtracting
  `GetModuleHandleW(nullptr)` from every fault address. Both supplied Windows
  faults are outside the main executable. Resolve the owning module per address,
  print its basename/ID and bounded module-relative offset, and do the same for
  each stack frame. Include the actual fault PC separately from the walked frames.
- `runtime-recomp/src/game/crash_reports/queue.cpp:redact` removes an absolute
  `/tmp`, `/run`, `/home` or other matched path through the end of that line. An
  AppImage executable stack frame therefore loses its offset and address as well
  as its private path. Serialize technical stack fields separately from free text,
  redact private path components and preserve the safe module ID and offset.
- Reports need the crashed producer's immutable build ID/revision and symbol
  identity, not just a marketing version from the next launch's collector. Keep
  matching PDB/ELF symbols privately for each distributed test/release build.
- Record a small bounded set of useful technical context: last operation, scene
  and map, API/driver, active content IDs and faulting thread. Avoid constant log
  spam and personal paths. A missing dump should explain its actual collection
  state when known. Do not silently enable raw-memory uploads.

Tests should cover Windows EXE and external-DLL frames, Linux AppImage paths in
all matched locations, Unicode paths, unknown modules, consent on/off, no dump,
oversized dump, report retries and log rotation. Synthetic reports must retain
the intended source offsets while still removing usernames, paths, tokens,
endpoints and chat. Validate through the complete ZIP pipeline.

## Delivery order and acceptance

1. Repair diagnostic attribution and redaction, with focused regressions.
2. Contain and audit the native file-picker path.
3. Correct the AppImage dependency mismatch and test real pickers.
4. Reproduce and repair the graphics-startup access violation.
5. Reproduce the Linux gameplay fault with preserved stack evidence, then repair
   its confirmed cause. Items 4 and 5 remain open until that evidence exists.
6. Run guard/tooling and the required C++ host checks; test Windows and Linux
   packages with both supported ROM revisions. Prepare Windows and AppImage
   builds together. Physical driver/distribution/Steam Deck coverage must be
   reported as tested or outstanding, never inferred from WSL alone.

Use first-party source locations and existing checked hook/policy boundaries.
Any third-party correction goes through a registered patch and checksum; never
edit submodules or emitted guest C. These investigations do not require changing
simulation/network identity. A repair that does must follow the existing
compatibility approval process.

## Selected Player Select design

Option 3 is refined as a design preview: the game-style name and compact L/R
prompts sit above the selected custom stage actor. There is no added player
number plate, global name strip or footer. Only selected custom actors receive
labels, up to four players; stock and unselected actors stay unlabelled. Duplicate
selections share one actor label. Prompts hide when browsing is unavailable.

Implementation should derive anchors from the displayed stage actor and current
camera/projection, then use the game's text rendering path. Do not hard-code the
preview's percentage coordinates into the game. Use bounded wrapping for long
names and local collision/safe-area handling at 4:3, 16:9, 16:10 and ultrawide.
Test 1-4 players, mixed stock/custom picks, ready/unready states, replacement while
browsing and duplicate selection. The preview is not an in-game implementation.

## References

- [Microsoft NTSTATUS definitions](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55)
  identify the Windows exception codes.
- [Microsoft SymGetModuleBase64](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-symgetmodulebase64)
  documents resolving the module containing an address.

## Implementation, 8 November 2026

### Crash evidence

Windows fault and stack addresses now use their own owning module, rather than
subtracting the executable base from addresses in DLLs. Startup records the PE
timestamp/image size and CodeView GUID/age when available. Linux records ELF
build identities, the actual fault PC, module offset, fault address, thread and
stack pointer. Its fatal-signal writer uses a bounded buffer, `write`, and a
module snapshot published during normal startup; it does not allocate or call
the loader from the signal handler. It records the faulting instruction, not an
unverified reconstructed stack.

The report queue keeps numeric offsets from older Linux backtrace lines before
redacting private paths. A report identifies the crashed producer version and
binary separately from the collecting launch. API, adapter and driver details
are logged once when the renderer starts. Upload and raw-dump consent stay under
the existing controls.

A repeated Windows run exposed a consent-file reader/writer race: the UI could
show reporting disabled while its replacement file was blocked by a background
reader. Preference reads now share the UI mutex and cross-process preference
lease. UI writes allow a bounded wait for another instance's short read. The
native test checks the saved disabled choice during 25 rapid toggles, repeated
ten times on Windows. Synthetic queued reports are cleared before starting the
service portion of the test, so they are never uploaded.

### Native file pickers

Windows and Linux desktop open/save/folder dialogs run in `DKR-R-FilePicker`.
The UI uses a small client library and a bounded JSON result protocol. A helper
failure, malformed response or timeout returns an error to the parent process.
Cancellation and UTF-8 paths are preserved, including Windows command-line
decoding. The helper stops if its parent exits. Owned temporary request/result
files, including an interrupted durable-write `.new` file, are removed.
The Windows wait pumps the calling thread's native window messages, so opening
an isolated dialog does not starve the launcher's window thread. A hidden
message-window regression verifies this alongside exception-status child exits.

The Linux child removes AppImage-owned loader/module paths from its own
environment while retaining external paths. It loads the host GTK/GIO family
and statically links its C++ runtime. The game process environment is unchanged.
This addresses the observed host-GVFS/bundled-GLib mismatch; it does not establish
that the mismatch caused the later gameplay fault. Physical Steam Deck and other
distribution coverage still needs an owner test.

### Player Select

Only selected custom stage actors receive names. Labels use the native colourful
font and live actor/camera/projection data. Names wrap within a bounded two-line
budget. Shared selections use one label. Small shoulder prompts appear only when
that actor can be browsed; there is no new player-number plate or global footer.
The label view is derived presentation state, not persistent selector/checkpoint
state. Tests cover 1–4 players, duplicate picks, long names, camera movement and
restoration of the intercepted guest context and text state.

Both ROM payloads bind the same read-only native font/camera accessors. The
existing checked compatibility pipeline carries these first-party edits into
the retained release graph. No dependency or protected generated tree is edited.
The frozen v1.0.7 capsule and its manifest retain their exact bytes: the pinned
byte checker verifies historical base blobs against their original hashes at
the same Git revision already used by `restore_capsule.py`. It does not exempt
those historical bytes from verification.

### Remaining investigation

The supplied archives lack dumps, matching symbols and the original content
packages. No particular renderer lifetime violation or gameplay instruction can
be proved from them. The startup access violation and later Linux segmentation
fault therefore remain unresolved pending reproduction with the improved fault
context. Picker containment is not presented as a fix for either crash.

Local build and runtime results are recorded with the unpublished Windows and
Linux test handoff. Matching symbols must be kept privately against each test
binary identity. New test peers must use the same build; no release or cross-play
compatibility merge is authorized by these checks.

Completed local checks include the 172-test Linux host suite, 33,813 native
character-stage checks, Windows fault-context/picker/report tests, and a live
Linux Player Select run with five custom characters. The latter exercised stock
and custom selection, shoulder browsing, and resizing to 4:3, 16:9, 16:10 and a
wide window, then stopped cleanly after 90 seconds. These are WSL/software-GPU
checks, not physical Steam Deck or native Linux GPU acceptance.

### Lower-row label adjustment

The two custom actors on the lower lawn now anchor their selected names below
their model's feet. The two upper-shelf actors retain their above-head labels.
Both placements follow the existing camera projection. A long lower-row name
uses one shortened line if a second line would leave the screen. Labels still
appear only for selected custom actors, with one label for shared selections
and support for all four players. Native regressions check both model edges,
both label directions and the lower screen boundary.
