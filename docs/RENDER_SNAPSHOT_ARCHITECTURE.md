# Renderer snapshot ownership

## v1.0.12 release contract

SDK 2 local tools/camera and coherent HD-liquid selection preserve copied presentation ownership. No independent online terrain/wave allocation or rolled-back high-LOD experiment is advertised. Checkpoint/save persistence stays confirmed-only.

This reference describes the normal task/decode ownership. Experimental
Rollback additionally retires owned submissions before correction and separates
checkpoint memory from mutable presentation workspaces. See [architecture](ARCHITECTURE.md).

## Scheduler and submission ownership

The graphics-handoff Patch Pipeline captures a completed 8 MiB RDRAM
image through the existing `gfxtask_run_xbus` callback, before the
scheduler message. This protects the earlier wait before host RSP
submission. The lease transfers at `submit_rsp_task`, avoiding a second
normal-path capture. FIFO matching checks RAM owner, physical root and
native command count. It never searches by a recycled root alone.

Pending capture is bounded to four images. Invalid capture, mismatch
or overflow disables early matching until session reset, retaining the
accepted submission-copy fallback. Suppressed tasks retire their leases;
session generations reject captures that cross reset. Active workers
retain their images. SP/DP completion and guest freeing are unchanged.

The immutable task image is separate from the reusable private decode
workspace. Packed-vertex translation uses the upper private arena rather
than the original game heap. RT64 readback and decode writes cannot
alter live simulation RAM or the immutable submitted image.

Producer capture also protects Accurate presentation; semantic transform
sidecars remain Modern-only. Historical replay suppresses producer side
effects. No competing GPU queue, guest command format or gameplay state
is added. Malformed command guards and limited provenance reports remain.

## Decoded renderer stage

RT64 decodes the task synchronously into its owned `Workload` data: draw
commands and ranges, transforms, vertices, texture/load identities, viewport,
fog, lights, framebuffer storage, material/RDP state, and interpolation maps.
Its `WorkloadQueue` is a four-slot ring. A compile-time assertion prevents this
runtime from enabling Modern work against an RT64 revision with fewer than four
owned slots. RT64's cursor mutexes, condition variables, workload IDs, and
renderer/present fences control reuse.

The project therefore does not add a second competing renderer queue. Modern
semantic IDs are written while F3DDKR records the current RT64 workload; RT64
then pairs the two owned endpoint workloads. Missing history, scene changes,
identity failures, discontinuities, queue pressure, or snapshot audit failures
must present the newest authored endpoint without extrapolation.

## Semantic transform sidecar

DKR allocates object matrices from two fixed N64 frame heaps and reuses object
addresses after destruction. A physical matrix address is therefore not a
stable interpolation identity by itself. Patch Pipeline hooks at scene load,
object spawn/free, and the decompiled `render_object` boundary construct IDs
from the scene generation, object address, lifetime generation, object and
behaviour IDs, plus the matrix ordinal within that object. Bodies, limbs,
wheels, propellers, and other attachments consequently remain distinct.

No extra commands are written into DKR's fixed 4,500-command display-list
buffer. Instead, the host records address-to-ID bindings in a sidecar while the
game authors the frame. A hook at `gfxtask_run_xbus` freezes that sidecar in
the same FIFO order as the graphics task. `TaskIdentityScope` transfers the
matching immutable map to the graphics worker for exactly one F3DDKR decode.
This prevents a delayed worker from reading identities belonging to a newer
frame that reused the same N64 matrix heap.

Accurate mode never records or selects these IDs. Invalid object ranges,
unbalanced nesting, task-order mismatches, queue overflow, and 32-bit hash
collisions all resolve to `G_EX_ID_IGNORE`; a collision also removes bindings
already authored by the first colliding owner. These are presentation-only
fallbacks and never alter simulation memory.
