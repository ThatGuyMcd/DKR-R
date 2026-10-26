# Library workflow

The mod library website and its private API Worker: batch entry, saved drafts,
publication retry and the FTP picker. The authored pieces are `backend.inl`
and `ui.inl` (spliced into the Worker and the website app), `batch-client.js`,
`workflow.css`, `workflow-schema.sql` and `transforms.py` (every splice).

The base is the deployed portal (the `/dkr-r` website directory and the
`dkr-mod-library-api` Worker), outside this repo. `baseline/` holds the copy
this update was reviewed against, with `baseline/SHA256SUMS`.

    python -B services/library-workflow/build.py stage --output build/NEW-library-workflow/stage
    python -B services/library-workflow/build.py stage --base <copy of the live portal> --output ...
    node services/library-workflow/tests/run.cjs build/NEW-library-workflow/stage

Each splice must match its anchor exactly once, so a live portal that moved on
fails the stage instead of deploying half an update. Deploy with
[DEPLOYMENT.md](DEPLOYMENT.md); [QA.md](QA.md) lists what the tests cover.

Tests: `tests/build_tests.py` (the stage), `tests/run.cjs` (workflow, retry,
permission and transport checks on the staged bundle) and `tests/d1-runtime.cjs`
(the full Worker in Miniflare with a local D1 database).
