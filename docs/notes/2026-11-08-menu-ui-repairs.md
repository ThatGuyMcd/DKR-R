# Play background and save-name keyboard

The pinned Play layout added a child window that inherited an opaque fill.
Its background is now disabled, so the shared launcher/overlay page shows the
main panel background through the scrollable area. The launch bar, focus and
automatic scrollbar keep their existing behaviour.

The initials keyboard had two separate horizontal limits: the retail loop
only drew around the original 320-pixel canvas, and negative glyph rectangles
were clipped before reaching the renderer. The checked menu draw policy now
uses its existing signed-glyph scope around `filename_render`. Both loop tests
prepare a bounded row from -336 to 656; the renderer clips that row to the
current aspect. The selected letter stays centred. Input and save writing are
unchanged. The native 4:3 clip is retained at that aspect.

Both reviewed ROM revisions are checked. The native fixture executes the
fresh compiler output for every selection, fractional scrolling in both
directions and three menu heights. It observes text, selection colour, SP/DEL/OK,
stack bounds, scope restoration and unchanged initials/input state. Existing
signed rectangle and renderer tests cover the shared glyph path.

For per-function Patch Pipeline fixtures, run
`scripts/prepare_filename_draw_fixture.py --elf <reviewed ELF> --output <fixtures-v77 or fixtures-v80>`.
Configure `DKR_MEMORY_FIXTURE_ROOT` to their parent and build/run
`DKRFilenameNative77Tests` / `DKRFilenameNative80Tests`. The generated RAM files
contain game data and stay outside the repository and release packages.

The new CPU payloads are composed with the existing save-layout and character
portrait pipelines. Every retained retail function except `filename_render`
must remain identical. Normal and owned payloads use the same checked policy.
The private build has a new owned contract and requires matching peers; it is
not a compatibility-approved merge or public release. Both-owner review is
still required before any compatibility-breaking merge, as AGENTS.md requires.

At this investigation stage the custom-racer alternatives were previews only.
The subsequently approved selected-name implementation and visibility repair
are recorded in the [crash/menu follow-up](2026-11-08-three-crash-reports-plan.md)
and [label visibility record](2026-11-08-character-label-visibility.md).
