# Online directory

The Public/Friends lobby directory: `directory.mjs` is its own Worker (a
SQLite-backed Durable Object, deployed with `wrangler-online.jsonc`), and
`gateway.inl` goes in front of the library API Worker so the API can answer
`/v1/online/*` through a private service binding.

The base is the library API Worker, outside this repo: the live
`worker-single-file.mjs`, or `PRIVATE-API/worker-single-file.mjs` from a
`services/library-workflow/build.py` stage.

    python -B services/online-directory/build.py --api-worker <worker-single-file.mjs> --output build/NEW-online-directory
    node services/online-directory/tests/runtime.cjs build/NEW-online-directory <miniflare package dir>

`--expect-sha256` pins the base to the bytes you reviewed. Deploy with
[DEPLOYMENT.md](DEPLOYMENT.md), then follow [PLAYTEST.txt](PLAYTEST.txt);
[QA.md](QA.md) and [IMPLEMENTATION.txt](IMPLEMENTATION.txt) describe the checks
and the protocol. `tests/runtime.cjs` also takes `--running` (running spectator
entry), `--lease` and `--load`.
