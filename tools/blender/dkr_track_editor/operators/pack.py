"""Write the scene out as a ``.dkrmap`` track package."""

from __future__ import annotations

import json
import os
import tempfile
import traceback

import bpy
from bpy.props import BoolProperty, StringProperty
from bpy_extras.io_utils import ExportHelper

from .. import (catalog as catalog_module, dkrmap, level_model, level_types,
                minimap, prefs, scene, validate, water)
from . import geometry_export
from . import minimap as minimap_ops


class DKR_OT_export_dkrmap(bpy.types.Operator, ExportHelper):
    """Write a .dkrmap track directory for DKR-R's custom-tracks folder"""

    bl_idname = "dkr.export_dkrmap"
    bl_label = "Export .dkrmap"
    bl_options = {"REGISTER"}

    filename_ext = ""
    use_filter_folder = True
    filter_glob: StringProperty(default="", options={"HIDDEN"})

    validate_first: BoolProperty(
        name="Validate First",
        description="Refuse to write the package if validation finds an error",
        default=True,
    )

    def execute(self, context):
        settings = context.scene.dkr
        try:
            catalog = catalog_module.load()
            object_map = scene.export_object_map(context, catalog)
        except Exception as error:  # noqa: BLE001
            traceback.print_exc()
            self.report({"ERROR"}, "could not build the object map: %s" % error)
            return {"CANCELLED"}

        if not object_map.objects:
            self.report({"ERROR"}, "nothing to export; no DKR objects in the scene")
            return {"CANCELLED"}

        # The header's race type comes from here, and zero is a real race type
        # rather than an absence - a track that never chose would quietly
        # become a default race.
        key = level_types.current_key(settings)
        if key is None:
            self.report({"ERROR"}, "choose the level type first, at the top of "
                        "the DKR sidebar; the header's race type comes from it")
            return {"CANCELLED"}

        if self.validate_first:
            report = validate.validate(object_map, catalog, level_key=key)
            from . import waterfall
            report = validate.Report(list(report) + waterfall.issues(context))
            if report.errors:
                self.report(
                    {"ERROR"},
                    "%d validation error(s); fix them or turn off Validate First. "
                    "First: %s" % (len(report.errors), report.errors[0].message),
                )
                return {"CANCELLED"}

        directory = self.filepath
        if not directory.endswith(dkrmap.SUFFIX):
            directory = directory.rstrip("/\\") + dkrmap.SUFFIX

        # The id keys a track for arming and for sibling resolution, so two
        # tracks sharing one collide. Take the author's if they set one, then
        # the folder being exported to - the most direct statement of intent at
        # this moment - and only then the track name, which an import sets and
        # which therefore persists across exports without the author noticing.
        folder = os.path.splitext(os.path.basename(directory))[0]
        track_id = (
            settings.track_id
            or dkrmap.normalise_id(folder)
            or dkrmap.normalise_id(settings.track_name)
        )

        try:
            tree = prefs.resolve(context)
            package = dkrmap.TrackPackage(
                directory=directory,
                track_id=track_id,
                name=settings.track_name or track_id,
                author=settings.track_author,
                revision=tree.label if tree else "",
            )
            package.notes.append(
                "%d objects, roughly %d bytes encoded"
                % (len(object_map.objects),
                   validate.estimate_size(object_map, catalog))
            )
            # A level has two object maps and the runtime patches a different
            # header field from each, so they are written separately - a single
            # merged payload would spawn the whole track through the
            # collectables slot while the retail structure map kept spawning
            # beside it.
            table = tree.translation_table() if tree else []
            # Both slots are always written, even when one is empty. A header
            # leaves 0x36 and 0xBA at zero for the runtime to patch, and zero is
            # a valid index rather than "no map" - the game clamps out-of-range
            # ids to 0 and loads object map 0. So a header shipped without a
            # payload for a slot points that slot at another level's objects and
            # hangs. An empty map is the honest way to say "nothing here", and
            # it is what retail does: 16 of its object maps have fileSize 0.
            written = []
            for slot in scene.SLOTS:
                part = scene.export_object_map(context, catalog, slot=slot)
                package.write_object_map(part, "objects_" + slot)
                if not table:
                    continue
                payload = package.encode_object_map(
                    slot, part, catalog, table, tree.asset_index
                )
                written.append("%s %d objects, %d bytes"
                               % (slot, len(part.objects), len(payload)))

            if table:
                package.notes += written
            else:
                package.notes.append(
                    "object maps not compiled: no decomp assets configured, so "
                    "the level-object translation table was unavailable"
                )

            # A remix keeps the base geometry and overlays the author's
            # settings. Its world defaults to the Custom Tracks category.
            from .. import level_header_template as template  # noqa: PLC0415
            from . import header as header_ops  # noqa: PLC0415
            base = _base_header(context, tree)
            if base is not None:
                template.apply_overrides(base, level_types.settings_overrides(settings))
                template.apply_overrides(base, header_ops.inherited_overrides(context))
            else:
                base = _authored_header(context)
            if base is not None and geometry_has_waves(context):
                _check_wave_header(self, base, tree)
            # Before any payload is compiled: a music file that cannot ship
            # stops the export while nothing has been written yet.
            _attach_music(self, context, package, base)

            # Before the geometry, because the model's texture table names
            # these by position and a failure to compile one has to stop the
            # export rather than ship a model pointing at a payload that is
            # not there.
            own, texture_payloads = _encode_textures(self, context, package)
            edit = geometry_export.build_edited_model(context)
            # Before the header, whose "no minimap" bit follows from it, and
            # from the model the package will actually ship: the numbers are
            # fitted to its bounds.
            chart = _plan_minimap(self, context, package, edit)
            if base is not None:
                template.apply_overrides(base, {
                    minimap.NO_MINIMAP_POINTER: minimap_ops.header_value(
                        template.lookup(base, minimap.NO_MINIMAP_POINTER),
                        chart.state),
                })
                _require_header_asset_index(base, tree)
                header = package.encode_header(
                    base, catalog.raw.get("enumValues", {}),
                    tree.asset_index if tree else None,
                )
                package.notes.append("header.bin %d bytes" % len(header))
            _encode_geometry(self, context, package, edit, chart)
            _encode_minimap(self, package, chart)
            _warn_header_without_geometry(self, context, package)

            stale = _attach_existing_payloads(package)
            if stale:
                # The addon writes the object maps itself, so one it did not
                # write this time is one the scene has already moved past.
                # Shipping it is still safer than dropping it - a header whose
                # slot has no payload points at another level's objects and
                # hangs - but it must not pass for success.
                message = (
                    "the object maps could not be compiled this time, so the "
                    "package ships %s from an earlier export. The objects now "
                    "in the scene reached source/ only. Set the decomp asset "
                    "path in the addon preferences and export again"
                    % ", ".join(sorted(stale))
                )
                package.notes.append(message)
                self.report({"WARNING"}, message)
            # Last before the manifest, which records the pack's digest, and
            # after everything that can still refuse the export - a pack left
            # beside a package that was never written would belong to nothing.
            hd_pack = _write_hd_pack(self, context, package, own,
                                     texture_payloads, chart)
            package.write()
        except geometry_export.GeometryExportError as error:
            self.report(
                {"ERROR"},
                "the track geometry cannot be exported: %s" % error,
            )
            return {"CANCELLED"}
        except dkrmap.DkrMapError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        except OSError as error:
            self.report({"ERROR"}, "could not write %s: %s" % (directory, error))
            return {"CANCELLED"}

        if not package.manifest()["adds"]:
            self.report(
                {"ERROR"},
                "nothing was compiled, so the package would contain no payloads "
                "at all. Set the decomp asset path in the addon preferences and "
                "export again",
            )
            return {"CANCELLED"}

        # The track and its pack are two files installed through two doors, so
        # the one line that says the export worked names both.
        pack_note = (
            "; and %s - import it in DKR-R under Graphics > Custom Texture Packs"
            % hd_pack if hd_pack else ""
        )
        missing = package.missing_sections()
        if missing:
            self.report(
                {"WARNING"},
                "wrote %s with %d object(s). Still missing %s, which the "
                "addon does not write yet - see HOW-TO-BUILD.md%s"
                % (os.path.basename(directory), len(object_map.objects),
                   ", ".join(missing), pack_note),
            )
        else:
            self.report(
                {"INFO"},
                "wrote %s: %d objects, ready to install in custom-tracks/%s"
                % (os.path.basename(directory), len(object_map.objects),
                   pack_note),
            )
        return {"FINISHED"}


def _encode_textures(operator, context, package):
    """Compile the artwork the track ships itself into the package.

    Every texture the scene holds is written, not only the ones the geometry
    draws. An unused one costs a few kilobytes and keeps the numbering the
    author sees in the panel identical to the numbering the runtime hands out -
    and dropping the unused ones would renumber the used ones, which is the one
    thing that silently repaints a track.

    Returns ``(entries, payloads)`` in ordinal order, both empty for a track
    with none, for :func:`_write_hd_pack` to name its replacements after.
    """
    from .. import textures as texture_module  # noqa: PLC0415
    from . import custom_textures, geometry as geometry_ops  # noqa: PLC0415

    own = custom_textures.exported(context)

    # A table entry naming a texture the package will not carry is the one
    # failure here that the game cannot survive: the id falls outside the
    # extended table and load_texture reads whatever is past the end of it.
    # It happens if the scene is opened without the images, so it is checked
    # against what is about to be written rather than assumed away - in the
    # base table as well as the additions, because a track converted from a
    # textured mesh keeps its own textures in the base model it wrote.
    dangling = set()
    for obj in geometry_ops.geometry_objects(context):
        for record in geometry_ops.texture_table(obj):
            ordinal = texture_module.custom_ordinal(record.get("id", 0))
            if ordinal is not None and ordinal >= len(own):
                dangling.add(ordinal + 1)
    if dangling:
        raise dkrmap.DkrMapError(
            "the geometry draws with texture %s of this track's own, but the "
            "scene holds %d. Add the missing image, or point those faces at "
            "something else"
            % (", ".join(str(number) for number in sorted(dangling)), len(own))
        )

    if not own:
        return [], []

    # A scene moved without the folder beside it still has the pictures its
    # textures were made from, packed in the .blend, so it rebuilds them.
    restored, lost = custom_textures.restore_missing(context)
    if restored:
        own = custom_textures.exported(context)
        geometry_ops.show_own_pictures(context)
        package.notes.append(
            "%d texture(s) of this track's own were missing beside the .blend "
            "and were rebuilt from the pictures they were made from: %s"
            % (len(restored), ", ".join(restored))
        )
        for level, message in custom_textures.restoration_reports(
                context, restored, []):
            operator.report(level, message)

    missing = [entry.name for entry in own
               if not entry.png or not os.path.isfile(entry.png)]
    if missing:
        reasons = dict(lost)
        raise dkrmap.DkrMapError(
            "the resampled image for %s is gone from %s and could not be "
            "rebuilt (%s). The .blend records where it was, not the picture "
            "itself: put the folder back beside the .blend, or add it again"
            % (", ".join(missing), custom_textures.folder(context),
               "; ".join(reasons.get(name, "no reason given")
                         for name in missing[:4]))
        )

    separated = _separate_identical(context, own)
    if separated:
        own = custom_textures.exported(context)
        package.notes.append(
            "%s reduced to the same pixels as another of this track's textures "
            "with a different original, so one invisible bit was flipped to "
            "give each a high-resolution replacement of its own"
            % ", ".join(separated)
        )

    payloads = package.encode_textures(own)
    package.notes.append(
        "%d texture(s) of this track's own, %d bytes: %s"
        % (len(payloads), sum(len(payload) for payload in payloads),
           ", ".join("%s %dx%d" % (entry.name, entry.width, entry.height)
                     for entry in own))
    )
    return own, payloads


def _separate_identical(context, own) -> list:
    """Give every texture with its own original its own replacement name.

    A replacement is named by a hash of the texels, so two textures that reduce
    to the same 64x32 share one - and if their originals differ, only one of
    those could ever be drawn. Rather than tell the author about a problem they
    cannot see, the second is nudged: one bit of blue flipped, which nobody can
    see at that size (:func:`..textures.nudge_texels`). The nudge is kept on the
    texture, so every export after this one writes the same bytes.

    Returns the names of the textures nudged.
    """
    import hashlib  # noqa: PLC0415

    from .. import rice_identity, textures as texture_module  # noqa: PLC0415
    from . import custom_textures  # noqa: PLC0415

    def original_digest(ordinal):
        path = custom_textures.original_for(context, ordinal)
        if not path or not os.path.isfile(path):
            return None
        with open(path, "rb") as handle:
            return hashlib.sha256(handle.read()).hexdigest()

    current = {}
    for entry in own:
        try:
            current[entry.ordinal] = rice_identity.rice_identity(
                entry.texels(), entry.width, entry.height, entry.format
            )
        except (rice_identity.IdentityError, texture_module.TextureEncodeError):
            continue

    taken = set(current.values())
    holders = {}
    moved = []
    settings = context.scene.dkr
    for entry in own:
        identity = current.get(entry.ordinal)
        if identity is None:
            continue
        digest = original_digest(entry.ordinal)
        holder = holders.get(identity)
        if holder is not None and holder and digest and holder != digest:
            nudge, identity = rice_identity.free_nudge(
                entry.texels(nudge=0), entry.width, entry.height, entry.format,
                taken,
            )
            settings.custom_textures[entry.ordinal].nudge = nudge
            taken.add(identity)
            moved.append(entry.name)
        if not holders.get(identity):
            holders[identity] = digest
    return moved


def _write_hd_pack(operator, context, package, own, payloads, chart=None) -> str:
    """Write the high-resolution texture pack beside the package.

    One replacement per texture of the track's own that can have one, named by
    the identity RT64 will compute for the payload just written - computed from
    those very bytes, so the pack and the package cannot disagree about what
    the texture is. The track's own minimap goes in too, drawn again at
    :data:`minimap.HD_SCALE`. Returns ``"<file> with N HD texture(s)"`` for the
    export's report, or ``""`` if no pack was written.

    Nothing here can fail the export. A track is whole without its pack, so a
    texture with no original, one too large for the importer, or a pack that
    cannot be written is a warning and the track ships regardless.
    """
    from .. import rice_identity, rice_pack, textures as texture_module  # noqa: PLC0415
    from . import custom_textures  # noqa: PLC0415

    target = dkrmap.hd_pack_path(package.directory)
    chosen = []
    left_out = []
    for entry, payload in zip(own, payloads):
        start = texture_module.TEXTURE_HEADER_SIZE
        texels = payload[start:start + texture_module.texel_bytes(
            entry.width, entry.height, entry.format)]
        try:
            identity = rice_identity.rice_identity(
                texels, entry.width, entry.height, entry.format
            )
        except rice_identity.IdentityError as error:
            left_out.append((entry.name, str(error)))
            continue
        original = custom_textures.original_for(context, entry.ordinal)
        problem = (rice_pack.image_problem(original) if original else
                   "no full-resolution copy was kept, and the file it was "
                   "added from is not where it was")
        if problem:
            left_out.append((entry.name, problem))
            continue
        chosen.append((identity, _hd_picture(operator, entry, original), entry))

    for name, reason in left_out[:3]:
        operator.report({"WARNING"},
                        "no high-resolution version of %s: %s" % (name, reason))
    if len(left_out) > 3:
        operator.report({"WARNING"},
                        "and %d more texture(s) have none" % (len(left_out) - 3))

    map_entry = _hd_minimap(operator, package, chart,
                            {identity for identity, _o, _e in chosen})
    try:
        return _pack_entries(operator, package, target, chosen, payloads,
                             chart, map_entry)
    finally:
        if map_entry is not None:
            try:
                os.remove(map_entry[1])
            except OSError:
                pass


def _hd_minimap(operator, package, chart, taken):
    """``(identity, png path, (width, height))`` for the minimap's HD copy, or
    ``None`` - with the reason in the notes, since a track that ships no
    minimap of its own has nothing to replace."""
    if chart is None or not chart.own or chart.texture is None:
        return None
    if chart.built.from_png:
        package.notes.append("minimap: no HD copy - the texture is your own PNG, "
                             "which has no larger version")
        return None
    try:
        identity = minimap.texture_identity(chart.texture)
    except minimap.MinimapError as error:
        operator.report({"WARNING"},
                        "no high-resolution minimap: %s" % error)
        return None
    if identity in taken:
        operator.report({"WARNING"},
                        "no high-resolution minimap: it would have the same "
                        "name as one of the track's textures (%s)" % identity)
        return None
    try:
        width, height, rgba = chart.built.hd_picture()
        handle, path = tempfile.mkstemp(prefix="dkr-hd-minimap-", suffix=".png",
                                        dir=bpy.app.tempdir or None)
        os.close(handle)
        minimap.write_png(path, width, height, rgba)
    except Exception as error:  # noqa: BLE001 - HD is never worth failing over
        traceback.print_exc()
        operator.report({"WARNING"},
                        "no high-resolution minimap: %s" % error)
        return None
    return identity, path, (width, height)


def _pack_entries(operator, package, target, chosen, payloads, chart,
                  map_entry) -> str:
    from .. import rice_pack  # noqa: PLC0415

    if not chosen and map_entry is None:
        if os.path.isfile(target):
            operator.report(
                {"WARNING"},
                "%s is from an earlier export and this one has nothing to put "
                "in it, so it no longer matches the track. Delete it, and "
                "remove it from DKR-R's texture packs" % os.path.basename(target),
            )
        return ""

    # The minimap's payload joins the digest only when its picture is in the
    # pack, so a pack without one keeps the digest it always had.
    digest = rice_pack.texture_digest(
        list(payloads) + ([chart.texture] if map_entry is not None else []))
    stamp = {
        "track": package.track_id,
        "textureDigest": digest,
        "textures": [
            {"ordinal": entry.ordinal, "name": entry.name, "identity": identity}
            for identity, _original, entry in chosen
        ],
    }
    entries = [(identity, original) for identity, original, _e in chosen]
    if map_entry is not None:
        stamp["minimap"] = {"identity": map_entry[0],
                            "size": list(map_entry[2])}
        entries.append(map_entry[:2])
    try:
        written = rice_pack.write_pack(target, entries, stamp)
    except (rice_pack.PackError, OSError) as error:
        operator.report({"WARNING"},
                        "the high-resolution texture pack was not written: %s"
                        % error)
        return ""

    textures = written["count"] - (1 if map_entry is not None else 0)
    package.hd_pack = {
        "file": os.path.basename(target),
        "textureDigest": digest,
        "textures": textures,
        "minimap": map_entry is not None,
    }
    megabytes = written["bytes"] / (1024.0 * 1024.0)
    package.notes.append("%s %.1f MB: %d full-resolution original(s)%s"
                         % (os.path.basename(target), megabytes, textures,
                            " and the minimap at %dx%d" % map_entry[2]
                            if map_entry is not None else ""))
    if written["bytes"] > rice_pack.LARGE_PACK_BYTES:
        operator.report(
            {"WARNING"},
            "%s is %.0f MB - it carries every original at the size it was "
            "made. That is allowed; smaller originals make a smaller pack"
            % (os.path.basename(target), megabytes),
        )
    return "%s with %d HD texture(s)%s" % (
        os.path.basename(target), textures,
        " and an HD minimap" if map_entry is not None else "")


#: Pictures past this many pixels are hardened but not bled: spreading colour
#: takes a few full-size float copies, and a pack of photographs would spend
#: gigabytes on it.
_BLEED_PIXELS = 8 * 1024 * 1024


def _hd_picture(operator, entry, original) -> str:
    """The file the pack carries for one texture: the original, or a cut-out of it.

    A cut-out is drawn by RT64 with the render mode the console uses,
    ``G_RM_AA_ZB_TEX_EDGE``, and RT64 turns that into a discard below an eighth
    of alpha - where the console, reading the texture the export hardened at
    half, cuts at half. A soft-edged original would draw a fatter shape in HD
    than in the 64x32 and in Blender. So a cut-out's original is hardened at
    the same half, with colour spread into its holes as the reduction's is, and
    the copy goes in the pack. Nothing else about the original changes, and a
    blended or opaque texture's goes in as it is.
    """
    from .. import transparency as looks  # noqa: PLC0415

    if entry.mode != looks.CUTOUT:
        return original
    try:
        return _harden_copy(original, entry.ordinal)
    except Exception as error:  # noqa: BLE001 - HD is never worth failing over
        traceback.print_exc()
        operator.report({"WARNING"},
                        "the HD copy of %s keeps its soft alpha: %s"
                        % (entry.name, error))
        return original


def _harden_copy(path, ordinal) -> str:
    import numpy  # noqa: PLC0415 - Blender ships it

    from .. import transparency as looks  # noqa: PLC0415

    image = bpy.data.images.load(path, check_existing=False)
    try:
        width, height = image.size
        if width <= 0 or height <= 0 or image.channels < 4:
            return path
        try:
            image.colorspace_settings.name = "Non-Color"
        except (AttributeError, TypeError):
            pass
        pixels = numpy.empty(width * height * 4, dtype=numpy.float32)
        image.pixels.foreach_get(pixels)
    finally:
        bpy.data.images.remove(image)

    pixels = pixels.reshape(height, width, 4)
    solid = pixels[:, :, 3] >= (looks.HARD_THRESHOLD - 0.5) / 255.0
    if solid.all():
        return path
    pixels[:, :, 3] = solid
    if width * height <= _BLEED_PIXELS and solid.any():
        _bleed(numpy, pixels, solid)

    target = os.path.join(bpy.app.tempdir or os.path.dirname(path),
                          "dkr-hd-cutout-%d-%s" % (ordinal, os.path.basename(path)))
    copy = bpy.data.images.new("dkr-hd-cutout", width, height, alpha=True)
    try:
        try:
            copy.colorspace_settings.name = "Non-Color"
        except (AttributeError, TypeError):
            pass
        copy.pixels.foreach_set(pixels.ravel())
        copy.file_format = "PNG"
        copy.filepath_raw = target
        copy.save()
    finally:
        bpy.data.images.remove(copy)
    return target


def _bleed(numpy, pixels, solid, passes=6):
    """Spread visible colour into the holes, as :func:`..transparency.bleed` does.

    Each pass averages the colour of the solid texels in a 3x3 neighbourhood,
    wrapping, and hands it to the holes that have any - a ring a pass.
    """
    known = solid.copy()
    for _each in range(passes):
        if known.all():
            break
        weight = known.astype(numpy.float32)
        total = numpy.zeros(pixels.shape[:2] + (3,), dtype=numpy.float32)
        count = numpy.zeros(pixels.shape[:2], dtype=numpy.float32)
        coloured = pixels[:, :, :3] * weight[:, :, None]
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx or dy:
                    total += numpy.roll(coloured, (dy, dx), axis=(0, 1))
                    count += numpy.roll(weight, (dy, dx), axis=(0, 1))
        reached = (~known) & (count > 0)
        if not reached.any():
            break
        pixels[reached, :3] = total[reached] / count[reached][:, None]
        known |= reached


class MinimapPlan:
    """What the export does about the minimap, decided once."""

    __slots__ = ("state", "built", "texture")

    def __init__(self, state, built=None):
        self.state = state
        self.built = built
        #: The texture payload the package ships, once encoded.
        self.texture = None

    @property
    def own(self) -> bool:
        return self.state.ships_own and self.built is not None


def _plan_minimap(operator, context, package, edit):
    """Decide the minimap from the model this export ships.

    Never a reason to refuse the export: a track with no edges ships without a
    minimap and says so, and one whose picture cannot be made is told why and
    ships without one too.
    """
    found = minimap_ops.shape_of_edit(edit) if edit is not None else None
    if edit is not None:
        minimap_ops.remember_shape(context, found)
    state = minimap_ops.track_state(context, found=found)
    if not state.ships_own:
        if state.kind == minimap_ops.RETAIL:
            package.notes.append("minimap: %s's own, unchanged" % state.label)
        elif state.kind == minimap_ops.CHANGED:
            message = ("the track was reshaped, so it ships without %s's "
                       "minimap - it would show the wrong road. Draw the road's "
                       "edges under Track > Minimap to give it its own"
                       % (state.label or "the base track"))
            package.notes.append("minimap: none - " + message)
            operator.report({"WARNING"}, message)
        else:
            package.notes.append("minimap: none - no road edges drawn")
        return MinimapPlan(state)

    built = minimap_ops.build(context, state)
    if built is None or built.error:
        message = ("the minimap could not be made, so the track ships without "
                   "one: %s" % (built.error if built else "no picture"))
        package.notes.append("minimap: none - " + message)
        operator.report({"WARNING"}, message)
        state.kind = minimap_ops.CHANGED
        return MinimapPlan(state)
    for index, gap in state.open:
        operator.report({"WARNING"}, "minimap edge %d is open by %d units; the "
                        "picture closes it with a straight line" % (index + 1, gap))
    if built.png_error:
        operator.report({"WARNING"}, "minimap: your PNG is not used - %s"
                        % built.png_error)
    return MinimapPlan(state, built)


def _encode_minimap(operator, package, chart):
    if not chart.own:
        package.drop_minimap()
        return
    placement = chart.built.placement
    texture = minimap.texture_payload(chart.built.rgba, placement.width,
                                      placement.height, placement.sprite_x,
                                      placement.sprite_y)
    sprite = minimap.sprite_payload(placement.anchor_x, placement.anchor_y)
    package.encode_minimap(texture, sprite)
    chart.texture = texture
    package.notes.append(
        "minimap: %dx%d, turned %d degrees, scale %.3f, offsets %d,%d "
        "(mirrored %d,%d); %d edge(s)"
        % (placement.width, placement.height, placement.rotation,
           placement.x_scale, placement.offset_x, placement.offset_y,
           placement.offset_x2, placement.offset_y2, len(chart.state.edges)))


def _encode_geometry(operator, context, package, edit, chart):
    """Compile the edited track geometry into the package, if it changed.

    A track that only reworks the objects standing on shipped geometry ships no
    model payload at all: the header's geometry field then keeps pointing at the
    base track's model, and the package stays small and stays correct. Writing
    an unchanged copy would work and would make every remix carry a hundred
    kilobytes that say nothing.

    The one exception is a minimap of the track's own: its numbers live in the
    model's header, so a track that has one ships its model even untouched.
    """
    if edit is None:
        _report_unusable_meshes(operator, context)
        if chart.own:
            _encode_base_with_minimap(operator, context, package, chart)
        return

    for note in edit.notes:
        operator.report({"WARNING"}, note)

    if chart.own:
        minimap.apply_to_model(edit.model, chart.built.placement)

    if edit.ships or chart.own:
        payload = package.encode_level_model(edit.model)
        package.notes.append(
            "model.bin %d bytes: %s" % (
                len(payload), edit.describe() if edit.ships
                else "the base geometry, carrying the minimap's numbers")
        )
        return

    package.notes.append(
        "geometry unchanged, so no model payload; the header keeps pointing at %s"
        % os.path.basename(edit.path)
    )
    # A model.bin from an earlier export is kept, like any payload sitting in
    # the directory - but an author who has since undone their edits would
    # otherwise have no way of knowing the old geometry is still shipping.
    stale = os.path.join(package.directory, dkrmap.SECTIONS["LEVEL_MODELS"])
    if os.path.isfile(stale):
        operator.report(
            {"WARNING"},
            "the geometry in the scene matches the base track, but %s already "
            "holds a model.bin from an earlier export and it is kept. Delete it "
            "if the track should ship the shipped geometry"
            % os.path.basename(package.directory),
        )


def _encode_base_with_minimap(operator, context, package, chart):
    """The base track's model, unedited but for its minimap numbers.

    For a remix with no geometry in the scene: the model is still the one the
    header names, and the minimap's numbers have nowhere else to go.
    """
    from . import level_type as level_type_ops  # noqa: PLC0415

    level, _header = level_type_ops.imported_level(context)
    if level is None or not level.model_path:
        operator.report({"WARNING"}, "the minimap needs the track's model, and "
                        "this scene names none; it ships without one")
        chart.built = None
        return
    model = level_model.load(level.model_path)
    minimap.apply_to_model(model, chart.built.placement)
    payload = package.encode_level_model(model)
    package.notes.append("model.bin %d bytes: %s's geometry, carrying the "
                         "minimap's numbers" % (len(payload), level.label))


def _report_unusable_meshes(operator, context):
    """Say why a mesh the author modelled themselves produced no geometry.

    The addon can only write geometry it decoded from a level model, because a
    vertex has to name the segment of the track it belongs to and a mesh built
    in Blender names nothing. Modelling a track from scratch needs a segmenter,
    which does not exist yet - but an author finding that out from an empty
    package and no message is the wrong way to learn it.
    """
    from . import geometry as geometry_ops

    meshes = geometry_ops.unusable_meshes(context)
    if not meshes:
        return
    names = ", ".join(sorted(o.name for o in meshes)[:4])
    if len(meshes) > 4:
        names += " and %d more" % (len(meshes) - 4)
    operator.report(
        {"WARNING"},
        "%d mesh(es) in the scene were not exported as track geometry (%s). The "
        "addon can only write geometry it imported from a level model: every "
        "vertex has to name the segment of the track it belongs to, and a mesh "
        "modelled in Blender names none. Import a track's geometry and reshape "
        "that, or wait for the segmenter" % (len(meshes), names),
    )


def _warn_header_without_geometry(operator, context, package):
    """A header authored from nothing has nothing to point at yet.

    A remix leaves ``/model`` alone because the header it inherited already
    names the base track's geometry. A track built from scratch has neither: the
    template leaves ``/model`` unset on purpose, since the runtime patches
    ``0x34`` from the ``LEVEL_MODELS`` payload - and with no payload nothing
    patches it. The package is well formed and the manifest is honest, so
    nothing here refuses; but "ready to install" would be a lie about a track
    with no ground in it.
    """
    if "LEVEL_HEADERS" not in package.payloads:
        return
    if "LEVEL_MODELS" in package.payloads:
        return
    from . import geometry as geometry_ops

    # A remix inherits a header that still names the base track's geometry, so
    # shipping no model is correct there. The test cannot be "was anything
    # imported" - a track built from a mesh sets the geometry path too, and
    # pointed at its own file, which is exactly the case that must not stay
    # quiet.
    if any(geometry_ops.PROP_AUTHORED_BASE not in o
           for o in geometry_ops.geometry_objects(context)):
        return
    if not geometry_ops.geometry_objects(context) and (
            context.scene.dkr.source_path or context.scene.dkr.geometry_path):
        return
    operator.report(
        {"WARNING"},
        "this package has a header but no geometry, so there is nothing for it "
        "to point at and the track will not load. The header leaves the "
        "geometry field for the runtime to patch from a LEVEL_MODELS payload, "
        "and there is none. Building track geometry from a Blender mesh is not "
        "supported yet - import a track's geometry and reshape it instead",
    )


def _attach_music(operator, context, package, header):
    """Ship the track's own music, or remove what an earlier export left.

    The carrier is the header's ``/music``: the game song that plays silently
    under a recording so its fades and final-lap speed-up still reach it, or
    that a converted MIDI song plays in place of. Checked here, with the file
    (and a recording's loop), so a broken song is an export error and never a
    silent fall-back to the game song in the race.
    """
    from .. import level_header_template as template  # noqa: PLC0415
    from .. import midi_import, music_sequence  # noqa: PLC0415
    from . import music  # noqa: PLC0415

    settings = context.scene.dkr
    if not music.uses_file(settings) and not music.uses_midi(settings):
        package.drop_music()
        return
    if header is None:
        raise dkrmap.DkrMapError(
            "the track's music needs a level header to bind to; this track "
            "exports none"
        )
    if music.uses_midi(settings):
        try:
            conversion = music.convert_midi(settings, context)
        except (midi_import.MidiError, music_sequence.SequenceError) as error:
            raise dkrmap.DkrMapError(
                "the track's MIDI music cannot be used: %s" % error) from error
        # Every channel the song uses must start enabled; an inherited
        # header's mask belongs to another composition.
        template.apply_overrides(header, {"/instruments": dkrmap.SEQUENCE_CHANNEL_MASK})
        package.set_sequence(
            conversion.data,
            carrier=template.lookup(header, "/music") or 0,
            tempo_bpm=conversion.bpm,
            volume=music.sequence_volume(settings),
            source_path=music.midi_path(settings),
        )
        package.notes += ["music: %s" % line for line in conversion.warnings]
        settings.music_report = music.describe(conversion)
        settings.music_warnings = "\n".join(conversion.warnings)
        settings.music_ok = True
        return
    package.set_music(
        music.file_path(settings),
        carrier=template.lookup(header, "/music") or 0,
        volume=settings.music_volume,
        loop_start=settings.music_loop_start,
        loop_end=settings.music_loop_end,
        final_lap=settings.music_final_lap,
    )
    channel_objects = music.channel_objects(context)
    if channel_objects:
        message = (
            "%d music-channel object(s) (MidiFade/MidiChSet) have no effect "
            "with a music file, which has no channels to switch; they are "
            "exported unchanged" % len(channel_objects)
        )
        package.notes.append(message)
        operator.report({"WARNING"}, message)


def _require_header_asset_index(document, tree):
    """Refuse symbolic asset references when no asset tree can resolve them.

    Level headers store integer indices, while extracted JSON uses build ids such
    as ``ASSET_OBJECT_DOME1``.  A numeric value can be written without the
    decomp, but a build id cannot safely be guessed: asset ordering is supplied
    by the extracted tree for the selected game revision.
    """
    if tree is not None:
        return

    from .. import level_header  # noqa: PLC0415
    from .. import level_header_template as template  # noqa: PLC0415

    unresolved = []
    for field in level_header.LAYOUT:
        if field.kind != "asset":
            continue
        value = template.lookup(document, field.pointer)
        if value is None:
            value = field.default
        if isinstance(value, str) and value:
            unresolved.append((field.pointer, value))

    if not unresolved:
        return

    preview = ", ".join("%s = %s" % pair for pair in unresolved[:3])
    if len(unresolved) > 3:
        preview += ", and %d more" % (len(unresolved) - 3)
    raise dkrmap.DkrMapError(
        "the level header contains asset names that must be resolved to numeric "
        "indices, but no decomp asset tree is configured (%s). Set Decomp "
        "Assets in Preferences > Add-ons > DKR Track Editor to the extracted "
        "assets/.vanilla/<region>.<version>/ folder, or clear those asset-backed "
        "header choices before exporting" % preview
    )


def _authored_header(context):
    """A header built from the template, for a track with no ancestor.

    Returns ``None`` when the author has answered nothing, which leaves the
    package exactly as it was before this existed - no header, and the warning
    that says so. A partial answer is refused because the race type has no
    default: encoding an unanswered type as zero would silently make a race.
    """
    from . import header as header_ops
    from .. import level_header_template as template

    # The Level Type always has something to say, so it cannot be what decides
    # whether the author wanted a header; their own answers are.
    if not header_ops.overrides(context):
        return None
    overrides = header_ops.effective_overrides(context)
    # The one wave byte the template cannot default, because it names an
    # asset. Without it the header says -1 and the waves load texture 0xFFFF.
    if geometry_has_waves(context) and water.DETAIL_POINTER not in overrides:
        detail = template.asset_defaults().get(water.DETAIL_POINTER)
        if detail:
            overrides[water.DETAIL_POINTER] = detail

    outstanding = template.missing(overrides)
    if outstanding:
        raise dkrmap.DkrMapError(
            "the level header has %d unanswered field(s) - %s - and they have "
            "no default because retail has no dominant value for them. Zero is "
            "a real setting rather than an absence, so a track shipped without "
            "them becomes something other than what you built. Fill them in "
            "under Level Header"
            % (len(outstanding), ", ".join(outstanding))
        )
    return template.document(overrides)


def geometry_has_waves(context) -> bool:
    """Whether the track geometry holds wave water, read off the mesh."""
    from . import geometry as geometry_ops  # noqa: PLC0415

    for obj in geometry_ops.geometry_objects(context):
        attribute = obj.data.attributes.get(geometry_ops.ATTR_FLAGS)
        if attribute is None:
            continue
        values = [0] * len(attribute.data)
        attribute.data.foreach_get("value", values)
        if any(water.is_wavy(geometry_ops.to_unsigned32(v)) for v in values):
            return True
    return False


def _check_wave_header(operator, header, tree):
    """Say what in the header would stop the waves, before it ships."""
    from .. import level_header_template as template  # noqa: PLC0415

    values = {pointer: template.lookup(header, pointer)
              for pointer in water.HEADER_POINTERS}
    values = {k: v for k, v in values.items() if isinstance(v, int)}
    for problem in water.header_problems(values):
        operator.report({"WARNING"}, "waves: %s" % problem)
    detail = template.lookup(header, water.DETAIL_POINTER)
    if detail in (None, "", -1):
        operator.report(
            {"WARNING"},
            "waves: the header names no wave detail texture, and the game "
            "would load texture 0xFFFF for it. Set the decomp asset path so "
            "%s can be named" % water.DETAIL_TEXTURE)
    elif tree is None:
        operator.report(
            {"WARNING"},
            "waves: the wave detail texture %s can only be written with the "
            "decomp assets configured" % detail)


def _base_header(context, tree):
    """The extracted header of the track this scene was loaded from.

    A Phase 1 remix reworks objects over shipped geometry, so its header is the
    base track's - only the name and whatever the author edits change. Without
    a base there is nothing to derive one from, and the addon says so rather
    than inventing 200 bytes.
    """
    if tree is None:
        return None
    source = context.scene.dkr.source_path or context.scene.dkr.geometry_path
    level = tree.level_using(source) if source else None
    if level is None or not level.header_path:
        return None
    try:
        with open(level.header_path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (ValueError, OSError):
        return None


def _attach_existing_payloads(package):
    """Pick up any payload already sitting in the track directory.

    Re-exporting must not throw away a payload the addon did not write. An
    author who drops in a ``header.bin`` - which the addon cannot produce yet -
    has to still have it after the next export, or the instruction to do so is a
    trap.

    The object maps are the one case where falling back is not neutral, so this
    returns the ones it had to fall back on. The addon compiles those itself
    whenever it can, and the only reason it cannot is that the decomp assets
    are unreachable and the level-object translation table with them. A file
    left from an earlier export then no longer matches the scene, and an export
    that quietly ships it has claimed to save work it did not save.
    """
    for section, filename in dkrmap.SECTIONS.items():
        candidate = os.path.join(package.directory, filename)
        if os.path.isfile(candidate):
            package.add_payload(section, candidate)

    stale = []
    for slot, filename in dkrmap.OBJECT_MAP_SLOTS.items():
        candidate = os.path.join(package.directory, filename)
        if slot not in package.object_maps and os.path.isfile(candidate):
            package.object_maps[slot] = candidate
            stale.append(filename)
    return stale


CLASSES = (DKR_OT_export_dkrmap,)
