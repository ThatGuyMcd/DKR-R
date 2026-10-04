"""Build a .dkrmap track whose road is one picture you supply.

The shortest possible demonstration that a custom track can ship artwork the
ROM does not hold, and a working example to copy. Run it headless:

    blender --background --factory-startup \\
        --python tools/blender/make_texture_demo_track.py -- \\
        --image tools/blender/leaked.jpeg --out build/my-track.dkrmap

It does by script exactly what the sidebar does by hand, in the same order and
through the same operators, so a failure here is a failure of the addon and not
of a second path that happens to look similar:

1. a flat mesh becomes track geometry - ``dkr.track_from_mesh_blank``, which
   needs no extracted assets because it borrows no texture table;
2. the image becomes a texture the track owns - ``dkr.add_custom_texture``,
   which resamples it to something the RDP can load and writes the PNG the
   package will be built from;
3. every face is given it and projected flat - ``dkr.apply_texture``, because a
   face with no mapping draws one texel stretched over the whole of it;
4. the package is written - ``dkr.export_dkrmap`` - and beside it
   ``<out>-hd.zip``, the texture pack that has DKR-R draw the picture at the
   resolution it was made rather than the 64x32 the track carries.

**What the result is and is not.** It is a level model and a texture payload,
which is the half this script exists to demonstrate. It is not a playable
track: a header and two object maps are what make one, the header needs a
world and a race type, and compiling the object maps needs the decomp's
extracted asset tree. The package says so itself in HOW-TO-BUILD.md, and the
export reports it as a warning rather than success.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import track_lab  # noqa: E402
from track_lab import level_model, textures as texture_module  # noqa: E402


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True,
                        help="the picture the track's road is made of")
    parser.add_argument("--out", required=True,
                        help="the .dkrmap directory to write")
    parser.add_argument("--name", default="Custom Texture Demo")
    parser.add_argument("--id", default="custom-texture-demo")
    parser.add_argument("--format", default="RGBA16",
                        choices=list(texture_module.CUSTOM_FORMATS))
    parser.add_argument("--size", default="",
                        help="WxH to resample to; blank takes the largest the "
                             "format allows")
    parser.add_argument("--track-size", type=float, default=6000.0,
                        help="how many map units across the flat track is")
    parser.add_argument("--repeat", type=float, default=512.0,
                        help="map units per repeat of the texture")
    return parser.parse_args(argv)


def main(argv):
    args = arguments(argv)
    image = os.path.abspath(args.image)
    if not os.path.isfile(image):
        raise SystemExit("no such image: %s" % image)

    out = os.path.abspath(args.out)
    if not out.endswith(".dkrmap"):
        out += ".dkrmap"
    workspace = os.path.dirname(out) or os.getcwd()
    os.makedirs(workspace, exist_ok=True)

    track_lab.register()
    try:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        # Saved first, because the addon keeps a track's resampled pictures
        # beside the .blend that names them and refuses to pretend otherwise.
        blend = os.path.join(workspace, "%s.blend" % args.id)
        bpy.ops.wm.save_as_mainfile(filepath=blend)

        settings = bpy.context.scene.dkr
        settings.track_name = args.name
        settings.track_id = args.id

        # 1. A flat quad grid, then the conversion that needs no donor track.
        bpy.ops.mesh.primitive_grid_add(
            size=args.track_size, x_subdivisions=8, y_subdivisions=8
        )
        say(bpy.ops.dkr.track_from_mesh_blank(keep_source=False),
            "track_from_mesh_blank")

        from track_lab.operators import geometry as geometry_ops

        built = geometry_ops.geometry_objects(bpy.context)
        if not built:
            raise SystemExit("the conversion produced no geometry")
        obj = built[0]
        print("geometry: %s, %d faces" % (obj.data.name, len(obj.data.polygons)))

        # 2. The picture becomes a texture this track owns.
        say(bpy.ops.dkr.add_custom_texture(
                filepath=image,
                texture_format=str(texture_module.FORMAT_CODES[args.format]),
                size=args.size,
            ),
            "add_custom_texture")
        own = settings.custom_textures
        if not len(own):
            raise SystemExit("the image was not accepted")
        print("texture: %s, %dx%d, format %d, id 0x%04X"
              % (own[0].name, own[0].width, own[0].height, own[0].format,
                 texture_module.custom_id(0)))

        # 3. Every face draws it, projected onto the world so it tiles.
        for polygon in obj.data.polygons:
            polygon.select = True
        settings.texture_mapping = "PROJECT"
        settings.texture_scale = args.repeat
        settings.texture_surface = "0"
        say(bpy.ops.dkr.apply_texture(), "apply_texture")

        # 4. A header written from nothing, so the package is a track and not
        #    only a model. The two choices it cannot default - which world the
        #    track is in and what is raced on it - are answered from the
        #    bundled catalogue rather than guessed at.
        bpy.ops.dkr.header_defaults()
        for pointer, subject in (("/world", "World"),
                                 ("/race-type", "RaceType")):
            member = enum_member(subject)
            if member is not None:
                bpy.context.scene[header_key(pointer)] = member

        # 5. A start position, so the export has an object map to write, and
        #    then the package.
        bpy.ops.dkr.place_object(object_id="ASSET_OBJECT_SETUPPOINT")
        say(bpy.ops.dkr.export_dkrmap(filepath=out, validate_first=False),
            "export_dkrmap")
        bpy.ops.wm.save_mainfile()
    finally:
        track_lab.unregister()

    report(out)
    return 0


def enum_member(subject):
    """Any valid member of one of the catalogue's enums, or ``None``.

    The catalogue is bundled with the addon, so this needs no extraction - the
    header template's two unanswerable questions can be answered on a machine
    that has never seen a ROM.
    """
    from track_lab import catalog as catalog_module

    members = sorted(catalog_module.load().raw.get("enumValues", {})
                     .get(subject, {}))
    return members[0] if members else None


def header_key(pointer):
    from track_lab.operators import header as header_ops

    return header_ops.key_for(pointer)


def say(result, what):
    print("%-26s %s" % (what, sorted(result)))
    if "FINISHED" not in result:
        raise SystemExit("%s did not finish" % what)


def report(out):
    """Read the package back and say what is in it, from the bytes."""
    print("")
    print("wrote %s" % out)

    with open(os.path.join(out, "manifest.json"), "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    for entry in manifest["adds"]:
        path = os.path.join(out, entry["file"])
        size = os.path.getsize(path) if os.path.isfile(path) else -1
        print("  %-18s %-22s %d bytes"
              % (entry["section"], entry["file"], size))

    payload = os.path.join(out, "textures", "0.bin")
    if os.path.isfile(payload):
        with open(payload, "rb") as handle:
            blob = handle.read()
        names = {value: key for key, value in texture_module.FORMAT_CODES.items()}
        print("")
        print("  texture 0: %dx%d %s, %d frame(s), %scompressed, "
              "textureSize %d"
              % (blob[0x00], blob[0x01], names.get(blob[0x02] & 0xF, "?"),
                 struct.unpack_from(">H", blob, 0x12)[0] >> 8,
                 "" if blob[0x1D] else "un",
                 struct.unpack_from(">h", blob, 0x16)[0]))

    model_path = os.path.join(out, "model.bin")
    if os.path.isfile(model_path):
        model = level_model.load(model_path)
        own = [texture.texture_id for texture in model.textures
               if texture_module.is_custom_id(texture.texture_id)]
        print("  model:     %d triangles, %d segment(s), %d texture(s), "
              "%d of them this track's own"
              % (model.triangle_count, len(model.segments),
                 len(model.textures), len(own)))
        print("             its own are %s, which DKR-R rewrites to the "
              "ROM's texture count plus 0, 1, ... as the model is served"
              % ", ".join("0x%04X" % value for value in own))

    pack = os.path.splitext(out)[0] + "-hd.zip"
    if os.path.isfile(pack):
        import zipfile

        with zipfile.ZipFile(pack) as archive:
            names = [name for name in archive.namelist() if name.endswith(".png")]
        print("")
        print("  hd pack:   %s, %d replacement(s), %d bytes"
              % (os.path.basename(pack), len(names), os.path.getsize(pack)))
        for name in names:
            print("             %s" % name)

    print("")
    print("Install: copy the .dkrmap into DKR-R's custom-tracks/, or point")
    print("Track Lab's working directory at the folder it is in.")
    if os.path.isfile(pack):
        print("Then import %s under Graphics > Custom Texture Packs and"
              % os.path.basename(pack))
        print("enable it, for the full-resolution picture (Modern preset).")
    print("See HOW-TO-BUILD.md inside it for what a playable track still needs.")


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
