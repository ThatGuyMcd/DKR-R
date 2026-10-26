# Verification

The Node test suite uses the actual composed Worker, SQLite transactions and the
actual composed browser clients. It blocks external requests and FTP writes.

Coverage includes:

- Staff permissions, anonymous/player denial, private batch ownership and invalid
  upload references; publication keeps the existing approved-host and checksum gates.
- Draft/batch revision conflicts, idempotent creation/publication, lost save and
  publication responses, edits made after a lost save response, partial batch
  failure and retry without duplicate listings.
- Inventory usage across releases, archived listings, catalogue drafts and saved
  batches; full-address matching preserves folder identity.
- Consecutive images without page reload, five retries with bounded backoff,
  exhaustion after six attempts, lost chunk acknowledgements, transfer creation,
  finalisation, checksum registration and mismatch rejection before resuming bytes.
- Hosted/local batch creation, filename artwork matching, saved recovery,
  original-file reselection and clearing private state on sign-out.
- The FTPS transport module is byte-for-byte identical to the frozen baseline.
  Website Vehicles labels retain the `legacy` API key and existing FTP paths.

Browser checks used an isolated localhost preview with synthetic accounts and
database records, and the real staged website scripts/styles. Verified:

- All / Not in Mod Library reduces the package list correctly.
- FTP image selection can be removed without refreshing.
- Multiple existing FTP files become separate editable entries.
- Shared details, review and publication of two entries, with per-entry results.
- Publish and add another opens a clean form, retaining only the chosen creator
  default; title and download URL clear.
- Vehicles is present in navigation, category selection and installation help.
- The bulk page fits a narrow 390px mobile viewport without horizontal overflow.

The automated suite is run against the final staged bundle before packaging.
Run it again with the command in DEPLOYMENT.md when rebuilding.

The optional saved-draft ID and revision are declared in the batch object
initializer so JavaScript type inference recognizes them. A targeted TypeScript
5.9.3 check of workflowBatchContent, using inference from the entire composed
Worker, reproduces the original two TS2339 diagnostics and verifies their removal.
Saved IDs/revisions and rejection of malformed values have runtime regression
coverage as well.

Live Discord sign-in, actual FTP transfer and production D1 migrations were not
exercised. The owner requested a deployment bundle and will deploy it. Follow the
post-deployment smoke checks in DEPLOYMENT.md using an existing staff account.

The r3 FTP picker regression also runs the complete exported Worker fetch entry
point in Miniflare 4.20260730.0 with Cloudflare's local D1 implementation. This
reproduces the original /v1/assets HTTP 500: D1_ERROR, too many terms in compound
SELECT. Standard SQLite alone allowed that query. The corrected query is checked
against empty inventory usage, archived items, historical releases, private saved
drafts and batches, malformed older JSON, unused files and account permissions.
The final r3 bundle passes 71 workflow checks and 21 Cloudflare D1 runtime
checks. The earlier r2 Worker fails the new D1 regression at the FTP picker
request with the same HTTP 500 service_error reported in production.
