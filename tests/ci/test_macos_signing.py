"""Exercise signing traversal and failure handling using an external codesign fixture."""
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.name == 'posix', 'executable fixture requires POSIX')
class MacSigningTests(unittest.TestCase):
    def sign(self, fail=False, missing_framework=False):
        with tempfile.TemporaryDirectory(prefix='zzlogg signing ') as directory:
            root = Path(directory)
            app = root / 'ZzLogg.app'
            contents = app / 'Contents'
            contents.mkdir(parents=True)
            (contents / 'Info.plist').write_bytes(plistlib.dumps({'SUFeedURL': 'https://example.test/feed.xml'}))
            framework = contents / 'Frameworks/Sparkle.framework'
            if not missing_framework:
                for binary in ['Sparkle', 'Autoupdate', 'Updater.app/Contents/MacOS/Updater',
                               'XPCServices/Downloader.xpc/Contents/MacOS/Downloader',
                               'XPCServices/Installer.xpc/Contents/MacOS/Installer']:
                    target = framework / 'Versions/B' / binary
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(b'\xcf\xfa\xed\xfe' + b'fixture')
                (framework / 'Versions/Current').symlink_to('B')
                (framework / 'Sparkle').symlink_to('Versions/Current/Sparkle')
            binary = contents / 'MacOS/ZzLogg'
            binary.parent.mkdir()
            binary.write_bytes(b'\xcf\xfa\xed\xfe' + b'fixture')
            log = root / 'calls.jsonl'
            tool = root / 'codesign'
            tool.write_text('#!' + sys.executable + '\n' + '''import json, os, sys
with open(os.environ['SIGN_LOG'], 'a') as f:
    f.write(json.dumps(sys.argv[1:]) + '\\n')
if os.environ.get('SIGN_FAIL') == '1': sys.exit(23)
''')
            tool.chmod(0o755)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                       SIGN_LOG=str(log), SIGN_FAIL='1' if fail else '0')
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/ci/sign_macos_bundle.py'),
                                     '--app', str(app), '--identity', 'Developer ID: Test'],
                                    env=env, capture_output=True, text=True)
            calls = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
            return result, calls, str(app)

    def test_nested_helpers_signed_before_enclosing_bundles(self):
        result, calls, app = self.sign()
        self.assertEqual(result.returncode, 0, result.stderr)
        signed = [call[-1] for call in calls if '--sign' in call]
        framework = app + '/Contents/Frameworks/Sparkle.framework'
        self.assertLess(signed.index(framework + '/Versions/B/Autoupdate'), signed.index(framework))
        self.assertLess(signed.index(framework + '/Versions/B/Updater.app'), signed.index(framework))
        self.assertLess(signed.index(framework + '/Versions/B/XPCServices/Downloader.xpc'), signed.index(framework))
        self.assertLess(signed.index(framework + '/Versions/B/XPCServices/Installer.xpc'), signed.index(framework))
        self.assertEqual(signed[-1], app)
        self.assertEqual(len(signed), len(set(signed)))
        self.assertEqual(calls[-1], ['--verify', '--deep', '--strict', '--verbose=2', app])
        for call in calls[:-1]:
            self.assertNotIn('--deep', call)
            self.assertIn('--timestamp', call)
            self.assertIn('--preserve-metadata=entitlements', call)

    def test_failed_nested_signature_stops_packaging(self):
        result, calls, _ = self.sign(fail=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(calls), 1)

    def test_official_feed_cannot_ship_without_framework(self):
        result, calls, _ = self.sign(missing_framework=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])


if __name__ == '__main__':
    unittest.main()
