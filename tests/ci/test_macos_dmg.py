"""Exercise the real packager with simulated external CPack/hdiutil commands."""
import contextlib
import io
import os
from pathlib import Path
import sys
import tempfile
import textwrap
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
from package_macos_dmg import package


@unittest.skipUnless(os.name == 'posix', 'fake executables require POSIX')
class MacDmgTests(unittest.TestCase):
    def run_packaging(self, scenario, attempts, success):
        with tempfile.TemporaryDirectory(prefix='zzlogg dmg ') as directory:
            root = Path(directory)
            command = '#!' + sys.executable + '\n' + textwrap.dedent('''\
                import os
                from pathlib import Path
                import sys

                scenario = os.environ['DMG_TEST_SCENARIO']
                if Path(sys.argv[0]).name == 'cpack':
                    args = sys.argv[1:]
                    assert args[:3] == ['--verbose', '-G', 'DragNDrop']
                    # Both options must be empty to prevent CPack's remount.
                    assert 'CPACK_PACKAGE_ICON=' in args
                    assert 'CPACK_DMG_DS_STORE_SETUP_SCRIPT=' in args
                    staging = Path(args[args.index('-B') + 1])
                    with Path('attempts').open('a') as stream:
                        stream.write(str(staging) + '\\n')
                    attempt = len(Path('attempts').read_text().splitlines())
                    prefix = next((arg.split('=', 1)[1] for arg in args
                                   if arg.startswith('CPACK_OUTPUT_FILE_PREFIX=')), 'packages')
                    Path(prefix).mkdir(exist_ok=True)
                    image = Path(prefix) / 'ZzLogg-26.09.01-OSX.dmg'
                    if scenario == 'missing_image':
                        sys.exit(0)
                    if scenario == 'empty_image':
                        image.touch()
                        sys.exit(0)
                    if scenario in ('first_success', 'verify_fails') or (attempt == 2 and scenario == 'create_busy'):
                        image.write_bytes(b'simulated disk image')
                        sys.exit(0)
                    # Failed CPack may leave a partial file. Never publish it.
                    image.write_bytes(b'partial image')
                    if scenario in ('create_busy', 'always_busy', 'info_fails'):
                        print('CPack Error: Error generating temporary disk image.')
                        print('hdiutil: create failed - Resource busy')
                    elif scenario == 'unrelated_busy':
                        print('codesign: Resource busy')
                    else:
                        print('CPack Error: No space left on device')
                    sys.exit(7)
                if sys.argv[1:3] == ['info', '-plist']:
                    print('<?xml version="1.0"?><plist><dict><key>images</key><array/></dict></plist>')
                    sys.exit(8 if scenario == 'info_fails' else 0)
                assert sys.argv[1] == 'verify'
                assert Path(sys.argv[2]).read_bytes() == b'simulated disk image'
                Path('verified').touch()
                sys.exit(9 if scenario == 'verify_fails' else 0)
            ''')
            for name in ('cpack', 'hdiutil'):
                path = root / name
                path.write_text(command)
                path.chmod(0o755)
            env = {'PATH': directory + os.pathsep + os.environ['PATH'],
                   'DMG_TEST_SCENARIO': scenario}
            with patch.dict(os.environ, env), patch('package_macos_dmg.sleep') as sleep:
                with contextlib.redirect_stdout(io.StringIO()):
                    if scenario in ('missing_image', 'empty_image'):
                        with self.assertRaises(ValueError):
                            package(root, '26.09.01')
                        status = 1
                    else:
                        status = package(root, '26.09.01')
            self.assertEqual(status == 0, success)
            directories = (root / 'attempts').read_text().splitlines()
            self.assertEqual(len(directories), attempts)
            self.assertEqual(len(set(directories)), attempts)
            self.assertEqual(sleep.call_count, attempts - 1)
            destination = root / 'packages/ZzLogg-26.09.01-OSX.dmg'
            self.assertEqual(destination.exists(), success)
            if success:
                self.assertTrue((root / 'verified').exists())
                self.assertEqual(destination.read_bytes(), b'simulated disk image')
            return status

    def test_create_resource_busy_retries_with_fresh_directory(self):
        self.run_packaging('create_busy', 2, True)

    def test_first_attempt_succeeds(self):
        self.run_packaging('first_success', 1, True)

    def test_persistent_busy_stops_after_three_attempts(self):
        self.assertEqual(self.run_packaging('always_busy', 3, False), 7)

    def test_unrelated_cpack_error_is_not_retried(self):
        self.run_packaging('unrelated_error', 1, False)

    def test_other_resource_busy_error_is_not_retried(self):
        self.run_packaging('unrelated_busy', 1, False)

    def test_diagnostic_failure_cannot_hide_packaging_failure(self):
        self.assertEqual(self.run_packaging('info_fails', 3, False), 7)

    def test_failed_verification_cannot_publish_image(self):
        self.assertEqual(self.run_packaging('verify_fails', 1, False), 9)

    def test_success_without_image_is_rejected(self):
        self.run_packaging('missing_image', 1, False)

    def test_empty_image_is_rejected(self):
        self.run_packaging('empty_image', 1, False)

    def test_existing_package_is_not_reused(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'packages/ZzLogg-26.09.01-OSX.dmg'
            target.parent.mkdir()
            target.write_bytes(b'old package')
            with self.assertRaises(ValueError):
                package(directory, '26.09.01')
            self.assertEqual(target.read_bytes(), b'old package')


if __name__ == '__main__':
    unittest.main()
