"""Exercise the release signer through its actual command-line boundary."""
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/ci/sign_update_manifest.py'


class UpdateManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.release = self.root / 'release'
        self.release.mkdir()
        self.key = Ed25519PrivateKey.generate()
        self.pem = self.key.private_bytes(serialization.Encoding.PEM,
                                         serialization.PrivateFormat.PKCS8,
                                         serialization.NoEncryption())
        self.key_file = self.root / 'test-only.pem'
        self.key_file.write_bytes(self.pem)
        self.key_file.chmod(0o600)
        self.name = 'ZzLogg-26.09.00-windows-x64-setup.exe'
        self.package = self.release / self.name
        self.package.write_bytes(b'local assembled installer bytes')
        self.info = {'schema': 1, 'version': '26.09.00', 'artifact_label': '26.09.00',
                     'source_commit': 'a' * 40, 'tag': 'v26.09.00',
                     'workflow_run': 'https://github.com/example/ZzLogg/actions/runs/1',
                     'assets': [self.asset(self.name, self.package.read_bytes()),
                                self.asset('ZzLogg-26.09.00-windows-x64-portable.zip', b'zip')]}
        self.info_path = self.release / 'release-info-26.09.00.json'
        self.output = self.root / 'manifests'

    @staticmethod
    def asset(name, data):
        return {'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}

    def run_signer(self, *, changes=None, remove=(), env=None, write_info=True):
        if write_info:
            self.info_path.write_text(json.dumps(self.info), encoding='utf-8')
        options = {'--release-info': str(self.info_path), '--repository': 'example/ZzLogg',
                   '--channel': 'stable', '--metadata-sequence': '42', '--release-sequence': '17',
                   '--issued-at': '1800000000', '--ttl-seconds': '2592000',
                   '--min-data-schema': '0', '--max-data-schema': '3',
                   '--min-os-version': '10.0.19041', '--key-id': 'release-2026',
                   '--private-key-file': str(self.key_file), '--output-dir': str(self.output)}
        options.update(changes or {})
        for name in remove:
            options.pop(name, None)
        return subprocess.run([sys.executable, str(SCRIPT),
                               *[item for pair in options.items() for item in pair]],
                              env=dict(os.environ, **(env or {})), capture_output=True, text=True)

    def assert_rejected(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn('error:', result.stderr)
        self.assertFalse(self.output.exists() and list(self.output.iterdir()))
        self.assertNotIn(self.pem.decode(), result.stdout + result.stderr)
        self.assertNotIn(self.pem.decode().splitlines()[1], result.stdout + result.stderr)

    def test_signed_bytes_match_protocol_and_only_installer_is_advertised(self):
        result = self.run_signer()
        self.assertEqual(result.returncode, 0, result.stderr)
        envelope = json.loads((self.output / 'update-stable.json').read_text())
        self.assertEqual(set(envelope), {'schema', 'keyId', 'payload', 'signature'})
        self.assertEqual(envelope['schema'], 1)
        self.assertEqual(envelope['keyId'], 'release-2026')
        raw = base64.b64decode(envelope['payload'], validate=True)
        signature = base64.b64decode(envelope['signature'], validate=True)
        message = b'ZzLogg update manifest v1\nrelease-2026\n' + raw
        self.key.public_key().verify(signature, message)
        with self.assertRaises(InvalidSignature):
            self.key.public_key().verify(signature, message + b' ')
        self.assertEqual(json.loads(raw), {
            'schema': 1, 'product': 'com.gitcode.jackfahdinqt.zzlogg',
            'metadataSequence': '42', 'releaseSequence': '17', 'issuedAt': 1800000000,
            'expiresAt': 1802592000, 'channel': 'stable', 'version': '26.09.00',
            'minUpdaterProtocol': 1, 'minDataSchema': 0, 'maxDataSchema': 3,
            'notes': {'en': '', 'zh_CN': '', 'zh_TW': ''},
            'artifacts': [{'os': 'windows', 'arch': 'x64', 'distribution': 'installer',
                           'format': 'nsis-exe', 'minOsVersion': '10.0.19041',
                           'url': 'https://github.com/example/ZzLogg/releases/download/v26.09.00/' + self.name,
                           'size': '31', 'sha256': hashlib.sha256(self.package.read_bytes()).hexdigest()}]})
        public = self.key.public_key().public_bytes_raw().hex()
        self.assertIn(public, result.stdout)
        self.assertNotIn(self.pem.decode().splitlines()[1], result.stdout + result.stderr)

    def test_environment_key_and_preview_have_separate_output(self):
        stable = self.run_signer()
        self.assertEqual(stable.returncode, 0, stable.stderr)
        stable_bytes = (self.output / 'update-stable.json').read_bytes()
        label = 'continuous-20260920-123-1-aaaaaaaaaaaa'
        self.info.update(tag='continuous-build', artifact_label=label)
        name = f'ZzLogg-{label}-windows-x64-setup.exe'
        self.package.rename(self.release / name)
        self.info['assets'][0]['name'] = name
        self.info_path = self.release / f'release-info-{label}.json'
        result = self.run_signer(changes={'--channel': 'preview', '--private-key-env': 'TEST_SIGNING_PEM'},
                                 remove=['--private-key-file'], env={'TEST_SIGNING_PEM': self.pem.decode()})
        self.assertEqual(result.returncode, 0, result.stderr)
        envelope = json.loads((self.output / 'update-preview.json').read_text())
        payload = json.loads(base64.b64decode(envelope['payload']))
        self.assertEqual(payload['channel'], 'preview')
        self.assertEqual(payload['artifacts'][0]['url'],
                         f'https://github.com/example/ZzLogg/releases/download/continuous-build/{name}')
        self.assertEqual((self.output / 'update-stable.json').read_bytes(), stable_bytes)

    def test_tampered_installer_or_hash_or_size_is_rejected(self):
        for field, value in [('sha256', '0' * 64), ('size', 32), ('size', True),
                             ('size', 0), ('size', 536870913), ('sha256', 'A' * 64)]:
            original = self.info['assets'][0][field]
            with self.subTest(field=field, value=value):
                self.info['assets'][0][field] = value
                self.assert_rejected(self.run_signer())
            self.info['assets'][0][field] = original
        self.package.write_bytes(b'tampered installer bytes')
        self.assert_rejected(self.run_signer())

    def test_invalid_numeric_arguments_are_rejected(self):
        invalid = {
            '--metadata-sequence': ['0', '01', '-1', '1.0', '18446744073709551616'],
            '--release-sequence': ['0', ' 1', '+1', '1e2'],
            '--issued-at': ['-1', '01', '9007199254740992', '9007199254740991'],
            '--ttl-seconds': ['0', '-1', '2592001', '1.5'],
            '--min-data-schema': ['-1', '4294967296', '4'],
            '--max-data-schema': ['-1', '4294967296', '01'],
            '--min-os-version': ['10.00.1', '10.0', '10.0.4294967296'],
        }
        for option, values in invalid.items():
            for value in values:
                with self.subTest(option=option, value=value):
                    self.assert_rejected(self.run_signer(changes={option: value}))

    def test_sequence_time_and_schema_inputs_are_required(self):
        for option in ['--metadata-sequence', '--release-sequence', '--issued-at',
                       '--ttl-seconds', '--min-data-schema', '--max-data-schema']:
            with self.subTest(option=option):
                self.assert_rejected(self.run_signer(remove=[option]))

    def test_version_and_channel_tag_mismatch_are_rejected(self):
        for version in ['26.9.00', '26.13.00', '26.09.00.0', '２６.09.00', '26.09.01']:
            with self.subTest(version=version):
                self.info['version'] = version
                self.assert_rejected(self.run_signer())
        self.info['version'] = '26.09.00'
        self.assert_rejected(self.run_signer(changes={'--channel': 'preview'}))
        for tag in ['continuous-build', '../v26.09.00', 'v26.09.00?x', 'v26.09.00/other']:
            with self.subTest(tag=tag):
                self.info['tag'] = tag
                self.assert_rejected(self.run_signer())

    def test_unsafe_repository_key_id_or_names_are_rejected(self):
        for repo in ['https://github.com/owner/repo', '../repo', 'owner/repo/other',
                     'owner/repo?x', 'owner/repo#x', 'owner\\repo', 'owner/%2e%2e']:
            with self.subTest(repo=repo):
                self.assert_rejected(self.run_signer(changes={'--repository': repo}))
        self.assert_rejected(self.run_signer(changes={'--key-id': 'key\nother'}))
        for name in ['../evil.exe', '/tmp/evil.exe', '..\\evil.exe', 'C:evil.exe',
                     'installer.exe', self.name + '?x', 'ZzLogg-%2e%2e-windows-x64-setup.exe']:
            with self.subTest(name=name):
                self.info['assets'][0]['name'] = name
                self.assert_rejected(self.run_signer())

    def test_duplicate_installer_missing_package_and_symlink_escape_are_rejected(self):
        self.info['assets'].append(dict(self.info['assets'][0]))
        self.assert_rejected(self.run_signer())
        self.info['assets'].pop()
        self.package.unlink()
        self.assert_rejected(self.run_signer())
        outside = self.root / 'outside.exe'
        outside.write_bytes(b'local assembled installer bytes')
        self.package.symlink_to(outside)
        self.assert_rejected(self.run_signer())

    def test_duplicate_json_keys_and_wrong_metadata_filename_are_rejected(self):
        self.info_path.write_text(json.dumps(self.info).replace('"schema": 1', '"schema": 1, "schema": 1'))
        self.assert_rejected(self.run_signer(write_info=False))
        self.info_path = self.release / 'release-info-other.json'
        self.assert_rejected(self.run_signer())

    def test_existing_manifest_is_never_overwritten(self):
        self.output.mkdir()
        path = self.output / 'update-stable.json'
        path.write_bytes(b'previous published manifest')
        result = self.run_signer()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(path.read_bytes(), b'previous published manifest')

    def test_bad_or_missing_private_key_does_not_leak_material(self):
        secret = 'deliberately-invalid-private-key-material'
        result = self.run_signer(changes={'--private-key-env': 'TEST_SIGNING_PEM'},
                                 remove=['--private-key-file'], env={'TEST_SIGNING_PEM': secret})
        self.assert_rejected(result)
        self.assertNotIn(secret, result.stdout + result.stderr)
        self.assert_rejected(self.run_signer(remove=['--private-key-file']))

    def test_notes_remain_signed_plain_text_in_all_protocol_languages(self):
        notes = {'en': '<b>Release</b>\nSecond line', 'zh_CN': '更新说明', 'zh_TW': '更新說明'}
        notes_path = self.root / 'notes.json'
        notes_path.write_text(json.dumps(notes), encoding='utf-8')
        result = self.run_signer(changes={'--notes-file': str(notes_path)})
        self.assertEqual(result.returncode, 0, result.stderr)
        envelope = json.loads((self.output / 'update-stable.json').read_text())
        raw = base64.b64decode(envelope['payload'])
        self.key.public_key().verify(base64.b64decode(envelope['signature']),
                                     b'ZzLogg update manifest v1\nrelease-2026\n' + raw)
        self.assertEqual(json.loads(raw)['notes'], notes)

    def test_invalid_notes_and_wrong_key_type_are_rejected(self):
        notes_path = self.root / 'notes.json'
        for notes in [{'en': 'a'}, {'en': 1, 'zh_CN': '', 'zh_TW': ''},
                      {'en': '', 'zh_CN': '中' * 5462, 'zh_TW': ''}]:
            with self.subTest(notes_type=type(notes.get('en')).__name__):
                notes_path.write_text(json.dumps(notes), encoding='utf-8')
                self.assert_rejected(self.run_signer(changes={'--notes-file': str(notes_path)}))
        from cryptography.hazmat.primitives.asymmetric import ec
        wrong_key = ec.generate_private_key(ec.SECP256R1())
        self.key_file.write_bytes(wrong_key.private_bytes(serialization.Encoding.PEM,
                                  serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        self.assert_rejected(self.run_signer())

    @unittest.skipUnless(os.name == 'posix', 'POSIX private-key permission check')
    def test_world_readable_key_file_is_rejected(self):
        self.key_file.chmod(0o644)
        self.assert_rejected(self.run_signer())


if __name__ == '__main__':
    unittest.main()
