# Fullscreen flow and intro presentation

## Current release composition

For v1.0.11 use the [release pipeline](../release-prep/v1.0.11/BUILDING.txt). The module notes below
explain this layer; their dated playtest paths are historical inputs, not the
current release command. The accepted water/source capsule preserves the
composed behaviour. Dependencies and generated guest checkouts stay read-only.

This Patch Pipeline layer composes the accepted beta 10 save, Store, SDK and
vanilla-water stages. It does not edit submodules, generated guest functions,
guest RAM, replay state, save files or persistent graphics preferences.

## Flowing water first

RT64's existing queued matcher requires matching transform vertex counts before
it can calculate per-vertex UV velocities. Period metadata alone cannot make
that work when an entire terrain segment's visible vertex stream changes. A
window/fullscreen aspect change can change visibility and expose this weakness.
The owner's first follow-up reports that this isolation alone was insufficient:
water still stuttered regularly in both windowed and fullscreen modes. The log
confirms the new batch-history adapter was running, without sidecar errors.
The current owner's log confirms Modern / 180 FPS, but contains no per-frame
GPU/culling trace: it does not prove this is the only cause of the reported
stutter. GPU saturation and animation frames are distinct from UV interpolation.

The authored repair caches reviewed flowing-water vertex ranges while the
existing immutable registry is built. Each admitted vertex load gets a matching
key derived from the original scene/camera/segment key, source, size and vertex
destination. All original transform, aspect and component rules are retained.
The complete previous group is restored before any following vertex submission.
The ordinary load must create its own transform range too, so it cannot append
to the preceding water range. No geometry is duplicated or re-emitted.

The decomp's `obj_loop_texscroll` wraps level UVs at dimension << 8. F3DDKR
submits triangle ST through modifyVertex(ST), which converts by 32, independently
of the earlier vertex texture scale. Its matching period is therefore dimension
times 8. Optical shader mapping retains its existing texture-pixel units. The
queued matcher still requires both endpoints' periods and matching counts;
camera cuts, scene changes and rejected scopes retain their normal safeguards.

Retail scroll objects also retain two quarter-ST remainders (unk8/unkA & 3).
Submitting only the integral triangle ST discards those fractions, creating a
regular hold/jump at low scroll speeds. Capture the fractions from the immutable
task snapshot and add them, divided by 4*32, AFTER raw ST is decoded for the
actual RT64 vertex. Sum independently authored scroll objects as retail does.
Skip TRI_FLAG_80 faces exactly as retail does. No correction can accumulate:
each triangle corner is first reset from its raw authored ST. This does not
change enhanced-water optical units or modify any guest triangle/object.

A five-second aggregate log reports authored task cadence, real/interpolated
presentation rates, sidecar approvals, isolated flow loads and fractional faces.
It uses existing atomic presentation counters, no GPU readbacks or sync calls.

The next owner run confirmed approximately 180 presented / 150 interpolated
frames and 30 authored tasks per second during repeated pauses. All static-flow
batches and fractions were admitted, with intact sidecars. This rules out a
sustained global presentation collapse in those intervals; admission alone
still does not prove queued UV matching succeeded.

Original water therefore bypasses generic queued UV matching entirely. A bounded
native two-frame map tracks the six UV components of each reviewed static
triangle, independently of camera, transform vertex totals and submission order.
The existing GPU tcVelFloats channel receives these velocities directly. Those
isolated groups use texcoord SKIP and publish no waterfall period tags, so the
generic matcher cannot overwrite native UV motion. All other transform/camera
matching is retained. Enhanced optics and object-image fallbacks keep the old
queued path. History resets across scene/model/time discontinuities, reappearing
triangles snap for their first sample, and repeated split-screen views reuse a
single sample. The cache is bounded to 16,384 visible source triangles.

The cadence log now also reports manual faces, actual held-source UVs and image
changes. This distinguishes generic matching failure from authored flipbook or
texture data that genuinely holds still. Do not claim owner acceptance from
compilation or the presence of a log marker.

Object-model image fallbacks retain their existing matching route. This change
does not fabricate interpolation between separate texture animation images.
Water Off still skips ray meshes, flow meshes, shader optics and lava lighting.

## Intro second

Admission requires both MENU_LOGOS in the task snapshot and the reviewed intro
inset scissor. Similar rectangles in credits/Track Select are not enough. Only
Modern + Fit to window admits it. Accurate and explicitly original aspect keep
the original layout. Both US revisions use the reviewed menu layout.

Normalise the 319-wide scissor and 160.5 viewport X centre so RT64 recognises
the 3D pass as full-width. Reveal the complete top/bottom picture and centre
the logo in the full-height scene. The owner explicitly requested removal of
the bottom copyright bar too: its text remains at the original position, drawn
over the scene through the separate ordinary 2D copyright viewport. Retain both
viewport scales and mirroring. The scene fill alone
uses full-width edge origins; no text or logo stretching is introduced. Before
fullSync, normalise the producer-owned aggregate back to its native 320-wide
reference so wide background rectangles cannot distort aspect inference.

No intro duration, fade, music or skip-intro destination logic changes.

## Delivery and owner acceptance

Create a NEW stage using `pipeline.py stage --base <accepted vanilla-water stage>
--intro --output <new build stage>`. Omit `--intro` for a water-only checkpoint.
Configure using this directory's NativeOverlay.cmake and DKR_VIEWPORT_FLOW_STAGE;
keep DKR_VANILLA_WATER_STAGE and every earlier accepted stage configured too.

Compile and inspect packages only. Do not launch DKR-R, run replay captures or
execute gameplay/unit tests. Owner visual acceptance is still required:

- Water Off: compare Snowflake Mountain rivers and hub waterfalls in windowed
  and fullscreen mode at the same selected refresh target. Toggle repeatedly,
  move the camera through visibility boundaries and observe wrap transitions.
- Enhanced water: repeat without changing authored flow direction or optics.
- Check standard/HD textures, split-screen, online and camera/scene transitions.
- Intro: 4:3, 16:9, 16:10, 21:9 and wider; both spinning logos; no bars;
  copyright visibility/readability over the background and original fades;
  transitions to the title screen; both skip-intro destinations; window resize.
- Accurate/original aspect must retain its authored framing and cadence.

No source or guest contract change is required in the online build identity.
