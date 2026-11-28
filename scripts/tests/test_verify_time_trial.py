import json
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
    first_adventure_checkpoint_diverged,
    replay_course,
    replay_metadata,
    verification_environment,
    verify,
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

    def test_legacy_scenery_retries_only_the_first_full_game_checkpoint(self):
        checkpoint = f"[adventure] tick=1800 clock=1454 hash={'a' * 64}\n"
        first_failure = "[time-trial] FAIL update=1800 Adventure input, progress or RNG diverged.\n"
        self.assertTrue(first_adventure_checkpoint_diverged(checkpoint + first_failure, "adventure-us80-7"))
        self.assertFalse(first_adventure_checkpoint_diverged(checkpoint + first_failure, "adventure-us80-6"))
        later_checkpoint = checkpoint.replace("1800", "3600")
        later_failure = first_failure.replace("1800", "3600")
        self.assertFalse(
            first_adventure_checkpoint_diverged(checkpoint + later_checkpoint + later_failure, "adventure-us80-7")
        )
        wrong_route = "[time-trial] FAIL update=1800 The first Trophy set did not match the selected category.\n"
        self.assertFalse(first_adventure_checkpoint_diverged(checkpoint + wrong_route, "adventure-us80-7"))

    def test_legacy_scenery_restarts_with_a_fresh_profile_and_the_same_replay(self):
        digest = "a" * 64
        checkpoint = [[1800, digest]]
        manifest = {
            "ruleset": "adventure-us80-7",
            "category": "all-trophy-races",
            "rom": 80,
            "splits": [["Finish", 1454]],
        }
        document = json.dumps(manifest).encode()
        evidence = b"DKRREPL2" + len(document).to_bytes(4, "little") + document
        initial = f"[adventure] tick=1800 clock=1454 hash={digest}\n"
        rejected = initial + "[time-trial] FAIL update=1800 Adventure input, progress or RNG diverged.\n"
        passed = (
            "[adventure] category=all-trophy-races\n"
            + initial
            + '[adventure] splits=[["Finish",1454]]\n'
            + "[time-trial] PASS update=1800 Adventure objective and gameplay checkpoints verified.\n"
        )

        accept_extended = True

        def execute(arguments, cwd, env, timeout, stdout, stderr):
            profile = Path(arguments[arguments.index("--config") + 1])
            self.assertFalse(profile.exists())
            self.assertEqual(Path(arguments[arguments.index("--verify-time-trial") + 1]).read_bytes(), evidence)
            logs = profile / "logs"
            logs.mkdir(parents=True)
            extended = "--verify-adventure-legacy-scenery" in arguments
            matched = extended and accept_extended
            (logs / "runtime.log").write_text(passed if matched else rejected, encoding="utf-8")
            return (0 if matched else 6), 0.0, None, False

        with TemporaryDirectory() as temp, patch("verify_time_trial.execute", side_effect=execute) as runner:
            root = Path(temp)
            runtime, rom, replay = (root / filename for filename in ("runtime", "rom", "replay"))
            runtime.touch()
            rom.touch()
            replay.write_bytes(evidence)
            result = verify(runtime, rom, replay, root / "result", 900, False)
            self.assertEqual(runner.call_count, 2)
            self.assertEqual(result["checkpoints"], checkpoint)
            accept_extended = False
            with self.assertRaises(InvalidReplay):
                verify(runtime, rom, replay, root / "invalid-result", 900, False)
            self.assertEqual(runner.call_count, 4)

    def test_invalid_replay_metadata(self):
        for evidence in (
            b"DKRREPL2",
            b"DKRREPL2" + (2).to_bytes(4, "little") + b"[]",
            b"DKRREPL2" + (5000).to_bytes(4, "little") + b"{}",
        ):
            with self.assertRaises(InvalidReplay):
                replay_metadata(evidence)
