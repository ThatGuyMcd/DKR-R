# A real minimap for custom tracks

Status: investigation + agreed plan, 2026-09-23. **All four steps built
2026-09-24** - see section 8 for what shipped, where it departs from the plan,
and what is still to be seen in game. Everything in "How it works" was read
from the decomp and the addon source, and the retail numbers were read from the
`us.v77` level models. Claims that are inferred from code but not yet seen in
game are marked *(unverified)*.

---

## The short version

- The minimap is **two things drawn on top of each other**: a small grey
  **picture** of the track, and **dots/arrows** for every racer.
- The picture and the dots **do not know about each other**. The dots are placed
  by a little formula that uses **nine numbers** stored in the track's
  geometry file (`LevelModel` header). If those numbers don't match the
  picture, the dots float in the wrong place.
- A custom track made by reshaping Ancient Lake **inherits Ancient Lake's
  picture and Ancient Lake's nine numbers**. That's why every custom track
  shows the Ancient Lake map.
- The fix is: **the author traces the road's edges with Grease Pencil in
  Blender, the addon turns them into the picture and works out the nine numbers
  by itself, and the runtime learns to load that picture from the `.dkrmap`**.
  The plan below does it in four small steps, and the first one is useful on
  its own.
- The panel has no options to get wrong: **road edges drawn → the track has a
  minimap; no edges → it ships without one.** See section 5 for the UI and
  section 6 for why each choice was made.

---

## 1. How the minimap works

### The picture

```
   ┌──────────────┐
   │   ╭──╮       │  ← a tiny greyscale image (32–80 px wide),
   │   │  ╰──╮    │     white road on a transparent background
   │   ╰─────╯    │
   └──────────────┘
```

- Each retail track has one minimap image, stored as a 2D texture
  (`textures/2d/minimap/*.png`, format **IA8** = grey + transparency).
  Ancient Lake's is 32×68 pixels; the biggest (Central Area) is 80×50.
- The image is wrapped in a **sprite** (`sprites/minimap/*.json`). The sprite
  adds one important thing: an **anchor point** (the `unk4`/`unk6` numbers,
  e.g. 45, 69 for Ancient Lake). The game uses it to line the dots up.
- The track's geometry header says **which** sprite to use, by number
  (`minimapSpriteIndex`). 0 is Fossil Canyon, 1 is Ancient Lake, 2 is Pirate
  Lagoon, and so on.
- The game **tints** the grey image with a colour from the header
  (`minimapColor`). Almost every track uses white; Snowflake Mountain tracks
  use red (`ff2828`), Crescent Island a teal (`3c8080`).
- It is drawn **semi-transparent** (never more than 160/255) at a fixed spot on
  the screen that depends on the player count (bottom-right in 1 player, the
  empty quarter in 3/4 player). The map **does not spin** with the player; it is
  always the same way up.

### The dots

Every frame, for every racer, the game takes the racer's position on the
ground (X and Z, ignoring height) and turns it into a spot on the screen:

```
  Racer in the world                       Dot on the screen
  ┌──────── track bounds ────────┐         ┌── picture ──┐
  │                              │   ①     │             │
  │          🚗 (x, z)           │  ───▶   │     •       │
  │                              │ ② ③ ④   │             │
  └──────────────────────────────┘         └─────────────┘
```

1. **Where inside the track box is the racer?** The box is the track's
   *bounds* — the smallest box that holds all the geometry. The game works out
   "the racer is 40% of the way across and 70% of the way down".
2. **Stretch it.** That fraction is multiplied by **60 pixels** and by the
   header's **X scale / Y scale**. So with scale 1.0 the whole track depth
   becomes a 60-pixel-tall map.
3. **Turn it.** It's rotated by the header's **rotation** (in degrees). Retail
   uses this a lot: Fossil Canyon is turned 245°, Jungle Falls 269°. It lets a
   long track fit a small image.
4. **Nudge it.** Finally the header's **offset X / offset Y** is added (plus
   the screen position and the sprite's anchor), so the dots land on the
   picture.

There are **two sets of offsets**: one for normal play (*Adventure 1*) and one
for the mirrored tracks (*Adventure 2* cheat), because when the track is
mirrored the picture is flipped and the dots must be flipped too.

### The nine numbers

All of these live in the level model header (bytes `0x20`–`0x3B`). The addon
already reads and writes them (`level_model.py`, `HEADER_FIELDS`).

| Field | What it means in plain words | Ancient Lake |
|---|---|---|
| `minimapSpriteIndex` | Which picture to show | 1 (Ancient Lake) |
| `minimapRotation` | How much to turn the dots, in degrees | 0 |
| `minimapXScale` | How wide the dot area is | 1.56 |
| `minimapYScale` | How tall the dot area is | 1.49 |
| `minimapOffsetXAdv1` / `YAdv1` | Slide the dots to sit on the picture | 6, 9 |
| `minimapOffsetXAdv2` / `YAdv2` | Same, for mirrored (Adventure 2) | 41, 9 |
| `minimapColor` | Tint of the picture | white |

The **track bounds** (bytes `0x3C`–`0x47`) are also part of the formula. The
addon already recalculates those correctly when you export — good, but see
section 3 for why that alone makes things worse.

A separate switch in the **level header** (`unkBC`, lowest bit) says "this level has
no minimap at all". Hubs and cutscenes use it.

---

## 2. Who shows up on the map: players and CPUs

- When a race starts, the game looks for the **Setup Points** in the track's
  object map (the starting grid). Each one has a slot number 0–7 and gives a
  position and a facing angle.
- **Humans are placed first**, then **CPUs fill the remaining slots**. In a
  1-player race there are 8 racers (7 CPUs); in 2-player it's the multiplayer
  racer setting; in 3–4 player it's just the humans; boss races are 2.
- Every racer object remembers `playerIndex`. A human has 0–3, a CPU has the
  special value **"computer"** (4).
- The minimap walks the list of racers (`get_racer_objects_by_port`) and:
  - **Humans** get an **arrow** that turns with the direction they're driving.
  - **CPUs** get a **round dot** that doesn't turn.
  - The colour comes from the **character** (Krunch, Diddy, Bumper…), from a
    fixed colour-per-character table in `game_ui.c` (`gHudMinimapColours`).
  - In battle mode the dot grows/shrinks with how high the racer is.
  - In challenges, a racer who's finished disappears from the map.
- Extra markers: your **time-trial ghost** (grey), **T.T.'s ghost** (grey from
  the same table), and **Taj** on the hub (purple).
- The whole map fades in and out, and is hidden when no human is still racing,
  during cutscenes, or when the player switches it off (the HUD toggle).

**Good news for us:** none of this needs changing. Once the picture and the nine
numbers are right, players, CPUs, ghosts and colours all "just work", because
the game places them from their real positions.

---

## 3. Why custom tracks show the Ancient Lake map

There are two ways to make a track in the addon, and both go wrong:

**A. Reshaping an imported track (the usual way, e.g. starting from Ancient
Lake).** The export loads Ancient Lake's model file and applies your edits on
top. The nine minimap numbers are never touched, so the track keeps:

- Ancient Lake's **picture**,
- Ancient Lake's **scale, rotation and offsets**,
- but **new bounds**, because the addon recalculates them from your geometry.

So you see the wrong picture, *and* the dots are stretched to fit your new
track's box using numbers that were tuned for a different box. The dots end
up roughly in the picture's area but don't follow the drawn road.

**B. "Track from mesh" (a brand new model).** The addon builds a blank header,
which sets every minimap number to zero. That means picture 0 (**Fossil
Canyon**), and a scale of 0, which squashes every dot onto one point.
*(unverified in game — read from `level_model_layout.blank_model`.)*

And the addon can't simply ship its own picture today, because a `.dkrmap` can
only carry **3D** textures. The minimap is a **2D texture + sprite**, and the
runtime has no way to add those yet (`docs/CUSTOM_TRACKS.md`, "Sections a track
can replace or extend").

---

## 4. The plan

Four steps. Each one ships on its own and leaves the game in a better state than
before.

### Step 1 — Stop showing the wrong map (addon only, small)

**What you get:** custom tracks stop showing Ancient Lake's map. There is
nothing to choose — the addon decides from what the author did:

- **An imported retail track whose shape didn't change** keeps its own minimap,
  automatically. The picture and the nine numbers are already in the base file,
  and the bounds are the same, so it still lines up. The panel just says
  *"Using Ancient Lake's own minimap"*.
- **Once the shape changes** (the bounds no longer match the base file), the
  inherited minimap is dropped: the export sets the "no minimap" bit in the
  level header (`unkBC`, lowest bit — what the game already does for hubs).
  The panel warns *"Shape changed — draw road edges, or the track ships without
  a minimap"*.
- **"Track from mesh"** also ships without a minimap, instead of the broken
  zeros it gets today.
- Remove the "in development" placeholder.

Until step 4 lands, drawn road edges only show in the Blender preview; the
exported track still ships without a minimap.

### Step 2 — The maths and the preview (addon only, medium)

**What you get:** a preview inside Blender that shows exactly what the game
will draw, and numbers that are always calculated, never typed.

- Write the game's dot formula in Python (`minimap.py`, no `bpy`) — the exact
  same maths as `minimap_marker_pos` in `game_ui.c`.
- **Check it against all 31 retail levels that have a minimap.** For each, load
  the retail picture, run a few AI-path points through our formula and check
  they land on the drawn road. This is our proof that we understood the game.
  It becomes a test in `tools/blender/tests/`.
- **Automatic numbers.** From the picture step 3 makes, work out scale,
  rotation and offsets. *Auto* rotation stands the track upright, the way
  retail uses rotation to fit a long track into the corner of the screen.
- **Mirror.** Calculate the Adventure 2 offsets from the Adventure 1 ones, so
  mirrored races work without the author doing anything.
- **Panel preview:** the picture as the game draws it, enlarged, with an arrow
  for the player and 7 dots for the CPUs on the starting grid, in the
  characters' colours.
- **Show on Track:** a viewport overlay that paints the minimap's road in
  translucent white over the 3D track (same technique as the AI Racers line),
  so an edge that wanders off the road is obvious at once.

### Step 3 — Trace the road edges with Grease Pencil (addon only, medium)

**What retail minimaps look like.** Enlarged, every retail picture is the same
recipe:

- **only the road** — a white band with a slightly soft edge;
- **no scenery** — no trees, walls, water or hills;
- the band's **width follows the real road**: Jungle Falls' wide parts are
  wide, Crescent Island's shortcuts are there;
- a small **checkered flag** (about 5×5 px) on the finish line.

**The one rule the author has to know:**

> **Each closed line is an edge. The road is what lies between edges.**

```
  ╭──────────────╮
  │ ░░░░░░░░░░░░ │  outer edge
  │ ░╭────────╮░ │  inner edge → the middle becomes a hole
  │ ░│  ╭──╮  │░ │
  │ ░╰──╯░░╰──╯░ │  a shortcut? just one more edge,
  ╰──────────────╯  the same rule still works
```

This is the "even-odd" fill rule, so there are no "outer" and "inner" layers to
manage: shortcuts and islands just work.

**What the addon does:**

- **Draw Edge** switches to the top view, frames the track, creates or selects
  the Grease Pencil object *DKR Minimap* and enters draw mode. The author just
  traces. The strokes live in world coordinates, on top of the 3D track, so
  they line up with the racers by construction — even if the view moves.
- **Start from AI Path** creates two closed edges, one on each side of the CPU
  racing line (which is already a full lap), at road width. The author only
  adjusts them: widen where the road is wide, add shortcuts. Much faster than
  starting from nothing.
- **Closing edges.** An edge whose ends are close together is closed
  automatically (the stroke's `cyclic` flag). One whose ends are far apart is
  reported with the size of the gap, and a **Close** button closes all of them.

**Turning edges into the picture** is our own Python code — not a camera
render, and not Grease Pencil's own fill (see section 6):

1. Flatten the edges onto the ground (X/Z, height ignored).
2. Fill between them at **4× the final size**, with the even-odd rule.
3. Shrink to the final size — this gives the soft retail edge for free.
4. Add the checkered flag on the finish line (its position comes from the
   starting grid).
5. Crop around the road. The **offsets** are how much was cropped, so the dots
   still land on the road.
6. Convert to IA8 (grey + alpha), the retail format. The addon already writes
   texture payloads (`custom_textures`), so most of the encoder exists.

**Escape hatch** for authors who want to retouch by hand: *Save PNG…* writes the
picture, and *Use My PNG…* brings a retouched one back. The size must not
change — the addon refuses a PNG of a different size and says why, because a
resized picture no longer lines up with the dots.

**Bonus:** put a sharper, bigger version in the `-hd.zip` texture pack the
export already writes, so the map looks crisp in DKR-R.

### Step 4 — Ship the picture with the track (runtime + addon, larger)

**What you get:** the picture actually appears in the game.

- Extend the `.dkrmap` format with two new sections: **`TEXTURES_2D`** (the
  image) and **`SPRITES`** (the small wrapper with the anchor). This is the
  same mechanism already used for `TEXTURES_3D`: publish a longer table so the
  new entry "exists" after the retail ones (`custom_tracks.cpp`,
  `custom_tracks_hooks.cpp`).
- The track's header then points `minimapSpriteIndex` at *its own* new sprite
  number (retail count + 0).
- Update `docs/CUSTOM_TRACKS.md` and the manifest schema, and make the runtime
  refuse a package whose minimap sprite number points nowhere, with a clear
  message.
- Test in game: 1P, 2P, 3P, 4P, Adventure 2 (mirrored), and time trial with a
  ghost.

### Order and effort

| Step | Where | Size | Useful alone? |
|---|---|---|---|
| 1. No wrong map | Addon | Small | Yes — fixes the confusing Ancient Lake map today |
| 2. Maths + preview | Addon | Medium | Yes — proves we understood the game, preview works |
| 3. Trace road edges | Addon | Medium | Partly — preview works, the game needs step 4 |
| 4. Ship picture | Runtime + addon | Larger | This is where it all shows up in game |

Steps 2 and 3 can happen at the same time as step 4 (different people/areas).

---

## 5. The Minimap panel

It lives where the placeholder is today, **Track › Minimap**, and follows the
layout of the Water and AI Racers panels: a status box on top, the action
buttons, then settings, then grey hints. UI text is in English like the rest of
the addon.

There is **no source selector and no "borrow"**. What the panel shows depends
only on the track.

**Imported retail track, shape unchanged**

```
▾ Minimap
  ┌────────────────────────────────────┐
  │ ✓  Using Ancient Lake's own minimap│
  └────────────────────────────────────┘
  [ ✎  Draw Edge                     ]   ← drawing replaces it
```

**No edges yet** (new track, or a retail track whose shape changed)

```
▾ Minimap
  ┌────────────────────────────────────┐
  │ ✎  No road edges — track ships     │
  │    without a minimap               │
  └────────────────────────────────────┘
  [ ⤳  Start from AI Path            ]
  [ ✎  Draw Edge                     ]
  ⓘ Trace the outside border of the road,
    then the inside one. Each closed line is
    an edge; the road is what lies between.
```

When a retail track's shape changed, the status box says *"Shape changed —
draw road edges, or the track ships without a minimap"* instead.

**Edges drawn**

```
▾ Minimap
  ┌────────────────────────────────────┐
  │ ◯  3 edges · 1 shortcut            │
  │ ⚠  1 edge is open (gap 35 m)       │
  │                        [ Close ]   │
  └────────────────────────────────────┘
  ┌────────────────────────────────────┐
  │        ╭────────╮                  │
  │        │ • ▲  • │   ← the picture  │
  │        ╰──╮  ╭──╯     as the game  │
  │      •  • ╰──╯        draws it     │
  └────────────────────────────────────┘
  [ 👁  Show on Track ] [ ✎ Draw Edge ]
  [ 🗑  Clear ]
```

- The **status** counts the edges and flags any open one; **Close** closes them
  all.
- The **preview** is the final picture, enlarged, with the player's arrow and
  the CPU dots on the starting grid in the characters' colours.
- **Show on Track** toggles the viewport overlay from step 2.

**Appearance** sub-panel (closed by default)

```
▸ Appearance
  Size          [━━━━●━━━]  64 px
  Rotation      [ Auto ▾ ]
  Colour        [■ white ]   ▾ presets
  Soft Edge     [━━●━━━━━]  1 px
  [✓] Checkered finish line
```

- **Size** is the longest side, 48–80 px — the range retail uses.
- **Rotation**: *Auto* stands the track upright; 0/90/180/270 are available.
- **Colour** is the tint, with the retail colours as presets (white, Snowflake
  red `ff2828`, Crescent teal `3c8080`).
- **Checkered finish line** is placed from the starting grid.

**Numbers** sub-panel (only with *show raw* on, like the other panels)

```
▸ Numbers
  Scale X / Y      0.92 / 0.92      (calculated)
  Offset Adv 1     12, -40          (calculated)
  Offset Adv 2     55, -40          (mirror, calculated)
  [ Save PNG… ]  [ Use My PNG… ]
```

Read-only; the addon calculates every number, mirror included.

**Outside the panel**

- **Validate** gets a line: *"Minimap: 1 open edge"*, *"Minimap: none"*, or
  *"Minimap: ok"*.
- **Package** exports the picture and the numbers with the track. With no edges
  it ships without a minimap and says so — it never blocks the export.

**Left out of the first version:** typing the numbers by hand, more than one
minimap per track, animated minimaps.

---

## 6. Decisions and why (2026-09-23)

| Decision | Why |
|---|---|
| **Trace with Grease Pencil**, not a camera render | At 64 px a render of the whole track is a blob of road, scenery, walls and bridges. A render also needs a camera that matches the game's formula pixel for pixel, a render engine set up, and it can't run in the headless tests. Grease Pencil strokes sit in world coordinates, so they line up with the racers by construction. *(Rendering the track and tracing over it in Krita was the first idea; the Save / Use My PNG buttons keep that door open.)* |
| **Two edges (outline)**, not one centre line | Gives the exact road shape, wide parts and shortcuts included. A single closed centre line whose per-point thickness sets the band width was considered and is simpler to draw, but less exact. |
| **Our own fill**, not Grease Pencil's fill | Same result in every Blender version, and the even-odd rule gives holes and shortcuts for free. |
| **No "borrow a retail minimap"**, no source selector | A borrowed picture never lines up: the dots use *your* track's bounds. *Start from AI Path* makes a real one in seconds instead. |
| **An untouched retail track keeps its own map** | Don't break what works today: a remix that only changes textures, objects or music still shows the right map. |
| **No edges = no minimap** | Showing nothing is better than showing the wrong track. |

---

## 7. Things we still need to find out

- **Exactly where the picture sits relative to its anchor.** The dot formula
  is clear, but the picture's own placement goes through
  `render_ortho_triangle_image`. Step 2's retail check will pin this down; if
  our numbers match all 31 retail levels, we're right.
- **How big the picture may be.** Retail stays at or below 80×68. Bigger may
  overlap other HUD parts or hit texture-memory limits. Stay inside retail
  sizes for the first version.
- **Whether the game frees and reloads the minimap sprite between races**, so
  a custom sprite never outlives its track. Check in the runtime.
- **3-player mode** draws the picture white regardless of `minimapColor`. Not a
  bug for us, just don't be surprised.
- DKR-R's HUD layout code (`runtime_hud_layout.cpp`) moves and scales the
  whole minimap for widescreen. It moves picture and dots together, so it
  should not need changes — confirm during the in-game test.
- **Grease Pencil API.** Checked in Blender 5.2 (headless): a stroke has
  `cyclic`, each point has `position` and `radius`, and
  `bpy.ops.grease_pencil.cyclical_set` closes strokes. Still to decide: the
  drawing plane for *Draw Edge* (flat top plane vs. projected onto the track).
- **Axes.** Blender is Z-up and the game is Y-up; reuse the conversion the
  addon already applies on geometry import/export rather than a new one.
- **Road width for *Start from AI Path*.** Measure it from the road faces around
  each AI node, or fall back to a fixed default the author then adjusts.
- **"Shape changed".** Step 1 compares the exported bounds with the base file's.
  Check that an untouched remix really produces identical bounds (no rounding
  drift), or it would lose its minimap for nothing.

---

## Appendix — for the person implementing it

**Code locations**

| What | Where |
|---|---|
| Picture loading, tint | `extern/dkr-decomp/src/game_ui.c` → `minimap_init` |
| Drawing the picture and all markers | `game_ui.c` → `hud_render_general` (from "minimap = …") |
| Dot formula | `game_ui.c` → `minimap_marker_pos` |
| Marker colours per character | `game_ui.c` → `gHudMinimapColours` |
| Sprite anchor | `src/textures_sprites.c` → `load_sprite_info` |
| Racer spawning, humans vs CPU | `src/objects.c` → the setup-point loop and `racerEntry->playerIndex = …` |
| Header fields | `include/structs.h` → `LevelModel` 0x20–0x46 |
| Addon header fields | `tools/blender/dkr_track_editor/level_model.py` → `HEADER_FIELDS` |
| Blank model (zeros) | `level_model_layout.py` → `blank_model` |
| Bounds recalculated on export | `level_model_layout.py` → `_rebuild_boxes`, `level_model_edit.recompute_bounds` |
| Placeholder to replace | `operators/placeholders.py` → `DKR_OT_minimap_fit`; `ui/panels.py` → `DKR_PT_minimap` |
| "No minimap" bit | `level_header.py` → `Field(0xBC, "u8", "/unknown/unkBC")` |
| Panel layout to follow | `ui/panels.py` → `DKR_PT_water` (status box, actions, settings, hints) |
| Viewport overlay pattern | `operators/race_ai.py` (the AI Racers line overlay) |
| Enlarged preview pattern | `operators/skybox.py` (gallery thumbnails) |
| Runtime sections | `runtime-recomp/src/game/custom_tracks.cpp` (section table) |

**New code, suggested split**

- `dkr_track_editor/minimap.py`, no `bpy`: the dot formula, the automatic
  numbers, the even-odd fill, the checkered flag, the IA8 conversion. Testable
  headless like `dkrmap.py` and `level_model_layout.py`.
- `dkr_track_editor/operators/minimap.py`: *Draw Edge*, *Start from AI Path*,
  *Close*, *Clear*, *Show on Track*, *Save PNG…*, *Use My PNG…*.

**The dot formula** (normal play; `W`/`D` are the bounds' X and Z spans,
`minX`/`minZ` their low corner, `θ` the rotation):

```
sx = 60 · (W/D) · XScale · (x − minX) / W        = 60 · XScale · (x − minX) / D
sy = −60 · YScale · (z − minZ) / D

dot.x = screenX + ( sx·cosθ + sy·sinθ) + OffsetXAdv1 − anchorX
dot.y = screenY + OffsetYAdv1 − ( sx·sinθ − sy·cosθ) + anchorY
```

Note both axes are divided by the **depth** `D`, so X and Z use the same
pixels-per-unit; the scales only fine-tune. Mirrored play flips the sign of
the rotated X term and uses the `Adv2` offsets.

**Retail reference values** (all 31 levels with a minimap, including two hubs) can be dumped from
`extern/dkr-decomp/assets/.vanilla/us.v77/levels/models/*/*.bin` with
`level_model.load(path)`; tracks without one have sprite 0, scale 1.0 and zero
offsets.

---

## 8. What was built (2026-09-24)

All four steps, in one go. Section 4 still describes the design; this section
is the record of what the code actually does and where it chose differently.

### Where it lives

| What | Where |
|---|---|
| The dot formula, fitting, the even-odd fill, the flag, IA8 + line swap, both payloads, PNG in/out, retail lookup | `tools/blender/dkr_track_editor/minimap.py` (no `bpy`) |
| Edges on Grease Pencil, the track's state, the picture cache, preview, overlay, the eight operators | `dkr_track_editor/operators/minimap.py` |
| Panel, Appearance and Numbers sub-panels | `ui/panels.py` → `DKR_PT_minimap*` |
| Settings (size, rotation, colour, soft edge, flag, overlay, own PNG) | `props.py` → `minimap_*` |
| Export: header bit, model numbers, `minimap/texture.bin` + `sprite.bin` | `operators/pack.py` → `_plan_minimap`, `_encode_minimap`; `dkrmap.py` |
| Validate line | `operators/checks.py` → `minimap.issues` |
| `TEXTURES_2D` / `SPRITES` sections, placeholders, "installed after boot" | `runtime-recomp/src/game/custom_tracks.{hpp,cpp}`, `custom_tracks_hooks.cpp` |
| Track Lab: *own minimap* | `runtime_ui.cpp` |
| Tests | `tools/blender/tests/test_minimap.py`, `test_blender_minimap.py`; `runtime-recomp/tests/custom_tracks_tests.cpp` |

### The retail check (step 2's proof)

`test_minimap.py` runs two lanes of every vehicle set's AI line of every retail
level with a minimap through the retail numbers and onto the retail picture:
**96.2% of the points land on the drawn road** over 27 race levels, every level
above 80% (the lowest, Treasure Caves and Snowball Valley, have stretches where
the line leaves the road the artist drew). The mirrored mapping lands 95%.

What pinned it down, answering the first question in section 7:

- `sprite_init_frame` puts a sprite's texture at `(posX - anchor.x, anchor.y -
  posY)` from the anchor vertex, Y up, on a quad one texel narrower than the
  texture. Solving that against `minimap_marker_pos` gives the dot for world
  `(x, z)` at texel `u = rx + offsetX - posX - 1.5`, `v = ry - offsetY - posY -
  1.25`, where `(rx, ry)` is the rotated, scaled position. The two constants are
  the half-texel quad, the dot sprite's own half-texel centre and the
  truncation of the marker position to a vertex; the data picks 1.5 / 1.0-1.5.
- Mirrored: the picture flips about its anchor, so `offsetXAdv2 = anchor.x -
  offsetXAdv1 + 2` and `offsetYAdv2 = offsetYAdv1`. Retail's hand-set values
  are within two pixels of that on X for all 32 levels that have one (Y was
  nudged by up to four on two of them).
- Retail anchors sit at the picture's bottom-right: `anchor = (posX + width -
  2, posY + height)`, `posX/posY = 15/2`. That corner is what the one-player HUD
  pins at (135, -98); following it keeps the 2-, 3- and 4-player placements
  (which centre on half the anchor) retail's too.

### Where it departs from the plan

- **"Shape changed" is not only the bounds.** A reshaped Ancient Lake whose
  outermost vertices did not move keeps its bounds, and would have kept
  Ancient Lake's picture - the bug this plan exists to fix. So the track keeps
  its retail minimap only when no vertex moved, appeared or went *and* the
  bounds match; textures, colours, flags, surfaces, objects and music still
  keep it. Both the panel and the export decide it with the export's own
  `build_edited_model`; the panel runs it on a timer after the mesh changes,
  because the check writes to the object and a panel cannot.
- **A base with no minimap also gets the bit.** The unused tracks carry sprite
  0, scale 1.0 and zero offsets, so without the bit the game would draw Fossil
  Canyon's picture over them. Hubs that have no minimap already set it.
- **No shortcut count.** Under the even-odd rule a shortcut is just an edge, so
  the status says *N edges* and flags the open ones.
- **Size is also bounded by texture memory.** IA8 widths are whole TMEM lines
  (multiples of 8, like every retail minimap) and `width x height <= 4096`, so a
  square picture stops at 64x64 whatever the slider says.
- **Start From AI Path** takes the road's width from the span of the four lanes,
  widened 1.8x, never under 3.2% of the lap's size on each side, and puts it
  square to the line rather than from lane 1 to lane 4 - some gates list their
  lanes the other way round, which twisted the two edges through each other.
  Loops an inner edge folds into at tight corners are cut out.
- **The drawing plane** is a flat plane 50 units above the track's highest
  point (`plane_height`); only X and Z reach the picture.
- **The "untouched remix" rounding question** is closed: all 55 retail models'
  header bounds equal their vertex extents exactly, so an untouched export
  keeps its bounds byte for byte.
- **A track with its own minimap always ships `model.bin`**, even with the
  geometry untouched, because the numbers live in the model header.

### Answered from section 7

- The sprite is freed with its track: `minimap_init` at `tracks.c:2903`,
  `sprite_free` at `tracks.c:2967`.
- `unkBC` bit 0 only gates the minimap (`game_ui.c:3585`); bit 1 is something
  else (`game_ui.c:692`), so the export sets and clears bit 0 alone.
- `minimap_init` still loads the header's sprite when the bit is set, so a
  track without a minimap costs one small retail sprite. Harmless.

### Still open

- **Not yet seen in game.** Everything above is checked against retail data,
  the payload readers and the runtime's own unit tests, and the full runtime
  builds - but nobody has raced a track with its own minimap yet. The step 4
  list stands: 1P, 2P, 3P, 4P, Adventure 2, time trial with a ghost, and the
  widescreen HUD layout moving picture and dots together.
- **The HD bonus** (a sharper minimap in `-hd.zip`) is not built. A line-swapped
  IA8 texture needs its own replacement hash in `rice_identity`, which only
  knows the level-texture loads today.
- **The viewport overlay** draws with Blender's GPU module, which headless tests
  cannot reach; only the placement of its corners is tested.
