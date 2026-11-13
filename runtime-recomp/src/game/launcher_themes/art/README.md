# Seasonal launcher artwork

These are original interface decorations made for DKR-R. The rounded models,
bright colours and simple shading follow the approved design previews. No
textures, models, characters or pixels were extracted from the game.

`models.mjs` and `base-models.mjs` contain the editable 3D shapes. They use
Three.js 0.180.0 for offline authoring only. The launcher does not run a browser,
load JavaScript or render these models.

`atlas.png` holds 38 transparent sprites in 128-pixel cells, eight across.
Their order is the `Icon` enum in `../art.hpp`. The last two cells are empty.
The US and Canadian flags each have their own matching bunting.

The atlas is embedded in the runtime, so the same artwork is present in Windows
and Linux builds. It is decoded once for each launcher renderer, uploaded as
one texture and released before that renderer closes. Normal drawing submits
two image quads. Changing the theme reuses the same atlas.

After updating the approved atlas, run this from the repository root:

```console
python scripts/embed_launcher_art.py
python scripts/embed_launcher_art.py --check
```

This exports asset bytes only. It is not run during configure or build, and
does not rewrite any gameplay or generated source. The native render check
decodes the embedded image and checks every sprite, including its transparency.
