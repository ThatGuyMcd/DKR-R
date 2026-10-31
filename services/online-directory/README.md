# Online directory and launcher profiles

`directory.mjs` owns the SQLite Durable Object directory. The maintained API
modules in `api/` own library authentication, directory service-binding ingress,
Discord profile metadata and launcher picture/key registration. The API entry
is `api/worker.mjs`; its library and profile modules remain ordinary source.

Install the pinned local tools and build a fresh owner bundle:

```sh
npm ci --prefix services/test-tools --ignore-scripts
python3 services/online-directory/build.py --output build/NEW-online-directory
python3 services/online-directory/tests/build_tests.py
node services/online-directory/tests/runtime.cjs build/NEW-online-directory services/test-tools/node_modules/miniflare
node services/online-directory/tests/runtime.cjs build/NEW-online-directory services/test-tools/node_modules/miniflare --running
```

The build uses esbuild 0.28.2 from the pinned tools by default; `--esbuild FILE`
selects another executable of that version. `--rollback-api FILE` and
`--rollback-directory FILE` copy exact owner-supplied backups into the bundle.
`--verify --output DIR` checks its output hashes, and `--archive FILE` creates
a new archive. The build does not deploy anything or overwrite existing output.

With host dependencies prepared, the native/Worker proof integration runs with:

```sh
cmake --preset host -S runtime-recomp
cmake --build build/host --target DKRProfileBridgeTests --parallel 4
node services/online-directory/tests/profile_api.cjs build/NEW-online-directory build/NEW-profile-fixtures services/test-tools/node_modules/miniflare build/host/DKRProfileBridgeTests
```

Use a fresh fixture directory. Tests create synthetic accounts and temporary
native identities, run D1 and the directory locally in Miniflare, and verify
Monocypher proofs against Worker Web Crypto. They use no deployed service or
real credentials. The directory test also supports `--lease` and `--load`.

Deployment and rollback remain owner actions described in
[DEPLOYMENT.md](DEPLOYMENT.md). Reconcile any newer owner API edits with the
modules before producing a bundle; an old live Worker is no longer a build input.
