#!/usr/bin/env python3
"""Private capture orchestration tests; no ROM or game-derived fixture data."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import capture_replay_fixture as capture


class ReplayCaptureTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='dkr capture tests ')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.runtime = self.root / 'runtime'
        self.runtime.write_text('synthetic executable')
        self.runtime.chmod(0o700)
        self.rom = self.root / 'synthetic.z64'
        self.rom.write_text('not a ROM')
        self.output = self.root / 'capture'
        self.original_hash = capture.file_hash
        self.hash_patch = patch.object(capture, 'file_hash', side_effect=self.hash_input)
        self.hash_patch.start()
        self.addCleanup(self.hash_patch.stop)

    def hash_input(self, path, algorithm='sha256'):
        if path == self.rom and algorithm == 'sha1':
            return next(iter(capture.ROM_REVISIONS))
        return self.original_hash(path, algorithm)

    def child(self, command, cwd, env, timeout, stdout, stderr):
        self.assertEqual(timeout, 90)
        self.assertEqual(command, [str(self.runtime), '--rom', str(self.rom), '--config',
                                   str(self.output / 'profile'), '--timeout', '60'])
        self.assertEqual({key for key in env if key.startswith('DKR_')}, {
            'DKR_REPLAY_CAPTURE_FILE', 'DKR_REPLAY_CAPTURE_MAP', 'DKR_REPLAY_CAPTURE_PLAYERS'})
        self.assertEqual(env['DKR_REPLAY_CAPTURE_PLAYERS'], '2')
        self.assertEqual(env['DKR_REPLAY_CAPTURE_MAP'], '3')
        Path(env['DKR_REPLAY_CAPTURE_FILE']).write_bytes(b'synthetic fixture')
        self.log = cwd / 'profile' / 'logs' / 'runtime.log'
        self.log.parent.mkdir(parents=True)
        self.log.write_text('[replay-probe][quiesced] revision=77 bytes=17 success=1\n')
        stdout.write_text('')
        stderr.write_text('')
        return 0, 100.0, '', False

    def run_capture(self, child=None):
        with patch.object(capture, 'execute', side_effect=child or self.child):
            return capture.capture(self.runtime, self.rom, self.output)

    def test_success_and_environment_isolation(self):
        with patch.dict(os.environ, {'DKR_WATER_TEST_MAP': '5', 'DKR_PROBE_REFERENCE_OUTPUT': 'unsafe'}):
            manifest = self.run_capture()
        metadata = json.loads(manifest.read_text())
        self.assertEqual(metadata['revision'], 77)
        self.assertEqual(metadata['fixture_bytes'], 17)
        self.assertEqual(metadata['fixture_sha256'], self.original_hash(self.output / 'race.dkr-component'))
        self.assertNotIn('synthetic fixture', manifest.read_text())

    def test_wrong_rom_rejected_before_writes(self):
        with patch.object(capture, 'file_hash', return_value='wrong'), patch.object(capture, 'execute') as child:
            with self.assertRaisesRegex(ValueError, 'SHA-1'):
                capture.capture(self.runtime, self.rom, self.output)
            child.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_missing_runtime_rejected_before_writes(self):
        self.runtime.unlink()
        with self.assertRaisesRegex(ValueError, 'executable'):
            self.run_capture()
        self.assertFalse(self.output.exists())

    def test_existing_output_preserved(self):
        self.output.mkdir()
        marker = self.output / 'keep'
        marker.write_text('unchanged')
        with self.assertRaises(FileExistsError):
            self.run_capture()
        self.assertEqual(marker.read_text(), 'unchanged')
        self.assertEqual(list(self.output.iterdir()), [marker])

    def assert_failed_child(self, mutate, message):
        def child(*args):
            result = self.child(*args)
            return mutate(result) or result
        with self.assertRaisesRegex(RuntimeError, message):
            self.run_capture(child)
        self.assertFalse((self.output / 'fixture.json').exists())
        self.assertTrue(self.log.exists())

    def test_missing_success_marker(self):
        self.assert_failed_child(lambda _: self.log.write_text('not a capture\n') and None, 'quiescence')

    def test_wrong_revision(self):
        self.assert_failed_child(lambda _: self.log.write_text(
            '[replay-probe][quiesced] revision=80 bytes=17 success=1\n') and None, 'revision 77')

    def test_truncated_fixture(self):
        self.assert_failed_child(lambda _: (self.output / 'race.dkr-component').write_bytes(b'x') and None, 'size')

    def test_nonzero_exit_despite_success_marker(self):
        self.assert_failed_child(lambda _: (5, 100, '', False), 'exited 5')

    def test_watchdog_failure_despite_success_marker(self):
        self.assert_failed_child(lambda _: (-15, 90000, 'Timed out after 90 seconds', False), 'Timed out')


if __name__ == '__main__':
    unittest.main()
