"""Sign a GitHub release's Windows installer manifest without publishing it.

The installer must already be assembled (and, when applicable, Authenticode
signed) beside release-info-LABEL.json. Keys are unencrypted Ed25519 PKCS8 PEM,
read only from the named environment variable or a protected file outside the
checkout. No private key is accepted on the command line or written to output.
Sequence allocation and periodic renewal remain the caller's responsibility.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import stat

from release_assets import is_preview_label

from cryptography.exceptions import UnsupportedAlgorithm
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


UINT32_MAX = 2**32 - 1
UINT64_MAX = 2**64 - 1
MAX_TIMESTAMP = 2**53 - 1
MAX_PACKAGE_SIZE = 512 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def decimal(text, maximum, name, minimum=0):
    require(isinstance(text, str) and len(text) <= 20
            and re.fullmatch(r'0|[1-9][0-9]*', text), f'Invalid {name}')
    value = int(text)
    require(minimum <= value <= maximum, f'{name} is out of range')
    return value


def fields(value, expected, name):
    require(isinstance(value, dict) and set(value) == set(expected), f'Invalid {name} fields')


def no_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON key')
        result[key] = value
    return result


def regular_file(path):
    require(not any(part.is_symlink() for part in (path, *path.parents)),
            'Input files and their directories must not be symbolic links')
    require(stat.S_ISREG(path.stat().st_mode), 'Input must be a regular file')


def read_json(path, maximum):
    regular_file(path)
    with path.open('rb') as stream:
        data = stream.read(maximum + 1)
    require(len(data) <= maximum, 'JSON input exceeds the size limit')
    return json.loads(data.decode('utf-8'), object_pairs_hook=no_duplicate_keys)


def installer_from_release(path, channel):
    info = read_json(path, 1024 * 1024)
    fields(info, ['schema', 'version', 'artifact_label', 'source_commit', 'tag',
                  'workflow_run', 'assets'], 'release information')
    require(type(info['schema']) is int and info['schema'] == 1, 'Unsupported release schema')
    version, label, tag = info['version'], info['artifact_label'], info['tag']
    require(isinstance(version, str)
            and re.fullmatch(r'[0-9]{2}\.(?:0[1-9]|1[0-2])\.[0-9]{2}', version),
            'Version must be YY.MM.PP')
    require(isinstance(label, str) and len(label) <= 180
            and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', label), 'Invalid artifact label')
    require(path.name == f'release-info-{label}.json', 'Release information filename does not match label')
    require(isinstance(info['source_commit'], str)
            and re.fullmatch(r'[0-9a-f]{40}', info['source_commit']), 'Invalid source commit')
    require(isinstance(info['workflow_run'], str), 'Invalid workflow run')
    if channel == 'stable':
        require(tag == f'v{version}' and label == version, 'Stable tag and label must match version')
    else:
        require(tag == 'continuous-build', 'Preview tag must be continuous-build')
        require(is_preview_label(label), 'Invalid preview artifact label')
    assets = info['assets']
    require(isinstance(assets, list) and 1 <= len(assets) <= 64, 'Invalid release assets')
    expected_name = f'ZzLogg-{label}-windows-x64-setup.exe'
    installer = None
    seen = set()
    for asset in assets:
        fields(asset, ['name', 'size', 'sha256'], 'release asset')
        name, size, digest = asset['name'], asset['size'], asset['sha256']
        require(isinstance(name, str) and len(name) <= 255
                and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', name), 'Invalid asset filename')
        require(name not in seen, 'Duplicate release asset')
        seen.add(name)
        require(type(size) is int and size > 0, 'Invalid asset size')
        require(isinstance(digest, str) and re.fullmatch(r'[0-9a-f]{64}', digest), 'Invalid asset SHA-256')
        if name == expected_name:
            installer = asset
    require(installer is not None, 'Release must contain the exact Windows x64 installer asset')
    require(installer['size'] <= MAX_PACKAGE_SIZE, 'Installer exceeds 512 MiB')
    package = path.parent / installer['name']
    regular_file(package)
    digest = hashlib.sha256()
    count = 0
    with package.open('rb') as stream:
        require(os.fstat(stream.fileno()).st_size == installer['size'], 'Installer size mismatch')
        while block := stream.read(64 * 1024):
            count += len(block)
            require(count <= installer['size'], 'Installer size changed during hashing')
            digest.update(block)
    require(count == installer['size'] and digest.hexdigest() == installer['sha256'],
            'Installer size or SHA-256 mismatch')
    return info, installer


def load_key(args):
    # Do not propagate parser/library exceptions containing any key material.
    try:
        if args.private_key_file:
            path = Path(args.private_key_file).absolute()
            regular_file(path)
            require(not path.resolve().is_relative_to(Path(__file__).resolve().parents[2]),
                    'Private key file must be outside the checkout')
            if os.name == 'posix':
                require(path.stat().st_mode & 0o077 == 0, 'Private key file must be owner-only')
            with path.open('rb') as stream:
                pem = stream.read(16 * 1024 + 1)
        else:
            pem = os.environ[args.private_key_env].encode('utf-8')
        require(len(pem) <= 16 * 1024, 'Private key exceeds the size limit')
        key = serialization.load_pem_private_key(pem, password=None)
        require(isinstance(key, Ed25519PrivateKey), 'Expected an Ed25519 private key')
        return key
    except (KeyError, OSError, ValueError, TypeError, UnsupportedAlgorithm):
        raise ValueError('Cannot load Ed25519 private key from the specified protected source') from None


def sign_manifest(args):
    require(re.fullmatch(r'[A-Za-z0-9_-]{1,64}', args.key_id), 'Invalid key ID')
    require(re.fullmatch(r'[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})/[A-Za-z0-9][A-Za-z0-9._-]{0,99}',
                         args.repository), 'Repository must be a GitHub owner/repository pair')
    decimal(args.metadata_sequence, UINT64_MAX, 'metadata sequence', 1)
    decimal(args.release_sequence, UINT64_MAX, 'release sequence', 1)
    issued = decimal(args.issued_at, MAX_TIMESTAMP, 'issued-at')
    ttl = decimal(args.ttl_seconds, 30 * 86400, 'TTL', 1)
    require(issued + ttl <= MAX_TIMESTAMP, 'Expiration timestamp exceeds the protocol limit')
    minimum = decimal(args.min_data_schema, UINT32_MAX, 'minimum data schema')
    maximum = decimal(args.max_data_schema, UINT32_MAX, 'maximum data schema')
    require(minimum <= maximum, 'Minimum data schema exceeds maximum')
    parts = args.min_os_version.split('.')
    require(len(parts) == 3, 'Minimum OS version must have three components')
    for part in parts:
        decimal(part, UINT32_MAX, 'minimum OS version')
    info, installer = installer_from_release(Path(args.release_info).absolute(), args.channel)
    notes = {'en': '', 'zh_CN': '', 'zh_TW': ''}
    if args.notes_file:
        notes = read_json(Path(args.notes_file).absolute(), 128 * 1024)
        fields(notes, ['en', 'zh_CN', 'zh_TW'], 'notes')
        for value in notes.values():
            require(isinstance(value, str) and len(value.encode('utf-8')) <= 16 * 1024,
                    'Each release note must be text of at most 16 KiB')
    payload = {'schema': 1, 'product': 'com.gitcode.jackfahdinqt.zzlogg',
               'metadataSequence': args.metadata_sequence, 'issuedAt': issued,
               'expiresAt': issued + ttl, 'channel': args.channel,
               'releaseSequence': args.release_sequence, 'version': info['version'],
               'minUpdaterProtocol': 1, 'minDataSchema': minimum, 'maxDataSchema': maximum,
               'notes': notes,
               'artifacts': [{'os': 'windows', 'arch': 'x64', 'distribution': 'installer',
                              'format': 'nsis-exe', 'minOsVersion': args.min_os_version,
                              'url': f'https://github.com/{args.repository}/releases/download/'
                                     f'{info["tag"]}/{installer["name"]}',
                              'size': str(installer['size']), 'sha256': installer['sha256']}]}
    raw = json.dumps(payload, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    require(len(raw) <= 128 * 1024, 'Payload exceeds protocol size limit')
    key = load_key(args)
    signature = key.sign(b'ZzLogg update manifest v1\n' + args.key_id.encode('ascii') + b'\n' + raw)
    envelope = {'schema': 1, 'keyId': args.key_id,
                'payload': base64.b64encode(raw).decode('ascii'),
                'signature': base64.b64encode(signature).decode('ascii')}
    encoded = (json.dumps(envelope, separators=(',', ':')) + '\n').encode('utf-8')
    require(len(encoded) <= 256 * 1024, 'Envelope exceeds protocol size limit')
    output = Path(args.output_dir) / f'update-{args.channel}.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as stream:
        stream.write(encoded)
    return {'keyId': args.key_id, 'publicKey': key.public_key().public_bytes_raw().hex(),
            'output': str(output)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release-info', required=True)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--channel', choices=['stable', 'preview'], required=True)
    parser.add_argument('--metadata-sequence', required=True)
    parser.add_argument('--release-sequence', required=True)
    parser.add_argument('--issued-at', required=True, help='UTC Unix seconds, explicitly chosen by the caller')
    parser.add_argument('--ttl-seconds', required=True, help='Validity, 1 through 2592000 seconds')
    parser.add_argument('--min-data-schema', required=True)
    parser.add_argument('--max-data-schema', required=True)
    parser.add_argument('--min-os-version', required=True)
    parser.add_argument('--notes-file', help='Optional JSON object with en, zh_CN and zh_TW plain text')
    parser.add_argument('--key-id', required=True)
    key = parser.add_mutually_exclusive_group(required=True)
    key.add_argument('--private-key-file', help='Owner-only PEM file outside the checkout')
    key.add_argument('--private-key-env', help='Name of an environment variable containing the PEM key')
    parser.add_argument('--output-dir', required=True, help='Creates update-CHANNEL.json without overwriting')
    args = parser.parse_args()
    try:
        result = sign_manifest(args)
    except (ValueError, OSError, TypeError, RecursionError):
        # Inputs, library messages and file paths may contain secrets; never echo them.
        parser.error('Cannot sign manifest: invalid inputs, key source, release files, or output destination')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
