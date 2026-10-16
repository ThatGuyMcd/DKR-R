"""Fixture-free regression checks for developer command-line utilities."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
try:
    import tomllib
except ImportError:  # Python 3.10 remains usable for the other utility checks.
    tomllib = None
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ToolingTests(unittest.TestCase):
    @unittest.skipIf(tomllib is None, "TOML parser roundtrip requires Python 3.11+")
    def test_toml_control_roundtrip(self):
        module = load('scripts/generate_recomp_config.py')
        value = '"\\\n\r\t' + ''.join(chr(c) for c in range(32)) + chr(127) + ' café'
        self.assertEqual(tomllib.loads('value = ' + module.toml_string(value))['value'], value)

    def test_shader_wrapper_preserves_exit_and_arguments(self):
        module = load('scripts/run_host_shader_tool.py')
        with mock.patch.object(module.subprocess, 'call', return_value=23) as call:
            self.assertEqual(module.main([sys.executable, '/target/file_to_c', '--flag', 'a b']), 23)
            self.assertEqual(call.call_args.args[0][1:], ['--flag', 'a b'])
        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(module.main(['/missing/file_to_c', 'file_to_c']), 127)
        self.assertIn('Build the host file_to_c', err.getvalue())

    def test_fragment_generation_failure_does_not_create_output(self):
        module = load('scripts/prepare_legacy_character_menu_fragment.py')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'fragment.json'
            with mock.patch.object(module, 'prepare', side_effect=ValueError('bad ELF')):
                with self.assertRaises(ValueError):
                    module.main(['--elf', 'missing', '--revision', next(iter(module.ELFS)), '--output', str(output)])
            self.assertFalse(output.exists())

    def test_qualification_preflight_before_generation(self):
        module = load('scripts/generate_legacy_menu_qualification.py')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'out'
            args = ['--output', str(output), '--v77-build', directory, '--v80-build', directory,
                    '--recompiler', sys.executable]
            for extra in (['--character-menu'], ['--refresh-presentation'], []):
                with mock.patch.object(module.subprocess, 'run') as run, contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        module.main(args + extra)
                    run.assert_not_called()
                    self.assertFalse(output.exists())

    def test_qualification_checks_second_revision_before_starting(self):
        module = load('scripts/generate_legacy_menu_qualification.py')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'dkr.us.v77.elf').write_bytes(b'fixture')
            (root/'dkr.us.v77.z64').write_bytes(b'fixture')
            output = root/'out'
            with mock.patch.object(module.subprocess, 'run') as run, contextlib.redirect_stderr(io.StringIO()) as err:
                with self.assertRaises(SystemExit):
                    module.main(['--output', str(output), '--v77-build', directory,
                                 '--v80-build', directory, '--recompiler', sys.executable])
                self.assertIn('Missing v80 input', err.getvalue())
                run.assert_not_called()
                self.assertFalse(output.exists())

    def test_qualification_stage_failure_is_concise(self):
        module = load('scripts/generate_legacy_menu_qualification.py')
        with mock.patch.object(module.subprocess, 'run', side_effect=subprocess.CalledProcessError(9, ['tool'])):
            with self.assertRaisesRegex(RuntimeError, 'test stage failed.*exit 9'):
                module.run_stage(['tool'], 'test stage')

    def test_blender_invocation_and_skip_status(self):
        module = load('tools/blender/run_tests.py')
        self.assertEqual(Path(module.REPO_ROOT), ROOT)
        with mock.patch.object(module, 'PLAIN_SUITES', ['plain.py']), mock.patch.object(module, 'BLENDER_SUITES', ['blend.py']), \
             mock.patch.object(module, 'find_blender', return_value='/blender'), \
             mock.patch.object(module, 'run', return_value=77) as run, contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(module.main([]), 0)
            self.assertIn('SKIP  plain.py', out.getvalue())
            self.assertIn('SKIP  blend.py', out.getvalue())
            self.assertIn('--python-exit-code', run.call_args.args[1])
        with mock.patch.object(module, 'run') as run, contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                module.main(['--blender', '/missing/blender'])
            run.assert_not_called()

    def test_discovery_defaults(self):
        for name, variable in [('legacy_behaviour_pipeline_tests.py', 'INPUTS'), ('legacy_memory_pipeline_tests.py', 'ELF_INPUTS')]:
            self.assertEqual(getattr(load('runtime-recomp/tests/' + name), variable), [])


if __name__ == '__main__':
    unittest.main()
