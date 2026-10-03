"""Scene-level settings for the track editor."""

from __future__ import annotations

import bpy
from bpy.props import (
    BoolProperty, CollectionProperty, EnumProperty, FloatProperty,
    FloatVectorProperty, IntProperty, PointerProperty, StringProperty,
)

from . import catalog as catalog_module, level_types, race_ai, water

#: Blender hands an enum callback's strings to C without taking a reference, so
#: a list built fresh each call can be collected while the menu still points at
#: it - the symptom being a picker that opens empty. Holding the last list
#: returned for each key is what keeps them alive.
_ITEMS = {}


def _keep(key, items):
    _ITEMS[key] = items
    return items


def category_items(self, context):
    """Categories with something usable in the chosen tab and level type.

    Counted the way the list will show them, so a category never opens onto
    nothing. ``ALL`` stays first whatever else changes: the tab and the level
    type both reset the choice to it, and a dynamic enum is stored by position.
    """
    try:
        catalog = catalog_module.load()
    except Exception:  # noqa: BLE001 - an enum callback must not raise
        return _keep("category", [("ALL", "All", "Every object type")])
    counts = level_types.category_counts(
        catalog, level_types.current_key(self), self.slot,
        show_all=self.show_incompatible,
    )
    items = [("ALL", "All (%d)" % sum(counts.values()),
              "Every type in this tab")]
    for category in catalog.categories:
        if counts.get(category):
            items.append((category, "%s (%d)" % (category.title(), counts[category]),
                          "%d types" % counts[category]))
    return _keep("category", items)


def _reset_category(self, context):
    """A new tab or level type has a different set of categories."""
    self.category = "ALL"


def _level_items():
    items = [(level_types.NONE, "Not Chosen", "No level type yet", 0)]
    for number, key in enumerate(level_types.FAMILIES, start=1):
        family = level_types.FAMILIES[key]
        items.append((key, family.label, family.tooltip, number))
    return items


def _sub_items(family):
    return [(key, text, help_text, number)
            for number, (key, text, help_text)
            in enumerate(level_types.SUBTYPES[family])]


def texture_group_items(self, context):
    """The folders the ROM's 3D textures are extracted into.

    Imported here rather than at module scope because resolving them reaches for
    the asset tree, and this module is imported while the addon is still being
    registered.
    """
    items = [("ALL", "All", "Every texture in the ROM")]
    try:
        from . import prefs, textures as texture_catalogue  # noqa: PLC0415

        entries = texture_catalogue.catalogue(prefs.resolve(context))
        for group in texture_catalogue.groups(entries):
            count = sum(1 for entry in entries if entry.group == group)
            items.append((group, group.title(), "%d textures" % count))
    except Exception:  # noqa: BLE001 - an enum callback must not raise
        pass
    return _keep("texture_group", items)


def texture_surface_items(self, context):
    """What the ground made of a newly applied texture behaves like.

    The same list the Set Surface Type operator offers, from the catalogue's
    ``SurfaceType``, because it is the same choice - made when the texture is
    applied rather than afterwards, since the surface type is part of what
    decides whether an entry can be reused or a new one is needed.
    """
    try:
        from .operators import geometry as geometry_ops  # noqa: PLC0415

        found = [(str(value), name, help_text)
                 for value, name, help_text in geometry_ops.surface_items()]
    except Exception:  # noqa: BLE001 - an enum callback must not raise
        found = []
    return _keep("texture_surface", found or [("0", "Road", "")])


def _texture_opacity_changed(_settings, context):
    from .operators import textures  # noqa: PLC0415
    textures.show_panel_opacity(context)


def transparency_items(auto=True):
    """The looks a face can have, for the panels. ``AUTO`` first if asked for."""
    from . import transparency as looks  # noqa: PLC0415

    items = []
    if auto:
        items.append((looks.AUTO, "As Made",
                      "Whatever the texture is made as: its own look for one of "
                      "this track's, and solid or see-through for one of the "
                      "ROM's, as the ROM wrote it"))
    for mode, label in ((looks.OPAQUE, "Opaque"), (looks.CUTOUT, "Cut-Out"),
                        (looks.BLEND, "Blended")):
        items.append((mode, label, looks.DESCRIPTIONS[mode]))
    return items


def texture_format_items(self, context):
    """The formats a texture a track brings with it may be written in.

    Not the whole of ``FORMAT_CODES``: the two colour-indexed ones need a
    palette out of ``ASSET_EMPTY_14``, and a track cannot add one of those.
    """
    from . import textures as texture_module  # noqa: PLC0415

    described = {
        "RGBA16": "Colour, one bit of alpha. What 1034 of the ROM's own use",
        "RGBA32": "Full colour and alpha. Four times the memory, so 32x32 at most",
        "IA16": "Greyscale with a full alpha channel",
        "IA8": "Greyscale with alpha, four bits each",
        "IA4": "Greyscale with one bit of alpha, four bits a texel",
        "I8": "Greyscale. Reaches 64x64, where colour stops at 64x32",
        "I4": "Greyscale, four bits a texel. The smallest",
    }
    items = []
    for name in texture_module.CUSTOM_FORMATS:
        code = texture_module.FORMAT_CODES[name]
        best = texture_module.largest_size(code) or (0, 0)
        items.append((
            str(code), name,
            "%s. Up to %dx%d" % (described.get(name, name), best[0], best[1]),
        ))
    return _keep("texture_format", items)


class DKR_CustomTexture(bpy.types.PropertyGroup):
    """One image the track ships itself, as the scene remembers it.

    The PNG is the record, not the file the author picked: it has already been
    resampled to a size the RDP can load and written where a re-export can find
    it, so the package can be rebuilt from a ``.blend`` alone. ``source`` is
    kept only so the panel can say where the picture came from.
    """

    name: StringProperty(name="Name", default="Texture")
    source: StringProperty(name="From", default="")
    #: Relative to the directory the ``.blend`` is in, so the scene and its
    #: pictures move together. Plain text rather than a ``FILE_PATH``: Blender's
    #: ``//`` prefix is what a path property understands, and only from 4.5
    #: onwards and only when the property opts in - a warning on every assign
    #: for anyone on 4.2, which is the version this addon says it needs.
    #: :func:`..operators.custom_textures.resolve` is the other half.
    png: StringProperty(name="Image", default="")
    #: The same picture at full resolution, as a PNG, stored the way ``png`` is.
    #: The track never reads it; the high-resolution texture pack the export
    #: writes beside the ``.dkrmap`` does. Empty for a texture added before the
    #: pack existed, which the export then rebuilds from ``source`` if it can.
    original: StringProperty(name="Original", default="")
    width: IntProperty(default=0)
    height: IntProperty(default=0)
    #: A ``FORMAT_CODES`` value, stored as the number the file stores.
    format: IntProperty(default=1)
    #: What a texture added before transparency existed was written with. A
    #: texture with a :attr:`transparency` takes its render mode from that.
    render_mode: StringProperty(default="OPAQUE")
    #: The look, one of :data:`..transparency.MODES`, or empty for a texture
    #: added before there was a choice - which keeps encoding exactly as it
    #: did. Set by the import from the picture's own alpha, and by Set
    #: Transparency.
    transparency: StringProperty(default="")
    #: Which invisible bit the export flips to tell this texture apart from
    #: another that reduced to the same pixels; 0 for none. See
    #: :func:`..textures.nudge_texels`.
    nudge: IntProperty(default=0)


class DKR_ValidationEntry(bpy.types.PropertyGroup):
    severity: StringProperty(default="info")
    message: StringProperty(default="")
    object_id: StringProperty(default="")
    #: Comma-separated document positions, so the result can select what it is
    #: about rather than leaving the author to hunt for it.
    objects: StringProperty(default="")


#: A music path is kept relative to the .blend ("//..."). Blender 4.5 warns
#: when such a path is assigned to a property that does not declare it; older
#: versions do not know the option.
_RELATIVE_PATH = ({"PATH_SUPPORTS_BLEND_RELATIVE"}
                  if bpy.app.version >= (4, 5, 0) else {"ANIMATABLE"})


def _music_changed(settings, context):
    """Re-check the chosen file whenever the source or path changes."""
    from .operators import music  # noqa: PLC0415 - registered after this module

    music.refresh_report(settings, context)


def _redraw_views(context):
    screen = getattr(context, "screen", None)
    if screen is None:
        return
    for area in screen.areas:
        if area.type == "VIEW_3D":
            area.tag_redraw()


def _redraw_update(self, context):
    """An ``update`` for settings the viewport draws.

    A named function defined above the class, never a lambda calling something
    defined below it: Blender evaluates a class's deferred annotations against
    the module's globals as they stood when the class was made, so a name
    defined later is missing when the lambda finally runs.
    """
    _redraw_views(context)


class DKR_SceneSettings(bpy.types.PropertyGroup):
    """Everything the sidebar needs to remember between clicks."""

    waterfall_object: StringProperty(name="Waterfall", default="")

    source_path: StringProperty(
        name="Source",
        description="The object map this scene was imported from",
        default="",
        subtype="FILE_PATH",
    )

    asset_root: StringProperty(
        name="Asset Tree",
        description=(
            "Extracted decomp asset version directory the artwork is read from"
        ),
        default="",
        subtype="DIR_PATH",
    )

    geometry_path: StringProperty(
        name="Geometry",
        description="The level model loaded as reference geometry",
        default="",
        subtype="FILE_PATH",
    )

    # -- the Level Type ----------------------------------------------------
    #
    # Written only by dkr.set_level_type and the importers, so a change always
    # passes through the confirmation that says what it affects. The three
    # sub-selectors are separate static enums rather than one that changes
    # meaning with the family, because Blender stores an enum by position.

    level_type: EnumProperty(
        name="Level Type",
        description=(
            "What kind of level this is. Everything in the addon follows it: "
            "what can be placed, the start grid, validation and the header"
        ),
        items=_level_items(),
        default=level_types.NONE,
        update=_reset_category,
    )
    challenge_type: EnumProperty(
        name="Challenge",
        description=(
            "The game treats these three differently (race types 64, 65, 66), "
            "and some objects only work in one of them"
        ),
        items=_sub_items(level_types.CHALLENGE),
        default=level_types.BATTLE,
        update=_reset_category,
    )
    special_type: EnumProperty(
        name="Kind",
        description="The level types the game uses for scenes, menus and testing",
        items=_sub_items(level_types.SPECIAL),
        default=level_types.CUTSCENE,
        update=_reset_category,
    )
    boss: EnumProperty(
        name="Boss",
        description=(
            "Which boss the player races. Written to the header as "
            "/boss-race-id (byte 0xB8), and the race is run in its vehicle"
        ),
        items=[
            (boss_id, text, "%s. The retail race uses the %s"
             % (boss_id, level_types.vehicle_name(vehicle).lower()), number)
            for number, (boss_id, text, vehicle) in enumerate(level_types.BOSSES)
        ],
        default="BOSS_RACE_TRICKY1",
    )
    vehicles: EnumProperty(
        name="Vehicles",
        description=(
            "The vehicles the track allows. The player picks from them, and "
            "the bots race in the player's vehicle"
        ),
        items=[(vehicle, name, "", 1 << number)
               for number, (vehicle, name) in enumerate(level_types.PLAYER_VEHICLES)],
        options={"ENUM_FLAG"},
        default={"VEHICLE_CAR"},
    )
    default_vehicle: EnumProperty(
        name="Default Vehicle",
        description="The vehicle selected when the player enters the track",
        items=[(vehicle, level_types.vehicle_name(vehicle), "", number)
               for number, (vehicle, _name) in enumerate(level_types.PLAYER_VEHICLES)],
        default="VEHICLE_CAR",
    )
    laps: IntProperty(
        name="Laps",
        description="How many laps the race has, from 1 to 9",
        default=3, min=1, max=9,
    )
    vehicle_override: EnumProperty(
        name="Vehicle Override",
        description=(
            "These are not player vehicles: the bots never use them, and no "
            "retail track lists them. For debugging only. Replaces the default "
            "vehicle chosen in Level Type"
        ),
        items=[("NONE", "None", "Use the vehicles chosen in Level Type", 0)]
        + [(vehicle, vehicle, "", number)
           for number, vehicle in enumerate(level_types.DEBUG_VEHICLES, start=1)],
        default="NONE",
    )
    show_special: BoolProperty(
        name="Special (advanced)",
        description=(
            "The level types the game uses for scenes, menus and testing. "
            "Not normal game modes"
        ),
        default=False,
    )
    show_incompatible: BoolProperty(
        name="Show Incompatible Types",
        description=(
            "Also list the types this level type does not use, marked with a "
            "warning. They can still be placed; validation warns about them"
        ),
        default=False,
        update=_reset_category,
    )

    category: EnumProperty(
        name="Category",
        description=(
            "Narrow the list. Only categories with something usable in this "
            "tab and level type are offered"
        ),
        items=category_items,
    )

    object_type: StringProperty(
        name="Object Type",
        description="The type the Place button will add",
        default="ASSET_OBJECT_GROUNDZIPPER",
    )

    track_name: StringProperty(
        name="Track Name",
        description="Shown in game and in the mod list",
        default="",
    )
    track_id: StringProperty(
        name="Track Id",
        description=(
            "Directory name and manifest id: lowercase words joined by hyphens. "
            "Left blank, it is derived from the track name"
        ),
        default="",
    )
    track_author: StringProperty(name="Author", default="")

    # -- the track's own music ---------------------------------------------
    #
    # An MP3 or WAV the runtime plays over a silent retail song (the header's
    # /music, the "carrier"), which keeps the game's fades and final-lap
    # speed-up driving it; or a MIDI file converted to a native sequence that
    # the game plays in that song's place. See music_audio, midi_import and
    # operators/music.py. A .blend saved before these existed opens on "Game
    # Music", which is what it had.

    music_source: EnumProperty(
        name="Music Source",
        items=[
            ("GAME", "Game Music",
             "One of the game's own songs, chosen below"),
            ("FILE", "Music File",
             "Your own MP3 or WAV, played over the game song chosen below, "
             "which stays silent and only drives fades and the final-lap "
             "speed-up"),
            ("MIDI", "MIDI File",
             "Your own .mid, converted to the game's music format and played "
             "by DKR itself, on its own instruments, in place of the game "
             "song chosen below"),
        ],
        default="GAME",
        update=_music_changed,
    )
    music_file: StringProperty(
        name="Music File",
        description="The MP3 or WAV the track ships. Kept relative to the "
                    ".blend when it is saved",
        default="",
        subtype="FILE_PATH",
        options=_RELATIVE_PATH,
        update=_music_changed,
    )
    music_midi: StringProperty(
        name="MIDI File",
        description="The .mid the track's music is converted from. Kept "
                    "relative to the .blend when it is saved",
        default="",
        subtype="FILE_PATH",
        options=_RELATIVE_PATH,
        update=_music_changed,
    )
    music_volume: IntProperty(
        name="Volume",
        description="Level of the music in the game's mix. 100% plays a file "
                    "as recorded, and a MIDI song at a retail song's level "
                    "(up to 115%); the game's music slider and fades still apply",
        default=100, min=0, max=200, subtype="PERCENTAGE",
    )
    music_loop_start: FloatProperty(
        name="Loop Start",
        description="Where the music jumps back to after reaching Loop End. "
                    "Anything before it plays once, as an intro",
        default=0.0, min=0.0, precision=3, unit="TIME_ABSOLUTE",
    )
    music_loop_end: FloatProperty(
        name="Loop End",
        description="Where the music jumps back to Loop Start. 0 loops at the "
                    "end of the file",
        default=0.0, min=0.0, precision=3, unit="TIME_ABSOLUTE",
    )
    music_final_lap: EnumProperty(
        name="Final Lap",
        description="What the music does when the game speeds its song up on "
                    "the last lap",
        items=[
            ("speedup", "Speed Up",
             "Play faster on the final lap, pitch rising, as the game's own "
             "songs do"),
            ("constant", "Keep Speed",
             "Keep playing at the recorded speed and pitch"),
        ],
        default="speedup",
    )
    #: What the last check of music_file found - a summary, or the reason it
    #: cannot be used. Written by the operators so a redraw never re-reads a
    #: large file; the export checks the file again regardless.
    music_report: StringProperty(default="")
    music_ok: BoolProperty(default=False)
    #: A MIDI conversion's notes for the author, one per line.
    music_warnings: StringProperty(default="")

    slot: EnumProperty(
        name="Object Map",
        description=(
            "Which of the level's two object maps a newly placed object joins, "
            "and which types the Place list shows. The game loads both maps "
            "the same way, so the split is kept per object rather than inferred"
        ),
        items=[
            ("structure", "Structure",
             "The track itself: start positions, checkpoints, zippers, doors, "
             "scenery. What you place from this tab goes into the structure "
             "object map"),
            ("collectables", "Collectables",
             "Pickups: bananas, coins, weapon balloons, keys. What you place "
             "from this tab goes into the collectables object map"),
        ],
        default="structure",
        update=_reset_category,
    )

    show_raw: BoolProperty(
        name="Show Raw Bytes",
        description=(
            "Show the pad and unk fields. They exist so an entry encodes to the "
            "bytes the game expects, and nobody has identified what the unk ones "
            "do - a checkpoint has 15 of them around the 4 that matter"
        ),
        default=False,
    )

    # -- the race AI overlay ---------------------------------------------

    show_ai_lines: BoolProperty(
        name="Show Bot Lines",
        description=(
            "Draw the four lanes the computer racers drive: the game's own "
            "spline through the checkpoints, moved by each lane's offsets. It "
            "follows a checkpoint while you drag it"
        ),
        default=False,
        update=_redraw_update,
    )

    ai_lines_on_top: BoolProperty(
        name="Draw On Top",
        description=(
            "Draw the lanes through the track instead of behind it. The lanes "
            "run at checkpoint height, which is usually at or under the road"
        ),
        default=True,
        update=_redraw_update,
    )

    ai_line_vehicle: EnumProperty(
        name="Vehicle",
        description=(
            "Whose line to show. Each vehicle's racers load their own set of "
            "checkpoints, chosen in AI Racers > Checkpoint Sets"
        ),
        items=[(vehicle, label, "The line %s racers follow" % label.lower())
               for vehicle, label in level_types.PLAYER_VEHICLES],
        default="VEHICLE_CAR",
        update=_redraw_update,
    )

    # -- the minimap -------------------------------------------------------
    #
    # How the picture looks. Where the dots go is never a setting: the numbers
    # follow from the edges and these, so nothing here can put them off the road.

    minimap_size: IntProperty(
        name="Size",
        description=(
            "The picture's longest side. Retail's run from 60 to 80 pixels. "
            "It is loaded into 4KB of texture memory, so a square one stops at "
            "64 and the width is rounded to a multiple of 8"
        ),
        default=64, min=48, max=80, subtype="PIXEL",
        update=_redraw_update,
    )
    minimap_rotation: EnumProperty(
        name="Rotation",
        description="Which way up the map is drawn. The game never turns it with "
                    "the player",
        items=[
            ("AUTO", "Auto", "Stand the road upright in the smallest picture, "
                             "as retail turns its long tracks"),
            ("0", "0°", "North up, as the top view shows the track"),
            ("90", "90°", "A quarter turn"),
            ("180", "180°", "Upside down"),
            ("270", "270°", "Three quarters of a turn"),
        ],
        default="AUTO",
        update=_redraw_update,
    )
    minimap_colour: FloatVectorProperty(
        name="Colour",
        description=(
            "The tint the game multiplies the picture by. Most tracks are "
            "white; Snowflake Mountain's are red and Crescent Island's teal. "
            "Three-player split screen draws it white whatever this is"
        ),
        subtype="COLOR_GAMMA", size=3, min=0.0, max=1.0,
        default=(1.0, 1.0, 1.0),
        update=_redraw_update,
    )
    minimap_soft: IntProperty(
        name="Soft Edge",
        description="0 is a hard edge, 1 retail's soft one, 2 softer still",
        default=1, min=0, max=2,
        update=_redraw_update,
    )
    minimap_flag: BoolProperty(
        name="Checkered Finish Line",
        description="Draw the 5x5 checkerboard retail maps put just behind the "
                    "start grid",
        default=True,
        update=_redraw_update,
    )
    show_minimap_overlay: BoolProperty(
        name="Show On Track",
        description=(
            "Lay the minimap over the track in the viewport, where the game's "
            "dots would put it, so an edge that wanders off the road shows"
        ),
        default=False,
        update=_redraw_update,
    )
    minimap_png: StringProperty(
        name="Own Picture",
        description="A retouched PNG used instead of the picture the edges "
                    "make. It has to be the same size",
        default="", subtype="FILE_PATH",
    )

    # -- the texture browser ---------------------------------------------
    #
    # A track can draw with any texture the ROM holds, not only the ones its
    # base model shipped with, so what is remembered here is a choice out of
    # 1401 rather than out of a table.

    texture_id: IntProperty(
        name="Texture",
        description=(
            "Index into the ROM's 3D texture list - what a level model's "
            "texture table actually stores. -1 means nothing is chosen"
        ),
        default=-1,
    )

    texture_query: StringProperty(
        name="Search",
        description=(
            "Narrow the textures by name. Every word has to appear, so "
            "\"ice wall\" finds the icy walls and not every wall"
        ),
        default="",
        options={"TEXTEDIT_UPDATE"},
    )

    texture_group: EnumProperty(
        name="Set",
        description="Which of the extraction's texture folders to browse",
        items=texture_group_items,
    )

    texture_surface: EnumProperty(
        name="Surface",
        description=(
            "What the ground made of this texture behaves like. It is stored on "
            "the texture table entry, so applying the same picture with two "
            "surface types makes two entries - which is how the game gets one "
            "image that is road in one place and grass in another"
        ),
        items=texture_surface_items,
    )

    texture_mapping: EnumProperty(
        name="Mapping",
        description="What to do with the UVs of the faces being retextured",
        items=[
            ("KEEP", "Keep The Mapping",
             "Leave the picture covering the same ground as the one it "
             "replaces, rescaled for the new texture's size. A face that had no "
             "texture has no mapping to keep, so project those instead"),
            ("PROJECT", "Project Flat",
             "Plant the texture on the world along whichever axis each face "
             "most faces. This is what new geometry needs: a face extruded out "
             "of the track inherits UVs that are well-formed and mean nothing"),
        ],
        default="KEEP",
    )

    texture_transparency: EnumProperty(
        name="Transparency",
        description=(
            "How the picture's alpha is used on the faces it is applied to. "
            "The game decides from the texture's render mode and the faces' "
            "cut-out flag, and draws see-through faces in a second pass - the "
            "addon keeps both in step"
        ),
        items=transparency_items(auto=True),
        default="AUTO",
    )

    texture_opacity: FloatProperty(
        name="Opacity",
        description=(
            "How see-through the faces are drawn, whatever texture goes on "
            "them - the ROM's included. Faces already drawing the picked "
            "texture fade as it moves, and Apply carries it onto new ones. "
            "Below 100% the game fades them by vertex alpha, which replaces "
            "their baked light with a flat grey and turns their fog off"
        ),
        min=0.0, max=1.0, default=1.0, subtype="FACTOR",
        update=_texture_opacity_changed,
    )

    texture_scale: FloatProperty(
        name="Units Per Repeat",
        description=(
            "How much ground one repeat of the texture covers when projecting. "
            "Retail's median is 268 map units, measured over 257,035 textured "
            "triangle edges"
        ),
        default=256.0,
        min=1.0,
        soft_max=2048.0,
    )

    # -- the track's own artwork -----------------------------------------
    #
    # Position in this collection is the texture's identity: the runtime hands
    # out ids by position within a track's manifest, and the level model refers
    # to them by the same position. Reordering it repaints the track, which is
    # why nothing here offers to sort or move an entry.

    custom_textures: CollectionProperty(type=DKR_CustomTexture)

    custom_format: EnumProperty(
        name="Format",
        description=(
            "How the image is stored. The choice sets the largest size it can "
            "be: the RDP has 4KB of texture memory and a level texture is "
            "loaded into it as one block"
        ),
        items=texture_format_items,
    )

    custom_size: StringProperty(
        name="Size",
        description=(
            "Width and height to resample to, as WxH. Both have to be powers "
            "of two no larger than 64 - material_init clamps anything else "
            "instead of tiling it. Blank picks the largest the format allows"
        ),
        default="",
    )

    show_wave_details: BoolProperty(
        name="Wave Shape",
        description="Show the two sine waves and the pattern the simulation "
                    "adds up",
        default=False,
    )

    has_validated: BoolProperty(default=False)
    results: CollectionProperty(type=DKR_ValidationEntry)


# ---------------------------------------------------------------------------
# The race AI's header bytes
# ---------------------------------------------------------------------------

def _answer_key(pointer):
    # Imported here, not at module scope: this module is registered first.
    from .operators import header as header_ops  # noqa: PLC0415
    return header_ops.key_for(pointer)


def _bridge(pointer, name, description, maximum, soft_max=None):
    """An int property whose value *is* the header answer for ``pointer``.

    The answer stays a scene custom property, where the header form keeps all
    of its answers, so the export, the inherited-header overlay and an import
    see one source of truth. This only gives it a widget with a range and a
    tooltip. Unanswered, it reads the surveyed default and writes nothing.
    """
    def get(self):
        value = self.id_data.get(_answer_key(pointer))
        if value is None:
            value = race_ai.header_default(pointer)
        try:
            return int(value)
        except (TypeError, ValueError):
            return 0

    def set(self, value):
        self.id_data[_answer_key(pointer)] = int(value)

    options = {"name": name, "description": description, "min": 0,
               "max": maximum, "get": get, "set": set}
    if soft_max is not None:
        options["soft_max"] = soft_max
    return IntProperty(**options)


_SKILL_HELP = (
    "0 Master always gets the start boost, 1 Expert gets it when the player "
    "does, 2 Hard goes on the beep, 3 Medium and 4 Easy are 4 and 8 frames "
    "late. Nothing after the start depends on it"
)


def _race_ai_properties():
    found = {}
    for adventure, adventure_label in race_ai.ADVENTURES:
        for number, (slot, label, help_text) in enumerate(race_ai.AI_LEVEL_SLOTS):
            found["%s_%d" % (adventure, number)] = _bridge(
                race_ai.ai_level_pointer(adventure, slot),
                "%s, %s" % (adventure_label, label),
                "%s. Behaviour table 0 (easiest) to 9 (the one the Ultimate AI "
                "cheat forces): target speed of the slowest and the leading "
                "bot, %s" % (help_text, race_ai.describe_levels()),
                race_ai.BEHAVIOUR_LEVELS - 1,
            )
    for index, character in enumerate(race_ai.CHARACTERS):
        found["skill_%d" % index] = _bridge(
            race_ai.skill_pointer(index), character,
            "%s's start skill in single-player races, boss races and Taj's "
            "challenges; with two or more players each bot draws Master to "
            "Hard instead. %s" % (character, _SKILL_HELP),
            255, soft_max=5,
        )
        found["trophy_%d" % index] = _bridge(
            race_ai.skill_pointer(index, True), character,
            "%s's start skill when this race is part of a trophy race. %s"
            % (character, _SKILL_HELP),
            255, soft_max=5,
        )
    for index, (_vehicle, label) in enumerate(level_types.PLAYER_VEHICLES):
        found["set_%d" % index] = _bridge(
            race_ai.vehicle_set_pointer(index), label,
            "Which checkpoints a %s's racers load: the ones whose vehicleType "
            "equals this. Retail gives the plane its own route this way"
            % label.lower(),
            255, soft_max=7,
        )
    return found


class DKR_RaceAiSettings(bpy.types.PropertyGroup):
    """The race AI's header bytes as properties the AI Racers panel can draw.

    Built from :mod:`race_ai`'s descriptors rather than written out, so the
    slots, the characters and their order cannot drift from the bytes.
    """


DKR_RaceAiSettings.__annotations__ = _race_ai_properties()


# ---------------------------------------------------------------------------
# The waves' header bytes
# ---------------------------------------------------------------------------

def _wave_default(pointer):
    value = race_ai.header_default(pointer)
    return 0 if value is None else value


def _wave_bridge(field):
    """A property whose value *is* the header answer for one wave byte.

    The same arrangement as the race AI's: the answer is a scene custom
    property, where the export and the inherited-header overlay read it, and
    this only gives it a widget. A toggle is stored as the 0 or 1 the byte is.
    """
    pointer = field.pointer

    def read(self):
        value = self.id_data.get(_answer_key(pointer))
        if value is None:
            value = _wave_default(pointer)
        try:
            return int(value)
        except (TypeError, ValueError):
            return 0

    if field.toggle:
        def get(self):
            return bool(read(self))

        def set(self, value):
            self.id_data[_answer_key(pointer)] = 1 if value else 0

        return BoolProperty(name=field.label, description=field.help,
                            get=get, set=set)

    def set_int(self, value):
        if pointer == "/waves/seed-size":
            value = max(2, int(value) & ~1)
        self.id_data[_answer_key(pointer)] = int(value)

    return IntProperty(name=field.label, description=field.help,
                       min=field.minimum, max=field.maximum,
                       get=read, set=set_int)


class DKR_WaterSettings(bpy.types.PropertyGroup):
    """The header's wave bytes as properties the Water panel can draw.

    Built from :data:`..water.HEADER_FIELDS`, so the panel cannot name a byte
    the header does not have.
    """


DKR_WaterSettings.__annotations__ = {
    field.name: _wave_bridge(field) for field in water.HEADER_FIELDS
}


CLASSES = (
    DKR_CustomTexture,
    DKR_ValidationEntry,
    DKR_SceneSettings,
    DKR_RaceAiSettings,
    DKR_WaterSettings,
)


def _opacity_changed(material, _context):
    from .operators import geometry  # noqa: PLC0415
    geometry.show_opacity(material)


def register_pointers():
    from .operators.waterfall import DKR_ScrollSettings
    bpy.types.Object.dkr_scroll = PointerProperty(type=DKR_ScrollSettings)
    bpy.types.Material.dkr_opacity = FloatProperty(
        name="Opacity",
        description=(
            "How see-through every face drawing this material is, whatever "
            "its texture's alpha says - works on the ROM's textures too. Below "
            "100% the game fades the faces by vertex alpha, which replaces "
            "their baked light with a flat grey and turns their fog off"),
        min=0.0, max=1.0, default=1.0, subtype="FACTOR",
        update=_opacity_changed,
    )
    bpy.types.Scene.dkr = PointerProperty(type=DKR_SceneSettings)
    bpy.types.Scene.dkr_ai = PointerProperty(type=DKR_RaceAiSettings)
    bpy.types.Scene.dkr_water = PointerProperty(type=DKR_WaterSettings)


def unregister_pointers():
    if hasattr(bpy.types.Object, "dkr_scroll"):
        del bpy.types.Object.dkr_scroll
    if hasattr(bpy.types.Material, "dkr_opacity"):
        del bpy.types.Material.dkr_opacity
    for name in ("dkr_water", "dkr_ai", "dkr"):
        if hasattr(bpy.types.Scene, name):
            delattr(bpy.types.Scene, name)
