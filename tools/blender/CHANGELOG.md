# Changelog

Each section is the release notes of that version: the Track Lab repository
releases the version `track_lab/blender_manifest.toml` names, with its section
from this file. Bump the version in the manifest and in `bl_info`
(`track_lab/__init__.py`), and add the section here, in the same commit.

## 0.1.0

The first release of Track Lab.

**Needs** Blender 4.2 or newer, and a DKR-R build newer than 1.0.5 Beta 12:
tracks with their own music or minimap do not play correctly on Beta 12 or
older.

- **Game assets from your ROM.** One click extracts the artwork, textures and
  retail tracks from a clean USA 1.0 or 1.1 ROM - no decomp needed.
- **Start from a retail track or from scratch.** Import any of the 65 retail
  levels with its objects, AI and textured geometry, or build the track from
  your own mesh. The Level Type decides what can be placed and what
  validation asks for.
- **Edit the geometry.** Reshape, add and remove faces; segments and vertex
  colours survive Merge by Distance.
- **Texture it.** Any texture in the ROM, or a picture of your own, with
  transparency and an HD copy the game uses in Modern presentation.
- **Water.** Waves the way the game simulates them, and waterfalls with
  scrolling water.
- **Objects.** Place objects and items with click-to-place and snapping, edit
  their fields, generate the start grid and renumber the checkpoints.
- **AI racers.** See the lines the bots drive and set the track's difficulty.
- **Skybox.** Pick any of the game's 18 domes and preview it in the viewport.
- **Minimap.** Draw it from the track or the AI path, in standard and HD.
- **Music.** Use a game song, your own MP3 or WAV, or a MIDI file converted
  into a native song.
- **Validate and package.** Checks the rules of the track's level type, then
  exports a `.dkrmap` for DKR-R's `custom-tracks/` folder.
