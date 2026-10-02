# DKR track editor - Blender addon

Author a Diddy Kong Racing track in Blender: place objects, items and the AI
AI node graph over a track's geometry, reshape the geometry itself, then write
it all back out and package it as a `.dkrmap` for DKR-R's `custom-tracks/`
folder.

This is Phase 1 of `docs/BLENDER_ADDON_PLAN.md` - remixing an existing track -
plus all three steps of Phase 2: the track's own geometry can be reshaped, and
geometry can be added and removed.

## Install

```
python tools/blender/generate_catalog.py     # needs the decomp checked out
python tools/blender/package_addon.py
```

Then in Blender: **Edit > Preferences > Add-ons > Install from Disk**, pick
`tools/blender/dkr_track_editor.zip`, and enable *DKR Track Editor*. The panels
appear in the 3D viewport sidebar (`N`) under a **DKR** tab.

**Point it at your decomp assets.** Object artwork lives in an extracted decomp
tree, not inside the addon. When the addon is run from a checkout of this
repository it finds `extern/dkr-decomp/assets/.vanilla/<version>` on its own; an
installed copy cannot, so set **Decomp Assets** in the addon's preferences. The
Place panel shows which tree is in use, and warns when there is none.

Verified against Blender 5.2. The manifest declares 4.2 as the minimum, and the
zip carries both `blender_manifest.toml` and `bl_info` so it installs as an
extension or as a legacy addon.

## Use

**Waterfalls.** Select sloping or vertical track faces in Edit Mode, then use
**DKR > Water > Waterfalls > Add Waterfall**. Choose a game preset or **Use
Selected Texture** to use the image picked in Textures, including a custom PNG.
Set speed in texels/s (default 44.53), Down/Up and the repetitions over the
selected height. Under Appearance, choose Blended or Cutout, Pass-through
(on by default) and Double-sided (on by default). Opaque artwork stays opaque.

Each waterfall gets a dedicated texture-table entry and a TexScroll object;
using the same picture elsewhere does not make that surface move. The image
must wrap vertically and be no taller than 64 texels. Flat horizontal faces
have no fall direction and are refused. Select Faces finds the affected
geometry; Remove stops its movement and keeps its faces and texture. Speed can
be edited in Waterfalls or on the TexScroll's DKR Object panel. Pick From Active
Face repairs a missing link or links a manually placed TexScroll to an entry;
every face using that entry then moves.

Validate Track checks missing references, invalid indices, unsafe UV ranges,
triangle flags that prevent scrolling, duplicate controllers and incompatible
wrap modes. Re-segment and Add Water preserve the links. Motion uses the game's
TexScroll; animated viewport preview is not included yet. Custom-texture motion,
Modern/Accurate interpolation, HD replacements and Track Lab reloads still need
manual verification in game.

**Music.** Under *Export > Header > Music*, choose **Game Music** to use one of
the game's songs, or **Music File** to ship your own **MP3 or WAV** (mono or
stereo, 8-192 kHz, 1 s to 15 min, up to 64 MB; compressed WAV is refused). The
file is checked by its bytes when you pick it, and again at export. Set
**Volume**, **Loop Start** and **Loop End** (0 loops at the end of the file;
whatever is before Loop Start plays once as an intro) and what happens on the
**Final Lap** - *Speed Up* like the game's songs, pitch rising, or *Keep Speed*.
The game song chosen below the file still matters: it plays silently under your
music, and its fades, the pause menu and the final-lap speed-up are what your
music follows - so pick a race song. MidiFade/MidiChSet objects switch channels
of a game song and do nothing with a file; the panel and the export say so.
Listen in Track Lab; the Play button stays unavailable. MP3 encoders add a
little silence at the edges, so a seamless loop is easiest from a WAV or an MP3
with a LAME/Xing header (which almost all encoders write).

**Choose the Level Type first.** A new scene shows one panel, *Level Type*,
asking what kind of level this is: Race, Boss Race, Challenge (Battle, Bananas
or Eggs), Hub, or the advanced *Special* group (Cutscene, Menu Backdrop, Test
Race). Everything else follows the answer, so the other panels stay hidden
until it is given: what the Place list offers, how many start positions a grid
gets, what validation demands, and the header's race type. Importing a retail
track answers it from that track's header. Changing it later asks first when
objects or grids would be affected, and offers to rebuild the grids where they
stand. `LEVEL_TYPE_PLAN.md` gives the source of every rule.

Under the dropdown sit the settings the level type owns, which the header
never has a second answer for: the boss (a boss race is raced in the boss's own
vehicle), the vehicles the track allows and the default one (a challenge
allows exactly one), the lap count, and the start grid.

**Import a track.** Press **Import Track** and pick one of the 65 retail levels
by name. That loads its geometry and *both* of its object maps in one go.

The two maps matter. A level header names `map-2` for the track - checkpoints,
zippers, scenery, the AI graph - and `map-collectables` for the pickups. Loading
only the first gives a track with no coins and no balloons: Ancient Lake goes
from 184 objects to 98.

`File > Import > DKR Object Map` still loads a single map by path, for a map
that is not part of a retail level. Either way, objects arrive **drawn with the
artwork the game uses** - a palm tree looks like
a palm tree, a boost balloon looks different from a trap balloon - sorted into a
collection per category, with every decoded field as a custom property. Track
coordinates run to several thousand units, so the importer widens the viewport
clipping to match.

**Import the track geometry.** The Geometry panel loads a level model from
`levels/models/<world>/*.bin`; *Import Track* does it for you. Without it there
is nothing to aim at: objects float in an empty viewport. The geometry arrives
**textured**, from the level model's own texture table and per-triangle UVs, so
the road looks like road and the grass like grass.

It arrives as **one mesh**, holding every vertex of every segment exactly once.
That is what makes it editable. An edit is addressed in the file as
`(segment, vertex index)`, so each Blender vertex records its own in the
`dkr_segment` and `dkr_vertex` integer attributes and the exporter can put a
moved vertex back where it came from. The three kinds a batch can be are
material slots rather than three separate objects, because a segment holding
batches of more than one kind would otherwise need its vertices in two meshes at
once - and then "which copy did the author move" has no answer:

| Slot | What it is |
|---|---|
| `dkr surface ...` | the road, drawn and driven on |
| `dkr decoration ...` | drawn but not collidable |
| `dkr invisible walls` | `RENDER_HIDDEN` batches, most of them still solid |

Blender's own material-slot *Select* button is how you isolate one kind in Edit
Mode, and solid viewport shading tints each slot so the split reads at a glance
without turning the textures off. Invisible walls start masked out, since they
otherwise bury the track; *Hide/Show Invisible Walls* toggles the mask, and the
mesh keeps their vertices either way.

**Reshape the track.** *Edit Geometry* unlocks the mesh and opens it in Edit
Mode. Move vertices, extrude, subdivide, delete faces. *Check Geometry Changes*
reports what an export would write without writing anything.

There are two ways back out, and which one runs is decided by what you did
rather than chosen:

- **Nothing was added or removed.** The shipped `.bin` is reloaded and only your
  differences are applied to it, so everything you did not touch stays
  byte-identical. Import a track, change nothing, export, and the model is the
  one the game ships, byte for byte - checked against all 110 retail models.
- **Counts changed.** Then every offset after the change shifts, so each segment
  is rebatched and the file is laid out afresh. That cannot be byte-identical to
  retail and is not meant to be; retail's padding between arrays follows no
  rule, so a rebuild writes a valid layout rather than the original one.

Keeping the two apart is the point: reshaping a track never loses its byte
equality just because the addon is now able to rebuild one.

When the geometry is untouched the package carries no `model.bin` at all and the
track keeps pointing at the base track's geometry.

New geometry keeps working because Blender propagates the attributes: a face you
extrude inherits the render flags, texture and source batch of the face it grew
from, and its vertices inherit which segment they are in. A face built from
nothing instead of extruded has no source, so if it also belongs to no segment
the export says so rather than putting it somewhere arbitrary. Extrude makes
quads, which the file cannot store, so they are fanned into triangles on the way
out.

**Merge by Distance** preserves each face's segment and averages vertex colours
channel by channel. Imported meshes carry `dkr_face_segment` on faces and
`dkr_colour_r/g/b/a` on vertices, so welding a boundary cannot invent a distant
segment ID or mix bits between colour channels. Older saved meshes still export
with their original attributes; reimport their geometry before welding to get
these additional attributes.

Two ceilings are worth knowing about, both shown in the Geometry panel:

- **Load budget.** The game reserves a fixed arena for a level model and the
  heaviest retail track already uses 68% of it. Going over does not fail - it
  writes past the heap, and the only complaint is a debug print no retail build
  shows - so the panel shows the percentage and how many more triangles fit.
- **Oversized segments.** Collision considers at most ten segments at a time,
  chosen by bounding-box overlap, so a segment stretched across the map holds a
  slot everywhere and can push the ground a racer is standing on out of the
  running. The symptom is falling through the floor somewhere else entirely,
  with no diagnostic at all. One giant polygon is enough to do it.

**Or build the track from your own mesh.** Model one in Blender - textured, if
you like, with image materials and a UV unwrap - join the pieces with `Ctrl+J`,
and the Geometry panel offers to convert it. *Track From Mesh* keeps the
pictures the materials draw: each becomes one of the track's own textures (see
*Or with a picture of your own* below), mapped with the UVs you gave it, and the
viewport shows the result. A material with no picture comes out untextured.
*Track From Mesh + A Track's Textures* does the same starting from a shipped
track's texture table, which brings a coherent set of images and their surface
types with it. Either way the Textures panel can change anything afterwards. The
mesh is partitioned into segments with the bounding boxes, BSP and PVS to match,
written out as its own `.bin`, and imported back as ordinary editable geometry.

Three things the conversion reports rather than decides silently:

- A material names a picture, not a kind of ground, so every picture starts as
  road. Give grass, sand or ice their surface per material with *Set Surface
  Type*.
- `Ctrl+J` matches UV maps by name. Pieces whose maps were named differently
  come out with one map per name, each piece mapped in only one of them. The
  conversion takes each face's mapping from the map that has it, and says so.
- A track holds at most 255 textures of its own and a model's table 255
  entries. A mesh past either is refused whole, with the materials named - never
  half converted. *Keep The Mesh's Textures*, in the conversion dialog, turns
  the whole behaviour off.

**Texture the track with anything in the ROM.** The Textures panel browses every
one of DKR's 3D textures - **1401** of them in the US v1.0 extraction - as
thumbnails, filtered by folder (`dino`, `winter`, `water`, `space` and the rest)
and by a search over their names. Select faces in Edit Mode, pick a texture,
press *Apply To Selected Faces*.

A track is not limited to the textures it was built from. A level model's
texture table stores indices into the ROM's global texture list, so it can name
any of them; the addon appends an entry to the table and points the faces at it.

Four things come with the texture rather than being asked about:

- **Surface type.** Chosen in the panel, because it lives on the table entry
  rather than on the face. The same picture applied twice with different surface
  types is two entries, which is how one image is road in one place and grass in
  another.
- **Animation.** A texture with more than one frame flags the batches drawing it
  for animation, and a still one clears the flag. That is what retail does for
  every animated texture and only for those, so a picked waterfall moves.
- **Which pass draws it.** The game draws see-through textures in a second pass,
  after the solid track, and a face left in the wrong pass is never drawn at
  all. Applying a texture moves the faces to the pass it belongs in, and the
  export checks every face again, so a road retextured with the ROM's water
  shows up.
- **Mapping.** *Keep The Mapping* leaves the picture covering the same ground as
  the one it replaced, rescaled for the new texture's size. *Project Flat*
  plants the texture on the world along whichever axis each face most faces, at
  a scale in map units per repeat - which is what a face you built or extruded
  needs, since it inherits UVs that are well-formed and mean nothing. The
  default, 256 units, is retail's own median. Projection tiles continuously
  across a join rather than restarting the texture at every triangle.

*Select* picks out every face already drawn with the chosen texture, *Remove*
puts faces back on their baked colours alone, and *Apply UV Editing* writes the
mapping you made in Blender's UV editor into the track.

**Transparency.** A picture's alpha can be used three ways, and the panel's
*Transparency* picks one for the faces you apply it to:

- *Opaque* - alpha is ignored.
- *Cut-Out* - alpha cuts holes, for fences, leaves and grates. The rest is solid
  and hides what is behind it.
- *Blended* - alpha blends, for glass, water and smoke. Drawn after the solid
  track.

*As Made* (the default) takes the texture's own look. Whether a texture blends
at all is decided by how it was made - one of the ROM's keeps its render mode -
so a solid texture can be cut out but not blended, and the panel says so rather
than doing something else. *Transparency Of Selected* changes only the look of
faces that already have a texture. Materials show the alpha in Material
Preview, cut at half as the game cuts it.

For a picture of your own the look belongs to the texture: the import reads it
off the image - no alpha is *Opaque*, clean holes are *Cut-Out*, anything softer
is *Blended* - and *Change*, beside the chosen texture, sets it afterwards; every
face drawing it follows. RGBA16 keeps one bit of alpha, so a blend in it is all
or nothing; RGBA32 keeps soft edges at 32x32. A cut-out is hardened at half and
its edge colours are spread into the holes, so no dark fringe appears when the
texture is filtered, and the HD pack's copy of a cut-out is hardened the same
way, so DKR-R cuts the HD picture where it cuts the 64x32. A mesh converted with
*Track From Mesh* keeps what its pictures' alpha asks for.

**Or with a picture of your own.** The top of the Textures panel is *This
track's own artwork*: press **+**, pick any image Blender can read, and the
track ships it. The package carries the texture, DKR-R publishes a longer
`ASSET_TEXTURES_3D` table for it, and from that point it behaves like any other
texture in the panel - browse it, apply it, project it, select by it.

Two limits are the console's and the addon refuses rather than warns:

- A level texture is loaded into the RDP's **4 KiB of texture memory as one
  block**, so a colour image gets 2048 texels - **64x32**. The eight-bit
  greyscale formats reach 64x64. Pick the format in the file dialog; it decides
  the largest size, and the import says what it took the picture down from.
- A side larger than **64** cannot tile. `material_init` only recognises powers
  of two up to that, and gives anything else a clamp - the texture stretches
  once across each face instead of repeating.

The reduction is severe and there is no way around it inside the track, so look
at the thumbnail before building a track on it. Everything else is arranged so
you do not have to think about it: the image is resampled to the largest size
that fits, in the shape closest to the original's, and written as a PNG in
`dkr_textures/` beside the `.blend` so the package can be rebuilt from the scene
alone. The original is kept beside it too, at full size, in
`dkr_textures/original/`.

**The export gives the resolution back.** Beside `my-track.dkrmap` it writes
`my-track-hd.zip`: a texture pack holding each picture at the size it was made,
named so that DKR-R's renderer draws it in place of the 64x32. You do not import
it yourself. Keep it next to the `.dkrmap` and import the folder that holds
both with DKR-R's **Mods / Hacks → Import mods → DKR-R tracks** - DKR-R
installs the pack with the track: enabled, tied to the track, and out of the
texture-pack browser. It loads the next time the game starts. A track you are
still editing in Track Lab's working folder gets **Install HD textures** on its
row instead; that row then reads *HD textures: restart to load*, and in game one
**Restart & play in HD** relaunches straight into it in HD. The track does not
need the pack: without it the game draws the 64x32, which is what the Accurate
preset always does. And the pack replaces only textures the addon wrote, which
exist in no other track. See `docs/TEXTURE_PACKS.md` in DKR-R.

The **order** of the list is the texture's identity - the runtime hands out ids
by position - so removing one moves the rest, and removing one the geometry has
already given a table entry is refused with the mesh named. Point those faces at
something else first.

**Water that moves.** Waves in DKR are not a material: the game simulates them
over a grid of equal squares, and draws the water it finds on each. The *Water*
panel lays that water:

1. Select the lake bed in Edit Mode, or nothing to cover the whole track.
2. Put the 3D cursor at the water line.
3. *Add Water*, choosing *Waves* or *Calm*.

It lays one flat square of water per tile at that height, skips squares where
the ground stands above it everywhere, and flags them the way retail flags its
own. For waves it then cuts the **whole track** into squares of that size, one
segment each, because the game places every segment on the wave grid and a
segment of dry land on a wave square would draw waves over it. The track
becomes its own base file, as *Re-segment Track* makes it. *Tile Size* on Auto
picks the smallest square - 1024 upwards - that keeps the track under the 127
segments a model holds and the water inside the 32 columns the game can mark;
a track that still has too many squares has its dry ones joined into blocks.
Retail's water texture is used unless *Use The Chosen Texture* is on; waves
need a 16x16 or 32x32 RGBA texture.

*How the waves move* sets the header bytes the simulation reads: height, detail,
how fast the texture flows, how often it repeats, whether the waves blend, and
the shape of the two sine waves under *Wave Shape*. *Preset* copies a retail
track's - Whale Bay's rolling sea, Pirate Lagoon's chop, Hot Top Volcano's lava.
A remix starts with its base track's settings. Waves run in single player; split
screen draws the water flat, as retail does. Calm water works everywhere, and a
car sinks into it as on Ancient Lake.

The grid keeps itself honest. *Re-segment Track* cuts along the grid when the
track has waves, an export cuts the track again if an edit knocked a segment off
its square or deleted the reference water, and *Remove Water* switches the
waves off where the water went. A retail wave track that you only reshape
exports byte for byte.

For a one-command example, `tools/blender/make_texture_demo_track.py` builds a
flat track surfaced with an image you pass it, through the same operators:

```sh
blender --background --factory-startup \
    --python tools/blender/make_texture_demo_track.py -- \
    --image path/to/picture.jpg --out build/my-track.dkrmap
```

Adding a texture makes the export rebuild the layout rather than patch the file,
since everything after the texture table moves. A track's table tops out at 255
entries, which no retail track comes near - the largest is Spaceport Alpha at 63.

One thing does not come back yet: vertex colours are read for display only, so
repainting the baked lighting in Blender does not reach the track. UVs do come
back, but only through *Apply UV Editing* - editing the `UVMap` alone leaves the
file's own values in place.

Once geometry is loaded, Blender's own face snapping works, and *Drop To
Surface* casts the selected objects straight down onto the road - carrying the
ray on through decoration and walls, which now share one mesh with it.

**Place objects.** The Place panel has two tabs, *Structure* and
*Collectables*, and the tab is both the list and the object map a placed
object joins. The list shows what the level type uses - the Egg Creator only in
an egg challenge, hub doors only in a hub - measured from where the 65 retail
levels place each type rather than decided by taste; *Show incompatible types*
lists the rest, marked, and validation warns about them. The weapon balloon is
offered in its five colours (red missile, blue boost, green trap, yellow
shield, the rainbow magnet), and every button's tooltip says what it places,
where retail uses it and whether this level type does. The types an author
reaches for first are pinned at the top. Press *Coin* and a coin appears; press *Frog* and the
decoded frog mesh appears, textured. A new object is placed at the 3D cursor
carrying the field values retail uses most, so it behaves like the ones already
shipped.

How an object is drawn comes from its own header, so it matches the game:

| | |
|---|---|
| sprite billboard | 49 of the 85 types - trees, balloons, coins, bushes |
| textured mesh | 32 types, built from the decoded object model |
| marker | 4 types whose header points at a debug sphere, such as AI nodes |

A weapon balloon reads its `balloonType` and shows that weapon's sprite, so a
boost balloon and a trap balloon look different the way they do in game.

*Refresh Object Artwork* redraws everything, which is what to press after
setting the asset path for the first time.

**Edit fields.** Select an object and the DKR Object panel shows its fields with
the right widget for each: a slider bounded by what the C type can hold, or a
dropdown of an enum's members. Where a type has an angle, rotate the object in
the viewport and the field follows.

`pad*` and `unk*` fields are hidden behind the **Show Raw Bytes** toggle. They
exist so an entry encodes to the bytes the game expects, and nobody has
identified what the `unk` ones do - a checkpoint declares 19 editable fields of
which 15 are `unk`, which buried the four that decide how it behaves. Thirteen
object types are nothing but raw bytes; the panel says so rather than showing a
wall of them.

**Generate the start grid.** *Generate Start Grid*, under Level Type, places
the start positions the level type needs at the 3D cursor - eight in two
staggered rows for a race (the median of the 20 retail grids), two side by side
for a boss race, four around the arena facing the centre for a challenge, one
per entrance for a hub - with `racerIndex` and `entranceID` filled in. They are
parented to one root Empty: move and turn the root and the grid follows, and
each start position exports its world position and world angle. The root itself
is never exported. *Adjust Last Operation* tunes the spacing, a challenge's
radius and facing, and whether each position is dropped onto the road. A
missing index is not cosmetic - the game starts that racer at the map origin,
facing nowhere in particular - so validation reports it as an error.

**See what the bots drive.** A race or boss track shows the *AI Racers* panel.
*Show Bot Lines* draws the four lanes the computer racers drive, as the game
computes them: the checkpoints of one vehicle's set, sorted and paired the way
`checkpoint_update_all` does it, each moved by that lane's lateral and vertical
offsets, and joined with the same Catmull-Rom spline `func_80045C48` steers
along. The line follows a checkpoint while you drag it, and the faint lines are
the alternate route. The panel says when the vehicle shown loads a set with no
checkpoints, when an index sits on two checkpoints, and when a set passes the 60
the game loads.

*Difficulty* sets the header's behaviour levels, 0 to 9, one for each point a
save can be at (not won yet, race won, silver coins, Tracks mode, trophy race)
in each adventure; *Copy Difficulty From Retail* takes a retail race's. The
asset tool's JSON calls byte 1 of that block `silver-coins` and byte 2
`completed`, but the game uses them the other way round, so the panel labels
them by what the game does. *Start Skill* is each character's reaction at the
start and nothing else. *Checkpoint Sets* picks which checkpoints each vehicle's
racers load. A remix starts with its base track's values.

**Draw the AI node graph.** The panel is greyed out and marked *in
development* while the arena AI is studied; the
operators below are still there, in F3 search. This is *not* the racing line, which the game
interpolates from the checkpoints and which no node is read for. The graph
drives the Battle and Bananas challenges, hub NPCs and loop-de-loops, so it is
worth drawing for an arena or a hub and does nothing for a normal circuit.

Add a curve, draw the route, then *AI Nodes From
Curve*. It samples the curve at a spacing you choose, numbers the nodes and
wires the adjacency, closing the loop if the curve is cyclic. A second curve can
be spliced on as a branch, attaching to the nearest nodes that still have a free
link slot.

**Validate.** Uses the rules of the track's level type, and checks the things
that are painful to diagnose in game: a start grid missing a racer, an AI
graph with one-way or dangling links, duplicate checkpoint indices, two racers
on the same start square, an exit pointing nowhere, a grid built for another
level type. Errors mean the map is wrong; warnings mean it is unusual, which
retail tracks sometimes are too. Every retail level passes in its own level
type.

**Package.** Fill in the track name and author, then *Export .dkrmap*. The
export refuses a track with no level type, since the header's race type comes
from it. The *Level Header* subpanel shows what Level Type sets as locked rows,
steps through the game's music list by name, and picks the skybox from a
gallery of all 18 domes. Each thumbnail is a panorama of the dome as seen from
its centre - the game moves the dome onto the camera every frame, so that is the
only way a racer ever sees it - and *Show In Viewport* puts the chosen dome
around the track, as a preview that is never exported. An imported track brings
its music, sky and AI difficulty, and a remix that changes any of them ships
the change over the header it inherits. Listening to the music is not there yet, and the *Minimap*
panel is marked *in development*. The next section covers what lands in the package and
the one rule about sharing it.

## What the packager produces

`Export .dkrmap` writes a directory holding the manifest, a compiled
`header.bin`, both compiled object maps, any textures the track ships in
`textures/`, and the glTF sources beside them. A track with pictures of its own
(textures, or a minimap drawn from its edges) also gets `<track>-hd.zip` next
to the directory - the high-resolution pack, the minimap at 8 times its size. A
track with its own music carries it as `music/main.mp3` (or `.wav`) and a
`music` entry in a schema 2 manifest; see `docs/CUSTOM_TRACKS.md`. It
stays a separate file (a track can be shared without it), but DKR-R's installer
treats the two as one gesture: import the folder that holds both and the pack
goes in with the track.

**Everything is compiled here**, without the decomp's `dkr_assets_tool` - that
tool builds a whole `assets.bin` and ships as a Linux binary, so depending on it
would have put a C++ toolchain between an author and their track. Neither
encoder is merely plausible: the correct bytes already exist in the retail
`assets.bin`, and the tests require reproducing them exactly - all 136 shipped
object maps, all 65 level headers, byte for byte.

**A level has two object maps and they stay separate.** The runtime patches a
different header field from each - `0xBA` from the structure slot, `0x36` from
collectables - so the package carries `objects_structure.bin` and
`objects_collectables.bin`, and each manifest entry names its slot.

Two header offsets are left at **zero** for the runtime to patch, since a custom
track's maps only get their indices when it builds the extended asset table.
Zero rather than -1: the game guards the table with a signed `mapID >= i`, so -1
slips past the clamp and reads `objMapTable[-1]`.

**A header therefore always ships with both slots.** Zero is a valid index, not
an absence - anything out of range clamps to 0 and loads object map 0 - so a
header with only one payload points the other slot at another level's objects
and hangs. An empty slot gets an **empty** map, 16 bytes with `fileSize` 0,
which is what retail does in 16 of its own maps.

Payloads dropped in by hand survive a re-export, and a manifest never claims a
payload that is not there.

**Do not commit a built package.** A remix keeps whatever the base track had, so
its payloads contain retail object-map data verbatim wherever nothing was
changed - the structure slot of a lightly edited remix is the base track's map
byte for byte. `docs/ASSET_POLICY.md` forbids committing extracted maps, and
`scripts/scan_for_game_assets.py` will not catch it: that scanner checks file
extensions, ROM magic and size, and cannot see map data inside a 1.3 KB `.bin`.
`custom-tracks/` is in `.gitignore` for this reason. Share the `.blend` instead -
anyone with the decomp can rebuild the package from it.

Install a package from **Mods / Hacks → Import mods → DKR-R tracks** - choose
the `.dkrmap` or the folder around it, or a `.zip` of either - or by copying the
folder into DKR-R's `custom-tracks/` directory. Not `mods/`: librecomp owns that
for its `.nrm` format and rejects anything else it finds there. Track Lab works
in the Accurate profile too; importing, arming or playing a track switches to
Modern, which custom tracks need, and tells you it did.

## Layout

```
tools/blender/
  generate_catalog.py       builds data/catalog.json from the decomp
  package_addon.py          builds the installable zip
  run_tests.py              runs every suite
  dkr_track_editor/
    __init__.py             registration; imports bpy only inside register()
    gltf_io.py              object-map reader and writer
    catalog.py              the generated catalogue, and value coercion
    level_model.py          track geometry decoder, and the shared primitives
    object_model.py         object mesh decoder
    object_map_encoder.py   object map -> section bytes
    level_header.py         level header -> its 200 bytes
    assets.py               resolve an asset name to a file in the decomp tree
    preview.py              build the artwork an object is drawn with
    ai_graph.py             sampling, adjacency and the format's limits
    race_ai.py              the race bots' lanes and the header's AI bytes
    transparency.py         which pass draws a face, and a picture's alpha
    water.py                the wave grid, as the game builds it
    validate.py             pre-export checks
    dkrmap.py               the .dkrmap container
    music_audio.py          recognise and measure a track's MP3/WAV music
    textures.py             the ROM's 3D textures, and encoding your own
    scene.py                object map <-> Blender scene
    props.py                scene settings
    prefs.py                where to find the decomp assets
    operators/              import, export, place, AI, validate, package
    operators/geometry_export.py   the mesh's edits -> a level model
    operators/textures.py          pick, apply and map a texture
    operators/custom_textures.py   an image -> a texture the track ships
    operators/race_ai.py           the bot lines overlay, Copy Difficulty
    operators/water.py             Add Water, Select/Remove Water, presets
    operators/music.py             choose and check the track's music file
    ui/panels.py            the sidebar
    data/catalog.json       generated; do not edit by hand
  tests/
    test_roundtrip.py           byte-exact read/write, no Blender needed
    test_validate.py            the rules, checked against retail tracks
    test_geometry.py            every retail level model decodes
    test_assets.py              every object type resolves to its artwork
    test_binary_format.py       the object-map binary format, vs assets.bin
    test_encoder.py             all 136 retail object maps re-encode exactly
    test_header.py              all 65 retail headers rebuild exactly
    test_textures.py            the ROM's table, vs what retail wrote
    test_custom_textures.py     an image -> the bytes the asset tool would write
    test_race_ai.py             the bots' lanes and AI bytes, vs the game's rules
    test_transparency.py        the pass rule, vs every retail batch
    test_water.py               the wave grid, vs every retail wave track
    test_blender_roundtrip.py   byte-exact through a real Blender scene
    test_blender_operators.py   the operators actually work
    test_music.py               MP3/WAV recognition and the music in a package
    test_blender_music.py       the music controls, save/reopen and export
    fixtures/                   two short test tones (generated, original)
```

Everything except `scene.py`, `preview.py`, `props.py`, `operators/` and `ui/`
avoids `bpy`, so the formats and rules can be tested on a plain Python.

## Tests

```
python tools/blender/run_tests.py            # all nine suites
python tools/blender/run_tests.py --all      # every retail map, not a sample
python tools/blender/run_tests.py --skip-blender
```

The one that matters most is the round trip. Every retail object map - 272 of
them across `us.v77` and `us.v80`, 18,863 placed objects - is read, turned into
Blender objects, exported and compared byte for byte against the original, and
then again with the object artwork built. That is the guarantee that editing one
zipper does not silently perturb anything else in the track, and that what an
object is *drawn* with never leaks into what it *is*.

`test_geometry.py` decodes all 110 retail level models and `test_assets.py` all
390 object models, checking the structural limits the formats impose (a batch
addresses at most 256 vertices, because the index is a `u8`).

`test_validate.py` runs the validation rules over those same retail maps on the
principle that a rule which rejects a shipped track is a wrong rule. It caught
two during development; see the plan document.

## Regenerating the catalogue

`data/catalog.json` is generated, never hand-edited. Rebuild it when the decomp
moves:

```
python tools/blender/generate_catalog.py
python tools/blender/generate_catalog.py --check   # fail if stale, for CI
```
