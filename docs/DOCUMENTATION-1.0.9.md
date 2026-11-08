# v1.0.9 documentation audit

The release documentation update inventories first-party Markdown, TXT and
reStructuredText files in tracked and non-ignored source. It excludes dependency
checkouts, generated recompilation trees, build output and local Worker caches.
Frozen source-capsule TXT blobs are classified as source inputs, not guides.
Licences, notices and contributor attribution are preserved.

Current guides are checked for version/default/menu changes, removed backend
choices, public discovery, spectators, Scoreboard, cars, achievements, texture
tools, website FTP/batch administration, platform coverage and update signing.
Version-independent format/CI/input references remain current without an
artificial version bump. The Blender addon's own changelog/version and SDK API
numbers are independent of the application release.

Dated beta/playtest plans, old public release notes, investigations, archived
validation and patch-layer handoffs retain their original version, commands,
limits and test results. They are engineering history, not installation advice
for v1.0.9. The documentation index separates them from current guides.

All changes come from the
[documentation Patch Pipeline](../patches/release-prep/v1.0.9/documentation/pipeline.py).
It creates a recoverable pre-edit source ZIP/hash ledger, preflights every
target, records before/after hashes and checks that unrelated first-party files
and submodule revisions remain unchanged. Its external
`DOCUMENTATION-INVENTORY.json` records each document's disposition and version
mentions; `VERIFICATION.json` records counts and local-link checks. Current
release metadata/dependency patch checks are run separately. No native package
is rebuilt or altered for documentation-only updates.

The [v1.0.9 validation record](../RELEASE-VALIDATION.md) remains precise about
static checks and outstanding hardware acceptance. The v1.0.8 record is archived
verbatim; its signing status describes that earlier build pass. Signing the
current manifest has its own receipt and requires the original private key.
