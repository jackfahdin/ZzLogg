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
  python scripts/dev/local_update_feed.py --installer <setup.exe> --version 26.10.05
"""
import argparse
import base64
import functools
import hashlib
import http.server
import json
import os
import subprocess
import re
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
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
    parser.add_argument('--workspace', type=Path, default=WORKSPACE, help='Directory for the local feed and package')
    parser.add_argument('--launch', type=Path, help='Start this installed test client after binding the HTTP server')
    parser.add_argument('--launch-args', nargs=argparse.REMAINDER, default=[],
                        help='Client arguments; this option must be last')
    args = parser.parse_args()

    if not re.fullmatch(r'[0-9]{2}\.(?:0[1-9]|1[0-2])\.[0-9]{2}', args.version):
        parser.error('version must be YY.MM.PP')
    if not 1 <= args.port <= 65535:
        parser.error('port must be between 1 and 65535')
    if args.launch and (args.no_serve or not args.launch.is_file()):
        parser.error('--launch requires an existing executable and cannot be combined with --no-serve')
    if args.launch_args and not args.launch:
        parser.error('--launch-args requires --launch')
    installer = Path(args.installer).resolve()
    if not installer.is_file():
        parser.error(f'installer not found: {installer}')
    public = ensure_key()

    workspace = args.workspace.resolve()
    workspace.mkdir(parents=True, exist_ok=True)
    # Keep the local download name aligned with the installer output and UAC.
    staged_installer = workspace / f'ZzLogg-{args.version}-x64-Qt6-setup.exe'
    staged_installer.write_bytes(installer.read_bytes())
    digest = hashlib.sha256(staged_installer.read_bytes()).hexdigest()
    size = staged_installer.stat().st_size

    release_info = workspace / f'release-info-{args.version}.json'
    release_info.write_text(json.dumps({
        'schema': 1, 'version': args.version, 'release_sequence': str(int(args.version.replace('.', ''))),
        'artifact_label': args.version,
        'source_commit': '0' * 40, 'tag': f'v{args.version}', 'workflow_run': 'local',
        'assets': [{'name': staged_installer.name, 'size': size, 'sha256': digest}],
    }, indent=2), encoding='utf-8')

    notes = workspace / 'notes.json'
    notes.write_text(json.dumps({'en': 'Local test update', 'zh_CN': '本地测试更新',
                                 'zh_TW': '本機測試更新'}, ensure_ascii=False), encoding='utf-8')

    # This is an isolated Test-environment fixture, not a production release.
    # Sign only the supplied installer; never fabricate other platform assets
    # merely to satisfy the production publisher's complete-release contract.
    from cryptography.hazmat.primitives import serialization
    key = serialization.load_pem_private_key(KEY_FILE.read_bytes(), password=None)
    now = int(time.time())
    payload = {'schema': 1, 'product': 'com.gitcode.jackfahdinqt.zzlogg',
               'metadataSequence': str(time.time_ns() // 1_000_000),
               'releaseSequence': str(int(args.version.replace('.', ''))),
               'issuedAt': now, 'expiresAt': now + 7 * 86400,
               'channel': 'stable', 'version': args.version,
               'minUpdaterProtocol': 1, 'minDataSchema': 0, 'maxDataSchema': 0,
               'notes': json.loads(notes.read_text(encoding='utf-8')),
               'artifacts': [{'os': 'windows', 'arch': 'x64', 'distribution': 'installer',
                              'format': 'nsis-exe', 'minOsVersion': '10.0.19041',
                              'url': f'http://127.0.0.1:{args.port}/{staged_installer.name}',
                              'size': str(size), 'sha256': digest}]}
    raw = json.dumps(payload, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    envelope = {'schema': 1, 'keyId': KEY_ID, 'payload': base64.b64encode(raw).decode(),
                'signature': base64.b64encode(key.sign(
                    b'ZzLogg update manifest v1\n' + KEY_ID.encode() + b'\n' + raw)).decode()}
    (workspace / 'update-stable.json').write_text(json.dumps(envelope), encoding='utf-8')

    signed_feed = workspace / 'update-stable.json'
    feed = workspace / 'stable.json'
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
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(workspace))
    with http.server.ThreadingHTTPServer(('127.0.0.1', args.port), handler) as server:
        print(f'Serving {workspace} on http://127.0.0.1:{args.port} (Ctrl+C to stop)', flush=True)
        if args.launch:
            environment = dict(os.environ,
                ZZLOGG_UPDATE_FEED_URL=f'http://127.0.0.1:{args.port}/stable.json',
                ZZLOGG_UPDATE_FEED_PREVIEW_URL=f'http://127.0.0.1:{args.port}/stable.json',
                ZZLOGG_UPDATE_FEED_KEY=f'{KEY_ID}:{public}',
                ZZLOGG_UPDATE_FEED_HOSTS='127.0.0.1;localhost')
            subprocess.Popen([str(args.launch.resolve()), *args.launch_args], env=environment,
                             cwd=args.launch.resolve().parent)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
