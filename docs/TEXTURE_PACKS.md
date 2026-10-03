# CRT filters and texture packs

These features belong to the **Modern** preset. Accurate always uses the
original game textures and presentation so it remains the release reference.

## CRT display filters

Graphics includes six optional built-in PNG masks. A mask is drawn after the
game image and before DKR-R's settings and performance overlays, so controls and
telemetry remain readable. Scaling can stretch one mask over the viewport or
tile it at native pixel size. Filter Density controls compositing strength and
can be changed while the game is running.

Use **Import Custom CRT Filter** to copy a PNG (maximum 64 MB) into the local
`filters` settings directory. DKR-R decodes and uploads a filter on first use,
then retains the resource so switching does not destroy a texture referenced by
an in-flight frame.

## Native RT64 and Rice texture packs

Use **Graphics > Custom Texture Packs > Import Texture Pack** with a `.zip` or
`.rtz` archive. DKR-R validates its format and lists it with a compatibility
result. Native RT64 archives are retained as archives. Rice archives are
converted transactionally into a managed directory; a failed conversion is
removed before it can be enabled.

A native pack must contain a parseable RT64 `rt64.json` database, either at the
archive root or within one containing directory. Referenced replacements use
formats supported by the pinned RT64 loader. Native and converted Rice packs
can be enabled and disabled independently.

Live changes are queued to RT64's presentation thread. RT64 stops its texture
stream workers, clears the previous replacement mapping, installs the validated
set, preloads required entries, and re-resolves resident textures. If a new set
is rejected, DKR-R attempts to restore the previous set and does not retry a
failed transaction every frame.

## Rice compatibility bridge

Rice PNG names such as `Game#CRC#format#size_rgb.png`, `_a.png`, and
`_all.png` are supported directly. During import DKR-R:

- validates every archive path and Rice identity;
- groups variants by the full identity, including optional palette hashes;
- combines `_rgb` with the red channel of `_a` as alpha, matching Rice's
  channel convention;
- makes unpaired `_rgb` images explicitly opaque and prefers complete `_all`
  images when supplied;
- generates a collision-checked RT64 database and stable 64-bit native aliases;
  and
- reports source-image, identity, and merged-pair coverage in the Graphics UI.

The RT64 patch pipeline computes the same Rice identity from DKR's live texture
load operation. It selects the alias only when an enabled pack contains that
replacement, so Accurate mode and native RT64 hashes remain unchanged when no
Rice pack is active. Rice identity calculation is cached by native texture
identity while the enabled-replacement check remains live for hot-swapping.

The supplied `diddy_kong_racing_pm_.zip` validation archive passes with 475
source PNGs, 328 unique replacement identities, and 131 reconstructed
RGB/alpha pairs. All 328 generated RT64 aliases and Rice identities are unique,
and every merged output was verified pixel-for-pixel. The archive is not
bundled with DKR-R.

## High-resolution textures for a custom track

A custom track that ships artwork of its own carries each picture at the size
the console can load - 64x32 for a colour image, because a level texture goes
into the RDP's 4 KiB of texture memory as one block. The Blender addon's export
also writes a Rice pack beside the track, `<track>-hd.zip`, holding each picture
at the resolution its author made it. With it enabled, the renderer draws the
original in place of the reduction.

**You do not import this pack yourself.** **Mods / Hacks → Import mods → DKR-R
tracks** finds `<track>-hd.zip` beside the track it just installed and imports
it in the same gesture (`import_archive` with a `TrackPackOwner`). Such a pack
is *born enabled*, filed against the track (`Origin::TrackPack`), and does
**not** get a row in the library above - it is not one of the third-party packs
you collect and browse. It shows only under the browser's **Track packs**
visibility filter, for auditing; in the track's **Details** in **My mods**
(**Manage HD textures**); and, for a working-folder track, on its Track Lab
row, which carries a one-line HD status and a **Manage** button. Both open this
page's single-pack modal. A working-folder track whose matching
`<track>-hd.zip` is not installed yet offers **Install HD textures** on that
row. A rescan during authoring re-imports nothing: the pack is keyed by its
archive's entries (each name, CRC-32 and size), so an unchanged re-export is
skipped and
any real change - a new picture, or the same payloads under new replacement
names - replaces the old pack rather than adding another.

This is the one case where a pack replaces one track's textures and nothing
else. A pack matches by hash, globally - but these textures were written by the
addon and exist nowhere in the ROM, so the only thing a match can reach is the
track that brought them.

**How the names are made.** The Rice identity is `<crc>#<fmt>#<siz>`: the CRC of
the texture's bytes as loaded, over a width, height and row stride the RT64
patch derives from the tile `material_init` sets up. The CRC reads RDRAM, which
N64Recomp stores with every 32-bit word byte-swapped on a little-endian host,
so the addon hashes the payload's texels in that swapped order - a pack
exported before it did (every one written before this note) names textures
the game never asks for, and has to be exported again. The addon computes it
offline from the payload it has just written
(`tools/blender/track_lab/rice_identity.py`), and
`tools/blender/tests/test_rice_identity.py` holds that to the source: the
patch's own `riceCRC32` and `reverseDXT`, compiled from
`patches/rt64/0011-enable-runtime-rice-texture-aliases.patch` and compared over
thousands of buffers, and this importer's `parse_filename` reading back every
name the pack writes. The derivation is transcribed too, and lands on the
texture's own size for every size the addon accepts.

- **It fails cleanly.** A name that does not match finds no replacement, and the
  game draws the 64x32. Nothing about the track changes.
- **Modern only**, as for every pack. Accurate draws the track's own textures.
- **Alpha.** The pack's picture is drawn with the render mode the track gives
  the texture. An opaque texture ignores the original's alpha. A blended one
  uses all of it, so an original with soft alpha looks softer than the 64x32,
  which RGBA16 limits to one bit - that is the pack working. A cut-out is drawn
  with `G_RM_AA_ZB_TEX_EDGE`, which RT64 turns into a discard below an eighth of
  alpha where the console reads a hardened texture; the export therefore puts
  a copy of a cut-out's original in the pack, hardened at half and with its
  edge colours spread into the holes, so the HD picture is cut where the
  64x32 is.
- **Some sizes cannot have one.** A texture whose rows are narrower than one
  8-byte word of texture memory - a 4-bit texture under 16 texels wide, an
  8-bit one under 8 - is read by the game with a stride its rows do not have,
  so its identity depends on memory the addon never wrote. The export names
  these and leaves them out.
- **The minimap.** A track that draws its own minimap gets that picture in the
  pack too, drawn again from the same road edges at 8 pixels a texel, so it
  covers what the texture does and the racers' dots stay on the road. The game
  loads a minimap line-swapped (`gDPLoadTextureBlockS`), and its name is
  computed for that load. A minimap from *Use My PNG* has no larger version
  and is left out.
- **One pack per export.** The pack's `dkr-r-track.json` and the track's
  `manifest.json` carry the same texture digest, and a pack from another
  export of the same track matches nothing. The installer compares them: a
  `<track>-hd.zip` whose digest does not match the manifest's is left out, and
  Track Lab's row for the track says so. The fix is to re-export or download
  the matching pair - keep the two together.
- **Size.** The pack carries every original as it was made, so twenty 4 MB
  photographs make an 80 MB pack. The export says when a pack is large, and
  leaves out any single image past this importer's limits (256 MB, 64
  megapixels) rather than let it fail the whole import.

## Other Project64-era formats

Jabo packs use `pack.xml` and opaque plugin-specific hashes. DKR-R detects and
retains them for inspection but does not guess a replacement mapping. A wrong
database can replace unrelated textures or cause intermittent corruption.
Convert and verify those assets with RT64's texture tooling so the result
includes `rt64.json`, then import that native archive.

## Storage and release safety

- Windows: `%APPDATA%\DKRPort\texture-packs`
- Linux: `$XDG_CONFIG_HOME/dkr-port/texture-packs` or
  `~/.config/dkr-port/texture-packs`
- Portable Windows: `dkr-runtime-data\texture-packs` beside the executable

Imported filters and packs remain user data and are never copied into a DKR-R
release. Rice conversion writes only normalized generated filenames into a
temporary directory beneath `texture-packs`. Absolute paths, parent traversal,
duplicate variants, unsafe image dimensions, alias collisions, archives over
4 GB, unreadable ZIPs, and malformed native databases are rejected before
renderer activation.
