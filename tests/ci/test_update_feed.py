"""Offline checks of the feed publisher; only network/process boundaries are faked."""
import base64
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/ci'))
SPEC = importlib.util.spec_from_file_location('publish_update_feed', ROOT / 'scripts/ci/publish_update_feed.py')
feed = importlib.util.module_from_spec(SPEC)
if SPEC.loader and Path(SPEC.origin).exists():
    SPEC.loader.exec_module(feed)


class Response(io.BytesIO):
    headers = {}


class ProductionPublicConfigurationTests(unittest.TestCase):
    def test_versioned_public_record_is_accepted_by_publisher(self):
        key_id, key = feed.trusted_key()
        record = json.loads((ROOT / 'packaging/update/github-public-key.json').read_text())
        self.assertEqual(key_id, record['keyId'])
        self.assertEqual(key.public_bytes_raw().hex(), record['publicKey'])


class FeedTests(unittest.TestCase):
    def setUp(self):
        self.package = b'verified installer'
        self.package_overrides = {}
        self.now_ms = 1800000000123
        self.label = '26.09.00'
        self.info = {'schema': 1, 'version': '26.09.00', 'release_sequence': '260900', 'artifact_label': self.label,
                     'tag': 'v26.09.00', 'source_commit': 'a' * 40,
                     'workflow_run': 'https://github.com/example/ZzLogg/actions/runs/123',
                     'assets': [{'name': f'ZzLogg-{self.label}-windows-x64-setup.exe',
                                 'size': len(self.package), 'sha256': hashlib.sha256(self.package).hexdigest()}]}
        for suffix in ['windows-x64-portable.zip', 'linux-x64.deb', 'linux-x64.rpm',
                       'macos-x64.dmg', 'macos-arm64.dmg']:
            self.info['assets'].append({'name': f'ZzLogg-{self.label}-{suffix}',
                                       'size': len(self.package),
                                       'sha256': hashlib.sha256(self.package).hexdigest()})
        self.release = {'id': 123, 'tag_name': 'v26.09.00', 'draft': False,
                        'prerelease': False, 'body': '# Changes\n\n**Fixed** [logs](https://example.org).\n<!-- hidden -->'}
        self.stored = {}
        self.updates = []
        self.branch = True
        self.created_tree = None
        self.created_commit = None
        self.branch_creation_conflict = False
        self.conflict = False
        self.sign_failure = False
        self.sign_options = None
        self.assets = None
        self.key = Ed25519PrivateKey.generate()
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.trust_file = Path(temporary.name) / 'public-key.json'
        self.trust_file.write_text(json.dumps({'schema': 1, 'keyId': 'zzlogg-update-2026-09',
                                              'publicKey': self.key.public_key().public_bytes_raw().hex()}))
        self.blob_sha = 'old-sha'
        self.race = False
        self.wrong_signing_key = False
        self.appcast_mutation = None

    def payload(self, **changes):
        payload = {'schema': 1, 'product': 'com.gitcode.jackfahdinqt.zzlogg', 'channel': 'stable',
                   'metadataSequence': '100', 'releaseSequence': '260900', 'version': '26.09.00',
                   'issuedAt': 1700000000, 'expiresAt': 1701209600,
                   'artifacts': [{'sha256': hashlib.sha256(self.package).hexdigest()}],
                   'notes': {language: 'Changes\n\nFixed logs.' for language in ('en', 'zh_CN', 'zh_TW')}}
        payload.update(changes)
        return payload

    def signed(self, payload, key=None):
        raw = json.dumps(payload).encode()
        signature = (key or self.key).sign(b'ZzLogg update manifest v1\nzzlogg-update-2026-09\n' + raw)
        return json.dumps({'schema': 1, 'keyId': 'zzlogg-update-2026-09',
                           'payload': base64.b64encode(raw).decode(),
                           'signature': base64.b64encode(signature).decode()}).encode()

    def remote_assets(self):
        names = [(f'release-info-{self.label}.json', len(json.dumps(self.info).encode())),
                 *[(asset['name'], len(self.package_overrides.get(asset['name'], self.package)))
                   for asset in self.info['assets']]]
        return [{'id': n + 1, 'name': name, 'size': size,
                 'browser_download_url': f'https://github.com/example/ZzLogg/releases/download/{self.release["tag_name"]}/{name}'}
                for n, (name, size) in enumerate(names)]

    def appcast_xml(self, arch, channel='stable'):
        name = f'ZzLogg-{self.label}-macos-{arch}.dmg'
        data = self.package_overrides.get(name, self.package)
        signature = base64.b64encode(self.key.sign(data)).decode()
        url = f'https://github.com/example/ZzLogg/releases/download/{self.info["tag"]}/{name}'
        minimum = '13.3' if arch == 'x64' else '14.0'
        return (f'<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle"><channel><item>'
                f'<sparkle:minimumSystemVersion>{minimum}</sparkle:minimumSystemVersion>'
                f'<enclosure url="{url}" length="{len(data)}" sparkle:version="{self.info["release_sequence"]}" '
                f'sparkle:shortVersionString="{self.info["version"]}" sparkle:edSignature="{signature}"/>'
                f'</item></channel></rss>')

    def seed_stable_appcasts(self):
        self.stored.update({f'macos-{arch}-stable.xml': self.appcast_xml(arch).encode()
                            for arch in ('x64', 'arm64')})

    def run_process(self, command, **kwargs):
        if command[0] != 'gh':
            if self.sign_failure:
                raise subprocess.CalledProcessError(1, command)
            opts = dict(zip(command[2::2], command[3::2]))
            self.sign_options = opts
            self.assertEqual((Path(opts['--release-info']).parent / self.info['assets'][0]['name']).read_bytes(), self.package)
            self.notes = Path(opts['--notes-file']).read_text()
            output = Path(opts['--output-dir'])
            output.mkdir(exist_ok=True)
            self.signed_bytes = self.signed(self.payload(channel=opts['--channel'],
                                                        metadataSequence=opts['--metadata-sequence'],
                                                        releaseSequence=opts['--release-sequence'],
                                                        version=self.info['version'], notes=json.loads(self.notes)),
                                            Ed25519PrivateKey.generate() if self.wrong_signing_key else None)
            (output / f'update-{opts["--channel"]}.json').write_bytes(self.signed_bytes)
            for arch in ('x64', 'arm64'):
                xml = self.appcast_xml(arch, opts['--channel'])
                if self.appcast_mutation:
                    xml = self.appcast_mutation(xml)
                (output / f'macos-{arch}-{opts["--channel"]}.xml').write_text(xml)
            if self.race:
                self.stored['stable.json'] = self.signed(self.payload(metadataSequence='1800000000999'))
                self.blob_sha = 'newer-sha'
            return subprocess.CompletedProcess(command, 0, '', '')
        path = command[2].removeprefix('repos/example/ZzLogg/')
        method = command[command.index('--method') + 1]
        data = json.loads(kwargs['input']) if kwargs.get('input') else None
        status, value = 0, None
        if path == 'releases/latest' or path == 'releases/tags/continuous-build':
            if self.release is None:
                status = 404
            else:
                value = self.release
        elif path == 'releases/123/assets?per_page=100&page=1':
            value = self.remote_assets() if self.assets is None else self.assets
        elif path == 'git/ref/heads/update-feed':
            value = {'object': {'sha': 'b' * 40}}
            if not self.branch:
                status = 404
        elif path == 'git/commits/' + 'b' * 40:
            value = {'tree': {'sha': 'a' * 40}}
        elif path == 'git/refs/heads/update-feed':
            self.assertEqual(method, 'PATCH')
            self.assertFalse(data['force'])
            self.updates.append(data)
            if self.conflict or self.race:
                status = 409
            else:
                self.stored.update({entry['path']: entry['content'].encode('utf-8')
                                    for entry in self.created_tree['tree']})
        elif path == 'git/ref/heads/master':
            value = {'object': {'sha': 'c' * 40}}
        elif path == 'git/trees':
            self.assertEqual(method, 'POST')
            self.created_tree = data
            value = {'sha': 'd' * 40}
        elif path == 'git/commits':
            self.assertEqual(method, 'POST')
            self.created_commit = data
            value = {'sha': 'e' * 40}
        elif path == 'git/refs':
            self.assertEqual(data['ref'], 'refs/heads/update-feed')
            if self.branch_creation_conflict:
                status = 422
            else:
                self.branch = True
                if data['sha'] == 'c' * 40:
                    self.stored['src/main.cpp'] = b'source inherited from master'
                else:
                    self.assertEqual(data['sha'], 'e' * 40)
                    self.assertEqual(self.created_commit['tree'], 'd' * 40)
                    self.stored = {entry['path']: entry['content'].encode('utf-8')
                                   for entry in self.created_tree['tree']}
        elif path.startswith('contents/'):
            name = path.removeprefix('contents/').split('?')[0]
            if method == 'GET':
                if name in self.stored:
                    value = {'sha': self.blob_sha, 'type': 'file', 'encoding': 'base64',
                             'content': base64.b64encode(self.stored[name]).decode(), 'size': len(self.stored[name])}
                else:
                    status = 404
            else:
                self.updates.append(data)
                if self.conflict or data.get('sha') != (self.blob_sha if name in self.stored else None):
                    status = 409
                else:
                    self.assertEqual(data.get('sha'), self.blob_sha if name in self.stored else None)
                    self.stored[name] = base64.b64decode(data['content'])
        else:
            raise AssertionError((path, method, data))
        return subprocess.CompletedProcess(command, int(status != 0), json.dumps(value), f'HTTP {status}' if status else '')

    def network(self, request, **kwargs):
        name = request.full_url.rsplit('/', 1)[-1]
        return Response(json.dumps(self.info).encode() if name.startswith('release-info-') else self.package_overrides.get(name, self.package))

    def publish(self, channel='stable'):
        self.assertTrue(hasattr(feed, 'publish_channel'), 'feed publishing implementation is missing')
        with patch('subprocess.run', side_effect=self.run_process), patch.object(feed, 'open_download', side_effect=self.network), patch.object(feed, 'TRUST_KEY_PATH', self.trust_file, create=True):
            return feed.publish_channel(feed.GitHub('example/ZzLogg'), channel, 'zzlogg-update-2026-09', now_ms=self.now_ms)

    def test_verified_signed_output_is_stored_with_version_sequence(self):
        self.publish()
        self.assertEqual(self.stored['stable.json'], self.signed_bytes)
        self.assertEqual(set(self.stored), {'stable.json', 'macos-x64-stable.xml', 'macos-arm64-stable.xml'})
        self.assertEqual(self.sign_options['--metadata-sequence'], '1800000000123')
        self.assertEqual(self.sign_options['--release-sequence'], '260900')
        self.assertEqual(self.sign_options['--ttl-seconds'], '1209600')
        self.assertEqual(self.sign_options['--private-key-env'], 'ZZLOGG_UPDATE_SIGNING_KEY')
        self.assertIn('Fixed logs.', self.notes)
        self.assertNotIn('hidden', self.notes)
        self.assertNotIn('**', self.notes)

    def test_existing_file_sha_is_used_and_other_channel_survives(self):
        self.stored = {'stable.json': self.signed(self.payload()), 'preview.json': b'keep'}
        self.seed_stable_appcasts()
        self.publish()
        self.assertEqual(self.stored['preview.json'], b'keep')
        self.assertEqual(self.created_commit['parents'], ['b' * 40])
        self.assertEqual(self.created_tree['base_tree'], 'a' * 40)

    def test_concurrent_contents_change_fails_without_retry_or_overwrite(self):
        original = self.signed(self.payload())
        self.stored = {'stable.json': original}
        self.seed_stable_appcasts()
        self.conflict = True
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.stored['stable.json'], original)
        self.assertEqual(len(self.updates), 1)

    def test_first_branch_contains_only_manifest_and_has_no_source_parent(self):
        self.branch = False
        self.publish()
        self.assertTrue(self.branch)
        self.assertEqual(self.stored['stable.json'], self.signed_bytes)
        self.assertEqual(set(self.stored), {'stable.json', 'macos-x64-stable.xml', 'macos-arm64-stable.xml'})
        self.assertEqual(self.created_commit['parents'], [])
        self.assertNotIn('base_tree', self.created_tree)
        self.assertEqual(self.updates, [])

    def test_concurrent_branch_creation_is_not_overwritten(self):
        self.branch = False
        self.branch_creation_conflict = True
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.stored, {})
        self.assertEqual(self.updates, [])

    def test_deleted_branch_after_baseline_read_is_not_recreated(self):
        self.branch = False
        self.stored['stable.json'] = self.signed(self.payload())
        original = self.stored.copy()
        with self.assertRaises(ValueError):
            self.publish()
        self.assertFalse(self.branch)
        self.assertEqual(self.stored, original)

    def test_tag_label_draft_or_channel_mismatch_never_publishes(self):
        mutations = [(self.release, 'tag_name', 'continuous-build'), (self.release, 'draft', True),
                     (self.release, 'prerelease', True), (self.info, 'tag', 'v26.09.01'),
                     (self.info, 'artifact_label', '26.09.01')]
        for target, key, value in mutations:
            old = target[key]
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                target[key] = value
                self.publish()
            target[key] = old
            self.assertEqual(self.stored, {})
        with self.assertRaises(ValueError):
            self.publish('preview')

    def test_ambiguous_missing_unsafe_or_oversized_attachment_never_publishes(self):
        good = self.remote_assets()
        bad_url = dict(good[0], browser_download_url='https://evil.example/release-info.json')
        for assets in [[], [good[0], good[0], good[1]], [bad_url, good[1]],
                       [dict(good[0], size=1048577), good[1]],
                       [good[0], dict(good[1], size=536870913)],
                       [dict(good[0], name='../release-info-26.09.00.json'), good[1]]]:
            self.assets = assets
            with self.subTest(assets=assets), self.assertRaises(ValueError):
                self.publish()
            self.assertEqual(self.stored, {})

    def test_hash_or_signing_failure_never_creates_branch_or_publishes(self):
        self.branch = False
        self.info['assets'][0]['sha256'] = '0' * 64
        with self.assertRaises(ValueError):
            self.publish()
        self.assertFalse(self.branch)
        self.info['assets'][0]['sha256'] = hashlib.sha256(self.package).hexdigest()
        self.sign_failure = True
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertFalse(self.branch)
        self.assertEqual(self.stored, {})

    def test_every_required_package_must_exist_before_publication(self):
        good = self.remote_assets()
        for asset in good[1:]:
            self.assets = [candidate for candidate in good if candidate != asset]
            with self.subTest(name=asset['name']), self.assertRaises(ValueError):
                self.publish()
            self.assertEqual(self.stored, {})

    def test_missing_preview_is_skipped_but_missing_stable_fails(self):
        self.release = None
        self.assertFalse(self.publish('preview'))
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.stored, {})

    def test_preview_has_separate_tag_label_and_destination(self):
        self.label = 'continuous-20260920-123-1-aaaaaaaaaaaa'
        self.info.update(artifact_label=self.label, tag='continuous-build', release_sequence='1234567890')
        for asset in self.info['assets']:
            asset['name'] = asset['name'].replace('26.09.00', self.label)
        self.info['assets'][0]['name'] = f'ZzLogg-{self.label}-windows-x64-setup.exe'
        self.release.update(tag_name='continuous-build', prerelease=True)
        self.publish('preview')
        self.assertEqual(set(self.stored), {'preview.json', 'macos-x64-preview.xml', 'macos-arm64-preview.xml'})


    def test_fixed_preview_filename_is_accepted(self):
        self.label = 'Continuous-Build'
        self.info.update(artifact_label=self.label, tag='continuous-build', release_sequence='1234567890')
        for asset in self.info['assets']:
            asset['name'] = asset['name'].replace('26.09.00', self.label)
        self.info['assets'][0]['name'] = 'ZzLogg-Continuous-Build-windows-x64-setup.exe'
        self.release.update(tag_name='continuous-build', prerelease=True)
        self.publish('preview')
        self.assertEqual(set(self.stored), {'preview.json', 'macos-x64-preview.xml', 'macos-arm64-preview.xml'})

    def test_preview_label_lookalikes_never_publish_feed(self):
        for label in ['continuous-build', 'Continuous-build', 'Continuous-Build-extra']:
            self.label = label
            self.info.update(artifact_label=label, tag='continuous-build')
            self.info['assets'][0]['name'] = f'ZzLogg-{label}-windows-x64-setup.exe'
            self.release.update(tag_name='continuous-build', prerelease=True)
            with self.subTest(label=label), self.assertRaises(ValueError):
                self.publish('preview')
            self.assertEqual(self.updates, [])

    def test_all_continues_preview_after_stable_failure_and_returns_failure(self):
        self.label = 'continuous-20260920-123-1-aaaaaaaaaaaa'
        self.info.update(artifact_label=self.label, tag='continuous-build', release_sequence='1234567890')
        for asset in self.info['assets']:
            asset['name'] = asset['name'].replace('26.09.00', self.label)
        self.info['assets'][0]['name'] = f'ZzLogg-{self.label}-windows-x64-setup.exe'
        self.release.update(tag_name='continuous-build', prerelease=True)
        def boundary(command, **kwargs):
            if command[:3] == ['gh', 'api', 'repos/example/ZzLogg/releases/latest']:
                return subprocess.CompletedProcess(command, 1, '', 'HTTP 403 SECRET_EXCEPTION_SENTINEL')
            return self.run_process(command, **kwargs)
        output, errors = io.StringIO(), io.StringIO()
        with patch('sys.argv', ['publish_update_feed.py', '--repository', 'example/ZzLogg']), \
                patch('subprocess.run', side_effect=boundary), \
                patch.object(feed, 'open_download', side_effect=self.network), \
                patch.object(feed, 'TRUST_KEY_PATH', self.trust_file), \
                patch('sys.stdout', output), patch('sys.stderr', errors):
            try:
                status = feed.main()
            except Exception:
                self.fail('One channel failure must not interrupt the other channel')
        self.assertEqual(status, 1)
        self.assertEqual(set(self.stored), {'preview.json', 'macos-x64-preview.xml', 'macos-arm64-preview.xml'})
        self.assertIn('stable', errors.getvalue())
        self.assertNotIn('SECRET_EXCEPTION_SENTINEL', output.getvalue() + errors.getvalue())

    def test_wrong_new_signing_key_never_publishes(self):
        self.wrong_signing_key = True
        with self.assertRaises(ValueError):
            self.publish()
        self.assertEqual(self.updates, [])

    def test_appcast_mismatch_or_signature_failure_prevents_all_publication(self):
        mutations = [
            lambda xml: xml.replace('sparkle:version="260900"', 'sparkle:version="260901"'),
            lambda xml: xml.replace('13.3', '12.0'),
            lambda xml: xml.replace('https://github.com/', 'https://evil.invalid/'),
            lambda xml: xml.replace(f'length="{len(self.package)}"', 'length="1"'),
            lambda xml: xml.replace(base64.b64encode(self.key.sign(self.package)).decode(),
                                    base64.b64encode(bytes(64)).decode()),
        ]
        for mutation in mutations:
            self.appcast_mutation = mutation
            with self.subTest(mutation=mutations.index(mutation)), self.assertRaises(ValueError):
                self.publish()
            self.assertEqual(self.stored, {})
            self.assertIsNone(self.created_tree)

    def test_invalid_old_signature_channel_or_product_never_publishes(self):
        candidates = [b'bad json', self.signed(self.payload(), Ed25519PrivateKey.generate()),
                      self.signed(self.payload(channel='preview')),
                      self.signed(self.payload(product='another-product'))]
        for original in candidates:
            self.stored = {'stable.json': original}
            with self.subTest(original=original[:24]), self.assertRaises(ValueError):
                self.publish()
            self.assertEqual(self.stored['stable.json'], original)
        self.assertEqual(self.updates, [])

    def test_metadata_or_release_regression_is_rejected(self):
        for changes in [{'metadataSequence': '1800000000123'}, {'metadataSequence': '1800000000999'},
                        {'version': '26.09.01', 'releaseSequence': '260901'},
                        {'releaseSequence': '260901'}]:
            self.stored = {'stable.json': self.signed(self.payload(**changes))}
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.publish()
        self.assertEqual(self.updates, [])

    def test_same_version_stable_payload_cannot_change(self):
        self.stored = {'stable.json': self.signed(self.payload(artifacts=[{'sha256': '0' * 64}]))}
        with self.assertRaises(ValueError):
            self.publish()
        self.assertEqual(self.updates, [])

    def test_stable_notes_can_be_edited_when_renewing_same_package(self):
        self.stored = {'stable.json': self.signed(self.payload(notes={language: 'Old notes' for language in ('en', 'zh_CN', 'zh_TW')}))}
        self.seed_stable_appcasts()
        try:
            self.publish()
        except ValueError:
            self.fail('Editing release notes must not prevent stable renewal')
        current = json.loads(base64.b64decode(json.loads(self.stored['stable.json'])['payload']))
        self.assertEqual(current['notes']['en'], 'Changes\n\nFixed logs.')
        self.assertEqual(current['artifacts'], [{'sha256': hashlib.sha256(self.package).hexdigest()}])

    def test_same_version_stable_dmg_replacement_never_changes_any_feed(self):
        self.publish()
        published = self.stored.copy()
        self.now_ms += 1000
        self.updates.clear()
        self.created_tree = None
        for arch in ('x64', 'arm64'):
            name = f'ZzLogg-{self.label}-macos-{arch}.dmg'
            replacement = b'resigned stable DMG with different bytes'
            self.package_overrides[name] = replacement
            asset = next(asset for asset in self.info['assets'] if asset['name'] == name)
            asset.update(size=len(replacement), sha256=hashlib.sha256(replacement).hexdigest())
            with self.subTest(arch=arch), self.assertRaises(ValueError):
                self.publish()
            self.assertEqual(self.stored, published)
            self.assertEqual(self.updates, [])
            self.assertIsNone(self.created_tree)
            self.package_overrides.pop(name)
            asset.update(size=len(self.package), sha256=hashlib.sha256(self.package).hexdigest())

    def test_preview_same_version_package_can_change(self):
        self.label = 'continuous-20260920-123-1-aaaaaaaaaaaa'
        self.info.update(artifact_label=self.label, tag='continuous-build', release_sequence='1234567890')
        for asset in self.info['assets']:
            asset['name'] = asset['name'].replace('26.09.00', self.label)
        self.info['assets'][0]['name'] = f'ZzLogg-{self.label}-windows-x64-setup.exe'
        self.release.update(tag_name='continuous-build', prerelease=True)
        self.stored = {'preview.json': self.signed(self.payload(channel='preview', artifacts=[{'sha256': '0' * 64}]))}
        self.publish('preview')
        self.assertEqual(self.stored['preview.json'], self.signed_bytes)

    def test_concurrent_newer_feed_during_signing_cannot_be_overwritten(self):
        self.stored = {'stable.json': self.signed(self.payload())}
        self.seed_stable_appcasts()
        self.race = True
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertNotEqual(self.stored['stable.json'], self.signed_bytes)
        self.assertEqual(self.created_commit['parents'], ['b' * 40])
        self.assertEqual(self.created_tree['base_tree'], 'a' * 40)
        self.assertEqual(len(self.updates), 1)

    def test_download_enforces_actual_byte_limit_and_redirect_origin(self):
        self.assertTrue(hasattr(feed, 'download'), 'bounded download implementation is missing')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'asset'
            with patch.object(feed, 'open_download', return_value=Response(b'12345')):
                with self.assertRaises(ValueError):
                    feed.download('https://github.com/example/ZzLogg/releases/download/v26.09.00/file', path, 4)
            self.assertFalse(path.exists())
        for url in ['http://github.com/x', 'https://evil.example/x', 'https://github.com.evil.example/x',
                    'https://user@github.com/x', 'https://github.com:444/x']:
            with self.subTest(url=url), self.assertRaises(ValueError):
                feed.validate_download_url(url)


if __name__ == '__main__':
    unittest.main()
