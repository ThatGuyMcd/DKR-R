# F3DDKR graphics HLE

Diddy Kong Racing uses Rare's F3DDKR display-list microcode. DKR-R implements
its graphics commands with a hand-authored high-level emulation (HLE) decoder
in `runtime-recomp/src/game/f3ddkr_rt64.cpp`. The class is named
`F3DDKRRT64Bridge`, but it implements the microcode's command semantics; it is
not a placeholder for a missing graphics RSP implementation.

## Execution path

`RT64Renderer::send_dl` passes a submitted graphics task and its owned RDRAM
snapshot to `F3DDKRRT64Bridge::process`. The bridge installs its command table
as RT64's `hleGBI` and translates the display list into RT64 workloads. It
implements DKR's three matrix slots, packed 10-byte vertices, DMA triangle
batches with per-corner texture coordinates, billboard anchors, DMA offsets,
texture offsets, and counted display lists. Shared Fast3D and RDP commands use
RT64's existing handlers.

Graphics do not execute a statically recompiled F3DDKR RSP program. Audio is
separate: `GetRspMicrocode` in `game_main.cpp` selects `dkrAspMain`, generated
by RSPRecomp using `runtime-recomp/rsp/aspMain.us.v77.toml`. That audio program
executes the original microcode's operations; it is not an audio HLE decoder.

## Command boundaries and task ownership

The graphics command walk checks each command address before RT64 reads its
opcode and validates the full span of commands that consume additional words,
including three-word texture rectangles and four-word forced-matrix sequences. A task-wide execution budget bounds loops
across top-level and counted display lists. Invalid command ranges or an
exhausted budget stop decoding with a diagnostic. A valid end-of-list command
at the end of RDRAM remains legal. These checks supplement the bridge's
matrix, vertex, triangle, texture-offset and nested-list range checks; they
do not establish bit-exact equivalence with the original RSP.

The decoder reads an owned 8 MiB task snapshot. It may use the snapshot's
reserved translation workspace, but it never writes renderer data back to live
simulation RDRAM. The runtime's snapshot and SP-completion patches let the CPU
recycle live display-list buffers while queued graphics still own their input.
This lifetime mechanism supports the HLE decoder rather than replacing it.

Presentation groups attach stable semantic identities to moving objects,
vehicle parts, billboards, shadows and animated surfaces so RT64 can interpolate
between owned workload endpoints. Accurate mode bypasses Modern semantic
identities and retains the original display cadence; it still uses graphics
HLE rather than an RSP instruction interpreter.

## Validation and maintenance

`runtime-recomp/tests/f3ddkr_hle_execution_tests.cpp` exercises the command
execution policy with synthetic lists, including looping branches, truncated
commands, nested returns and a shared counted-list budget. These tests require
neither a ROM nor a GPU. Private GPU probes exercise real F3DDKR workloads;
neither test category is a complete microcode conformance suite.

Direct edits to RT64 or generated RSP/CPU sources are forbidden. Renderer
dependency changes belong in `patches/rt64`; RSP and game hook changes belong in
the recomp policy and are regenerated through the Patch Pipeline.
