# Mod Library administration

The [website library](https://positron-portal.com/dkr-r/) and launcher Mod Store
use the existing library API and role-controlled Discord account flow. v1.0.12
does not change the FTP hosting/upload method or add a public lobby website UI.

## Add one entry or a batch

Use the website's library tools with an authorised account. Choose existing
FTP-hosted images/downloads or upload through the current flow. The image
selection has a clear/remove action; completing an entry resets its form state
so a previous image does not populate the next entry.

For multiple entries, create a batch and use shared defaults, then review each
row's metadata, image and download before publishing. A batch supports up to
50 entries. Select only the rows you intend to publish; retained drafts and
per-row status allow correction/retry without recreating successful entries.

Upload failures have up to **five automatic retries after the initial attempt**
before the final error. Cancellation remains available. Retrying a transient
failure does not grant permission to publish an invalid or incomplete entry.
This release does not claim a faster FTP service or change its transfer backend.

## Existing FTP files

Both the image picker and mod/download picker offer **All** and **Not in Mod
Library**. All shows the eligible hosted files. Not in Mod Library excludes
files already referenced by a library entry, so unpublished assets are easier
to find. The filter changes the view; it does not delete files or create entries.

Use a real saved image file when Discord clipboard/paste content is rejected.
Clipboard data may not be the original encoded upload. The exact server error
and original filename/format are needed to diagnose another failure; avoid
renaming an unsupported file to `.jpg` as a conversion step.

## Vehicles and API compatibility

The old Legacy Mods public label is **Vehicles**. Existing published identities,
reviews, requests and hosted mappings retain the backend `legacy` category ID.
The launcher supports `.dkrkart` cars and packages; reserved hover/plane formats
must not be advertised as supported imports. See [Vehicles](VEHICLES.md).

## Deployment and rollback

Build the library workflow bundle with
[`services/library-workflow/build.py`](../services/library-workflow/README.md)
and follow its [deployment instructions](../services/library-workflow/DEPLOYMENT.md).
Preserve the live database, secrets, account bindings and upload configuration.
Keep a source/configuration backup before replacing the website/API files.

The Online directory composes over that API. Follow the separate
[directory deployment guide](../services/online-directory/DEPLOYMENT.md) for its
private Worker, binding and feature flags. Worker JavaScript is deployed to
Cloudflare, not copied to the FTP site. Compare newer live API edits with the
bundle baseline before replacing anything. No deployment is performed by the
native release or documentation pipeline.
