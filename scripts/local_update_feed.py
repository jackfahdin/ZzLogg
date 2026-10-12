#!/usr/bin/env python3
"""One-command local update feed for testing the in-app updater without CI.

Builds a Test-environment signed manifest for a locally produced installer and
serves it over loopback http. Use with a build compiled with
-DZZLOGG_UPDATE_LOCAL_FEED=ON, then start ZzLogg with these environment
variables set (the tool prints them again when serving):

  ZZLOGG_UPDATE_FEED_URL=http://127.0.0.1:<port>/stable.json
  ZZLOGG_UPDATE_FEED_KEY=<printed keyId:publicKey>
  ZZLOGG_UPDATE_FEED_HOSTS=127.0.0.1;localhost

The key pair is generated once into ~/.zzlogg-local-update/ (outside the
checkout, owner-readable only). The feed is valid for 7 days per invocation.

Usage:
  python scripts/local_update_feed.py --installer <setup.exe> --version 26.10.05
"""
import argparse
import base64
import functools
import hashlib
import http.server
import json
import os
import socketserver
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
KEY_DIR = Path.home() / '.zzlogg-local-update'
KEY_FILE = KEY_DIR / 'feed-key.pem'
KEY_ID = 'zzlogg-update-local'
WORKSPACE = REPO_ROOT / 'out' / 'local-feed'


def ensure_key():
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    if KEY_FILE.exists():
        key = serialization.load_pem_private_key(KEY_FILE.read_bytes(), password=None)
    else:
        KEY_DIR.mkdir(parents=True, exist_ok=True)
        key = Ed25519PrivateKey.generate()
        pem = key.private_bytes(serialization.Encoding.PEM,
                                serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption())
        fd = os.open(KEY_FILE, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(pem)
    public = key.public_key().public_bytes_raw().hex()
    return public


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installer', required=True, help='Locally built ZzLogg-*-setup.exe')
    parser.add_argument('--version', required=True, help='YY.MM.PP, must be newer than the installed build')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--no-serve', action='store_true', help='Sign only, do not start the http server')
    args = parser.parse_args()

    installer = Path(args.installer).resolve()
    if not installer.is_file():
        parser.error(f'installer not found: {installer}')
    public = ensure_key()

    WORKSPACE.mkdir(parents=True, exist_ok=True)
    # The signer pins the release asset naming convention.
    staged_installer = WORKSPACE / f'ZzLogg-{args.version}-windows-x64-setup.exe'
    staged_installer.write_bytes(installer.read_bytes())
    digest = hashlib.sha256(staged_installer.read_bytes()).hexdigest()
    size = staged_installer.stat().st_size

    release_info = WORKSPACE / f'release-info-{args.version}.json'
    release_info.write_text(json.dumps({
        'schema': 1, 'version': args.version, 'artifact_label': args.version,
        'source_commit': '0' * 40, 'tag': f'v{args.version}', 'workflow_run': 'local',
        'assets': [{'name': staged_installer.name, 'size': size, 'sha256': digest}],
    }, indent=2), encoding='utf-8')

    notes = WORKSPACE / 'notes.json'
    notes.write_text(json.dumps({'en': 'Local test update', 'zh_CN': '本地测试更新',
                                 'zh_TW': '本機測試更新'}, ensure_ascii=False), encoding='utf-8')

    sequence = args.version.replace('.', '')
    metadata_sequence = str(int(time.time() * 1000))
    signed = subprocess.run(
        [sys.executable, str(REPO_ROOT / 'scripts' / 'ci' / 'sign_update_manifest.py'),
         '--release-info', str(release_info), '--repository', 'local/feed',
         '--channel', 'stable', '--metadata-sequence', metadata_sequence,
         '--release-sequence', sequence, '--issued-at', str(int(time.time())),
         '--ttl-seconds', str(7 * 86400), '--min-data-schema', '0', '--max-data-schema', '0',
         '--min-os-version', '10.0.19041', '--notes-file', str(notes),
         '--key-id', KEY_ID, '--private-key-file', str(KEY_FILE),
         '--artifact-base-url', f'http://127.0.0.1:{args.port}',
         '--output-dir', str(WORKSPACE)],
        cwd=REPO_ROOT / 'scripts' / 'ci', check=True, capture_output=True, text=True)
    print(signed.stdout.strip())

    signed_feed = WORKSPACE / 'update-stable.json'
    feed = WORKSPACE / 'stable.json'
    if feed.exists():
        feed.unlink()
    signed_feed.replace(feed)

    print()
    print('Start ZzLogg with:')
    print(f'  ZZLOGG_UPDATE_FEED_URL=http://127.0.0.1:{args.port}/stable.json')
    print(f'  ZZLOGG_UPDATE_FEED_KEY={KEY_ID}:{public}')
    print('  ZZLOGG_UPDATE_FEED_HOSTS=127.0.0.1;localhost')
    print()
    if args.no_serve:
        return
    print(f'Serving {WORKSPACE} on http://127.0.0.1:{args.port} (Ctrl+C to stop)')
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(WORKSPACE))
    with socketserver.TCPServer(('127.0.0.1', args.port), handler) as server:
        server.serve_forever()


if __name__ == '__main__':
    main()
