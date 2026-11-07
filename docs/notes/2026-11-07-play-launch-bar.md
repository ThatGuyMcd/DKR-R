# Play launch bar

Play now keeps its heading and launch bar outside a scrolling content child.
Community Spotlight remains above Ready to Race. The settings, save preview,
Achievements, Session mods and Save Manager remain accessible in that child.
The launch action is 48 logical pixels high, with a separate ROM/session summary.
The footer background reaches the panel's inner edges and rounded bottom corners.
The button is centred at 85% of the available content width, capped at 1280 logical
pixels so it remains compact in wider windows. Text retains the page padding.

An explicit Up request from the pinned action enters the scrolling child and
restores its last selection, or initialises its first control. ImGui's flattened
navigation can otherwise miss every child item when the visible slice contains
no focusable controls. The host interaction test reproduces that case. The
sidebar switch now retains the sidebar's no-navigation flag, so the stick cannot
select it. Sidebar routing and pointer clicks still operate the switch.

The Play child reaches the right panel edge; its scrollbar appears only when
content overflows. Content retains normal page padding. Session Mods has no
tooltip. Save Manager uses the same footer drawing helper as Play, retaining
rounded lower corners while filling the sides and bottom of the panel.

A ready ROM selects Start Game when Play opens, including before a controller
is connected. Import and launch locks still apply. Focus waits while a modal is
open; the existing ROM-loaded focus request is retained. Without a ROM the
visible launch action is disabled and the ROM controls remain available.

The launcher and overlay use the same page. The live action remains Return to
the Race. Offline launches retain lobby-leave confirmation and the existing
Starting Game fade/preparation flow. This does not change session mod selection,
save ownership, simulation or online compatibility.

Right-stick scrolling uses the inner content when focus is on the pinned action.
Other pages restore their normal scrolling target, and popup scrolling keeps
priority. The host UI interaction checks exercise this with both sidebar input
modes and both live/launcher navigation contexts.

## Native layout qualification

`runtime-recomp/tests/play_launch_layout_tests.py` runs a complete production
package against a fresh, token-authenticated disposable profile. It captures
the actual SDL UI at six steps: initial focus, Up, right-stick scrolling, Left,
Down and Right. It checks that the launch action stays in place and that focus
remains in the main panel. Readback checks the button's centring, scrollbar
visibility/position and actual background pixels in the former side/bottom gaps.
Save Manager checks use an isolated copy and verify that its bytes stay unchanged.
Ordinary startup cannot arm the harness. It is excluded from the ROM-free tooling
suite because it requires a production runtime and a native window.

Windows and Linux checks cover 1024x600, 1280x720, 1280x800, 1920x1080 and 2560x1440,
the paired live page branch, both sidebar modes, English/German/Ukrainian, and
the no-ROM state. Additional package checks read back the Windows ZIP and
AppImage contents. The exact x86_64 AppImage is also supplied for Steam Deck.

These are desktop/WSL native UI checks, not a physical Steam Deck performance
measurement. ARM64 is cross-built and package-audited; physical ARM graphics
remain untested. The Linux host suite's Windows-mount regression-harness timeout
is checked separately by running its identical 25 tests from native Linux
storage. Evidence and disposable profiles stay outside the repository.
