# HUD layouts

## v1.0.12 release contract

Online My racer fullscreen retains corrected divider/sky and uses the offline-style HUD. Other human racers can show display name and position. Menus/shared scenes retain their layout. This is presentation only, not a change to input ownership or simulation.

The accepted HUD layout stays in place! **Graphics → HUD layout** offers
**4:3** and **Fit to window** for Modern one/two-player presentation,
with the artwork at 100% size. Three/four-player HUDs retain their separate
layout, and Accurate keeps the original presentation.

Fit to window moves whole groups without horizontally stretching them.
The minimap background and racer markers move together.
This setting changes HUD placement, not the world's aspect ratio.

## Disabled editor

The custom HUD editor, Workshop, size controls, preset editing and live review
are not enabled in the production build. Earlier saved custom placements/presets
remain stored but do not affect rendering. Old Custom/Safe Area settings fall
back to 4:3; old Fill stays Fit to window. Don't delete your config to migrate.

Selection persists in `hud-layouts-v2.json` with recovery handling.
A failed write leaves the earlier selection active and reports an error.

## Grouping and presentation

All 59 retail HUD slots are explicitly classified by race, time-trial, boss,
challenge and hub mode. Generated timer glyphs, direct minimap drawing and
scoped challenge strips use their reviewed group/caller.
Entry slides cover the expanded canvas without changing countdown timing.

Scoped HUD projection and rectangle aspect are restored before later
world/menu/dialogue draws. The post-race frame transition and finish
interpolation have separate presentation policies; HUD layout does not decide
when the world returns to 4:3.

Golden Balloon informed the anchor/grouping work. The
[reference report](HUD-REFERENCE-ALIGNMENT.md) retains the original comparison
and test matrix. It is historical evidence, not a claim that every later
hardware/layout combination has been visually checked.
Credits and provenance remain in [THIRD_PARTY.md](../THIRD_PARTY.md).
