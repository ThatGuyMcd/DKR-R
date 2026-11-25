import importlib.util
import io
from pathlib import Path
import struct
import unittest

path = Path(__file__).resolve().parents[2] / "services/online-directory/render_ranked_video.py"
spec = importlib.util.spec_from_file_location("render_ranked_video", path)
video = importlib.util.module_from_spec(spec)
spec.loader.exec_module(video)


class CaptureCadenceTests(unittest.TestCase):
    def setUp(self):
        self.audio = bytes(video.SAMPLES_PER_UPDATE * 4)
        self.events = []

    def capture(self, *packets, audio=None):
        data = io.BytesIO(struct.pack(f"<{len(packets)}I", *packets) + (self.audio if audio is None else audio))

        def read_picture(tag):
            self.events.append(("read", tag))
            return bytes([tag])

        return video.capture_update(
            data,
            7,
            read_picture,
            lambda picture: self.events.append(("picture", picture)),
            lambda samples: self.events.append(("audio", len(samples))),
            lambda: self.events.append(("ack",)),
        )

    def test_two_rt64_swaps_are_encoded_in_order(self):
        self.assertEqual(self.capture(29, 30, 28), (True, True))
        self.assertEqual(
            self.events,
            [
                ("read", 13),
                ("picture", b"\x0d"),
                ("ack",),
                ("read", 14),
                ("picture", b"\x0e"),
                ("ack",),
                ("audio", len(self.audio)),
                ("ack",),
            ],
        )

    def test_single_render_is_held_for_the_second_interval(self):
        self.assertEqual(self.capture(29, 28), (False, False))
        self.assertEqual(
            [event for event in self.events if event[0] == "picture"], [("picture", b"\x0d"), ("picture", b"\x0d")]
        )

    def test_wrong_update_and_missing_pcm_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "Missing first presentation"):
            self.capture(33, 32)
        with self.assertRaisesRegex(ValueError, "Unexpected capture packet"):
            self.capture(29, 30, 32)
        with self.assertRaisesRegex(ValueError, "missing audio"):
            self.capture(29, 28, audio=b"")


if __name__ == "__main__":
    unittest.main()
