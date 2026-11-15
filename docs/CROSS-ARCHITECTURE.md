# Cross-architecture play

Rollback only works if every peer computes exactly the same game state from
the same inputs. Lobby admission therefore puts each peer in a *determinism
class* and refuses peers from another class with "This platform pair has not
passed deterministic compatibility."

The class comes from the CPU architecture the build was compiled for
(`determinism_class` in `netplay/netplay_types.cpp`) and from the floating
point mode. It goes into the compatibility hash every racer must agree on
before a race launches, so it changes what builds can play each other: add an
architecture to a class only with a release version bump.

## Classes

| Class | Architectures | Floating point mode |
|---|---|---|
| `ieee754-le64-v1` | `x86_64`, `arm64` | `strict-ieee754-v1` |

An architecture that is not listed is a class of its own: it can race other
builds for the same architecture and nothing else. The operating system and
compiler are not part of the class. They are covered by the evidence below,
not by a check, so a new compiler or OS for a listed architecture wants the
same two runs before it ships.

## Evidence for `ieee754-le64-v1`

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

## Qualifying another architecture

1. Build the replay probe for the new target and for a target already in the
   class, from the same commit and the same generated payload. Capture the
   fixture once and copy the same bytes to both machines.
2. Run the probe for both ROM revisions, 2, 3 and 4 players, and compare every
   output with `scripts/compare_replay_references.py`. Any difference is a
   stop: find the first differing frame, do not normalise it away.
3. Race the two machines against each other with the self-test, in both host
   and joiner seats and on both revisions, including a run with
   `DKR_OWNED_CHECK_DELIVERY_MS=60` so both sides roll back. Compare the
   `[check][state]` lines with `scripts/compare_state_hashes.py`. The peers
   will refuse each other until step 4, so this run needs a private build
   with the new architecture added to the class.
4. Add the architecture to `determinism_class`, extend
   `netplay_core_tests.cpp`, record the evidence in this file, and ship it
   with a release version bump.
