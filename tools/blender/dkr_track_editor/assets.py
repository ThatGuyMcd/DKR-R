"""Resolve DKR asset names to files in an extracted decomp asset tree.

Everything the addon needs in order to show an object as it looks in game is
reachable by name, and every hop is data the decomp already publishes. Nothing
here is guessed or hard coded:

    ASSET_OBJECT_PALMTREETOP
      -> asset_objects.meta.json          -> objects/headers/PalmTreeTop.json
      -> the header's "models" list       -> ASSET_SPRITE_OBJECTS_PALMTREETOP
      -> asset_sprites.meta.json          -> sprites/objects/palm_tree_top.json
      -> the sprite's "start-texture"     -> ASSET_TEX2D_OBJECTS_PALMTREETOP_0
      -> asset_textures_2d.meta.json      -> textures/2d/objects/palm_tree_top_0.png

A 3D object takes the same first hop and then goes through
``asset_object_models.meta.json`` to a ``.bin`` instead.

Which of the two applies is the header's ``model-type``. Most DKR scenery -
palm trees, balloons, coins, bushes - is a **sprite billboard**, not a mesh:
84 of the 304 object headers, against 211 that are real models. So a preview
that only handled meshes would leave exactly the objects a track author places
most as nothing at all.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

import json
import os
import re
from typing import Dict, List, Optional

#: ``<asset>.meta.json`` at the root of an extracted version directory maps every
#: asset enum name to a file under ``folder``.
META_OBJECTS = "asset_objects.meta.json"
META_OBJECT_MODELS = "asset_object_models.meta.json"
META_SPRITES = "asset_sprites.meta.json"
META_TEXTURES_2D = "asset_textures_2d.meta.json"
META_TEXTURES_3D = "asset_textures_3d.meta.json"
META_LEVEL_HEADERS = "asset_level_headers.meta.json"
META_LEVEL_MODELS = "asset_level_models.meta.json"
META_LEVEL_OBJECT_MAPS = "asset_level_object_maps.meta.json"

MODEL_TYPE_3D = "OBJECT_MODEL_TYPE_3D_MODEL"
MODEL_TYPE_SPRITE = "OBJECT_MODEL_TYPE_SPRITE_BILLBOARD"
MODEL_TYPE_VEHICLE_PART = "OBJECT_MODEL_TYPE_VEHICLE_PART"
MODEL_TYPE_MISC = "OBJECT_MODEL_TYPE_MISC"


class AssetError(Exception):
    pass


class ObjectHeader:
    """One entry from ``objects/headers/*.json``."""

    __slots__ = ("asset_id", "name", "model_type", "models", "scale", "behavior",
                 "shadow_scale", "shadow_group", "shadow_texture", "path")

    def __init__(self, asset_id, raw, path):
        self.asset_id = asset_id
        self.path = path
        self.name = raw.get("internal-name") or asset_id
        self.model_type = raw.get("model-type", "")
        self.models: List[str] = list(raw.get("models") or [])
        self.scale = float(raw.get("scale", 1.0) or 1.0)
        self.behavior = raw.get("behavior", "")
        self.shadow_scale = float(raw.get("shadow-scale", 0.0) or 0.0)
        #: ``init_object_shadow`` loads a shadow texture only for a non-zero
        #: group, from the header word at 0x34 the extraction has not named.
        self.shadow_group = int(raw.get("shadow-group", 0) or 0)
        unknown = raw.get("unknown") or {}
        self.shadow_texture = int(unknown.get("unk34", -1)) if "unk34" in unknown else -1

    @property
    def is_sprite(self) -> bool:
        return self.model_type == MODEL_TYPE_SPRITE

    @property
    def is_mesh(self) -> bool:
        return self.model_type in (MODEL_TYPE_3D, MODEL_TYPE_VEHICLE_PART)

    def __repr__(self):
        return "ObjectHeader(%r, %s, %d model(s))" % (
            self.name, self.model_type, len(self.models)
        )


class SpriteTile:
    """One texture of a sprite frame, where the game puts it.

    ``x`` and ``y`` are the tile's top left corner in the frame's own pixel
    space, from the texture's ``sprite-x`` and ``sprite-y``: Y grows downwards,
    as in the picture.
    """

    __slots__ = ("png", "x", "y", "width", "height")

    def __init__(self, png, x, y, width, height):
        self.png = png
        self.x = x
        self.y = y
        self.width = width
        self.height = height

    def quad(self, anchor_x: int, anchor_y: int):
        """``(left, right, bottom, top)`` in pixels from the anchor, Y up.

        Exactly the corners ``sprite_init_frame`` writes, which is one pixel
        short of the texture each way: the quad spans ``width - 1`` by
        ``height - 1``. That is not a slip to correct - the next tile down
        starts on the last row of this one, and the shortfall is what makes the
        two meet without a seam or an overlap.
        """
        left = self.x - anchor_x
        top = anchor_y - self.y
        return (left, left + self.width - 1, top - self.height, top - 1)


class SpriteFrame:
    """Everything one frame of an ``ASSET_SPRITE_*`` draws.

    A frame is not one picture. A palm tree is five textures stacked down its
    height and a balloon is three - two halves and the string - because the
    N64's texture memory holds 4 KB, so anything bigger is cut into strips.
    Drawing only the first strip is what left a tree as a sliver of canopy.
    """

    __slots__ = ("anchor_x", "anchor_y", "tiles")

    def __init__(self, anchor_x, anchor_y, tiles):
        self.anchor_x = anchor_x
        self.anchor_y = anchor_y
        self.tiles: List[SpriteTile] = tiles

    def bounds(self):
        """``(left, right, bottom, top)`` of the whole frame, Y up."""
        quads = [t.quad(self.anchor_x, self.anchor_y) for t in self.tiles]
        return (min(q[0] for q in quads), max(q[1] for q in quads),
                min(q[2] for q in quads), max(q[3] for q in quads))


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_size(path: str):
    """``(width, height)`` from a PNG's header, without decoding it."""
    try:
        with open(path, "rb") as handle:
            head = handle.read(24)
    except OSError:
        return None
    if len(head) < 24 or head[:8] != PNG_SIGNATURE or head[12:16] != b"IHDR":
        return None
    return (int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big"))


class Level:
    """One entry from ``levels/headers/*.json``, with its files resolved.

    A track's objects are split across **two** object maps, not one: ``map-2``
    holds the track itself - checkpoints, zippers, scenery, the AI line - and
    ``map-collectables`` holds the pickups. All 65 retail headers have both.
    Loading only one leaves a track with no coins and no balloons, which is easy
    to mistake for a decoding bug.
    """

    __slots__ = ("name", "world", "race_type", "header_path", "model_path",
                 "objects_path", "collectables_path")

    def __init__(self, name, world, race_type, header_path, model_path,
                 objects_path, collectables_path):
        self.name = name
        self.world = world or ""
        self.race_type = race_type or ""
        self.header_path = header_path
        self.model_path = model_path
        self.objects_path = objects_path
        self.collectables_path = collectables_path

    @property
    def label(self) -> str:
        """``AncientLake`` reads better as ``Ancient Lake``.

        Some extractions suffix the header filename with the revision it came
        from, as ``AncientLake_v79``. That belongs to the file, not to the
        track, so it is dropped here while :attr:`name` keeps it as the identity
        the lookup uses.
        """
        stem = re.sub(r"_v\d+$", "", self.name)
        spaced = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", stem)
        return re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", " ", spaced)

    @property
    def world_label(self) -> str:
        return self.world.replace("WORLD_", "").replace("_", " ").title()

    @property
    def object_maps(self) -> List[str]:
        return [p for p in (self.objects_path, self.collectables_path) if p]

    @property
    def is_complete(self) -> bool:
        return bool(self.model_path and self.objects_path)

    def __repr__(self):
        return "Level(%r, %s)" % (self.name, self.world_label)


class AssetTree:
    """An extracted asset version directory, e.g. ``.vanilla/us.v77``."""

    def __init__(self, root: str):
        self.root = root
        self._meta_cache: Dict[str, dict] = {}
        self._header_cache: Dict[str, Optional[ObjectHeader]] = {}
        self._texture_cache: Dict[str, Optional[str]] = {}
        self._texture3d_cache: Dict[int, Optional[str]] = {}
        self._flip3d_cache: Dict[int, bool] = {}
        self._levels: Optional[List["Level"]] = None

    # -- discovery -------------------------------------------------------

    @classmethod
    def find(cls, start: str) -> Optional["AssetTree"]:
        """Walk up from a file inside the tree to the version directory.

        An author picks a level model or object map in the file browser, so the
        addon knows one path inside the tree and can find the rest from it.
        """
        if not start:
            return None
        current = os.path.abspath(start)
        if os.path.isfile(current):
            current = os.path.dirname(current)
        for _ in range(8):
            if os.path.isfile(os.path.join(current, META_OBJECTS)):
                return cls(current)
            parent = os.path.dirname(current)
            if parent == current:
                break
            current = parent
        return None

    @classmethod
    def discover(cls, *hints: str) -> Optional["AssetTree"]:
        """Find an extracted asset tree without being told exactly where it is.

        Placing a coin has to show a coin the first time the button is pressed,
        not only after an object map has been imported from inside the tree. So
        each hint is tried as a path inside a tree, and then as a repository
        root holding one under ``extern/dkr-decomp``.

        Later versions win, so ``us.v80`` is preferred over ``us.v77``.
        """
        for hint in hints:
            if not hint:
                continue
            direct = cls.find(hint)
            if direct is not None:
                return direct
            found = cls._search_below(hint)
            if found is not None:
                return found
        return None

    #: Where an extracted tree sits relative to a checkout of this repository.
    SEARCH_GLOBS = (
        os.path.join("extern", "dkr-decomp", "assets", ".vanilla", "*"),
        os.path.join("assets", ".vanilla", "*"),
        os.path.join(".vanilla", "*"),
        "*",
    )

    @classmethod
    def _search_below(cls, root: str) -> Optional["AssetTree"]:
        import glob as _glob

        if not os.path.isdir(root):
            return None
        for pattern in cls.SEARCH_GLOBS:
            candidates = sorted(_glob.glob(os.path.join(root, pattern)), reverse=True)
            for candidate in candidates:
                if os.path.isfile(os.path.join(candidate, META_OBJECTS)):
                    return cls(candidate)
        return None

    @property
    def label(self) -> str:
        """``us.v77`` rather than the whole path, for the sidebar."""
        return os.path.basename(self.root.rstrip(os.sep)) or self.root

    def is_usable(self) -> bool:
        return os.path.isfile(os.path.join(self.root, META_OBJECTS))

    # -- manifests -------------------------------------------------------

    def _meta(self, name: str) -> dict:
        if name not in self._meta_cache:
            path = os.path.join(self.root, name)
            if not os.path.isfile(path):
                raise AssetError("%s is missing from %s" % (name, self.root))
            with open(path, "r", encoding="utf-8") as handle:
                self._meta_cache[name] = json.load(handle)
        return self._meta_cache[name]

    def _lookup(self, meta_name: str, asset_id: str) -> Optional[str]:
        """Absolute path of the file an asset enum name refers to."""
        try:
            meta = self._meta(meta_name)
        except AssetError:
            return None
        entry = meta.get("files", {}).get("sections", {}).get(asset_id)
        if not entry:
            return None
        folder = meta.get("folder", "")
        return os.path.join(self.root, folder, entry["filename"])

    def path_for(self, meta_name: str, asset_id: str) -> Optional[str]:
        """The file an asset enum name refers to.

        The public form of the lookup, for the modules that walk a whole
        section rather than following a name chain - :mod:`textures`, which
        resolves all 1401 3D textures so an author can pick any of them.
        """
        return self._lookup(meta_name, asset_id)

    def order(self, meta_name: str) -> List[str]:
        """The asset ids of a section, in index order."""
        try:
            return list(self._meta(meta_name).get("files", {}).get("order", []))
        except AssetError:
            return []

    # -- objects ---------------------------------------------------------

    def object_header(self, asset_id: str) -> Optional[ObjectHeader]:
        """The ``ObjectHeader`` for an ``ASSET_OBJECT_*`` id."""
        if asset_id in self._header_cache:
            return self._header_cache[asset_id]

        path = self._lookup(META_OBJECTS, asset_id)
        header = None
        if path and os.path.isfile(path):
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    raw = json.load(handle)
                if raw.get("type") == "ObjectHeader":
                    header = ObjectHeader(asset_id, raw, path)
            except (ValueError, OSError):
                header = None
        self._header_cache[asset_id] = header
        return header

    def object_model_path(self, asset_id: str) -> Optional[str]:
        """The ``.bin`` an ``ASSET_OBJECTMODEL_*`` id names."""
        sidecar = self._lookup(META_OBJECT_MODELS, asset_id)
        return _binary_beside(sidecar, "raw")

    # -- levels ----------------------------------------------------------

    def levels(self) -> List[Level]:
        """Every level the tree describes, with its files already resolved.

        This is what lets an author pick "Ancient Lake" instead of guessing
        which of 138 files called ``asset_level_object_maps_<n>`` is the one.
        """
        if self._levels is not None:
            return self._levels

        found = []
        for asset_id in self.order(META_LEVEL_HEADERS):
            path = self._lookup(META_LEVEL_HEADERS, asset_id)
            if not path or not os.path.isfile(path):
                continue
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    header = json.load(handle)
            except (ValueError, OSError):
                continue
            if header.get("type") != "LevelHeader":
                continue
            found.append(Level(
                name=os.path.splitext(os.path.basename(path))[0],
                world=header.get("world"),
                race_type=header.get("race-type"),
                header_path=path,
                model_path=self._level_model_path(header.get("model")),
                objects_path=self._object_map_path(header.get("map-2")),
                collectables_path=self._object_map_path(
                    header.get("map-collectables")
                ),
            ))
        self._levels = found
        return found

    def level(self, name: str) -> Optional[Level]:
        for entry in self.levels():
            if entry.name == name:
                return entry
        return None

    def level_using(self, path: str) -> Optional[Level]:
        """The level that a model or object map belongs to, if any.

        Lets an importer handed the wrong half of a track say which track it
        was, so the author can load the whole thing instead.
        """
        if not path:
            return None
        target = os.path.normcase(os.path.abspath(path))
        stem = os.path.normcase(os.path.splitext(os.path.abspath(path))[0])
        for entry in self.levels():
            for candidate in [entry.model_path] + entry.object_maps:
                if not candidate:
                    continue
                candidate = os.path.abspath(candidate)
                if os.path.normcase(candidate) == target:
                    return entry
                if os.path.normcase(os.path.splitext(candidate)[0]) == stem:
                    return entry
        return None

    def _level_model_path(self, asset_id) -> Optional[str]:
        if not asset_id:
            return None
        return _binary_beside(self._lookup(META_LEVEL_MODELS, asset_id), "raw")

    def _object_map_path(self, asset_id) -> Optional[str]:
        """The glTF an ``ASSET_LEVEL_OBJECT_MAPS_*`` id names."""
        if not asset_id:
            return None
        sidecar = self._lookup(META_LEVEL_OBJECT_MAPS, asset_id)
        return _binary_beside(sidecar, "objects")

    def asset_index(self, section: str, build_id: str) -> int:
        """The index of ``build_id`` within an asset section, or -1.

        An ``AssetId`` hint names the section (``ASSET_LEVEL_HEADERS``) and the
        glTF stores the build id; the entry stores the index. The manifest for a
        section follows from its name, so no table has to be hard coded.
        """
        if not build_id:
            return -1
        meta = "asset_%s.meta.json" % section.replace("ASSET_", "").lower()
        try:
            index = self.order(meta).index(build_id)
        except ValueError:
            return -1
        return index

    def translation_table(self) -> List[str]:
        """``objects/level_object_translation_table.json``.

        An object map entry stores an index into this, not into the global
        object list, so an encoder cannot write a single entry without it.
        """
        path = os.path.join(
            self.root, "objects", "level_object_translation_table.json"
        )
        if not os.path.isfile(path):
            return []
        try:
            with open(path, "r", encoding="utf-8") as handle:
                return list(json.load(handle).get("table") or [])
        except (ValueError, OSError):
            return []

    # -- sprites and textures --------------------------------------------

    def sprite_textures(self, asset_id: str) -> List[str]:
        """Every frame of an ``ASSET_SPRITE_*`` as a PNG path.

        A sprite names one starting texture and a frame count per animation, and
        the frames follow it consecutively in the 2D texture order. Walking that
        order is how a multi-frame sprite is recovered without guessing at
        filename suffixes.
        """
        path = self._lookup(META_SPRITES, asset_id)
        if not path or not os.path.isfile(path):
            return []
        try:
            with open(path, "r", encoding="utf-8") as handle:
                sprite = json.load(handle)
        except (ValueError, OSError):
            return []

        start = sprite.get("start-texture")
        if not start:
            return []
        counts = sprite.get("frame-tex-count") or [1]
        total = max(1, sum(int(c) for c in counts))

        order = self.order(META_TEXTURES_2D)
        try:
            index = order.index(start)
        except ValueError:
            texture = self.texture_png(start)
            return [texture] if texture else []

        found = []
        for offset in range(total):
            if index + offset >= len(order):
                break
            texture = self.texture_png(order[index + offset])
            if texture:
                found.append(texture)
        return found

    def sprite_frame(self, asset_id: str, frame: int = 0) -> Optional[SpriteFrame]:
        """One frame of an ``ASSET_SPRITE_*``, every tile of it in place.

        ``frame-tex-count`` is the number of textures **in each frame**, not a
        frame count: a palm tree's ``[5]`` is one frame of five strips, a
        banana's ``[1, 1, 1]`` three frames of one. The frame is placed on the
        object by its anchor (the asset's ``unk4``/``unk6``,
        ``SpriteAsset.anchor``), which is usually near the bottom - a balloon
        hangs from the end of its string.

        A frame with no textures - a lava spurt's first is empty - falls
        through to the next one that has some, since an empty preview helps
        nobody.
        """
        path = self._lookup(META_SPRITES, asset_id)
        if not path or not os.path.isfile(path):
            return None
        try:
            with open(path, "r", encoding="utf-8") as handle:
                sprite = json.load(handle)
        except (ValueError, OSError):
            return None

        start = sprite.get("start-texture")
        counts = [int(c) for c in (sprite.get("frame-tex-count") or [1])]
        order = self.order(META_TEXTURES_2D)
        if not start or start not in order:
            return None
        base = order.index(start)

        offsets = [0]
        for count in counts:
            offsets.append(offsets[-1] + count)
        frames = [f for f in range(len(counts)) if counts[f] > 0]
        if not frames:
            return None
        frame = frame if frame in frames else frames[0]

        tiles = []
        for index in range(base + offsets[frame], base + offsets[frame + 1]):
            if index >= len(order):
                break
            sidecar = self._lookup(META_TEXTURES_2D, order[index])
            png = _texture_image(sidecar)
            size = png_size(png) if png else None
            if not size:
                continue
            try:
                with open(sidecar, "r", encoding="utf-8") as handle:
                    data = json.load(handle)
            except (ValueError, OSError):
                data = {}
            tiles.append(SpriteTile(png, int(data.get("sprite-x", 0) or 0),
                                    int(data.get("sprite-y", 0) or 0), *size))
        if not tiles:
            return None
        return SpriteFrame(int(sprite.get("unk4", 0) or 0),
                           int(sprite.get("unk6", 0) or 0), tiles)

    def shadow_png(self, header: Optional[ObjectHeader]) -> Optional[str]:
        """The texture a header's shadow is drawn with, if it has one.

        For most objects that is a dark blob under a tree. A ground zipper has
        nothing else: its model is the debug sphere, never drawn in a race, and
        the arrow on the road *is* its shadow, projected onto the track.
        """
        if header is None or not header.shadow_group or header.shadow_texture < 0:
            return None
        if header.shadow_texture & 0x8000:
            return None  # a 3D texture; no retail shadow uses one
        order = self.order(META_TEXTURES_2D)
        if header.shadow_texture >= len(order):
            return None
        return self.texture_png(order[header.shadow_texture])

    def model_id_for(self, asset_id: str, variant: int = 0) -> Optional[str]:
        """Which of the header's models a variant draws."""
        header = self.object_header(asset_id)
        if header is None or not header.models:
            return None
        return header.models[variant if 0 <= variant < len(header.models) else 0]

    def texture_png(self, asset_id: str) -> Optional[str]:
        """The PNG an ``ASSET_TEX2D_*`` id refers to, if it was extracted."""
        if asset_id in self._texture_cache:
            return self._texture_cache[asset_id]
        png = _texture_image(self._lookup(META_TEXTURES_2D, asset_id))
        self._texture_cache[asset_id] = png
        return png

    def texture_3d_png(self, texture_id: int) -> Optional[str]:
        """The PNG for an index into the global 3D texture list.

        An object model's texture table stores such an index rather than a
        name, so this is the last hop between a decoded mesh and the artwork it
        is drawn with.
        """
        if texture_id in self._texture3d_cache:
            return self._texture3d_cache[texture_id]

        order = self.order(META_TEXTURES_3D)
        png = None
        if 0 <= texture_id < len(order):
            png = _texture_image(self._lookup(META_TEXTURES_3D, order[texture_id]))
        self._texture3d_cache[texture_id] = png
        return png

    def texture_3d_flipped(self, texture_id: int) -> bool:
        """Whether a 3D texture's PNG holds its rows opposite to the ROM.

        Every retail 3D texture does - all 1416 sidecars say so. The extraction
        turns each picture right way up for a person to look at and records it
        as ``flipped-image``, and the asset tool turns it back when it builds
        (``buildTexture.cpp:112``). A texel's ``t`` counts from the ROM's first
        row, which is therefore the PNG's last: anything that draws a model's
        UVs over the extracted picture as it stands draws it upside down.
        """
        if texture_id in self._flip3d_cache:
            return self._flip3d_cache[texture_id]
        flipped = False
        order = self.order(META_TEXTURES_3D)
        if 0 <= texture_id < len(order):
            sidecar = self._lookup(META_TEXTURES_3D, order[texture_id])
            try:
                with open(sidecar, "r", encoding="utf-8") as handle:
                    flipped = bool(json.load(handle).get("flipped-image"))
            except (TypeError, OSError, ValueError):
                flipped = False
        self._flip3d_cache[texture_id] = flipped
        return flipped

    # -- the whole answer -------------------------------------------------

    def preview_for(self, asset_id: str, variant: int = 0):
        """How to draw one object type: ``(kind, path, header)``.

        ``kind`` is ``"sprite"`` with a PNG path, ``"mesh"`` with a ``.bin``
        path, or ``"none"`` when the object has no visual the addon can build -
        an AI node or a trigger, which are markers rather than things.

        ``variant`` picks among the header's models, which is what lets a weapon
        balloon show the right colour: its five entries are indexed by the
        object's own ``balloonType`` field.
        """
        header = self.object_header(asset_id)
        if header is None or not header.models:
            return "none", None, header

        index = variant if 0 <= variant < len(header.models) else 0
        model_id = header.models[index]

        if header.is_sprite or model_id.startswith("ASSET_SPRITE_"):
            frames = self.sprite_textures(model_id)
            return ("sprite", frames[0], header) if frames else ("none", None, header)

        path = self.object_model_path(model_id)
        return ("mesh", path, header) if path else ("none", None, header)


#: What a sidecar's ``type`` field says the file is.
KIND_OBJECT_MAP = "LevelObjectMap"
KIND_LEVEL_MODEL = "LevelModel"
KIND_OBJECT_MODEL = "ObjectModel"
KIND_OBJECT_HEADER = "ObjectHeader"
KIND_LEVEL_HEADER = "LevelHeader"


def identify(path: str) -> Optional[str]:
    """What kind of asset a path holds, from its sidecar's ``type``.

    The extracted tree names everything ``*.json``, so an object map and a level
    model look identical in a file browser. Knowing which is which is what lets
    an importer say "that is track geometry, use the other button" instead of
    "not a LevelObjectMap sidecar", which tells an author nothing they can act
    on.
    """
    if not path or not os.path.isfile(path):
        return None
    if path.lower().endswith(".bin"):
        sidecar = os.path.splitext(path)[0] + ".json"
        return identify(sidecar) if os.path.isfile(sidecar) else None
    if not path.lower().endswith(".json"):
        return None
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (ValueError, OSError):
        return None
    kind = data.get("type")
    return kind if isinstance(kind, str) else None


def _texture_image(sidecar: Optional[str]) -> Optional[str]:
    """The PNG a texture sidecar refers to.

    A still texture sits beside its sidecar under the same stem. An animated one
    names its frames explicitly in ``images`` and numbers them, so there is no
    ``zipper2.png`` to find - only ``zipper2_0.png`` and its fifteen successors.
    Taking the first frame is what turns a zipper from an untextured grey ring
    into a zipper.
    """
    if not sidecar or not os.path.isfile(sidecar):
        return None

    plain = os.path.splitext(sidecar)[0] + ".png"
    if os.path.isfile(plain):
        return plain

    try:
        with open(sidecar, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (ValueError, OSError):
        return None

    for name in data.get("images") or []:
        candidate = os.path.join(os.path.dirname(sidecar), name)
        if os.path.isfile(candidate):
            return candidate
    return _binary_beside(sidecar, "raw")


def _binary_beside(sidecar: Optional[str], key: str) -> Optional[str]:
    """Follow a ``{"raw": "x.bin"}`` sidecar to the file it names."""
    if not sidecar or not os.path.isfile(sidecar):
        return None
    try:
        with open(sidecar, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (ValueError, OSError):
        return None
    name = data.get(key)
    if not name:
        return None
    path = os.path.join(os.path.dirname(sidecar), name)
    return path if os.path.isfile(path) else None
