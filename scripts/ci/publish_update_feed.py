"""Validate GitHub release assets and publish signed manifests for manual updates.

Only the feed branch is written. Signing a manifest does not enable installation
or make the Windows executable Authenticode signed.
"""
import argparse
import base64
import hashlib
import html
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

from publish_release import GitHub, list_assets
from sign_update_manifest import decimal, no_duplicate_keys

TRUST_KEY_PATH = Path(__file__).resolve().parents[2] / 'packaging/update/github-public-key.json'
PRODUCT = 'com.gitcode.jackfahdinqt.zzlogg'
BRANCH = 'update-feed'
MAX_INFO_BYTES = 1024 * 1024
MAX_PACKAGE_BYTES = 512 * 1024 * 1024
MAX_MANIFEST_BYTES = 256 * 1024


def validate_download_url(url):
    parsed = urlsplit(url)
    if (parsed.scheme != 'https' or parsed.hostname not in {
            'github.com', 'release-assets.githubusercontent.com', 'objects.githubusercontent.com'}
            or parsed.username is not None or parsed.password is not None
            or parsed.port not in (None, 443) or parsed.fragment):
        raise ValueError('Asset download must remain on an approved GitHub HTTPS host')


class GitHubRedirectHandler(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        validate_download_url(new_url)
        return super().redirect_request(request, fp, code, message, headers, new_url)


def open_download(request, **kwargs):
    return build_opener(GitHubRedirectHandler()).open(request, **kwargs)


def download(url, path, limit):
    validate_download_url(url)
    path = Path(path)
    try:
        with open_download(Request(url, headers={'User-Agent': 'ZzLogg-update-feed'}), timeout=60) as response:
            length = response.headers.get('Content-Length')
            if length is not None and int(length) > limit:
                raise ValueError('Asset exceeds download byte limit')
            with path.open('xb') as target:
                size = 0
                while chunk := response.read(min(64 * 1024, limit + 1 - size)):
                    size += len(chunk)
                    if size > limit:
                        raise ValueError('Asset exceeds download byte limit')
                    target.write(chunk)
            if size == 0:
                raise ValueError('Empty release asset')
    except Exception:
        path.unlink(missing_ok=True)
        raise


def checked_asset(assets, name, repository, tag, limit):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,199}', name):
        raise ValueError('Unsafe release asset filename')
    matches = [asset for asset in assets if asset.get('name') == name]
    if len(matches) != 1:
        raise ValueError('Exactly one matching release asset is required')
    asset = matches[0]
    size = asset.get('size')
    if type(size) is not int or not 0 < size <= limit:
        raise ValueError('Invalid release asset size')
    url = f'https://github.com/{repository}/releases/download/{tag}/{name}'
    if asset.get('browser_download_url') != url:
        raise ValueError('Release asset URL does not match repository/tag/name')
    return asset


def plain_notes(body):
    if not isinstance(body, str) or len(body.encode('utf-8')) > 128 * 1024:
        raise ValueError('Release notes exceed input limit')
    text = re.sub(r'<!--.*?-->', '', body, flags=re.DOTALL)
    text = re.sub(r'<[^>]*>', '', text)
    text = re.sub(r'!?\[([^\]]*)\]\([^)]*\)', r'\1', text)
    text = re.sub(r'^\s{0,3}(?:#{1,6}\s+|>\s?)', '', text, flags=re.MULTILINE)
    text = re.sub(r'[*`~]', '', text)
    text = html.unescape(text).strip()
    # The client bounds each translated note to 16 KiB. Keep UTF-8 intact.
    text = text.encode('utf-8')[:16 * 1024].decode('utf-8', errors='ignore')
    return {language: text for language in ('en', 'zh_CN', 'zh_TW')}


def trusted_key():
    data = TRUST_KEY_PATH.read_bytes()
    if len(data) > 4096:
        raise ValueError('Public key file exceeds size limit')
    trust = json.loads(data, object_pairs_hook=no_duplicate_keys)
    if (not isinstance(trust, dict) or set(trust) != {'schema', 'keyId', 'publicKey'}
            or type(trust['schema']) is not int or trust['schema'] != 1
            or not isinstance(trust['keyId'], str)
            or not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', trust['keyId'])
            or not isinstance(trust['publicKey'], str)
            or not re.fullmatch(r'[0-9a-f]{64}', trust['publicKey'])):
        raise ValueError('Invalid pinned update public key')
    return trust['keyId'], Ed25519PublicKey.from_public_bytes(bytes.fromhex(trust['publicKey']))


def verified_payload(content, channel, key_id, public_key):
    if not 0 < len(content) <= MAX_MANIFEST_BYTES:
        raise ValueError('Manifest exceeds size limit')
    envelope = json.loads(content, object_pairs_hook=no_duplicate_keys)
    if (not isinstance(envelope, dict) or set(envelope) != {'schema', 'keyId', 'payload', 'signature'}
            or type(envelope['schema']) is not int or envelope['schema'] != 1
            or envelope['keyId'] != key_id):
        raise ValueError('Manifest does not use the pinned update key')
    try:
        raw = base64.b64decode(envelope['payload'], validate=True)
        signature = base64.b64decode(envelope['signature'], validate=True)
        public_key.verify(signature, b'ZzLogg update manifest v1\n' + key_id.encode() + b'\n' + raw)
    except (InvalidSignature, TypeError, ValueError):
        raise ValueError('Manifest signature does not match pinned update key') from None
    payload = json.loads(raw, object_pairs_hook=no_duplicate_keys)
    if (not isinstance(payload, dict) or type(payload.get('schema')) is not int or payload['schema'] != 1
            or payload.get('channel') != channel or payload.get('product') != PRODUCT
            or not isinstance(payload.get('version'), str)
            or not re.fullmatch(r'[0-9]{2}\.(?:0[1-9]|1[0-2])\.[0-9]{2}', payload['version'])
            or not isinstance(payload.get('artifacts'), list) or not payload['artifacts']):
        raise ValueError('Manifest product/channel/version is invalid')
    decimal(payload.get('metadataSequence'), 2**64 - 1, 'metadataSequence', 1)
    sequence = decimal(payload.get('releaseSequence'), 2**64 - 1, 'releaseSequence', 1)
    if sequence != int(payload['version'].replace('.', '')):
        raise ValueError('Manifest release sequence does not match version')
    return payload


def previous_manifest(github, channel, key_id, public_key):
    old = github.api(f'contents/{channel}.json?ref={BRANCH}', optional=True)
    if old is None:
        return None, None
    if (old.get('type') != 'file' or not old.get('sha') or old.get('encoding') != 'base64'
            or type(old.get('size')) is not int or not 0 < old['size'] <= MAX_MANIFEST_BYTES
            or not isinstance(old.get('content'), str) or len(old['content']) > MAX_MANIFEST_BYTES * 2):
        raise ValueError('Existing feed path is not a bounded manifest file')
    content = base64.b64decode(''.join(old['content'].split()), validate=True)
    if len(content) != old['size']:
        raise ValueError('Existing feed file size mismatch')
    return old, verified_payload(content, channel, key_id, public_key)


def check_progress(previous, current, channel):
    if previous is None:
        return
    if (int(current['metadataSequence']) <= int(previous['metadataSequence'])
            or current['version'] < previous['version']
            or int(current['releaseSequence']) < int(previous['releaseSequence'])):
        raise ValueError('Refusing to publish stale or regressing update metadata')
    if channel == 'stable' and current['version'] == previous['version']:
        # Release-note corrections do not change the immutable installer or policy.
        volatile = {'metadataSequence', 'issuedAt', 'expiresAt', 'notes'}
        if ({key: value for key, value in current.items() if key not in volatile}
                != {key: value for key, value in previous.items() if key not in volatile}):
            raise ValueError('Published stable version contents are immutable')


def store_manifest(github, channel, content, old):
    if github.api(f'git/ref/heads/{BRANCH}', optional=True) is None:
        # Bootstrap from a known branch without replacing any existing tree.
        # A concurrent branch creation fails rather than force-resetting it.
        source = github.api('git/ref/heads/master')['object']['sha']
        github.api('git/refs', method='POST', data={'ref': f'refs/heads/{BRANCH}', 'sha': source})
    name = f'{channel}.json'
    data = {'message': f'Renew signed {channel} update metadata', 'branch': BRANCH,
            'content': base64.b64encode(content).decode('ascii')}
    if old is not None:
        if old.get('type') != 'file' or not old.get('sha'):
            raise ValueError('Feed path is not a regular file')
        data['sha'] = old['sha']
    # Contents API's blob SHA is the compare-and-swap guard. Never retry a
    # conflict with a newly read SHA: that could overwrite a newer publication.
    github.api(f'contents/{name}', method='PUT', data=data)


def publish_channel(github, channel, key_id=None, *, now_ms=None):
    if channel not in ('stable', 'preview'):
        raise ValueError('Unsupported feed channel')
    pinned_id, public_key = trusted_key()
    if key_id is not None and key_id != pinned_id:
        raise ValueError('Requested key ID does not match pinned update key')
    key_id = pinned_id
    endpoint = 'releases/latest' if channel == 'stable' else 'releases/tags/continuous-build'
    release = github.api(endpoint, optional=channel == 'preview')
    if release is None:
        print('No preview release; skipping preview feed')
        return False
    if release.get('draft') is not False or release.get('prerelease') is not (channel == 'preview'):
        raise ValueError('Release draft/prerelease state does not match channel')
    tag = release.get('tag_name')
    if (channel == 'stable' and not re.fullmatch(r'v[0-9]{2}\.(?:0[1-9]|1[0-2])\.[0-9]{2}', tag or '')) or (
            channel == 'preview' and tag != 'continuous-build'):
        raise ValueError('Release tag does not match feed channel')
    # Capture and verify the CAS baseline before downloads/signing can race a newer run.
    old, previous = previous_manifest(github, channel, key_id, public_key)
    assets = list_assets(github, release['id'])
    metadata = [asset for asset in assets if asset.get('name', '').startswith('release-info-')
                and asset['name'].endswith('.json')]
    if len(metadata) != 1:
        raise ValueError('Exactly one release-info attachment is required')
    info_asset = checked_asset(assets, metadata[0]['name'], github.repository, tag, MAX_INFO_BYTES)
    with tempfile.TemporaryDirectory(prefix='zzlogg-update-feed-') as temporary:
        directory = Path(temporary)
        info_path = directory / info_asset['name']
        download(info_asset['browser_download_url'], info_path, info_asset['size'])
        if info_path.stat().st_size != info_asset['size']:
            raise ValueError('Release metadata size mismatch')
        info = json.loads(info_path.read_text(encoding='utf-8'))
        version, label = info.get('version'), info.get('artifact_label')
        if not isinstance(version, str) or not re.fullmatch(r'[0-9]{2}\.(?:0[1-9]|1[0-2])\.[0-9]{2}', version):
            raise ValueError('Invalid release version')
        if (not isinstance(label, str) or info_asset['name'] != f'release-info-{label}.json'
                or info.get('tag') != tag):
            raise ValueError('Release metadata tag/label mismatch')
        if channel == 'stable':
            if tag != f'v{version}' or label != version:
                raise ValueError('Stable version/tag/label mismatch')
        elif not re.fullmatch(r'continuous-[0-9]{8}-[0-9]+-[0-9]+-[0-9a-f]{12}', label):
            raise ValueError('Invalid preview artifact label')
        name = f'ZzLogg-{label}-windows-x64-setup.exe'
        asset = checked_asset(assets, name, github.repository, tag, MAX_PACKAGE_BYTES)
        packages = [item for item in info.get('assets', []) if item.get('name') == name]
        if len(packages) != 1 or type(packages[0].get('size')) is not int or packages[0]['size'] != asset['size']:
            raise ValueError('Installer metadata does not match release asset')
        package = directory / name
        download(asset['browser_download_url'], package, asset['size'])
        with package.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if package.stat().st_size != asset['size'] or digest != packages[0].get('sha256'):
            raise ValueError('Installer SHA256 or size mismatch')
        notes_path = directory / 'notes.json'
        notes_path.write_text(json.dumps(plain_notes(release.get('body') or ''), ensure_ascii=False), encoding='utf-8')
        sequence = now_ms if now_ms is not None else time.time_ns() // 1_000_000
        options = {'--release-info': str(info_path), '--repository': github.repository,
                   '--channel': channel, '--metadata-sequence': str(sequence),
                   '--release-sequence': str(int(version.replace('.', ''))),
                   '--issued-at': str(sequence // 1000), '--ttl-seconds': str(14 * 24 * 60 * 60),
                   '--min-data-schema': '0', '--max-data-schema': '0',
                   '--min-os-version': '10.0.19041', '--key-id': key_id,
                   '--private-key-env': 'ZZLOGG_UPDATE_SIGNING_KEY', '--notes-file': str(notes_path),
                   '--output-dir': str(directory / 'signed')}
        subprocess.run([sys.executable, str(Path(__file__).with_name('sign_update_manifest.py')),
                        *[item for pair in options.items() for item in pair]],
                       check=True, capture_output=True, text=True)
        manifest = directory / 'signed' / f'update-{channel}.json'
        if not 0 < manifest.stat().st_size <= MAX_MANIFEST_BYTES:
            raise ValueError('Signed manifest exceeds size limit')
        content = manifest.read_bytes()
        current = verified_payload(content, channel, key_id, public_key)
        check_progress(previous, current, channel)
        store_manifest(github, channel, content, old)
    print(f'Published {channel} feed for {version}; installation remains separately gated')
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--channel', choices=['stable', 'preview', 'all'], default='all')
    args = parser.parse_args()
    github = GitHub(args.repository)
    failed = []
    for channel in ('stable', 'preview') if args.channel == 'all' else (args.channel,):
        try:
            publish_channel(github, channel)
        except Exception:
            # Continue renewal of the other channel. Never echo exceptions or
            # captured subprocess output: they may contain signing key inputs.
            failed.append(channel)
    if failed:
        print('Update feed publication failed for: ' + ', '.join(failed), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
