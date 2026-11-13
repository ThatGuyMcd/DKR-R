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
    object_map_encoder,
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
