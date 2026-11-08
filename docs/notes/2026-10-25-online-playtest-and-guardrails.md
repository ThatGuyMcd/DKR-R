# Online playtest and guardrails

The test build is `1.0.11-online-playtest.1`. Everyone playing together needs
this build. It keeps the existing online, spectator, memory and rendering fixes,
and the current launcher design.

The stock-menu failure in the supplied log came from known menu notifications
being rejected when their native owner was absent. The game had already verified
its checkpoint, and that run did not transfer any mod payloads. The menu guard
now accepts those known notifications. Unknown calls still stop safely.

Lobby setup has a larger, bounded transfer queue and sends the initial
checkpoint in larger batches. Downloads flush to disk in larger batches too.
Hash checks, consent, final flushes and atomic publication are still required.
Prepared mods can stay cached within a memory limit, reducing repeat preparation
on machines with enough spare RAM. New timings separate downloads, preparation
and rollback work so the next multiplayer test can show where time is going.

Character mod entries now show portraits using the existing vehicle card layout.
Loading runs in the background with bounded caches. It does not change which
mods are enabled.

## Test builds

The Windows ZIP, Linux AppImage, checksums, test reports and source bundle are in
`D:/DiddykongWorkFolder/DKR-R-v1.0.11-Online-Playtest-1-20261025`.

- Windows ZIP SHA-256:
  `7be17490e09e53d5bc1a1d680567aad0fa3ef780cb68293e57505bc5bfff4dbf`
- Linux AppImage SHA-256:
  `aca119d0b6a957d2aad7892bf25a78492e610cf588c6ce2b7a7dfe0d9a391bad`

Both platforms passed the retained release checks and the new menu, setup,
logging, party and portrait checks. All 61 installed character portraits decoded
to identical pixels on Windows and Linux. Package contents and compatibility
identities were checked. No game or user ROM was executed for these checks;
actual race, spectator and connection performance still needs the group test.

These are test builds. The stable version and signed updater files are unchanged.

## Integration with PR #76

The completed changes were rebased onto merged PR #76 (`723acf5`). All 28
playtest source files still match the sealed bundle. The two layers were already
completed before this integration. They use the documented `transition-layer`
exception, with 33 exact baseline entries covering those layers only. There are
no new blanket exclusions. The full move from patch layers into normal source
is a separate task.

Guardrail setup also exposed Windows Git differences. Repository detection now
uses a relative path so native Python can use both native Git and MSYS2 Git.
The agent stop check verifies the commit type directly, avoiding a revision
expression that MSYS2 Git on this machine misreads. Hook discovery also uses
relative paths. Regression tests cover checkout detection and ignored builds;
the existing stop-check tests verify that new violations still block completion.
Run the full Windows contributor suite with Git for Windows on PATH.

Local pre-commit and pre-push hooks are installed. The GitHub ruleset protects
`main` and release branches, requires the configured checks and CODEOWNER
approval, and dismisses stale approvals after changes. Its existing administrator
bypass is retained. CODEOWNERS remains `@ThatGuyMcd` and `@swhitt`.

Seven earlier planning drafts were preserved outside the checkout in
`D:/DiddykongWorkFolder/guardrail-integration-20261025/previous-planning-drafts`
and in `PREVIOUS-PLANNING-DRAFTS.zip`. Their bytes were verified after moving.
They are not part of this integration.

Guardrail logs and source verification are in
`D:/DiddykongWorkFolder/guardrail-integration-20261025`.
