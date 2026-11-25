import os
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_time_trial import (  # noqa: E402
    InvalidReplay,
    adventure_result,
    replay_course,
    replay_metadata,
    verification_environment,
)


class VerifyTimeTrialTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "Linux verifier environment")
    def test_home_is_writable_inside_a_protected_service(self):
        with (
            TemporaryDirectory() as temp,
            patch.dict(
                os.environ, {"HOME": "/inaccessible", "XDG_CONFIG_HOME": "/inaccessible/config", "DKR_TEST": "ignored"}
            ),
        ):
            home = Path(temp) / "home"
            env = verification_environment(Path(temp), xvfb=True)
            self.assertEqual(env["HOME"], str(home))
            for kind in ("CONFIG", "CACHE", "DATA", "STATE"):
                path = Path(env[f"XDG_{kind}_HOME"])
                self.assertTrue(path.is_relative_to(home))
                self.assertTrue(path.is_dir())
            self.assertEqual(Path(env["XDG_RUNTIME_DIR"]).stat().st_mode & 0o777, 0o700)
            self.assertNotIn("DKR_TEST", env)
            self.assertEqual(env["LP_NUM_THREADS"], "4")

    def test_expanded_course_logs(self):
        label = "[time-trial] Fossil Canyon / Diddy / Car / US v1.1\n"
        target = "[time-trial] course=fossil-canyon vehicle=car\n"
        expected = {"course": "fossil-canyon", "vehicle": "car"}
        self.assertEqual(replay_course(target + label), expected)
        self.assertEqual(replay_course(label), expected)
        with self.assertRaises(ValueError):
            replay_course(target.replace("vehicle=car", "vehicle=plane") + label)

    def test_adventure_proof_requires_the_recorded_objective(self):
        digest = "0123456789abcdef" * 4
        start = "[adventure] category=all-trophy-races\n"
        progress = f"[adventure] tick=42 clock=26 hash={digest}\n"
        finished = "[time-trial] PASS update=42 Adventure objective and gameplay checkpoints verified.\n"
        manifest = {"category": "all-trophy-races"}
        self.assertEqual(
            adventure_result(start + progress + finished, manifest),
            {
                "category": "all-trophy-races",
                "clockTicks": 26,
                "updates": 42,
                "checkpoints": [[42, digest]],
                "finalCheckpoint": digest,
            },
        )
        with self.assertRaises(ValueError):
            adventure_result(start + progress, manifest)
        with self.assertRaises(ValueError):
            adventure_result(start + progress + finished, {"category": "any-percent"})
        with_splits = {**manifest, "ruleset": "adventure-us80-5", "splits": [["Overworld", 2], ["Finish", 26]]}
        line = '[adventure] splits=[["Overworld",2],["Finish",26]]\n'
        self.assertEqual(
            adventure_result(start + progress + line + finished, with_splits)["splits"], with_splits["splits"]
        )
        with self.assertRaises(ValueError):
            adventure_result(start + progress + line.replace("26", "24") + finished, with_splits)

    def test_invalid_replay_metadata(self):
        for evidence in (
            b"DKRREPL2",
            b"DKRREPL2" + (2).to_bytes(4, "little") + b"[]",
            b"DKRREPL2" + (5000).to_bytes(4, "little") + b"{}",
        ):
            with self.assertRaises(InvalidReplay):
                replay_metadata(evidence)
