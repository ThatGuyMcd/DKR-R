# Rollback self-test

A build with `DKR_EXPERIMENTAL_RACE_TEST` (every release preset except macOS
has it) can run a whole online race by itself: two to four copies of
`DKR-R-Runtime` on one machine meet in a lobby, start a rollback race and
drive it for a few seconds, then exit. It needs a ROM and nothing else: no
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

`-TestRoot` must not exist yet. `-Players` is 2 to 4, `-Seconds` 20 to 180
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

The rules it enforces before touching anything:

- `--config` is an absolute path to a folder that does not exist. It never
  runs against a real profile.
- The invitation file path is absolute. The host writes it, the joiner reads
  it.
- `--timeout` is 20 to 180 seconds, `--self-test-owned-players` 2 to 4.

Both peers must be given the same ROM revision.

## Reading the result

Each peer writes `logs\runtime.log` inside its profile folder. A good run has,
on the host:

```text
[rollback][check] normal Quick Join/countdown accepted; owners=2
[party][release] confirmed start epoch=.. frame=0
```

and on every peer `[rollback][render] epoch=.. frame=N` lines that keep
counting up until the timeout, then exit code 0. A stalled race shows as the
frame number stopping.

## Who is driving

The racers steer themselves along the track's checkpoints the way the game's
CPU racers do (`netplay/check_autopilot.cpp`), holding the accelerator. About
once a second each peer logs

```text
[check][autopilot] frame=.. stick=.. checkpoint=.. valid=1
```

`checkpoint` should climb as the lap goes on. `valid=0` means there was no
race to read (menus, loading) and the old pattern was used for that frame.

The menus are not steered: the test presses Start and A on a timer, so the
race is whatever the character, vehicle and track cursors start on.

## Switches

Set these in the environment of a peer before starting it. They only do
anything in a self-test run.

| Variable | Effect |
| --- | --- |
| `DKR_OWNED_CHECK_INPUT=wave` | Drive with the old pattern: accelerator held, stick left and right every 15 frames. |
| `DKR_OWNED_CHECK_MODERN=1` | Use the Modern presentation preset. Needed for the three below. |
| `DKR_OWNED_CHECK_FPS=60\|120\|180` | Fixed refresh target. |
| `DKR_OWNED_CHECK_API=Vulkan` | Force Vulkan. |
| `DKR_OWNED_CHECK_FULLSCREEN=1` | Fill the window with this peer's own racer. |
| `DKR_OWNED_CHECK_DELIVERY_MS=<n>` | Delay every route by `n` ms, to stand in for a real connection. `DKR_OWNED_CHECK_JITTER_MS`, `DKR_OWNED_CHECK_SPIKE_MS` and `DKR_OWNED_CHECK_LOSS_EVERY` add jitter, spikes and dropped packets on top. |
| `DKR_OWNED_CHECK_RECEIVE_MS=<n>` | Delay what this peer receives by `n` ms, with 15 ms of jitter. |
| `DKR_OWNED_CHECK_WATCH=1` | On a joiner: join as a spectator, not a racer. |

Two peers on one machine have almost no latency between them, so a plain run
never exercises late inputs. Use the delivery or receive delay when that is
what you are testing.
