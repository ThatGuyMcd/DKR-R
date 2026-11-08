# Changelog

Each section is the release notes of that version: the Track Lab repository
releases the version `track_lab/blender_manifest.toml` names, with its section
from this file. Bump the version in the manifest and in `bl_info`
(`track_lab/__init__.py`), and add the section here, in the same commit.

## Unreleased

- **Up to 127 checkpoints per vehicle set**, up from 60, for finer control of
  the AI racing line. The limit is the runtime's: a DKR-R build carrying the
  raised checkpoint table loads 127; older DKR-R builds and original hardware
  still load only the first 60 placed. Validation reports a set past 127 as an
  error and one past 60 as a warning, and the AI Racers panel says the same.
- **Mode Checklist** under Level Type, for Battle, Bananas, Eggs, Hub and Boss
  Race. It lists what each mode needs to play: start ring, AI graph,
  treasure chests, Coin Creators, egg nests and Head-For Points, exits and
  their destinations, the boss's checkpoints. Each row says why it matters
  and, where it can, has a button that adds it: chests, flags and Head-For
  Points are placed per racer with the racer filled in. Validate reports the
  same rows, so a battle arena with no AI graph ("AI racers will not move") or
  an egg arena with no Egg Creator now stops the export.
- **AI Node Graph is enabled** for Battle, Bananas and hubs, and for a race
  that has a loop-de-loop. New **Tag Destinations** gives the nodes nearest
  the weapon balloons and the chests the destination classes the arena AI
  looks for. Add Branch no longer resets every node's elevation and
  destination class to the branch's.
- **Weapon nodes for arena bots.** Battle and Bananas bots never look at a
  balloon: they drive the AI node graph to nodes of destination class 1. With
  the new *Auto Weapon Node* (Place, on by default), placing a weapon balloon
  there also places a class-1 node on it, parented so it moves with the
  balloon, and links it to the two nearest graph nodes within 1,500 units -
  or, with no graph yet, AI Line From Curve keeps it and links it once the
  graph is drawn. *Link Nodes* / *Unlink Nodes* join or split two selected
  nodes both ways, and *Link Weapon Nodes* joins any left loose.
- **Show AI Graph** overlay in AI Node Graph: every link, weapon nodes ringed
  in their balloon's colour with a 50-unit reach circle (green with a balloon
  inside, red and labelled when not), bank nodes in their racer's colour,
  optional node ids. Select a node for a heat map out from it, by steps or by
  distance, with unreachable parts in grey, and the dashed route a bot without
  a weapon drives from there. DKR Object says what a node is for, lists its
  links and selects a weapon node's balloon.
- The Mode Checklist and Validate warn about a weapon node with no balloon
  within 100 units or no link to the graph, and Tag Destinations warns when a
  balloon is more than 50 units from the node it tagged.
- **Hub exits to other custom tracks.** In DKR Object, *Leads to* picks a
  retail level or a custom track by id. The export writes a placeholder and
  a `levelLinks` entry in the manifest, and DKR-R fills in the track's level
  id when the hub loads (docs/CUSTOM_TRACKS.md, *Hub doors*).
- A few raw fields now have names: an AI node's destination class, a
  Head-For Point's kind and racer, and the racer a chest or flag belongs to.

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
