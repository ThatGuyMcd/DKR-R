# Fixed stock save layout

## Current release composition

For v1.0.11 use the [release pipeline](../release-prep/v1.0.11/BUILDING.txt). The module notes below
explain this layer; their dated playtest paths are historical inputs, not the
current release command. The accepted water/source capsule preserves the
composed behaviour. Dependencies and generated guest checkouts stay read-only.

This Patch Pipeline stage fixes the save display and record overflow caused by
additive `.dkrmap` courses. It does not edit runtime source, existing generated
CPU output or submodules.

The original game sizes its EEPROM bit streams using the live level catalogue.
Adding a course changes that catalogue, but does not make the 40-byte Adventure
slots or two 192-byte record blocks any larger. Reading a stock save then shifts
all later fields; writing can overrun those blocks. The record initialiser also
indexes past the original 65-row defaults table.

`guest_save_layout.h` keeps the original 34 course statuses, six worlds and 47
track/vehicle record pairs in their retail order. Live Settings arrays remain
large enough for custom courses. Their extra record entries start with an unset
time and blank initials, without reading beyond the default asset. They are not
inserted into the stock EEPROM format. Record-block tail bytes are retained.

All pointers, array extents, bit-stream lengths and relevant aliases are checked
before writing. Output is assembled into bounded local storage before copying to
the guest save buffer. The same guest-memory helper is compiled into the ordinary
CPU payload and the checked, checkpoint-owned rollback payload. No new native
file storage or speculative online file writes are introduced.

## Generate

Use the complete composed CPU pipeline that produced the current build. Its
`generated-v77`, `generated-v80`, `owned-v77`, `owned-v80`, `v77.toml` and
`v80.toml` must exist. The five original save routines are SHA-256 pinned; an
unreviewed upstream change stops generation instead of silently patching it.

```powershell
./scripts/Prepare-SaveLayoutPayload.ps1 -BaselinePipeline C:/path/to/pipeline-full -OutputRoot C:/path/to/NEW-save-pipeline -Python C:/Python314/python.exe
```

Configure the existing build with the new `generated-v77` and `generated-v80`
paths, both new owned payload paths, and both new complete policy TOMLs. The
driver preserves the baseline replay roots and options. Original inputs are
read-only, and the output directory must be new. Linux uses these same generated
files through its mounted path. Package Windows and Linux/AppImage as usual.

## Scope and release notes

- This prevents custom course counts from changing the stock save layout.
- It does not touch users' existing EEPROM files during installation or build.
- A save that only looked wrong with custom courses enabled should be read using
  the original field positions again. This does not promise recovery of a save
  already overwritten by an older build; retain backups and use Save Manager.
- Custom-course records are session-local. Separate persistent custom-record
  storage is deferred under the strict Patch Pipeline-only requirement.
- Every online player needs this same fixed build: its rollback contract changes.
- Compilation and package inspection are separate from gameplay testing, which
  remains with the project owner. No gameplay, replay or self-tests are run by
  this preparation script.

Suggested owner checks: compare the same Adventure slots with no tracks, each
reported course separately, and all courses enabled; finish stock/custom races;
restart and compare stock initials/balloons; repeat with both ROM revisions and
matching online peers. Keep the original save backed up throughout.
