# Storage scheduling follow-up — 7 November 2026

The first large-profile cleanup completed, but users could still hear repeated
background work after it finished. The service's daily timer was not the only
entry: every artwork decode queued an image-cache pass and completed installs
queued full cleanup. Their generic status also replaced the previous totals.

Automatic cleanup now has one startup budget per application lifetime, beginning
after a ten-second grace period. Artwork/install hints do not schedule more work.
The worker parks after completion instead of polling every thirty seconds.
Re-enabling the setting does not repeat a completed pass. A manual full cleanup
also satisfies the startup budget. Manual inspection remains available, and a
cleanup no longer automatically follows itself with a full profile inventory.

Gameplay changes the work pacing rather than cancelling maintenance. The worker
uses native background CPU/I/O priority; bounded reads and cooperative work/rest
slices allow foreground operations to interrupt it. Progress reports the stage
and retains cleared totals across deferrals. No game simulation or peer identity
change is needed.

Online synchronization previously held the general storage lease throughout a
lobby. Its existing checked policy now uses a separate online lease. Maintenance
can clear unrelated files while that lease pins online caches, preparation jobs
and immutable package pruning. Short publication/cache reads still use the
general barrier. New online work can interrupt a pass; active imports, downloads,
other processes, current installations and recovery references retain protection.

Tests cover the real startup timer, repeated artwork/install hints, preference
toggles, another launch, gameplay continuation, active online leases and streamed
checksum equivalence. Package and device coverage are recorded separately in the
release handoff. The previous storage investigation retains its original scope.
