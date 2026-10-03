"""Gate for giving a custom track any texture the ROM holds.

A level model's texture table names entries of ``ASSET_TEXTURES_3D``, so a track
is not limited to the images its base model shipped with. Acting on that means
writing table entries the addon composed rather than ones it decoded, and this
is what says those entries are the ones retail would have written.

Four things have to hold, and each is checked against the whole retail set
rather than against an example.

**The catalogue describes retail's own table.** Every texture entry in every
level model is looked up in the catalogue built from the extracted assets, and
its size and format must be what retail put in the table. 1360 entries, and the
two known disagreements are named rather than tolerated silently.

**An added texture survives the file.** A model is given a texture, laid out,
encoded, decoded again, and has to come back with exactly that entry and nothing
else disturbed.

**The layout is rebuilt, not patched.** The texture table is the first array
after the header, so an extra entry runs into the segment array rather than off
the end of the blob - which no bounds check would catch. Growing the table has
to make an in-place encode refuse.

**Animation is a fact about the artwork, not a choice.** ``RENDER_TEX_ANIM`` is
set on a batch exactly when its texture has more than one frame, across all
10,389 retail batches. The addon sets the bit from the catalogue, so if that
correlation were not perfect the rule would be wrong.

Run with any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_textures.py
"""

from __future__ import annotations

import glob
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from track_lab import (  # noqa: E402
    assets, level_model, level_model_edit, level_model_encoder,
    level_model_layout, textures,
)

REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
VANILLA = os.path.join(REPO_ROOT, "extern", "dkr-decomp", "assets", ".vanilla")

FAILURES = []

#: The two retail entries whose declared size is not the image's. Both declare
#: a size larger than the PNG, which stretches it - a deliberate thing to do and
#: not something an addon has to reproduce. Named so that a third one appearing
#: is a failure rather than a tolerance.
KNOWN_SIZE_DISAGREEMENTS = {
    ("darkmoon_caverns.bin", 9),
    ("wizpig2.bin", 22),
}


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def find_tree():
    for version in sorted(glob.glob(os.path.join(VANILLA, "*")), reverse=True):
        tree = assets.AssetTree.find(version)
        if tree is not None:
            return tree
    return None


def find_level_models(tree):
    return sorted(glob.glob(os.path.join(tree.root, "levels", "models", "*", "*.bin")))


# ---------------------------------------------------------------------------
# The catalogue
# ---------------------------------------------------------------------------

def test_catalogue(tree, catalogue):
    print("catalogue")
    check(len(catalogue) > 1000,
          "the ROM's 3D texture list resolved (%d entries)" % len(catalogue))

    order = tree.order(assets.META_TEXTURES_3D)
    check(len(catalogue) == len(order),
          "every one of the %d listed textures resolved to an image (%d did)"
          % (len(order), len(catalogue)))

    dense = all(entry.index == at for at, entry in enumerate(catalogue))
    check(dense, "the catalogue is in asset order, so entry n is texture id n")

    check(all(entry.width and entry.height for entry in catalogue),
          "every texture has a size to divide a UV by")
    check(all(0 <= entry.format <= 8 for entry in catalogue),
          "every format is one the hardware has")

    animated = [entry for entry in catalogue if entry.animated]
    check(animated, "animated textures are recognised (%d of them)" % len(animated))

    found = textures.search(catalogue, "water")
    check(found, "searching by name finds something (%d for 'water')" % len(found))
    check(all("water" in ("%s %s" % (e.name, e.asset_id)).lower() for e in found),
          "and finds only what matches")


def test_matches_retail(catalogue, models):
    """What the addon would write for a texture is what retail wrote for it."""
    print("the catalogue against every retail texture table")
    by_index = {entry.index: entry for entry in catalogue}

    entries = size_ok = format_ok = 0
    unknown = []
    size_bad = []
    format_bad = []
    for path in models:
        name = os.path.basename(path)
        model = level_model.load(path)
        for at, texture in enumerate(model.textures):
            entries += 1
            entry = by_index.get(texture.texture_id)
            if entry is None:
                unknown.append((name, at, texture.texture_id))
                continue
            if (entry.width, entry.height) == (texture.raw_width, texture.raw_height):
                size_ok += 1
            elif (name, at) not in KNOWN_SIZE_DISAGREEMENTS:
                size_bad.append((name, at, (entry.width, entry.height),
                                 (texture.raw_width, texture.raw_height)))
            # The high nibble carries something nothing in the assets predicts
            # and nothing in the game reads; the format is the low four bits.
            if entry.format == texture.format & 0x0F:
                format_ok += 1
            else:
                format_bad.append((name, at, entry.format, texture.format))

    check(not unknown,
          "every texture a retail track names is in the catalogue (%d are not: %s)"
          % (len(unknown), unknown[:3]))
    check(not size_bad,
          "the image's own size is the one retail declared (%d disagree: %s)"
          % (len(size_bad), size_bad[:3]))
    check(not format_bad,
          "the sidecar's format is retail's low nibble (%d disagree: %s)"
          % (len(format_bad), format_bad[:3]))
    print("  %d table entries: %d sizes and %d formats reproduced"
          % (entries, size_ok, format_ok))


def test_animation_rule(catalogue, models):
    """``RENDER_TEX_ANIM`` is set exactly when the artwork has frames to advance."""
    print("the animation rule")
    frames = {entry.index: entry.frames for entry in catalogue}

    batches = flagged = animated = 0
    wrong = []
    for path in models:
        model = level_model.load(path)
        for segment in model.segments:
            for at, batch in enumerate(segment.batches):
                batches += 1
                texture = model.texture_for(batch)
                is_animated = bool(texture and frames.get(texture.texture_id, 1) > 1)
                is_flagged = bool(batch.flags & level_model.RENDER_TEX_ANIM)
                animated += 1 if is_animated else 0
                flagged += 1 if is_flagged else 0
                if is_animated != is_flagged:
                    wrong.append((os.path.basename(path), segment.index, at))

    check(not wrong,
          "%d of %d batches flag animation without an animated texture, or the "
          "reverse: %s" % (len(wrong), batches, wrong[:3]))
    print("  %d batches, %d animated textures, %d flagged"
          % (batches, animated, flagged))


# ---------------------------------------------------------------------------
# Adding one
# ---------------------------------------------------------------------------

def test_adding(catalogue, models):
    """A texture the track never had goes in, and comes back out of the file."""
    print("adding a texture to a retail track")
    if not models or not catalogue:
        return

    path = models[0]
    model = level_model.load(path)
    before = len(model.textures)
    used = {texture.texture_id for texture in model.textures}
    fresh = next((e for e in catalogue if e.index not in used), None)
    if fresh is None:
        print("  skip: this track already uses every texture in the ROM")
        return

    index = level_model_edit.add_texture(
        model, fresh.index, fresh.width, fresh.height, fresh.format, 4
    )
    check(index == before, "the new entry lands after the ones already there")
    check(len(model.textures) == before + 1, "and the table grew by one")

    # Growing the table has to invalidate the in-place path, or the entry would
    # be written over the segment array with every bounds check satisfied.
    problems = level_model_encoder.check_layout(model)
    check(any("textures" in problem for problem in problems),
          "an in-place encode now refuses, naming the table (%s)" % (problems[:1],))
    try:
        level_model_encoder.encode(model)
        check(False, "encode refused rather than writing over the segments")
    except level_model_encoder.LevelModelEncodeError:
        pass

    level_model_layout.rebuild(model)
    check(model.texture_count_field == before + 1,
          "rebuilding the layout declares the new count")

    again = level_model.parse(
        level_model.decompress(level_model_encoder.pack(model))
    )
    check(len(again.textures) == before + 1,
          "the encoded file carries %d textures" % len(again.textures))
    if len(again.textures) == before + 1:
        written = again.textures[index]
        check(written.texture_id == fresh.index,
              "the entry names texture %d (got %d)" % (fresh.index, written.texture_id))
        check((written.raw_width, written.raw_height)
              == (fresh.width & 0xFF, fresh.height & 0xFF),
              "at its own size (%dx%d)" % (written.raw_width, written.raw_height))
        check(written.format == fresh.format, "in its own format")
        check(written.surface_type == 4, "with the surface type it was given")

    kept = all(
        (a.texture_id, a.raw_width, a.raw_height, a.format, a.surface_type)
        == (b.texture_id, b.raw_width, b.raw_height, b.format, b.surface_type)
        for a, b in zip(model.textures[:before], again.textures[:before])
    )
    check(kept, "and every entry the track already had is untouched")


def test_reuse(catalogue, models):
    """The same texture at the same surface is one entry; at another, two."""
    print("reusing an entry")
    if not models or not catalogue:
        return

    model = level_model.load(models[0])
    before = len(model.textures)
    used = {texture.texture_id for texture in model.textures}
    fresh = next((e for e in catalogue if e.index not in used), None)
    if fresh is None:
        return

    first = level_model_edit.add_texture(
        model, fresh.index, fresh.width, fresh.height, fresh.format, 0
    )
    again = level_model_edit.add_texture(
        model, fresh.index, fresh.width, fresh.height, fresh.format, 0
    )
    check(first == again and len(model.textures) == before + 1,
          "asking twice for the same entry gives the same index")

    other = level_model_edit.add_texture(
        model, fresh.index, fresh.width, fresh.height, fresh.format, 1
    )
    check(other != first and len(model.textures) == before + 2,
          "the same picture at another surface type is a second entry - which "
          "is how one image is road in one place and grass in another")

    # The existing entries have to stay findable, or an edit would silently
    # repoint faces at a new duplicate.
    for at, texture in enumerate(model.textures[:before]):
        found = level_model_edit.add_texture(
            model, texture.texture_id, texture.raw_width, texture.raw_height,
            texture.format, texture.surface_type
        )
        if found != at:
            check(False, "entry %d was duplicated at %d instead of reused"
                  % (at, found))
            break
    else:
        check(True, "and every entry the track already had is reused, not copied")


def test_ceiling(catalogue):
    """The table stops at what a u8 index can reach."""
    print("the table ceiling")
    if not catalogue:
        return

    model = level_model_layout.blank_model([])
    for at in range(level_model.MAX_TEXTURES):
        level_model_edit.add_texture(model, catalogue[0].index, 32, 32, 1, at & 0xFF)
    check(len(model.textures) == level_model.MAX_TEXTURES,
          "%d entries go in" % level_model.MAX_TEXTURES)
    try:
        level_model_edit.add_texture(model, catalogue[0].index, 32, 32, 1, 0xFF)
        # 0xFF as a surface is the 255th distinct entry, so this one is a reuse
        # if and only if the loop above already made it - which it did.
    except level_model_edit.EditError:
        pass
    try:
        level_model_edit.add_texture(model, catalogue[1].index, 16, 16, 1, 0)
        check(False, "the %dth entry was refused" % (level_model.MAX_TEXTURES + 1))
    except level_model_edit.EditError as error:
        check("ceiling" in str(error),
              "and the refusal says why: %s" % str(error)[:60])


def test_animation_gate(catalogue, models):
    """A track given an animated texture stops declaring it has none."""
    print("the animation gate")
    if not models:
        return

    model = level_model_layout.blank_model([])
    check(model.animated_texture_count == 0, "a new track declares no animation")
    check(level_model_edit.set_animation_gate(model, 2),
          "adding animated textures opens the gate")
    check(model.animated_texture_count == 2, "and the count is no longer zero")

    # A value a track already carries is not the addon's to reinterpret: it is
    # not a count of anything derivable, and 35 of the 55 retail models disagree
    # with every reading of it that would make it one.
    model.animated_texture_count = 7
    check(not level_model_edit.set_animation_gate(model, 3),
          "a gate that is already open is left exactly as the track had it")
    check(model.animated_texture_count == 7, "at its own value")


# ---------------------------------------------------------------------------
# Mapping
# ---------------------------------------------------------------------------

def test_rescale():
    """Changing a texture's size leaves the picture covering the same ground."""
    print("keeping a mapping across a size change")
    raw = [(0, 0), (32 * 32, 0), (32 * 32, 32 * 16)]
    was = textures.normalise_pair(raw, 32, 32)
    now = textures.normalise_pair(textures.rescale_uvs(raw, 32, 32, 64, 128), 64, 128)
    drift = max(max(abs(a[0] - b[0]), abs(a[1] - b[1])) for a, b in zip(was, now))
    check(drift < 1e-6,
          "the normalised mapping is unchanged (drift %.2e)" % drift)

    shrunk = textures.rescale_uvs(raw, 64, 64, 32, 32)
    check(shrunk == [(0, 0), (512, 0), (512, 256)],
          "and halving the texture halves the texel span (%r)" % (shrunk,))


def test_projection():
    """A projected texture tiles across a join, and fits the s16 it is stored in."""
    print("projecting a texture onto the world")
    scale = textures.DEFAULT_PROJECTION_SCALE
    width = height = 32

    # Two triangles of one quad, far from the origin, where a projection that
    # did not offset by whole repeats would overflow.
    a = (5000.0, 0.0, -9000.0)
    b = (5200.0, 0.0, -9000.0)
    c = (5200.0, 0.0, -8800.0)
    d = (5000.0, 0.0, -8800.0)

    left = textures.project_face([a, b, c], width, height, scale)
    right = textures.project_face([a, c, d], width, height, scale)
    check(textures.fits_s16(left) and textures.fits_s16(right),
          "a face 9000 units out still fits the s16 a UV is stored in (%r)" % (left,))

    # The shared corners have to land on the same point of the texture, give or
    # take a whole repeat - which is what "the tiling is continuous" means.
    shared = {0: 0, 2: 1}
    for at_left, at_right in shared.items():
        one = textures.normalise_pair([left[at_left]], width, height)[0]
        two = textures.normalise_pair([right[at_right]], width, height)[0]
        gap = (abs(one[0] - two[0]) % 1.0, abs(one[1] - two[1]) % 1.0)
        near = min(gap[0], 1.0 - gap[0]) < 1e-3 and min(gap[1], 1.0 - gap[1]) < 1e-3
        check(near,
              "the two faces meet on the same texel at the shared corner "
              "(%r against %r)" % (one, two))

    # The scale means what it says: one repeat over that many map units.
    span = textures.project_face(
        [(0.0, 0.0, 0.0), (scale, 0.0, 0.0), (scale, 0.0, scale)],
        width, height, scale,
    )
    across = abs(span[1][0] - span[0][0]) / textures.TEXEL / width
    check(abs(across - 1.0) < 1e-6,
          "a face %g units wide spans one repeat (%.4f)" % (scale, across))

    # A wall faces X or Z, so it must not take the ground plane.
    wall = textures.project_face(
        [(0.0, 0.0, 0.0), (0.0, 200.0, 0.0), (0.0, 200.0, 200.0)],
        width, height, scale,
    )
    check(len({pair[1] for pair in wall}) > 1,
          "a vertical face is mapped down its height, not flattened (%r)" % (wall,))


CASES = (
    "catalogue", "retail tables", "animation rule", "adding", "reuse",
    "ceiling", "animation gate", "rescale", "projection",
)


def main():
    tree = find_tree()
    if tree is None:
        print("no extracted asset tree found under %s" % VANILLA)
        print("skipping: this suite checks the addon against the real textures")
        return 0

    catalogue = textures.catalogue(tree)
    models = find_level_models(tree)
    print("assets: %s, %d level models" % (tree.label, len(models)))
    print("")

    test_catalogue(tree, catalogue)
    if models:
        test_matches_retail(catalogue, models)
        test_animation_rule(catalogue, models)
    test_adding(catalogue, models)
    test_reuse(catalogue, models)
    test_ceiling(catalogue)
    test_animation_gate(catalogue, models)
    test_rescale()
    test_projection()

    print("")
    if FAILURES:
        print("%d failure(s):" % len(FAILURES))
        for message in FAILURES:
            print("  - %s" % message)
        return 1
    print("a custom track can name any of the ROM's %d textures, and the entry "
          "it writes is the one retail writes" % len(catalogue))
    return 0


if __name__ == "__main__":
    sys.exit(main())
