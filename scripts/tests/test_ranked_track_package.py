import hashlib
from io import BytesIO
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "services" / "online-directory"))
from ranked_track_package import origin, retrieve_track, unpack_track  # noqa: E402


class ArchiveServer:
    def __init__(self, data):
        self.data = data

    def open(self, url, timeout):
        return BytesIO(self.data)


class TrackPackageTests(unittest.TestCase):
    def archive(self, path, extra=()):
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("Wrapper/race.dkrmap/manifest.json", json.dumps({"id": "race"}))
            archive.writestr("Wrapper/race.dkrmap/header.bin", b"native header")
            archive.writestr("Wrapper/optional-hd.zip", b"unrelated")
            for name, payload in extra:
                archive.writestr(name, payload)
        return path.read_bytes()

    def test_retrieve_checks_the_entire_release_before_unpack(self):
        with TemporaryDirectory() as temp:
            root = Path(temp)
            archive = self.archive(root / "fixture.zip")
            qualified = {
                "url": "https://example.test/store/race.zip",
                "sha256": hashlib.sha256(archive).hexdigest(),
                "bytes": len(archive),
                "trackId": "race",
                "trackHash": "b" * 64,
            }
            host = origin("https://example.test")
            for name, descriptor, body in (
                ("wrong-origin", {**qualified, "url": "https://other.test/race.zip"}, archive),
                ("changed", qualified, archive[:-1] + bytes([archive[-1] ^ 1])),
                ("size", qualified, archive + b"extra"),
            ):
                job = root / name
                job.mkdir()
                with self.assertRaises(ValueError):
                    retrieve_track(descriptor, job, ArchiveServer(body), host)
                self.assertFalse((job / "qualified.dkrmap").exists())
            job = root / "valid"
            job.mkdir()
            track = retrieve_track(qualified, job, ArchiveServer(archive), host)
            self.assertEqual((track / "header.bin").read_bytes(), b"native header")
            self.assertEqual(len(list(track.iterdir())), 2)

    def test_rejects_traversal_and_ambiguous_track(self):
        with TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "fixture.zip"
            for entry in (
                ("Wrapper/race.dkrmap/../outside", b"bad"),
                ("Wrapper/race.dkrmap/Header.bin", b"alias"),
                ("Other/second.dkrmap/manifest.json", b'{"id":"race"}'),
            ):
                self.archive(archive, (entry,))
                with self.assertRaises(ValueError):
                    unpack_track(archive, root / "output", "race")
                self.assertFalse((root / "output").exists())
