"""Exercise archive publication with synthetic source trees only."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PackagingTests(unittest.TestCase):
    def test_new_source_modules_and_excluded_source_diagnostic(self):
        module = load('scripts/package_source.py')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = [Path(p) for p in ('runtime-recomp/src/new/module.cpp', 'runtime-recomp/include/new.hpp',
                                      'tools/blender/track_lab/new.py', 'downloads/new.py', 'tools/blender/local.zip')]
            for path in paths:
                (root/path).parent.mkdir(parents=True, exist_ok=True)
                (root/path).write_text('synthetic')
            with mock.patch.object(module, 'ROOT', root), mock.patch.object(module, 'git_paths', side_effect=[[], paths]), \
                 contextlib.redirect_stderr(io.StringIO()) as err:
                self.assertEqual(set(module.source_files()), set(paths[:3]))
            self.assertIn(str(Path('downloads/new.py')), err.getvalue())

    def test_source_success_is_deterministic_and_no_clobber(self):
        module = load('scripts/package_source.py')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'module.py').write_text('value = 1\n')
            with mock.patch.object(module, 'ROOT', root), mock.patch.object(module, 'source_files', return_value=[Path('module.py')]), \
                 contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(module.main([str(root/'one.zip')]), 0)
                self.assertEqual(module.main([str(root/'two.zip')]), 0)
                self.assertEqual((root/'one.zip').read_bytes(), (root/'two.zip').read_bytes())
                self.assertEqual(module.main([str(root/'one.zip')]), 2)

    def test_source_validation_failure_leaves_no_artifact(self):
        module = load('scripts/package_source.py')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'bad.py').write_bytes(b'\x80\x37\x12\x40')
            with mock.patch.object(module, 'ROOT', root), mock.patch.object(module, 'source_files', return_value=[Path('bad.py')]), \
                 contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(module.main([str(root/'out.zip')]), 2)
            self.assertEqual(sorted(p.name for p in root.iterdir()), ['bad.py'])

    def test_source_publish_race_preserves_existing_artifact(self):
        module = load('scripts/package_source.py')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root/'out.zip'
            def validate(path):
                output.write_bytes(b'concurrent archive')
            with mock.patch.object(module, 'create_archive'), mock.patch.object(module, 'validate_archive', side_effect=validate), \
                 contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(module.main([str(output)]), 2)
            self.assertEqual(output.read_bytes(), b'concurrent archive')
            self.assertEqual(list(root.iterdir()), [output])

    def test_addon_reproducible_and_failure_preserves_previous(self):
        module = load('tools/blender/package_addon.py')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root/'package'
            (package/'data').mkdir(parents=True)
            (package/'data/catalog.json').write_text('{}')
            (package/'module.py').write_text('x = 1\n')
            output = root/'out'
            with mock.patch.object(module, 'PACKAGE_DIR', str(package)), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(module.main(['--output', str(output)]), 0)
                archive = output/'track_lab.zip'
                first = archive.read_bytes()
                self.assertEqual(module.main(['--output', str(output)]), 0)
                self.assertEqual(first, archive.read_bytes())
                with zipfile.ZipFile(archive) as zipped:
                    self.assertTrue(all(e.date_time == (1980,1,1,0,0,0) and e.external_attr >> 16 == 0o100644 for e in zipped.infolist()))
                with mock.patch.object(module, 'iter_files', side_effect=OSError('read failed')), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(module.main(['--output', str(output)]), 1)
                self.assertEqual(first, archive.read_bytes())
                self.assertEqual(list(output.iterdir()), [archive])


if __name__ == '__main__':
    unittest.main()
