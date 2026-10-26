# Website and API update

This package changes the mod-library website and private Cloudflare Worker only.
The launcher, its endpoints, existing category IDs and FTP paths are retained.
The category displayed as Vehicles still uses the existing `legacy` identifier
and `downloads/Legacy` / `images/Legacy` folders for compatibility.

## Stage and verify

Run from the repository root with Python 3.11+ and Node.js 24+:

    python -B services/library-workflow/build.py stage --output build/NEW-library-workflow/stage
    node services/library-workflow/tests/run.cjs build/NEW-library-workflow/stage
    python -B services/library-workflow/build.py verify --output build/NEW-library-workflow/stage

`build.py` stages over `baseline/`, the deployed portal this update was
reviewed against, and checks it against `baseline/SHA256SUMS`. If the live
portal has moved on, copy it into a directory with the same layout and pass
`--base DIR`; every splice must still match its anchor exactly once. All
authored changes are in services/library-workflow; it doesn't touch game
sources or build inputs.

## Deploy

1. Back up the current Worker source, D1 database and affected FTP website files.
2. Deploy PRIVATE-API/worker-single-file.mjs to the existing
   dkr-mod-library-api Worker. Preserve every binding, secret and configuration
   value. Never put PRIVATE-API or SQL in a public FTP directory.
3. The Worker creates only the additive workflow batch table/index on first use.
   Administrators may instead apply workflow-schema.sql to the existing D1 DB.
4. Merge FTP-PATCH into the existing /dkr-r website directory. Upload the assets
   first and index.html last. Preserve config.js, sign-in assets, images, mods and
   unrelated files. Existing asset URLs in the new index have a new cache version.
5. Check /health for workflowRevision 20261023-2, then verify with a staff account:
   two consecutive image/package uploads; remove/replace; both inventory filters;
   publish and add another; batch recovery and publication. Use owned test files.
6. The filters use the existing FTP inventory plus completed website uploads.
   Files added independently through an FTP client still need the existing
   inventory-update process. The filter does not scan the FTP tree on each click.

Uploads retain the same 4 MiB chunks, FTPS write/readback verification, final
rename, integrity registration and owner checks. Transient upload operations
receive up to five automatic retries (six attempts including the first), using
1, 2, 4, 8 and 16 second backoff. Chunk retries read confirmed offsets first.
Invalid files, conflicts and access denials require attention immediately.

## Recovery and rollback

Batch metadata autosaves to the signed-in account; file bytes and access tokens
are never saved to browser storage or the batch database. After reopening a
batch, reselect the original files for incomplete uploads. A failed recovery
save stays visible: keep the page open and retry Save batch before closing it.

ROLLBACK contains the exact accepted files changed by this update. Restore its
private Worker and FTP files, with index.html last. The added workflow table may
remain; rollback does not drop it or discard saved batches. The new standalone
batch-client.js and workflow.css are no longer referenced after rollback.

This is a website deployment bundle, not a launcher release. It contains no
Windows executable, Linux AppImage or other game build.

## FTP picker correction (r3)

If the workflow website files are already deployed, replace only the private
Worker with this package's PRIVATE-API/worker-single-file.mjs. The public FTP
files are unchanged from r1/r2. Existing bindings, secrets and database records
must be preserved. No additional schema migration is needed for this correction.

The previous inventory usage query combined six SELECT statements, exceeding
D1's compound SELECT limit and causing /v1/assets to return service_error.
The correction batches smaller reads and inspects saved draft/batch JSON in the
Worker. It retains both inventory filters and all usage sources.

To run the additional Cloudflare runtime regression test locally:

    npm install --prefix build/library-workflow-d1-runtime --no-save --ignore-scripts --package-lock=false miniflare@4.20260730.0
    node services/library-workflow/tests/d1-runtime.cjs build/NEW-library-workflow/stage

This test uses an ephemeral local D1 database and synthetic accounts. It does
not connect to the production database, sign in to Discord or transfer FTP files.
