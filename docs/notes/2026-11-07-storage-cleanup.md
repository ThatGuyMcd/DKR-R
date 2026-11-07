# Storage maintenance investigation — 7 November 2026

A read-only inventory found approximately 17.2 GiB in the development profile.
The largest disposable areas were Mod Library transaction preparation and
download archives, including duplicate verified filenames. Installed texture
packs were also large and must not be treated as cache. Abandoned Rice import
staging, online jobs and completed update handoffs contributed further growth.
These are logical file sizes; filesystem allocation can differ.

The owned engine and idle service live in `src/game/storage`. Tests exercise
synthetic profiles; no player's logs or dump are uploaded as fixtures. Policy
integration uses existing checked layers because the production importers still
come through the frozen source capsule. The capsule, submodules, generated guest
functions and pinned upstream source remain untouched. Final generated-source
bindings must be inspected as well as the authoring files.

Current and previous recovery are reference-pinned. Preparation compaction
checks both source and installed checksums. Download removal also verifies the
installed members. Immutable history retains its referenced packages, and
unrecognised or edited package bytes are kept. Archive lookup by digest preserves
offline activation after duplicate filenames are removed. A missing exact
archive queues the exact edition rather than substituting another release.

An in-process shared activity barrier protects publishers, cache readers and
online preparation. Maintenance yields to waiting work. New-build process
leases defer cleanup with another live copy, and retirement journals make
profile deletion resumable. Original dumps require their own explicit action.
Large checksums can delay an individual yield; physical Steam Deck performance
still needs device acceptance. Older builds do not participate in the lease.

See [Storage](../STORAGE.md) for user controls and the retention table. Build
outputs, detailed test evidence, source ledgers, signing material and profile
inventories stay outside the repository.
