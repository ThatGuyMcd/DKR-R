# Cross-architecture play

Rollback only works if every peer computes exactly the same game state from
the same inputs. Peers on different CPU architectures race each other as long
as their builds make the same promise about how they compute. Nothing lists
the architectures that are allowed: a build for a CPU nobody has tried yet
joins in if it can make the promise.

## If you have a new target

The short path, each step described below:

1. Build the game with `DKR_EXPERIMENTAL_RACE_TEST=ON` and the strict floating
   point flags, and read the `[online][contract]` line it logs (see "Porting
   to a new platform").
2. Run `scripts/port_check.py` against a machine that already races. It
   builds the replay probe for your toolchain and compares every byte (see
   "Checking a new target"). Do not go further until it says `IDENTICAL`.
3. Race your build against that machine with the self-test, in both host and
   joiner seats (`docs/ROLLBACK-SELF-TEST.md`, "Two machines").
4. Add what you ran to the tables under "Evidence".

## The contract

Each build puts a floating point contract in its lobby manifest
(`floating_point_mode`, from `netplay/determinism_contract.hpp`). Admission
refuses a peer whose contract differs with "This platform pair has not passed
deterministic compatibility."

`strict-ieee754-v1` means:

- `float` and `double` are IEEE 754 binary32 and binary64,
- the target is little-endian,
- intermediate results are evaluated in their own type (`FLT_EVAL_METHOD` 0,
  so no x87 extended precision) and fast-math is off,
- contraction is off (`-ffp-contract=off`, `/fp:strict`), so an FMA-capable
  CPU does not fuse what another computes in two steps.

The first three bullets are checked at compile time. The last is a build flag the
simulation sources are compiled with; keep it when adding a toolchain. A
build that fails a compile-time check advertises `unverified-<architecture>`
and only ever meets other builds for that architecture.

The game code itself is recompiled N64 code running against a 32-bit guest
memory image, which is why it does not care about the host's pointer size or
calling convention. What can differ between hosts is floating point, and that
is what the contract pins down.

Gameplay mods are covered the same way: every peer must hold the same mod
manifest and prepared resources (their hashes are in the same manifest), and
mod code runs under the same contract as the game.

## What stays compatible

Every racer has to agree on one compatibility hash before a race launches.
There are two forms of it:

- `manifest_hash` includes the architecture. It is what every build before
  contracts computes.
- `manifest_class_hash` leaves the architecture out.

The host puts the second form in the launch only when the lobby actually has
peers on more than one architecture, and a racer accepts either form of its
own manifest. So a lobby on one architecture launches exactly as it always
has, including against a build of the same release that predates this. A
lobby that spans architectures needs every peer to have it.

One case does not end cleanly: a host with contracts admits an older build of
the same release on another architecture, and that peer never accepts the
launch. The host cancels the start after 30 seconds. Releases cannot join
each other at all, so this only happens between development builds and the
release they are based on.

The host says so when it cancels. The status line names the racers that did not
acknowledge, and adds "This lobby spans CPU architectures, so every racer needs
a build that supports that." when the launch used the architecture-free hash. The
countdown line in the log shows which form the launch used:

```text
[lobby][countdown] five seconds; racers=3 launch-hash=class
```

`launch-hash=full` is the hash earlier builds compute, `class` the architecture-free
one. Note that a host on one architecture and every joiner on another also counts as
spanning: the host's own architecture is the reference.

## Live state check

Rollback netplay only sends controller inputs. Every machine then works out
the game for itself, so if two machines ever compute something different (a
compiler that rounds differently, a port that quietly breaks the contract)
nothing in the game would notice: the racers would just drift apart. For a
lobby that spans architectures the game therefore checks it while it runs, so a
divergence becomes a clear message instead of a silent desync.

When does it run? The host uses the architecture-free launch hash only when a
peer from another architecture was admitted into the lobby (spectators count:
they have to accept the launch hash too). Every racer in such a lobby runs the
check (`lobby_spans_architectures`, `netplay/state_cross_check.hpp`). Look for
this line at the start of a race:

```text
[party][desync-check] enabled reason=lobby spans architectures
```

No line means the lobby is on one architecture and nothing new is sent.

How it works:

- Every 30 confirmed frames each racer takes the hash saved with that frame's
  retained checkpoint (the value the self-test prints as `[check][state]`,
  hashed once) and sends `(epoch, frame, hash)` to the host. The host compares
  every report with its own hash of the same frame. Frames are numbered from
  the start of the epoch, so a racer that joins mid-race lines up too.
- Memory and traffic are bounded: one 20 byte message per racer per 30 frames,
  at most 16 sampled frames kept on the host, and a report that is off the
  cadence, from someone who is not a racer, repeats a frame with a different
  hash, or is older than everything kept is dropped and changes nothing.

What you will see if the two sides disagree, on every machine in the lobby:

```text
[party][desync] epoch=92 frame=1500 local=217D59FBE74B7E2A remote=217D593B18A520C7 peer=3288762986889906221
[party][failure] Rollback determinism failed at simulation frame 1500.
```

`local` is this machine's hash and `remote` the other side's, for the first
frame that differed, and `peer` is the sender id of the machine that disagreed
with the host. The race stops for everyone, a `rollback-desync` entry with both
hashes is written to the flight recorder dump that follows, and the screen says
"The game lost sync. Return to the lobby and try again." There is no resync.
Keep both logs: `scripts/compare_state_hashes.py` finds the same frame in the
`[check][state]` lines of a self-test run.

What it does not do: a peer that stops sending hashes is only unverified. The
host logs `[party][desync-check] peer=... is not reporting state hashes; not
verified` once and the race goes on; nothing waits for a report.

Nothing new is sent anywhere else. A lobby on one architecture sends exactly what
it always did, including to a build of the same release that predates this.
`StateHash` and `Desync` are new party message types appended after
`InputReceipt`; `PartyRuntime::message` ignores a type it does not know. Only
builds with the check can be in a lobby launched with the architecture-free hash,
so neither type reaches one that lacks it.

To rehearse it, `DKR_OWNED_CHECK_VERIFY=1` turns it on in an all one
architecture self-test and `DKR_OWNED_CHECK_CORRUPT_STATE_AT=1500` makes one
peer report a wrong hash inside the race (300 lands in the menus before it).
Both are in the switch table of `docs/ROLLBACK-SELF-TEST.md`.

## Evidence

The contract is a claim. This is what has been checked so far. A new
architecture, compiler or OS gets the same two checks (the replay probe, then
a live race) before it ships.

Recorded 14 and 15 November 2026 on main at `a81b7a5` and `96c67dd`.

Replay probe (`runtime-recomp/tests/replay_probe`, compared with
`scripts/compare_replay_references.py`): the same captured race replayed on
each machine, full final checkpoint and every effect journal compared byte for
byte.

| Target | Compiler | Result |
|---|---|---|
| macOS arm64 | AppleClang 21, `-O3 -ffp-contract=off -fno-fast-math` | reference |
| Windows x86_64 | MSVC 14.44, `/O2 /fp:strict` | identical, 18 of 18 |
| Linux arm64 | GCC 14.2, same flags | identical, 12 of 12 |
| Linux arm64 | Clang 19.1 | identical, 12 of 12 |
| Linux x86_64 (emulated) | GCC 14.2 | identical, 12 of 12 |
| Linux x86_64 (emulated) | Clang 19.1 | identical, 12 of 12 |
| Linux riscv64 (emulated) | GCC 14.2 | identical, 12 of 12 |
| Linux ppc64le (emulated) | GCC 14.2 | identical, 12 of 12 |
| Linux s390x (emulated) | GCC 14.2 | refused: not little-endian, see below |

Both ROM revisions, 2, 3 and 4 players, 120 and 300 frames, two tracks (the
Linux rows: 300 frames, two tracks). Each machine also matched itself on a
second run. An earlier run of the same probe (25 October 2026, `c328f63`)
matched macOS arm64, macOS x86_64 under Rosetta and Linux x86_64 with GCC 13.

riscv64 and ppc64le did not match at first, although both advertised
`strict-ieee754-v1`. Their GCC 14 `-O3` builds used the scalar fallback of the
RSP vector unit (only x86_64 and arm64 have the SIMD one), and two of its
multiplies overflowed a signed `int`. Both architectures agreed with each
other and not with the rest, from the first frame, in the audio heap. Patch
`0029` fixes the two sites with unsigned arithmetic; the numbers above are
after it. The x86_64 and arm64 output did not change, and the macOS reference
is byte-identical before and after.

s390x builds and passes the probe's self-test, but it is big-endian, so its
build correctly reports `unverified-unknown` (the little-endian clause fails)
and the captured fixture, which is host-endian, is refused.

The Linux rows ran in Docker on an arm64 Mac, riscv64, ppc64le and s390x as
static cross builds under qemu user emulation, so they show that the
simulation computes the same bits on those instruction sets, not how fast it
runs or that real silicon agrees.

Live races (`docs/ROLLBACK-SELF-TEST.md`, confirmed-state hashes compared with
`scripts/compare_state_hashes.py`): an arm64 Mac against an x86_64 Windows
machine over the internet path, on the code that is on main, with the live
state check on. Recorded 15 November 2026. Every hash was equal in every run.

| Race | State hashes compared |
|---|---|
| 2 players, v77 and v80, each machine hosting | 117 or 118 each |
| 2 players, 60 ms added delay, each machine hosting (about 1,700 rollbacks) | 118 each |
| 3 players, three host and join orders | 117 or 118 each |
| 4 players, 180 s, each machine hosting | 177 each |
| Packaged macOS release app, v77 and v80 | 117 and 118 |
| 80 ms delay, 15 ms jitter, every 50th packet dropped, 180 s, hash every frame, each machine hosting | 5346 and 5342 |
| A mod that changes race state on both sides (`DKR_OWNED_CHECK_MODS`), each machine hosting | 118 each |
| The first track of the other three worlds (`DKR_OWNED_CHECK_TRACK`): hovercraft, plane, hovercraft; v77 and v80 | 108 or 109 each |
| The Rollback connection check in the full lobby first (`DKR_OWNED_CHECK_CONNECTION_TEST=1`), party and classic lobby, each machine hosting | 147 or 148 each |

A peer told to report a wrong hash (`DKR_OWNED_CHECK_CORRUPT_STATE_AT=1500`)
stopped both machines at frame 1500, with either machine hosting.

Mod code runs in one interpreter (`mods/magic_code_vm.cpp`) on every
architecture, not in a per-architecture recompiler, so a mod adds no new
architecture-specific code path. The mod run above differs from the no-mod
run from its first sampled frame, so the mod was in effect.

What this does not cover: tracks beyond the first of each world (the self-test
races on a fresh save), a vehicle other than each track's default, more than
one mod, Linux or Android on arm64 against anything live,
an x86_64 Mac, and human input.

## Porting to a new platform

Cross-play needs nothing platform-specific. A port that builds the game with
`DKR_EXPERIMENTAL_RACE_TEST=ON` races every other platform, provided:

1. **It is a 64-bit target, not iOS.** CMake refuses the option on 32-bit
   targets and on iOS.
2. **Its compiler can keep floating point strict.** Use the flags the
   existing toolchains use for the simulation sources: `-ffp-contract=off
   -fno-fast-math` (GCC, Clang) or `/fp:strict` (MSVC). Do not add
   `-ffast-math`, `-Ofast` or anything that implies them.
3. **The build honours the contract.** Start the game and read the log:

   ```text
   [online][contract] architecture=riscv64 contract=strict-ieee754-v1
   ```

   That line means the port will be admitted by everyone else. If it says
   `contract=unverified-<architecture>`, the lines after it name the clause
   the build misses (`[online][contract] not met: ...`), and the port can
   only race itself until that is fixed.

If a lobby refuses the port anyway, the joiner's log has both sides:

```text
[online][admission] rejected reason=This platform pair has not passed deterministic compatibility.
[online][admission] host architecture=x86_64 contract=strict-ieee754-v1; local architecture=riscv64 contract=unverified-riscv64
```

A refusal for any other reason (build, ROM revision, mods, settings) is not
about the platform: every racer needs the same release and the same content.

`build_architecture()` in `netplay/determinism_contract.hpp` names the common
architectures for the log. An architecture it does not know shows as
`unknown` and still races; add its name there when you add the port.

Meeting the contract is necessary, not sufficient: it says the build is
configured to compute the same bits, and only the replay probe matching shows
that it does. A target can pass every compile-time check and still diverge,
as riscv64 and ppc64le did until an undefined-behaviour bug in a code path
no other architecture compiles was fixed. Build the probe with
`-fsanitize=signed-integer-overflow,shift` once and read what it reports.

The simulation sources are built with `-fwrapv` on GCC and Clang
(`cmake/SimulationFlags.cmake`), so signed overflow wraps instead of being
undefined. That removes one class of divergence that depends on the
optimiser; it does not replace the check below. MSVC has no such switch and
does not get one.

Then check that the promise actually holds on the new hardware, below. It
takes two machines and about an hour, and it is the only way to find a
compiler or libm that rounds differently.

## Checking a new target

Nothing has to be changed for a new architecture to be admitted, but nothing
else shows that it computes the same bits. `scripts/port_check.py` does: it
builds the replay probe with your toolchain, runs the same captured race, and
compares every byte of the result with the same run on a machine that already
races.

You need your own ROMs and a machine that races today (x86_64 or arm64), plus
the prepared payloads from `scripts/prepare_payloads.py`. Capture one fixture
per ROM revision there with `scripts/capture_replay_fixture.py` and copy the
same bytes to the target.

```sh
# On the known-good machine: writes the reference.
python3 scripts/port_check.py --out ref \
    --fixture 77=race77.dkr-component --fixture 80=race80.dkr-component \
    --rom 77=us77.z64 --rom 80=us80.z64

# On the target: native, or --toolchain-file t.cmake [--emulator qemu-riscv64],
# or --docker-image debian:trixie --docker-platform linux/riscv64.
python3 scripts/port_check.py --out target --reference ref \
    --fixture 77=race77.dkr-component --fixture 80=race80.dkr-component \
    --rom 77=us77.z64 --rom 80=us80.z64
```

It prints the contract line the build reports, runs the probe's self-test and
the 2, 3 and 4 player workloads, and ends with one of:

- `IDENTICAL`: every comparison matched byte for byte.
- `DIFFERENT`: it says which comparison, the first differing checkpoint byte
  and the first differing frame. Exit 1.
- `INCONCLUSIVE`: the two runs were not comparable (another fixture or frame
  count). Exit 2.

The reference and the fixture are full copies of guest memory, so they come
from your ROM and cannot be shared or committed. Everybody makes their own;
the script prints only hashes, offsets and frame numbers, and `--out` has to
stay private.

### When it says DIFFERENT

riscv64 and ppc64le did, with GCC 14 at `-O3`, although both reported
`strict-ieee754-v1`. The first difference was at frame 0, in the audio heap, a
single bit. Same code at `-O0` to `-O2` matched. To find it:

1. Rerun with `--ubsan` and a fresh `--out`. It builds with
   `-fsanitize=signed-integer-overflow,shift` and lists every `file:line` the
   run reported. The scalar RSP multiply in `rsp_vu_impl.hpp` was the only one
   in library code; shifts in the generated guest code are reported too and
   are not the cause.
2. Check it is optimisation dependent (`--cmake-arg=-DCMAKE_C_FLAGS_RELEASE=-O1`
   and the same for CXX). If `-O1` matches and `-O3 -fwrapv` matches, it is
   undefined behaviour the optimiser exploits on your target.
3. Fix the arithmetic (unsigned, or an explicit wrap) rather than the flags.
   Patch `0029` is the example.

Only x86_64 and arm64 run the SIMD RSP path, so a new architecture runs the
scalar fallback that nobody else compiles: expect the bug there.

If a target cannot honour the contract (it needs fast-math, or its compiler
cannot turn contraction off), make its build fail the compile-time check so
it advertises `unverified-<architecture>`. The script still runs, and the
contract report names the clause that is not met.

### Then race it

The probe replays one captured race. The second check is a live one: build
the game for the target, race it against a machine that already races with
the self-test (`docs/ROLLBACK-SELF-TEST.md`, "Two machines"), once with each
machine hosting, on both ROM revisions, and once with
`DKR_OWNED_CHECK_DELIVERY_MS=60` on both so they roll back. Every peer logs
`[check][state]` lines; `scripts/compare_state_hashes.py host.log join.log`
must exit 0. Because the lobby spans architectures the live state check is on
by itself, so a divergence also stops the race with a message.

When both pass, add the target to the tables under "Evidence": what you built
with, the probe result, and the races you ran with their hash counts.
