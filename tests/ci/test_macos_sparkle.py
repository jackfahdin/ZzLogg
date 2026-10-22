"""Configure real bundle metadata; do not require the macOS SDK or download Sparkle."""
import base64
import json
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SparkleMetadataTests(unittest.TestCase):
    def configure(self, official=True, arch='x86_64', channel='stable', sequence='261001', feed=True,
                  include_prefix=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / 'configure.cmake'
            script.write_text(f'''
include("{ROOT}/cmake/ZzSparkle.cmake")
set(ZZLOGG_OFFICIAL_RELEASE {'ON' if official else 'OFF'})
set(ZZLOGG_ENABLE_GITHUB_FEED {'ON' if feed else 'OFF'})
set(CMAKE_OSX_ARCHITECTURES "{arch}")
set(CMAKE_SYSTEM_PROCESSOR "{arch}")
set(ZZLOGG_RELEASE_CHANNEL "{channel}")
set(ZZLOGG_DISPLAY_VERSION "26.10.01")
set(ZZLOGG_RELEASE_SEQUENCE "{sequence}")
set(MACOSX_BUNDLE_EXECUTABLE_NAME ZzLogg)
zzlogg_sparkle_bundle_metadata()
file(WRITE "{root}/FeedPrefix.txt" "${{ZZLOGG_SPARKLE_FEED_PREFIX}}")
configure_file("{ROOT}/cmake/MacOSXBundleInfo.plist.in" "{root}/Info.plist")
''')
            result = subprocess.run(['cmake', '-P', str(script)], capture_output=True, text=True)
            if result.returncode:
                return result, None
            plist = plistlib.loads((root / 'Info.plist').read_bytes())
            if include_prefix:
                return result, plist, (root / 'FeedPrefix.txt').read_text()
            return result, plist

    def test_runtime_feed_prefix_keeps_architecture_independent_of_build_channel(self):
        for arch, feed_arch in [('x86_64', 'x64'), ('arm64', 'arm64')]:
            for channel in ['stable', 'preview']:
                with self.subTest(arch=arch, channel=channel):
                    result, _, prefix = self.configure(arch=arch, channel=channel, include_prefix=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(prefix,
                        f'https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-{feed_arch}-')

    def test_official_feed_matches_architecture_and_channel(self):
        for arch, feed_arch in [('x86_64', 'x64'), ('arm64', 'arm64')]:
            for channel in ['stable', 'preview']:
                with self.subTest(arch=arch, channel=channel):
                    result, plist = self.configure(arch=arch, channel=channel)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(plist['SUFeedURL'],
                        f'https://raw.githubusercontent.com/jackfahdin/ZzLogg/update-feed/macos-{feed_arch}-{channel}.xml')
                    key = json.loads((ROOT / 'packaging/update/github-public-key.json').read_text())
                    self.assertEqual(base64.b64decode(plist['SUPublicEDKey']).hex(), key['publicKey'])
                    self.assertEqual(plist['CFBundleVersion'], '261001')
                    self.assertEqual(plist['CFBundleShortVersionString'], '26.10.01')
                    self.assertFalse(plist['SUEnableAutomaticChecks'])
                    self.assertFalse(plist['SUAutomaticallyUpdate'])

    def test_preview_build_sequence_is_independent_of_display_version(self):
        result, plist = self.configure(channel='preview', sequence='98765432101')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(plist['CFBundleVersion'], '98765432101')
        self.assertEqual(plist['CFBundleShortVersionString'], '26.10.01')

    def test_missing_sequence_derives_stable_release_number(self):
        result, plist = self.configure(sequence='')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(plist['CFBundleVersion'], '261001')

    def test_development_bundle_does_not_enable_online_updates(self):
        result, plist = self.configure(official=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('SUFeedURL', plist)
        self.assertNotIn('SUPublicEDKey', plist)

    def test_official_build_honors_disabled_feed_toggle(self):
        result, plist = self.configure(official=True, feed=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('SUFeedURL', plist)
        self.assertNotIn('SUPublicEDKey', plist)
        self.assertEqual(plist['CFBundleVersion'], '261001')

    def test_invalid_identity_cannot_get_a_feed(self):
        for arch, channel in [('i386', 'stable'), ('arm64;x86_64', 'stable'), ('arm64', 'nightly')]:
            result, plist = self.configure(arch=arch, channel=channel)
            self.assertNotEqual(result.returncode, 0)
            self.assertIsNone(plist)


if __name__ == '__main__':
    unittest.main()
