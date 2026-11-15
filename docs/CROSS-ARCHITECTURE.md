# Cross-architecture play

Rollback only works if every peer computes exactly the same game state from
the same inputs. Peers on different CPU architectures race each other as long
as their builds make the same promise about how they compute. Nothing lists
the architectures that are allowed: a build for a CPU nobody has tried yet
joins in if it can make the promise.

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

The first three are checked at compile time. The last is a build flag the
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

## Evidence

The contract is a claim. This is what has been checked so far, and the same
two runs are what to do for a new architecture, compiler or OS before
shipping it.

Recorded 14 November 2026 on main at `a81b7a5`.

Replay probe (`runtime-recomp/tests/replay_probe`, compared with
`scripts/compare_replay_references.py`): the same captured race replayed on
each machine, full final checkpoint and every effect journal compared byte for
byte.

| Target | Compiler | Result |
|---|---|---|
| macOS arm64 | AppleClang 21, `-O3 -ffp-contract=off -fno-fast-math` | reference |
| Windows x86_64 | MSVC 14.44, `/O2 /fp:strict` | identical, 18 of 18 |

Both ROM revisions, 2, 3 and 4 players, 120 and 300 frames, two tracks. Each
machine also matched itself on a second run. An earlier run of the same probe
(25 October 2026, `c328f63`) matched macOS arm64, macOS x86_64 under Rosetta
and Linux x86_64 with GCC 13.

Live races (`docs/ROLLBACK-SELF-TEST.md`, confirmed-state hashes compared with
`scripts/compare_state_hashes.py`): an arm64 Mac against an x86_64 Windows
machine over the internet path, builds with the architecture check removed.

| Race | State hashes equal | Rollbacks, host / joiner |
|---|---|---|
| v77, Windows hosts | 118 of 118 | 61 / 12 |
| v77, Mac hosts | 118 of 118 | 1 / 0 |
| v80, Windows hosts | 118 of 118 | 2 / 0 |
| v80, Mac hosts | 118 of 118 | 1 / 0 |
| v77, 60 ms added delay, Windows hosts | 118 of 118 | 1732 / 2 |
| v77, 60 ms added delay, Mac hosts | 114 of 114 | 1643 / 45 |
| v77, 180 s, Mac hosts | 178 of 178 | 89 / 0 |

What this does not cover: more than two live players, tracks and vehicles
beyond the ones the self-test reaches, mods, Linux or Android on arm64 against
anything, and human input. State was sampled every 30 confirmed frames.

## Porting to a new platform

Cross-play needs nothing platform-specific. A port that builds the game with
`DKR_EXPERIMENTAL_RACE_TEST=ON` races every other platform, provided:

1. **It is a 64-bit target.** CMake refuses the option otherwise.
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

Then check that the promise actually holds on the new hardware, below. It
takes two machines and about an hour, and it is the only way to find a
compiler or libm that rounds differently.

## Checking a new target

Nothing has to be changed for a new architecture to be admitted. Before
shipping one, run the two checks above against a target that already has
evidence:

1. Build the replay probe for both targets from the same commit and the same
   generated payload. Capture the fixture once and copy the same bytes to both
   machines. Run it for both ROM revisions with 2, 3 and 4 players and compare
   every output with `scripts/compare_replay_references.py`. Any difference is
   a stop: find the first differing frame, do not normalise it away.
2. Race the two machines against each other with the self-test, in both host
   and joiner seats and on both revisions, including a run with
   `DKR_OWNED_CHECK_DELIVERY_MS=60` so both sides roll back. Compare the
   `[check][state]` lines with `scripts/compare_state_hashes.py`.
3. Record the result in the tables above.

If a target cannot honour the contract (it needs fast-math, or its compiler
cannot turn contraction off), make its build fail the compile-time check so
it advertises `unverified-<architecture>`.
