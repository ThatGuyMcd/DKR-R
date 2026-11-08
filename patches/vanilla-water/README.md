# Vanilla river/waterfall animation repair

## Current release composition

For v1.0.11 use the [release pipeline](../release-prep/v1.0.11/BUILDING.txt). The module notes below
explain this layer; their dated playtest paths are historical inputs, not the
current release command. The accepted water/source capsule preserves the
composed behaviour. Dependencies and generated guest checkouts stay read-only.

The enhanced-water adapter collected wrap periods only when optical replacement
was enabled. Without those periods, the queued UV matcher either skipped the
waterfall texcoords or interpreted texture wrap as a large backward step.

This authored pipeline separates animation metadata from optical rendering:

- Modern presentation always records validated flowing-water texture periods.
- Water Off builds only cached material/batch ranges and texture-frame spans.
  It skips triangle geometry, flow meshes, wakes, rays, opacity masks and glow.
- The bridge tags actual submitted RT64 vertices, not reusable guest slots.
- The existing queued matcher retains previous/current period agreement and
  existing scene/camera/transform matching. HUD and rejected scopes are excluded.
- No shader/dependency layout, simulation, replay, multiplayer contract or save
  format is changed. All earlier beta 10 save corrections remain composed.

Compose into a new stage with `pipeline.py stage --sdk <accepted SDK stage>
--output <new build stage>`. Configure with this folder's `NativeOverlay.cmake`,
`DKR_VANILLA_WATER_STAGE`, and the accepted `DKR_SHARED_SAVES_STAGE` options.

Owner visual acceptance: Modern profile; original and enhanced water; rivers
beside Snowflake Mountain, hub waterfalls, Whale Bay/Pirate Lagoon, custom
waterfall-hinted materials, splitscreen and rollback; texture wrap boundaries,
camera/scene changes, and live quality toggles. Accurate/native cadence remains
unchanged. Build/static inspection only; do not launch the app or run tests.
