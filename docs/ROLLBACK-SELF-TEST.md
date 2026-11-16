# Rollback self-test

A build with `DKR_EXPERIMENTAL_RACE_TEST` (every release preset has it) can
run a whole online race by itself: two to four copies of `DKR-R-Runtime` on one
machine meet in a lobby, start a rollback race and drive it for a few seconds,
then exit. It needs a ROM and nothing else: no
account, no saved profile, nobody at the controls.

It is the quickest way to check that two builds still play together, and to
watch a rollback race without finding a second player.

## Running it

`scripts/Test-IntegratedRollback.ps1` starts the peers, waits for them and
reads their logs:

```powershell
scripts\Test-IntegratedRollback.ps1 `
    -Binary  C:\path\to\libexec\dkr-r\DKR-R-Runtime.exe `
    -Rom     C:\path\to\dkr-us-v77.z64 `
    -TestRoot C:\temp\rollback-check-1
```

The script is Windows PowerShell. On macOS and Linux start the peers yourself,
below. `-TestRoot` must not exist yet. `-Players` is 2 to 4, `-Seconds` 20 to 180
(default 45). `-Presentation Modern`, `-TargetFps` and `-GraphicsApi Vulkan`
choose what is drawn.

To pair two different builds, or to keep the windows up and watch, start the
peers yourself. Each one needs its own new profile folder and the same
invitation file:

```powershell
$rom = 'C:\path\to\dkr-us-v77.z64'
$invite = 'C:\temp\check\invite.txt'

# host
& 'C:\build-a\libexec\dkr-r\DKR-R-Runtime.exe' --rom $rom --config C:\temp\check\host `
    --self-test-owned-host $invite --self-test-owned-players 2 --timeout 60

# joiner, a few seconds later, from another terminal
& 'C:\build-b\libexec\dkr-r\DKR-R-Runtime.exe' --rom $rom --config C:\temp\check\join `
    --self-test-owned-join $invite --self-test-owned-players 2 --timeout 60
```

The same thing in a POSIX shell. The runtime is the built executable
(`bin/Release/DKR-R` in a macOS build tree; the Windows package has
`libexec\dkr-r\DKR-R-Runtime.exe`):

```sh
rom=/path/to/dkr-us-v77.z64
invite=/tmp/check/invite.txt
runtime=/path/to/DKR-R
mkdir -p /tmp/check   # the profile folders below must not exist yet

"$runtime" --rom "$rom" --config /tmp/check/host --self-test-owned-host "$invite" \
    --self-test-owned-players 2 --timeout 60 &
sleep 5   # the joiner waits for the invitation file anyway
"$runtime" --rom "$rom" --config /tmp/check/join --self-test-owned-join "$invite" \
    --self-test-owned-players 2 --timeout 60
wait
```

The rules it enforces before touching anything:

- `--config` is an absolute path to a folder that does not exist. It never
  runs against a real profile.
- The invitation file path is absolute. The host writes it, the joiner reads
  it.
- `--timeout` is 20 to 180 seconds, `--self-test-owned-players` 2 to 4.

Both peers must be given the same ROM revision.

### Two machines

The invitation is a Quick Join code, so the peers do not have to share a
disk. Start the host, wait for it to write the invitation file, copy that
file (it is just the code) to the other machine at an absolute path, and start
the joiner there with `--self-test-owned-join <that path>`. Both machines need
internet: Quick Join meets through `wss://0.peerjs.com` and a Google STUN
server, and there is no relay, so a strict NAT between them can fail. Run it
once with each machine hosting.

## Reading the result

Each peer writes `logs\runtime.log` inside its profile folder. A good run has,
on the host:

```text
[rollback][check] normal Quick Join/countdown accepted; owners=2
[party][release] confirmed start epoch=.. frame=0
```

and on every peer `[rollback][render] epoch=.. frame=N` lines that keep
counting up until the timeout, then exit code 0. A stalled race shows as the
frame number stopping. A clean exit does not show the peers agree: compare
their state hashes (below).

## Who is driving

The racers steer themselves along the track's checkpoints the way the game's
CPU racers do (`netplay/check_autopilot.cpp`), holding the accelerator. Every
60 race frames (about two seconds) each peer logs

```text
[check][autopilot] frame=.. stick=.. checkpoint=.. valid=1
```

`checkpoint` should climb as the lap goes on and wrap to 0 at the line.
`frame` is the predicted frame, so it runs a little ahead of the last
`[rollback][render]` line. `valid=0` means the old pattern was used for that
frame: there was no race to read (menus, loading), or
`DKR_OWNED_CHECK_INPUT=wave` is set.

Each peer joins the lobby under a name it picks for itself, a race number
and a moniker such as `#27 Ping Kong`, and logs it as `[check][name]`. The
lists live in `netplay/check_names.hpp`; anything added there is checked
against the lobby's name rules when it compiles.

The menus are not steered: the test presses Start and A on a timer, so the
race is whatever the character, vehicle and track cursors start on.

## Comparing state between peers

A lobby whose racers are all on one CPU architecture does not cross-check
simulation state during a race. Inputs are exchanged, and state is compared
only when the checkpoint is admitted and at scene boundaries (the menu-to-race
hand-over). A mid-race divergence there would not stop the session or log a
failure, so a clean exit code says little about whether two peers still agree.
A lobby that spans architectures does check itself and stops on a mismatch
(`docs/CROSS-ARCHITECTURE.md`, "Live state check"); `DKR_OWNED_CHECK_VERIFY=1`
turns the same check on for any self-test.

To check it, every peer of a self-test run logs the hash of its confirmed
state at a fixed cadence (every 30 confirmed frames, `DKR_OWNED_CHECK_STATE_EVERY`):

```text
[check][state] epoch=92 frame=300 hash=7DC1FBCE66916E54 us=312
```

`hash` is the hash saved with that confirmed frame's checkpoint, the whole
simulation state, so it matches between peers whenever they agree. `us` is what
reading it cost on that peer (about 0.3 ms on an M-series Mac, a few ms at worst),
nothing else. Nothing is sent and nothing it reads is changed; it only runs in
`--self-test-owned-*` runs.

Compare the `runtime.log` files of two or more peers:

```sh
python3 scripts/compare_state_hashes.py host/logs/runtime.log join/logs/runtime.log
```

It prints how many frames both logs share and the first mismatching
`(epoch, frame)` if any. Exit 0 means every shared frame is equal, 1 means a
mismatch, 2 means the input is unusable (no state lines, no frames in common,
or fewer than `--minimum` frames compared, 20 by default, so a run that barely
started doesn't count as equal). It only speaks for the frames that were
logged, and logs from different runs aren't expected to agree.

## Switches

Set these in the environment of a peer before starting it. They only do
anything in a self-test run.

| Variable | Effect |
| --- | --- |
| `DKR_OWNED_CHECK_NAME=<name>` | Race under this name (1 to 24 printable ASCII characters). Without it each peer picks its own (see "Who is driving"). |
| `DKR_OWNED_CHECK_INPUT=wave` | Drive with the old pattern: accelerator held, stick left and right every 15 frames. |
| `DKR_OWNED_CHECK_MODERN=1` | Use the Modern presentation preset. Needed for the three below. |
| `DKR_OWNED_CHECK_FPS=60\|120\|180` | Fixed refresh target. |
| `DKR_OWNED_CHECK_API=Vulkan` | Force Vulkan. |
| `DKR_OWNED_CHECK_FULLSCREEN=1` | Fill the window with this peer's own racer. |
| `DKR_OWNED_CHECK_DELIVERY_MS=<n>` | Delay every route by `n` ms, to stand in for a real connection. `DKR_OWNED_CHECK_JITTER_MS`, `DKR_OWNED_CHECK_SPIKE_MS` and `DKR_OWNED_CHECK_LOSS_EVERY` add jitter, spikes and dropped packets on top. |
| `DKR_OWNED_CHECK_RECEIVE_MS=<n>` | Delay what this peer receives by `n` ms, with 15 ms of jitter. |
| `DKR_OWNED_CHECK_WATCH=1` | On a joiner: join as a spectator, not a racer. |
| `DKR_OWNED_CHECK_STATE_EVERY=<n>` | Log `[check][state]` every `n` confirmed frames (1 to 100, default 30). 1 is every frame. |
| `DKR_OWNED_CHECK_MODS=<absolute dir>` | Before the lobby forms, install every `.nrm` or `.dkrcode` in that directory through the ordinary Custom Magic Code library (import, select, approve, enable), as if the player had done it in the Mods page. The host offers them to the lobby; a joiner that sets it too accepts the lobby's exact mods, code included. The path must be absolute and exist, or the run refuses. Logs `[check][mods] installed id=... digest=...`. A joiner without it never consents and waits in the lobby. |
| `DKR_OWNED_CHECK_TRACK=<0..3>` | Race a different world's track: the host steers the real track-select cursor down that many rows and every peer then confirms. Set it on every peer (the others hold A until the cursor is in place). A fresh session save unlocks only the first track of each world, each with its default vehicle (choosing a vehicle needs a cleared race), so 0 is the default and nothing else can be selected. Logs `[check][course] steering ...` at start and `[check][course] raced level=.. vehicles=..` once the race is running. Any other value refuses. |
| `DKR_OWNED_CHECK_CONNECTION_TEST=1` | Host only. Once the lobby is full and everyone is ready, run the Rollback connection check (the Connection panel button) and wait for it, up to 30 s, before starting the race. Logs `[check][connection-test] complete=.. party=.. rows=..` and one line per peer. `DKR_OWNED_CHECK_PARTY=0` on the host asks for the classic lobby instead of the party lobby. |
| `DKR_OWNED_CHECK_PARTY=0` | Host: make a classic four-port lobby instead of the party lobby the check uses by default. |
| `DKR_OWNED_CHECK_CHAT=1` | Once the lobby chat is up, each peer sends one fixed message through the ordinary session chat and logs `[check][chat] sent role=host\|joiner text="..."`, then `[check][chat] received from=host\|joiner text="..."` once per message. The host holds the race start until it has the joiner's message (and 3 s for its own to arrive), up to 25 s. Set it on every peer. |
| `DKR_OWNED_CHECK_CHAT=off` | The same, with the host's chat switch off. Both peers log `[check][chat] send-refused ... error="The host has disabled lobby chat."` and neither logs a `received` line. |
| `DKR_OWNED_CHECK_PROXY=1\|force` | Needs `--self-test-owned-players 3` or 4. On the host, turn on proxy routes; on each joiner, volunteer to relay. This process only: nothing is written to the saved settings. `force` also makes the host prefer a fresh proxy route to the direct one, which on loopback otherwise never wins. Logs `[check][proxy] <value> role=host\|joiner`; a relayed endpoint then logs `[online][proxy] endpoint=<id> event=selected ... one-hop=1`, and a relaying peer logs `[online][proxy] teardown forwarded-bytes=<n> dropped=<n>` when the session ends. Set it on every peer. |
| `DKR_OWNED_CHECK_VERIFY=1` | Turn on the live state cross-check (`docs/CROSS-ARCHITECTURE.md`) even though every peer is on one architecture. Set it on every peer, all of which must be builds that have the check. |
| `DKR_OWNED_CHECK_CORRUPT_STATE_AT=<frame>` | This peer reports a wrong state hash for the first sampled frame at or after `frame`, so the check's stop shows up on every peer. Needs the check on. |

Two peers on one machine have almost no latency between them, so a plain run
never exercises late inputs. Use the delivery or receive delay when that is
what you are testing.
