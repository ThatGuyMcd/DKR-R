"""Waterfall rules and the retail TexScroll loop, runnable without Blender."""

import os
import sys
import unittest
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from track_lab import assets, gltf_io, level_model, texture_scroll as scroll, textures


def loop(raw, width, height, speed, steps=4096):
    """Transcription of obj_loop_texscroll, including s16 intermediate writes."""
    raw = [list(uv) for uv in raw]
    remainder = [0, 0]
    for tick in range(steps):
        # Exercise normal and dropped frames in both directions.
        rate = (1, 2, 3, 6)[tick % 4]
        for axis, size in enumerate((width, height)):
            total = remainder[axis] + speed[axis] * rate
            remainder[axis] = total & 3
            delta = total >> 2
            if raw[0][axis] > size << 8:
                for uv in raw:
                    uv[axis] -= size << 8
                    assert -32768 <= uv[axis] <= 32767
            if raw[0][axis] < 0:
                for uv in raw:
                    uv[axis] += size << 8
                    assert -32768 <= uv[axis] <= 32767
            for uv in raw:
                uv[axis] += delta
                assert -32768 <= uv[axis] <= 32767


class ScrollTests(unittest.TestCase):
    def setUp(self):
        self.texture = SimpleNamespace(width=32, height=32)

    def test_speed(self):
        for speed in range(-128, 128):
            self.assertEqual(scroll.speed_from_texels(scroll.texels_per_second(speed)), speed)
        self.assertEqual(scroll.speed_from_texels(9999), 127)
        self.assertEqual(scroll.speed_from_texels(-9999), -128)
        self.assertEqual(scroll.texels_per_second(95), 44.53125)

    def test_reference(self):
        a = dict(id=2, w=32, h=32, format=0, surface=0)
        b = dict(a, id=3)
        ref = scroll.reference([a, b], 0)
        self.assertEqual(scroll.resolve_index([a, a], ref), 0)
        self.assertEqual(scroll.resolve_index([b, a], ref), 1)
        for table in ([b], [dict(a, surface=1)], [b, a, a]):
            with self.assertRaises(scroll.ScrollError):
                scroll.resolve_index(table, ref)
        with self.assertRaises(scroll.ScrollError):
            scroll.resolve_index([a], {"index": 0})

    def test_mapping_direction_and_seams(self):
        points = [(0, 0, 0), (256, 0, 0), (256, 256, 0), (0, 256, 0)]
        raw = scroll.fall_mapping(points, self.texture, 3)
        self.assertGreater(raw[2][1], raw[1][1])
        # A constant feature at V moves down when the sample gains +V.
        before_y = 128
        after_y = before_y - 32 / (3 * 1024 / 256)
        self.assertLess(after_y, before_y)
        bounds = (0, 256, 0, 256)
        a = scroll.fall_mapping(points[:3], self.texture, 3, bounds=bounds)
        b = scroll.fall_mapping([points[i] for i in (0, 2, 3)], self.texture, 3, bounds=bounds)
        self.assertEqual(a[0], b[0])
        self.assertEqual(a[2], b[1])
        self.assertEqual(scroll.fall_mapping(list(reversed(points)), self.texture, 3), list(reversed(raw)))
        sloped = [(0, 0, 0), (256, 0, 0), (256, 256, 256)]
        self.assertGreater(scroll.fall_mapping(sloped, self.texture)[2][1], 0)
        with self.assertRaises(scroll.ScrollError):
            scroll.fall_mapping([(0, 0, 0), (1, 0, 0), (1, 0, 1)], self.texture)

    def test_runtime_bounds(self):
        safe = [(0, 0), (1024, 0), (1024, 23000)]
        unsafe = [(0, 0), (1024, 0), (1024, 25000)]
        self.assertFalse(scroll.uv_problems([safe], self.texture, 1, 127))
        self.assertTrue(scroll.uv_problems([unsafe], self.texture, 1, 127))
        for speed in (-128, 127):
            for first in range(3):
                loop(safe[first:] + safe[:first], 32, 32, (0, speed))
        issues = scroll.problems([dict(uvs=safe, flags=0x80)], self.texture, [(0, 95), (0, 0)])
        self.assertTrue(any(level == "error" and "0x80" in message for level, message in issues))
        self.assertTrue(any("add together" in message for _, message in issues))
        self.assertTrue(any("zero speed" in message for _, message in issues))

    def test_wrap_requirements(self):
        texture = SimpleNamespace(width=32, height=32, wrap_s="Clamp", wrap_t="Wrap")
        self.assertIsNone(scroll.axis_problem(texture, 1))
        self.assertIn("Clamp", scroll.axis_problem(texture, 0))
        texture.height = 128
        self.assertIn("64", scroll.axis_problem(texture, 1))
        wide = SimpleNamespace(width=128, height=16)
        self.assertFalse(scroll.uv_problems([[(0, 0), (4096, 0), (4096, 512)]], wide, 0, 0))
        self.assertTrue(scroll.uv_problems([[(-1, 0), (4096, 0), (4096, 512)]], wide, 0, 0))

    def test_retail(self):
        root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
        tree = assets.AssetTree.find(os.path.join(root, "extern/dkr-decomp/assets/.vanilla/us.v77"))
        if tree is None:
            self.skipTest("no extracted retail assets")
        catalogue = {t.index: t for t in textures.catalogue(tree)}
        presets = {asset for asset, _ in scroll.PRESETS}
        self.assertTrue(presets <= {t.asset_id for t in catalogue.values()})
        count, waterfall_faces = 0, 0
        seen = set()
        for level in tree.levels():
            if not level.model_path:
                continue
            model = None
            for path in (level.objects_path, level.collectables_path):
                if not path or path in seen:
                    continue
                seen.add(path)
                objects = gltf_io.load(path).by_id(scroll.OBJECT_ID)
                if not objects:
                    continue
                model = model or level_model.load(level.model_path)
                for obj in objects:
                    count += 1
                    index = obj.fields["textureIndex"]
                    entry = model.textures[index]
                    texture = catalogue[entry.texture_id]
                    if texture.asset_id not in presets:
                        continue
                    self.assertIsNone(scroll.axis_problem(texture, 1))
                    for segment in model.segments:
                        for batch in segment.batches:
                            if batch.texture_index != index:
                                continue
                            for face in range(batch.face_offset, batch.face_offset + batch.face_count):
                                triangle = segment.triangles[face]
                                waterfall_faces += 1
                                self.assertFalse(triangle[0] & 0x80)
                                raw = segment.uvs[face]
                                self.assertFalse(scroll.uv_problems([raw], texture, 1, obj.fields["unkB"]))
                                loop(raw, texture.width, texture.height, (obj.fields["unkA"], obj.fields["unkB"]))
        self.assertEqual(count, 27)
        self.assertEqual(waterfall_faces, 559)


if __name__ == "__main__":
    unittest.main()
