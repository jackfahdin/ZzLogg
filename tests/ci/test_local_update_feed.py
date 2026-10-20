"""The local fixture remains installer-only and uses an isolated test key."""
import base64
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('local_update_feed', ROOT / 'scripts/dev/local_update_feed.py')
local_feed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(local_feed)


class LocalUpdateFeedTests(unittest.TestCase):
    def test_single_installer_fixture_can_be_regenerated_without_fake_platform_packages(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            installer = directory / 'setup.exe'
            installer.write_bytes(b'local installer fixture')
            workspace = directory / 'feed'
            key_dir = directory / 'keys'
            with patch.object(local_feed, 'WORKSPACE', workspace), \
                    patch.object(local_feed, 'KEY_DIR', key_dir), \
                    patch.object(local_feed, 'KEY_FILE', key_dir / 'test.pem'), \
                    patch.object(sys, 'argv', ['local_update_feed.py', '--installer', str(installer),
                                               '--version', '26.10.05', '--no-serve']), \
                    contextlib.redirect_stdout(io.StringIO()):
                for _ in range(2):
                    local_feed.main()
                public = local_feed.ensure_key()
            envelope = json.loads((workspace / 'stable.json').read_bytes())
            payload_bytes = base64.b64decode(envelope['payload'])
            Ed25519PublicKey.from_public_bytes(bytes.fromhex(public)).verify(
                base64.b64decode(envelope['signature']),
                b'ZzLogg update manifest v1\nzzlogg-update-local\n' + payload_bytes)
            payload = json.loads(payload_bytes)
            self.assertEqual(payload['version'], '26.10.05')
            self.assertEqual(payload['releaseSequence'], '261005')
            self.assertEqual(len(payload['artifacts']), 1)
            self.assertEqual(payload['artifacts'][0]['format'], 'nsis-exe')
            self.assertTrue(payload['artifacts'][0]['url'].startswith('http://127.0.0.1:8765/'))
            self.assertEqual(list(workspace.glob('*.xml')), [])
            self.assertEqual(list(workspace.glob('*.dmg')), [])
