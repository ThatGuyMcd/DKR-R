# Mod Store installation management

All authoring for this change stays in this Patch Pipeline folder. The pipeline
copies the accepted SDK 2 / Camera++ baseline into a fresh build stage, appends
private preparation adapters, and records source/payload SHA-256 receipts.
Never edit a stage or a protected dependency to repair this feature.

## Ownership and identity

`mod-store/installations.json` stores local instance IDs, Store item/release IDs,
source checksums, owned native members, policies and a previous-edition record.
Download jobs are not installation receipts. Native IDs and compatibility hashes
are not rewritten to imitate separate copies.

Existing content is adopted only with exact provenance: source SHA for legacy
and code packages, verified converted-pixel evidence for textures, and complete
tree equality against a verified cached modern track. A first-party track
activation marker is excluded from the authored track comparison. A manual link
is explicitly recorded as a local version, not a verified Store release.

Receipts and journals use a strict managed-path allowlist. Only individual pack
roots, individual legacy group/review hashes, code package hashes and enumerated
native-library metadata files are eligible. Saves, ROMs, authoring directories
and entire library roots are excluded. Symlinks/junctions are rejected.

Native inventory emits generic UTF-8 relative paths on both Windows and Linux.
The manager canonicalises member separators without resolving dot components,
and checks ownership against the member kind as well as the managed roots.
External/native-only inventory is not adopted as writable Store ownership.
Receipts from the earlier beta 8 may have native Windows separators; these
are accepted only after canonicalisation passes the same strict allowlist.
Missing registries reload as empty, rather than retaining failed unsaved
adoptions. Failed reconciliation reloads saved receipts. Recovery snapshots
are validated before a publication journal can be written.

Windows inventory compares `store_io_path()` for both the native asset path
and config root. This preserves extended-path/UNC support without mixing an
extended root with an ordinary root in `lexically_relative()`. A valid native
track must not disappear from inventory merely because its reader uses long
paths. Installed status is separate from download history and enable state.

Modern-track membership also compares the owned directory, not just the ID.
Reconciliation cannot silently rewrite a receipt to own another directory
with that ID. An owned directory shadowed by the native duplicate-ID filter
counts as present only with the saved verified track-content proof; cached
proofs are keyed by path. The Installed page identifies another active copy.
Removal of such an owned shadowed directory never calls native uninstall by
ID, which could otherwise remove the other active directory. Truly incomplete
receipts expose a receipt-targeted Repair install action on Store cards.

## Store dialogs

Management dialogs use explicit viewport-bounded dimensions, not content-driven
auto-sizing or the shared helper that overrides desktop minimum widths. Their
single scroll body reserves the measured wrapped action-row height. Cancel
remains outside the scroll body and is not disabled by busy or invalid proposals.
Escape/controller Back only applies to the focused dialog, so a keyboard popup
does not also dismiss its parent. A completed or failed duplicate proposal
closes its stale popup automatically; no stored ImGui size is reused.

## Publication and recovery

One background installation worker serialises proposals, conversion, publication
and removal. The launcher supplies immutable library inventory and a safe-write
gate. The Store also holds an OS-level exclusive process lock while writing.
Normal imports/launch/online preparation are gated during Store publication.

Each operation has a random private directory under `mod-store/transactions`.
The journal is durable before mutation; backups are copied, verified and marked
ready before use. A wholly retired immutable legacy group instead moves into
its numbered backup by a same-volume atomic rename. A hashed write-ahead intent
is durable before that move. Recovery handles both sides of the move even if
the final ready flag was not flushed. The backup is retained after recovery.
Journals now use version 2 so older binaries block safely on unfinished moves;
this reader still accepts legacy version 1 copy-only journals. Publication copies verified incoming content beside its final
destination before replacement. A failed or interrupted operation restores
repeatably from backups. Startup recovers unfinished journals before allowing
launch. A recovery failure blocks launch and preserves evidence for repair.

Current/previous receipts retain their transaction directories. At startup the
twenty newest other completed transactions are also retained. Older completed
private transactions and recognisable abandoned private preparations are pruned.
Unknown directories and unfinished journals are left alone. Original download
cache files remain available for reinstall and previous-edition preparation.

Protection is limited to metadata for the affected native libraries. A character
install does not back up or refresh texture, modern-track or code libraries.
New destinations require one durable intent, not an empty-backup round trip.
Private survivor scratch copies are disposable and do not issue per-file durable
flushes. The real backups and published outputs still do. Copy buffers scale to
small files instead of allocating/zeroing 1 MiB for every tiny asset.

## Track archives

The pipeline replaces only the staged first-party ZIP helpers. miniz and all
other dependencies remain untouched. Native wide/UTF-8 FILE handles are passed
to miniz for archive reads and extracted files. Extended Windows paths are made
preferred before I/O; no narrow `path.string()` filename round trip remains.
Entry names are read in full and validated, never silently truncated. Absolute,
parent, device, special-file and linked destinations are rejected; existing
files cannot be silently overwritten by repeated entries. CRC/length checks and
entry/count/expanded-size limits remain active, with entry-specific diagnostics.
The Store cancellation token is checked between entries. Extraction is private
and retains the complete course directory, including declared audio.

Course discovery clears its outputs before searching, so an unsuccessful ZIP
search cannot reuse the archive filename as a directory. A bounded traversal
(eight wrapper levels, 100,000 entries, no links/reparse points) accepts the
Mario Circuit source-bundle layout as well as bare/single-wrapper exports.
Traversal stops at each course manifest and rejects multiple course roots.
The HD companion must have the exact manifest-declared safe filename; it is
looked up beside the course, inside the course, or at the extraction root.
It is still checked against the course's texture digest before publication.
Only the course payload and its verified companion become Store-owned files.

Legacy package updates prepare survivor groups privately through the existing
catalogue machinery. The live library never receives an independent nested
removal journal. Modern track payloads preserve relative audio/asset paths and
validated HD companions. Shared resources cannot be silently overwritten or
removed. Changed code continues through the existing permission model.

## Update policy

Catalogue refreshes occur on startup, on demand, and at bounded idle intervals.
Only the publisher-designated current release is offered. No update is installed
without a user request. Selection/skip/pin policies are per local instance;
bulk requests skip apparent downgrades. Changed local files ask again before
replacement. Changed code requires approval and compatible settings/data migrate.

The optional additive Worker change exposes `currentReleaseId`; the deployed
public version/file URL fields remain supported for backwards compatibility.
No backend deployment or remote catalogue mutation is performed by the build.

## Owner acceptance checklist

Compilation and static inspection do not replace owner testing. Use a disposable
copy of the data folder for interruption/low-space cases, not an original save.

- Install one texture pack, modern audio track, legacy character/track and code mod.
- Restart and check Installed status in both browsing layouts.
- Repeat each install: cancel, replace, and save another edition where supported.
- Check duplicate/update/remove/link dialogs at normal and small window sizes:
  readable text, one scroll body, reachable Cancel and controller Back/Escape.
- Retry after a failed install/reconciliation; no stale duplicate dialog or
  unsaved receipt should block the next operation.
- Install a wrapped track/HD ZIP from a Unicode Windows data path; check audio.
- Retry Mario Circuit from its cached source bundle; check its name, HD pack,
  music, Installed status after refresh/restart, and manual ZIP import too.
- Check Installed after idle refresh, navigation, restart and enable/disable.
- With two course directories sharing one ID, check that their ownership paths
  are not merged, and removal of one receipt leaves the other copy untouched.
- Remove a file only in a disposable copy and check Repair install is offered
  rather than falsely claiming the package is still complete.
- Compare complete character replacement and shared-group replacement timing.
- In disposable data, interrupt before/after a v2 retirement move and confirm
  startup recovery restores the old installation. Do not downgrade while an
  operation is unfinished; older builds intentionally stop on v2 journals.
- Confirm a texture copy's priority/enable state survives replacement.
- Update an enabled legacy mod and verify its selected/hidden state is retained.
- Update code with custom settings/data: retained values, changed code not approved.
- Check Update selected/all, skip, pin, unpin and an individually chosen downgrade.
- Remove one duplicate; verify other copies and shared companions remain.
- Check a track with external audio and bundled HD textures, including a later
  version that removes a companion.
- Close during private preparation, then during publication using a disposable
  profile; check startup recovery restores a complete library.
- Verify two launcher copies cannot publish to the same data folder simultaneously.
- Confirm failed downloads, insufficient disk space, invalid packages and unsafe
  paths produce actionable errors without changing the working installation.
- Confirm controller focus/keyboard, compact layouts and online/gameplay gating.

No game, launcher, installation or online acceptance run is automated here.

## Store polish acceptance

The Patch Pipeline replaces whole UI presentation sections with the authored
`store_player_*`, `store_staff_pages`, `store_account_page`, and `store_page_shell`
includes. Shared controls/modals are first-party only. Service changes are limited
to dismissing notices and clearing completed history through the background
queue; neither operation deletes cached downloads or installation receipts.
Download history snapshots are serialized before durable publication. Retrying
an update preserves its original installation instance and replacement intent.

Owner checks: navigate all pages using a controller at 1280x720 and 1280x800,
then resize at desktop and ultrawide resolutions. Return from a detail screen
and check focus/scroll restoration. Open every textbox's keyboard. Check all
modal actions remain reachable, including long titles and small displays.
Browse both existing layouts, use the gallery, submit/edit/report a review,
send and revise a request, and inspect its history. With staff accounts, check
private notes versus public replies, report-local reasons, all editor sections,
unsaved-change cancellation, upload resume/selection, publication confirmations,
role changes, expired sessions, stale revisions and a late save response after
opening another draft. Check history clearing retains installed mods and cache
data. Recheck beta 9 Mario Circuit and existing installation/duplicate recovery
acceptance above. No such runtime, account or install checks are run by this build.
