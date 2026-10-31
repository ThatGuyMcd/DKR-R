# Services

Server-side code for DKR-R's hosted features. None is compiled into the game.
The runtime pattern guard scopes its pipeline rules out of `services/`.
The Online/profile API bundles maintained ES modules; website adapters splice
reviewed deployed bases through their own `build.py`.

| Directory | What it is | Base it slots into |
|---|---|---|
| [`library-workflow/`](library-workflow/README.md) | Mod library website and API workflow (batch entry, drafts, FTP picker) | the deployed portal; reviewed copy in `library-workflow/baseline/` |
| [`online-directory/`](online-directory/README.md) | Online directory and launcher profile API | maintained ES modules in `online-directory/api/` |
| [`mod-store/`](mod-store/README.md) | Mod Store launcher sign-in, launcher service and upload integrity | the deployed library API Worker and portal frontend |

Each `build.py` writes a fresh stage and never deploys anything; the
service's deployment notes are the owner's procedure. `tests/` in each
directory runs in CI (`.github/workflows/services-*.yml`).
