# Issue #45: missing Wayland client in Linux AppImages

The original report concerns v1.0.4 on Batocera 43.1. Its follow-up confirms
v1.0.5-beta.12 also fails and that adding Ubuntu Noble's Wayland client resolves
startup. Inspection of the retained published v1.0.15 payload shows the same
omission: cursor/EGL libraries are present, but `libwayland-client.so.0` is not.

The existing dependency audit classifies the client as a host library, following
the upstream AppImage exclusion list. That makes the missing library an admitted
host requirement rather than a failed packaging check. Upstream excludes it
because an older bundled client can conflict with newer Mesa drivers. Simply
putting an old client into the global library directory would trade one failure
for another.

The fix installs a private fallback in `usr/lib/dkr-r/wayland-fallback`, refreshes
cursor/EGL from the matching target sysroot and includes the Wayland copyright
notice. The AppRun shell entry probes the cursor dependency with the normal
search path. A resolvable host client stays in use; otherwise the private client
is added. No graphics driver is bundled or replaced. The isolated file-dialog
helper keeps its existing host desktop stack and environment policy.
Only the fallback directory is added to the inherited runtime search path when
needed. Its own RUNPATH resolves bundled dependencies, so this does not expose
every AppImage library globally to host graphics drivers.

Both the generic shell packager and retained release package pipeline invoke
the shared first-party installer. Static inspection requires the private client,
correct ELF architecture/SONAME, matching AppRun and copyright notice, and rejects
a global client override. Cross packaging requires a target sysroot; it cannot
silently copy the build host's x64 client into an ARM64 package.

Contributor regressions cover the old omission, architecture/SONAME failures,
sysroot selection, notices, global overrides and escaping source links. Runtime
qualification uses an isolated filesystem containing the actual packaged game
and only explicitly selected host libraries. Its missing-client and host-present
cases distinguish a working fallback from a test accidentally using the build
machine's library. This is not physical Batocera or ARM graphics acceptance.

The v1.0.15 tag, published downloads and signed updater files remain unchanged.
Local test AppImages reuse the released runtime; only packaging changes. The
fix needs a future release version when distributed publicly. No GitHub issue
comment or closure is part of this local investigation.

Verification on 10 November 2026:

- Both final x64 and ARM64 AppImages passed archive readback and the complete
  ELF dependency audit. Their game executables and online identities match the
  released v1.0.15 payload byte for byte. Dependency floors remain glibc 2.38
  and GLIBCXX 3.4.32.
- In an isolated Linux filesystem without a host Wayland client, the old package
  fails with the reported loader error. The corrected AppRun passes the runtime's
  Controller Pak round-trip/backup test using the private fallback. Restoring
  the host client makes it take priority. Loader traces confirm both choices.
- The final x64 AppImage also extracted and launched itself in that filesystem,
  with a real mounted proc filesystem, and passed the same test. ARM64's actual
  image payload passed through QEMU with eager symbol binding and the private
  fallback. These are loader/CPU checks, not physical GPU acceptance.
- The final x64 image passed US v1.0 manual-launch and US v1.1 skip-launcher
  startup checks under Xvfb/software Vulkan, including the manual fade handover.
- Windows host checks passed 173/173. Linux passed 172/173; the regression
  harness hit its existing subprocess timeout on the mounted Windows checkout.
  Its unchanged 25 cases passed on native Linux storage. The final 12 Wayland
  tooling cases passed separately on both Windows and Linux.
- Guard, tooling, metadata, source-asset scans and the applicable pre-commit
  checks passed. No protected/generated sources or dependency submodules changed.

The reporter's Batocera hardware still needs to confirm startup and rendering
with the fixed image before calling that platform accepted.

Sources: [issue and confirmed workaround](https://github.com/ThatGuyMcd/DKR-R/issues/45#issuecomment-5984441330),
[upstream exclusion and Mesa rationale](https://github.com/AppImageCommunity/pkg2appimage/blob/master/excludelist),
[upstream fallback precedent](https://github.com/musescore/MuseScore/pull/24128).
