"""The local fixture remains installer-only and uses an isolated test key."""
import base64
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import subprocess
import time
import socket
import urllib.request
import unittest
from unittest.mock import patch

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('local_update_feed', ROOT / 'scripts/dev/local_update_feed.py')
local_feed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(local_feed)


class LocalUpdateFeedTests(unittest.TestCase):
    def test_busy_port_does_not_launch_client(self):
        with tempfile.TemporaryDirectory() as temporary, socket.socket() as listener:
            root = Path(temporary)
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            package = root / 'setup.exe'
            package.write_bytes(b'fixture')
            marker = root / 'launched'
            child = root / 'client.py'
            child.write_text('from pathlib import Path\nimport sys\nPath(sys.argv[1]).touch()\n')
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/dev/local_update_feed.py'),
                '--installer', str(package), '--version', '26.10.06',
                '--port', str(listener.getsockname()[1]), '--workspace', str(root / 'feed'),
                '--launch', sys.executable, '--launch-args', str(child), str(marker)],
                env=dict(os.environ, HOME=str(root), USERPROFILE=str(root)),
                capture_output=True, text=True, timeout=8)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(marker.exists())

    def test_server_launches_client_with_test_environment_and_serves_exact_package(self):
        # Detect a launcher missing the Test environment or serving the wrong
        # directory. The child is real Python, not a mocked Popen call.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / 'setup.exe'
            package.write_bytes(b'actual local package bytes')
            report = root / 'child.json'
            child = root / 'client.py'
            child.write_text('import os, json, sys\nfrom pathlib import Path\n'
                'temporary = Path(sys.argv[1]+".tmp")\n'
                'temporary.write_text(json.dumps({"url":os.environ.get("ZZLOGG_UPDATE_FEED_URL"),'
                '"key":os.environ.get("ZZLOGG_UPDATE_FEED_KEY"),"args":sys.argv[2:]}))\n'
                'temporary.replace(sys.argv[1])\n')
            with socket.socket() as listener:
                listener.bind(('127.0.0.1', 0))
                port = listener.getsockname()[1]
            environment = dict(os.environ, HOME=str(root), USERPROFILE=str(root))
            command = [sys.executable, str(ROOT / 'scripts/dev/local_update_feed.py'),
                '--installer', str(package), '--version', '26.10.06', '--port', str(port),
                '--workspace', str(root / 'feed'), '--launch', sys.executable,
                '--launch-args', str(child), str(report), '--data-dir', str(root / 'data')]
            with (root / 'server.log').open('w+') as log:
                process = subprocess.Popen(command, env=environment, stdout=log, stderr=log)
                try:
                    deadline = time.monotonic() + 8
                    while not report.exists() and process.poll() is None and time.monotonic() < deadline:
                        time.sleep(.05)
                    log.flush()
                    log.seek(0)
                    self.assertTrue(report.exists(), log.read())
                    result = json.loads(report.read_text())
                    self.assertEqual(result['url'], f'http://127.0.0.1:{port}/stable.json')
                    self.assertTrue(result['key'].startswith('zzlogg-update-local:'))
                    self.assertEqual(result['args'], ['--data-dir', str(root / 'data')])
                    with urllib.request.urlopen(result['url'], timeout=3) as response:
                        envelope = json.load(response)
                    raw = base64.b64decode(envelope['payload'])
                    Ed25519PublicKey.from_public_bytes(bytes.fromhex(result['key'].split(':')[1])).verify(
                        base64.b64decode(envelope['signature']),
                        b'ZzLogg update manifest v1\nzzlogg-update-local\n' + raw)
                    payload = json.loads(raw)
                    self.assertEqual(payload['artifacts'][0]['url'],
                        f'http://127.0.0.1:{port}/ZzLogg-26.10.06-x64-Qt6-setup.exe')
                    with urllib.request.urlopen(payload['artifacts'][0]['url'], timeout=3) as response:
                        self.assertEqual(response.read(), package.read_bytes())
                finally:
                    process.terminate()
                    process.wait(timeout=5)

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
