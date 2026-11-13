# Selected custom character label visibility

The reported symptom affects both stage rows and can follow different racers
between launches. This does not establish a bad character-name string.

The previous placement code could omit a selected label in four places: a model
edge projected outside the screen; a lower-row foot position left less than one
line; an upper-row head position left insufficient space above it; or overlap
handling pushed a lower label past the bottom. A native regression reproduced
missing names on both rows without changing character identity or selection.

The renderer now clamps finite model-edge projections into a visible area for
each physical stage slot. Upper labels remain above the heads and lower labels
remain below the feet at normal stage extents. Extreme extents use a bounded
position in the same row. Each row has separate left/right areas, so one
player's name cannot push another off screen. Lower names still shorten to one
line when needed. Invalid non-finite or behind-camera projections remain
rejected; menu transitions and unselected actors keep their existing rules.

Native tests cover repeated paging with 20 names, changed entry/slot assignments
between simulated sessions, one to four selected actors, shared selections,
long names, extreme model extents, camera motion and native text-state
restoration. The new edge-visibility regression fails against the preceding
implementation and passes with bounded placement. These are presentation-only
changes; no selector, gameplay or checkpoint state is changed.

## Completed local checks

The final native character-menu fixture passed 39,778 checks. An actual
long-name Wario package reproduced the missing upper label before the repair
and displayed it afterwards, including in a lower slot. The exact reported
session-dependent Rayman omission was not reproduced; Rayman displayed a label
in the live checks.

Three fresh WSL software-Vulkan launches exercised eight real character
packages, both rows and all four physical stage slots, shoulder browsing and
4:3, 16:9, 16:10 and wide-window resizing. All three stopped cleanly after
90 seconds. L/R draw controls were detected in 48 fixed-size selected-state
captures; this checks the label draw path rather than OCR of every name. Names
and resized layouts were inspected separately. The Linux host suite passed
172 tests; packaged Windows startup passed eight cases and Linux four, covering
both ROMs, manual/automatic startup and the applicable graphics APIs.

These are local software-GPU and native Windows checks, not physical Steam
Deck or four-controller visual acceptance. The external release handoff retains
the screenshots and exact package identities.
