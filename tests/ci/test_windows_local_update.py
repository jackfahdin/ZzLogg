"""Local build helpers must select an MSVC kit and stage only owned inputs."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/dev/windows_local_update.py'


class WindowsLocalUpdateTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.is_file(), 'The local Windows build entry is missing')
        spec = importlib.util.spec_from_file_location('windows_local_update', SCRIPT)
        self.tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.tool)

    def make_kit(self, path):
        for file in ['bin/qmake.exe', 'bin/windeployqt.exe', 'lib/cmake/Qt6/Qt6Config.cmake']:
            item = path / file
            item.parent.mkdir(parents=True, exist_ok=True)
            item.write_text('fixture')

    def test_version_directory_resolves_msvc_not_mingw_or_arm(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ['msvc2022_64', 'mingw_64', 'msvc2022_arm64']:
                self.make_kit(root / name)
            self.assertEqual(self.tool.resolve_qt(root), root / 'msvc2022_64')
            self.assertEqual(self.tool.resolve_qt(root / 'msvc2022_64'), root / 'msvc2022_64')
            self.make_kit(root / 'msvc2026_64')
            with self.assertRaisesRegex(ValueError, 'multiple|Multiple'):
                self.tool.resolve_qt(root)

    def test_incomplete_kit_fails_before_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, 'MSVC|Qt'):
                self.tool.resolve_qt(Path(temporary))

    def test_packaging_workspace_preserves_repository_staging(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime = root / 'runtime'
            runtime.mkdir()
            for name in ['ZzLogg.exe', 'ZzLoggUpdate.exe', 'ZzLoggPortableUpdate.exe']:
                (runtime / name).write_bytes(b'app')
            tx = root / 'ZzLoggUpdateTx.exe'
            tx.write_bytes(b'transaction engine')
            workspace = root / 'package'
            self.tool.stage_installer(ROOT, runtime, tx, workspace)
            self.assertEqual((workspace / 'release/ZzLogg.exe').read_bytes(), b'app')
            self.assertEqual((workspace / 'txpayload/ZzLoggUpdateTx.exe').read_bytes(), b'transaction engine')
            self.assertFalse((workspace / 'release/ZzLoggUpdateTx.exe').exists())
            self.assertTrue((workspace / 'packaging/windows/InstallerStagingFiles.ps1').is_file())
            self.assertTrue((workspace / 'packaging/assets/ZzLogg.ico').is_file())
            # Reusing a nonempty stage must fail instead of retaining stale DLLs.
            (workspace / 'release/stale.dll').write_text('old')
            with self.assertRaises(FileExistsError):
                self.tool.stage_installer(ROOT, runtime, tx, workspace)

    def test_changed_package_is_not_served_under_old_build_record(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / 'setup.exe'
            package.write_bytes(b'packaged build')
            self.tool.write_build_record(root, package, '26.10.06')
            self.assertEqual(self.tool.verified_package(root, '26.10.06'), package)
            package.write_bytes(b'replaced build')
            with self.assertRaisesRegex(ValueError, 'changed|match'):
                self.tool.verified_package(root, '26.10.06')
            record = json.loads((root / 'build.json').read_text())
            record['package'] = '../outside.exe'
            (root / 'build.json').write_text(json.dumps(record))
            with self.assertRaises(ValueError):
                self.tool.verified_package(root, '26.10.06')
