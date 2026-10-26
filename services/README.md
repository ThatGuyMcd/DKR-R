# Services

Server-side code for DKR-R's hosted features. None of it is compiled into the
game, and the runtime's pattern guard doesn't apply here (`scripts/pattern_guard.py`
scopes its pipeline rules out of `services/`): each service is spliced into a
deployed base that lives outside this repo, by its own `build.py`.

| Directory | What it is | Base it slots into |
|---|---|---|
| [`library-workflow/`](library-workflow/README.md) | Mod library website and API workflow (batch entry, drafts, FTP picker) | the deployed portal; reviewed copy in `library-workflow/baseline/` |
| [`online-directory/`](online-directory/README.md) | Online directory Worker and the API gateway in front of it | the library API Worker (the library workflow's stage, or the live one) |
| [`mod-store/`](mod-store/README.md) | Mod Store launcher sign-in, launcher service and upload integrity | the deployed library API Worker and portal frontend |

Each `build.py` writes a fresh stage and never deploys anything; the
service's deployment notes are the owner's procedure. `tests/` in each
directory runs in CI (`.github/workflows/services-*.yml`).
