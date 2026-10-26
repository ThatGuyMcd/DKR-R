# Mod Store backend

What the launcher's Mod Store needs from the library API Worker: launcher
sign-in (`launcher-auth.inl`), the launcher service (`launcher-service.inl`)
and upload integrity (`upload-integrity.inl`), plus the portal's catalogue
permissions and upload checksums (`frontend.py`, `upload-hash.inl`).
`launcher-schema.sql`, `web.config`, `release_manifest.py` and
`backfill_library.py` are the owner's deployment pieces.

The base is the deployed library API Worker and portal frontend, outside this
repo.

    python -B services/mod-store/build.py --worker <worker-single-file.mjs> \
        --frontend <copy of the portal> --output build/NEW-mod-store

Each splice must match its anchor exactly once. `tests/fixture/` is a minimal
base holding each anchor once; `tests/build_tests.py` stages over it and checks
that losing any anchor fails. Deploy with [OWNER-SETUP.txt](OWNER-SETUP.txt).
The game's Mod Store client is `runtime-recomp/src/game/mod_store*.cpp`.
