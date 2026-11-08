# Responsive online input test

This test is for checking how the controls feel during online play. The
launcher layout, manual input delay, current online fixes and mod handling
are kept in place. There are no website or API files to deploy.

The previous controller could add nine frames of input delay, or 300 ms at
30 ticks per second. It used the worst racing connection for the whole room,
allowed only two frames of routine correction, and needed ten quiet seconds
before trying a lower delay. Smooth frame delivery could still feel heavy.

## What changed

Receivers now report delivery separately for each input owner. The host uses
those reports to adjust the sending player's buffering. It no longer assigns
one shared delay to all racers. Spectators do not set racing input delay.

Late input that fits inside the rollback history can be corrected without
automatically increasing delay. Two frames of history remain reserved for
scheduling variation and recovery. After two stable seconds, the controller
can try one frame less delay even when inexpensive corrections are occurring.
Ping is recorded, but ping alone no longer determines the delay target.
After a real input wait or an exhausted prediction margin, a ten-second
recovery interval prevents repeatedly probing the same failed lower delay.
Healthy connections do not need that recovery interval before reducing delay.
The party runtime also waits for the actual tick deadline before publishing
new dynamic controls. It no longer authors the next tick early during a pacing
turn. Physical changes are observed between ticks; stick positions coalesce
and button transitions are retained. Manual scheduling is unchanged.
The emergency maximum-delay setting retains bounded early prefill so that
narrow-window recovery does not lose its existing safety margin.

Measured simulation and replay work protects machines that cannot afford
those corrections. If owner-thread work exceeds 70 percent of an interval,
with replay exceeding 20 percent, late input can trigger more buffering.
Reductions hold above 50 percent work with more than 10 percent replay work,
leaving a middle band to avoid repeated increases and decreases near the limit.
An owner whose inputs already arrive on time can still reduce its own delay.
These are conservative operating thresholds, not a hardware benchmark.

Targets are authenticated and scoped to the room, scene, controller generation,
member and input owner. Commands and acknowledgements are retried; duplicate
or old commands cannot reapply a change. Reductions drain already published
future inputs. Increases add held input. Neither operation rewrites an input,
changes a simulation clock or discards a button press/release.
The new delay payload uses extension tag 5, separate from proxy tag 3 and chat
tag 4. Real-session checks require successful negotiation, not just a race that
renders successfully with the manual fallback.

The maximum rollback window remains 20. The maximum added input delay remains
nine frames as an emergency limit, rather than a desired operating point.
Existing saved rollback settings are respected. A narrow rollback window still
requires more buffering on a distant connection.

## Measurements

The log records captured-input-to-assignment, execution, publication and
renderer-submission timings. Samples use the same computer's steady clock.
The ledger is fixed at 128 entries and reports counts, percentile bounds and
overwritten entries every five seconds. Renderer submission is not monitor
scanout and does not include controller hardware or display latency.

The party driver records its actual step cost. The ordinary driver uses an
owner-local scope around each step. Both publish speculative gameplay frames;
the renderer records the corresponding immutable frame after screen update
submission. Correction work is separate from a measured network-input wait.

Camera presentation remains speculative and local. Audio effects, persistence,
scene changes, checkpoint checks and GPU retirement keep their existing safety
rules. Making irreversible effects speculative to hide latency is outside this
change. The existing checkpoint reuse and delta-copy optimisations are retained.

## Qualification

Production party fixtures cover two, three and four racers, 150/300 ms RTT,
up to 20 ms added jitter per hop, live packet loss, and a one-second outage.
They exercise narrow, 12-frame and 20-frame prediction windows. They verify
immutable transmitted inputs and byte-identical confirmed state after 1,800
frames. The world in these fixtures is synthetic; it is not a game CPU test.

Qualification records the actual wait duration, replay count and mean delay
for each owner and prediction window, including 300 and 600 ms cases. Results
from lightweight state do not establish universal zero delay in real races.

Separate controller tests cover asymmetric routes, measured replay pressure,
lost target commands and acknowledgements, stale scenes, invalid command
origins, and retained button edges. Healthy owners reached zero added delay
while an impaired owner retained a higher target.

Matching Windows and Linux AppImage builds are required. The existing private
test compatibility ledger is used, as already authorised. Both-owner review is
still required before merging these compatibility changes. No merge, release
publication or stable updater change is part of this test.

Real WAN sessions and Steam Deck hardware testing remain necessary. Compare
the response timings, local delay target, replay work, correction depth and
waiting together. Zero added delay is useful only while gameplay remains
playable on the actual machines.
