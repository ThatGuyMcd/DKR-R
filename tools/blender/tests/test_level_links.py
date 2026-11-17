"""Check the byte offsets a hub's links to other custom tracks name.

The runtime writes a resolved level id into the byte a ``levelLinks`` entry
names, in the object map the game has just inflated. One byte off and it
rewrites some other field of some other object, so the offset is checked
against the encoder's own output rather than recomputed: an exit with a retail
destination must store that destination's id exactly at the offset the link
computation gives, and a linked exit must store the placeholder there.

    python tools/blender/tests/test_level_links.py
"""

from __future__ import annotations

import json
import os
import shutil
import struct
import sys
import tempfile
import zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))
sys.path.insert(0, _HERE)

from track_lab import (  # noqa: E402
    assets, catalog as catalog_module, dkrmap, gltf_io, level_links,
    object_map_encoder, validate,
)
from track_lab.gltf_io import MapObject, ObjectMap  # noqa: E402

from test_roundtrip import REPO_ROOT  # noqa: E402

FAILURES = []


def check(condition, message):
    if condition:
        print("  ok   %s" % message)
    else:
        print("  FAIL %s" % message)
        FAILURES.append(message)


def obj(object_id, **fields):
    return MapObject(object_id=object_id, name=object_id,
                     translation=[10.0, 20.0, 30.0], fields=fields)


def encoder_for(catalog, tree):
    """The real translation table and asset index when there is a tree; else
    the catalogue's own order, which is enough to place every byte."""
    if tree is not None:
        return object_map_encoder.ObjectMapEncoder(
            catalog, tree.translation_table(), tree.asset_index)
    levels = list(catalog.levels)
    return object_map_encoder.ObjectMapEncoder(
        catalog, sorted(catalog.types),
        lambda section, name: levels.index(name) if name in levels else -1)


def exit_to(destination, track=""):
    fields = {"destinationMapId": destination, "overworldSpawnIndex": 0,
              "returnSpawnIndex": 255, "radius": 1.5, "angleY": 0.0,
              "bossFlag": "WARP_FLAG_NORMAL"}
    if track:
        fields[level_links.TRACK_FIELD] = track
    return obj(level_links.EXIT, **fields)


def level_id(encoder, name):
    return encoder.asset_index("ASSET_LEVEL_HEADERS", name)


def inflate(packed: bytes) -> bytes:
    """The container is a u32 size and a tag, then raw deflate."""
    size, _tag = struct.unpack("<IB", packed[:5])
    blob = zlib.decompress(packed[5:], -15)
    assert len(blob) == size
    return blob


# ---------------------------------------------------------------------------

def test_offsets(catalog, encoder):
    print("entry offsets and the destination byte")
    mixed = ObjectMap(objects=[
        obj("ASSET_OBJECT_SETUPPOINT", racerIndex=0, entranceID=0),
        exit_to("ASSET_LEVEL_FOSSILCANYON"),
        obj("ASSET_OBJECT_WEAPONBALLOON", balloonType="BALLOON_TYPE_BOOST"),
        exit_to("ASSET_LEVEL_FOSSILCANYON", track="frosty-ridge"),
        exit_to("ASSET_LEVEL_WHALEBAY", track="sunset-bay"),
    ])
    offsets = encoder.entry_offsets(mixed)
    check(offsets[0] == object_map_encoder.HEADER_SIZE == 16,
          "the first entry starts after the 16-byte map header, where the game "
          "starts spawning (objects.c:980)")
    sizes = [len(encoder.encode_entry(o)) for o in mixed.objects]
    check(all(b - a == size for a, b, size in zip(offsets, offsets[1:], sizes)),
          "each entry starts where the previous one's bytes end")
    check(level_links.field_offset(catalog) == 8,
          "destinationMapId is byte 8 of LevelObjectEntry_Exit")

    prepared = level_links.with_placeholders(mixed)
    check(prepared.objects[1].fields["destinationMapId"] == "ASSET_LEVEL_FOSSILCANYON",
          "a retail exit keeps its destination")
    check(all(prepared.objects[i].fields["destinationMapId"]
              == level_links.PLACEHOLDER_LEVEL for i in (3, 4)),
          "a linked exit's leftover retail destination becomes the placeholder")
    check(mixed.objects[3].fields["destinationMapId"] == "ASSET_LEVEL_FOSSILCANYON",
          "the author's map is not changed in place")

    links = level_links.links(prepared, encoder.entry_offsets(prepared),
                              "structure", catalog)
    check([link["track"] for link in links] == ["frosty-ridge", "sunset-bay"],
          "one link per linked exit, in map order")
    check(all(set(link) == {"slot", "offset", "track"} and link["slot"] == "structure"
              for link in links), "each link carries slot, offset and track")

    blob = encoder.encode(prepared)
    for link in links:
        check(blob[link["offset"]] == level_id(encoder, level_links.PLACEHOLDER_LEVEL) == 0,
              "byte %d holds the placeholder, level 0" % link["offset"])
    retail = offsets[1] + level_links.field_offset(catalog)
    check(blob[retail] == level_id(encoder, "ASSET_LEVEL_FOSSILCANYON"),
          "the same arithmetic finds a retail exit's destination byte (%d holds "
          "Fossil Canyon's id %d)" % (retail, blob[retail]))
    check(links[0]["offset"] == offsets[3] + 8 and links[1]["offset"] == offsets[4] + 8,
          "offset = 16 + the entries before it + 8")

    plain = ObjectMap(objects=[exit_to("ASSET_LEVEL_FOSSILCANYON")])
    check(level_links.links(plain, encoder.entry_offsets(plain), "structure",
                            catalog) == [], "a map with no linked exit has no links")
    check(level_links.linked_track(obj("ASSET_OBJECT_COIN",
                                       **{level_links.TRACK_FIELD: "x"})) == "",
          "only an exit can carry a link")


def raises_link_error(call):
    try:
        call()
    except level_links.LevelLinkError:
        return True
    return False


def test_bounds(catalog, encoder):
    print("bounds on offsets and destination ids")
    top = level_links.MAX_OBJECT_MAP_BYTES
    check(top == 0x3000 and level_links.MAX_LEVEL_ID == 127,
          "the buffer is 0x3000 bytes and level ids run 0 to 127")
    check(level_links.check_offset(16) == 16 and level_links.check_offset(top - 1) == top - 1,
          "the first byte past the header and the last byte of the buffer are accepted")
    for bad in (-1, 0, 15, top, top + 1, 0x10000, 2.5, "40", None, True):
        check(raises_link_error(lambda b=bad: level_links.check_offset(b)),
              "offset %r is refused" % (bad,))
    check(raises_link_error(lambda: level_links.check_offset(100, map_size=100)),
          "an offset at the end of a 100 byte map is refused")
    check(level_links.check_offset(99, map_size=100) == 99,
          "and one before it is accepted")
    check(level_links.check_level_id(0) == 0 and level_links.check_level_id(127) == 127,
          "level ids 0 and 127 are accepted")
    for bad in (-1, 128, 200, 255, "5", None):
        check(raises_link_error(lambda b=bad: level_links.check_level_id(b)),
              "level id %r is refused" % (bad,))

    hostile = [{"slot": "structure", "offset": 0x2FFF, "track": "a"},
               {"slot": "structure", "offset": 0x3000, "track": "b"},
               {"slot": "structure", "offset": 40, "track": "c", "level_id": 200}]
    check(raises_link_error(lambda: level_links.check_links(hostile[1:2])),
          "check_links refuses an offset of 0x3000")
    check(raises_link_error(lambda: level_links.check_links(hostile[2:])),
          "check_links refuses level_id 200")
    check(not raises_link_error(lambda: level_links.check_links(hostile[:1])),
          "check_links accepts offset 0x2FFF")
    found = validate.check_manifest_links({"levelLinks": hostile})
    check(len(found) == 2 and all(i.severity == validate.ERROR for i in found),
          "validate reports the two bad links as errors (%d)" % len(found))
    check(validate.check_manifest_links({"levelLinks": ["x"]})[0].severity == validate.ERROR,
          "and a link that is not an object")

    # A linked exit at the far end of a map larger than the game's buffer.
    big = ObjectMap(objects=[obj("ASSET_OBJECT_COIN") for _ in range(top // 8)]
                    + [exit_to("ASSET_LEVEL_FOSSILCANYON", track="far-away")])
    prepared = level_links.with_placeholders(big)
    offsets = encoder.entry_offsets(prepared)
    check(raises_link_error(lambda: level_links.links(
        prepared, offsets, "structure", catalog)),
        "the exporter refuses a link whose byte is past 0x3000")

    # A retail exit whose destination encodes outside 0..127.
    for name in ("ASSET_LEVEL_FOSSILCANYON", "ASSET_LEVEL_NOT_A_LEVEL"):
        part = ObjectMap(objects=[exit_to(name)])
        blob = encoder.encode(part)
        refused = raises_link_error(lambda: level_links.check_exit_destinations(
            part, encoder.entry_offsets(part), blob, catalog))
        check(refused == (name == "ASSET_LEVEL_NOT_A_LEVEL"),
              "%s is %s" % (name, "refused" if refused else "accepted"))

    exits = ObjectMap(objects=[obj(level_links.EXIT, destinationMapId=200)])
    found = validate.validate(exits, catalog, require_racing_track=False)
    check(any("outside 0 to 127" in i.message for i in found.issues),
          "validate names a numeric destination of 200")


def test_package(catalog, tree):
    print("the package's manifest")
    if tree is None:
        print("  skip: compiling a package needs the extracted asset tree")
        return
    temporary = tempfile.mkdtemp(prefix="dkr-links-")
    try:
        package = dkrmap.TrackPackage(os.path.join(temporary, "hub.dkrmap"),
                                      "test-hub", "Test Hub")
        structure = ObjectMap(objects=[
            obj("ASSET_OBJECT_SETUPPOINT", racerIndex=0, entranceID=0),
            exit_to("ASSET_LEVEL_FOSSILCANYON", track="frosty-ridge"),
        ])
        collectables = ObjectMap(objects=[
            obj("ASSET_OBJECT_COIN"), obj("ASSET_OBJECT_COIN"),
            exit_to(level_links.PLACEHOLDER_LEVEL, track="sunset-bay"),
        ])
        payloads = {}
        for slot, part in (("structure", structure), ("collectables", collectables)):
            payloads[slot] = package.encode_object_map(
                slot, part, catalog, tree.translation_table(), tree.asset_index)
        manifest = package.manifest()
        links = manifest.get("levelLinks", [])
        check([(l["slot"], l["track"]) for l in links]
              == [("structure", "frosty-ridge"), ("collectables", "sunset-bay")],
              "the manifest lists both slots' links (%r)" % links)
        for link in links:
            blob = inflate(payloads[link["slot"]])
            check(blob[link["offset"]] == 0,
                  "%s byte %d, in the inflated payload, is the placeholder"
                  % (link["slot"], link["offset"]))
            entries = struct.unpack(">I", blob[:4])[0]
            check(16 <= link["offset"] < 16 + entries,
                  "and lies inside the map's entries")
        check(links[1]["offset"] == 16 + 8 + 8 + 8,
              "the collectables link counts the two 8-byte coins before it")
        package.write()
        with open(os.path.join(package.directory, "manifest.json"), encoding="utf-8") as handle:
            written = json.load(handle)
        check(written.get("levelLinks") == links, "manifest.json carries them")

        bare = dkrmap.TrackPackage(os.path.join(temporary, "plain.dkrmap"),
                                   "plain", "Plain")
        bare.encode_object_map("structure", ObjectMap(objects=[
            exit_to("ASSET_LEVEL_FOSSILCANYON")]), catalog,
            tree.translation_table(), tree.asset_index)
        check("levelLinks" not in bare.manifest(),
              "a package without custom destinations has no levelLinks key")
        try:
            bare.encode_object_map("structure", ObjectMap(objects=[
                obj("ASSET_OBJECT_COIN") for _ in range(0x3000 // 8)]
                + [exit_to("ASSET_LEVEL_FOSSILCANYON", track="far-away")]),
                catalog, tree.translation_table(), tree.asset_index)
            refused = False
        except dkrmap.DkrMapError:
            refused = True
        check(refused, "encode_object_map refuses a link past the 0x3000 buffer")
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def test_retail_hub(catalog, tree):
    print("a retail hub with one exit relinked")
    if tree is None:
        print("  skip: no extracted asset tree")
        return
    level = next((l for l in tree.levels() if l.label == "Dino Domain Hub"), None)
    if level is None:
        print("  skip: Dino Domain Hub not in this extraction")
        return
    retail = gltf_io.load(level.objects_path)
    exits = [i for i, o in enumerate(retail.objects) if o.object_id == level_links.EXIT]
    chosen = exits[len(exits) // 2]
    objects = [MapObject(o.object_id, o.name, list(o.translation), dict(o.fields))
               for o in retail.objects]
    original = objects[chosen].fields["destinationMapId"]
    objects[chosen].fields[level_links.TRACK_FIELD] = "my-race"
    edited = level_links.with_placeholders(ObjectMap(objects=objects))
    encoder = encoder_for(catalog, tree)
    links = level_links.links(edited, encoder.entry_offsets(edited), "structure", catalog)
    before = encoder.encode(retail)
    after = encoder.encode(edited)
    changed = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
    check(len(links) == 1 and changed == [links[0]["offset"]],
          "relinking exit %d of %d changes exactly the byte its link names (%r vs %r)"
          % (exits.index(chosen) + 1, len(exits), changed, [l["offset"] for l in links]))
    check(before[links[0]["offset"]] == level_id(encoder, original),
          "which held %s before" % original)


def main():
    catalog = catalog_module.load()
    tree = assets.AssetTree.discover(REPO_ROOT)
    test_offsets(catalog, encoder_for(catalog, tree))
    test_bounds(catalog, encoder_for(catalog, tree))
    test_package(catalog, tree)
    test_retail_hub(catalog, tree)
    print()
    if FAILURES:
        print("%d FAILURE(S)" % len(FAILURES))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
