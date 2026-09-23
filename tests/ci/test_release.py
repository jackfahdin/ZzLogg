import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
from release_assets import assemble, project_version, validate_tag
import prepare_release
from publish_release import publish, snapshot_unchanged, tag_commit, find_release

SHA = 'a' * 40
VERSION = '26.09.00'


class FakeGitHub:
    """In-memory GitHub API boundary: tests exercise publication ordering/failures."""
    def __init__(self, release=None, sha=SHA):
        self.release = release
        self.sha = sha
        self.assets = []
        self.events = []
        self.fail_upload = False
        self.fail_upload_after = None
        self.fail_promote = False
        self.fail_recreate = False
        self.omit_digest = False
        self.corrupt_upload = False
        self.fail_delete = False
        self.tag_created_with_draft = False
        self.upload_draft_states = []
        self.next_asset_id = 1000

    def api(self, path, *, method='GET', data=None, optional=False):
        self.events.append((method, path, data))
        if method == 'GET':
            if path.startswith('git/ref/tags/'):
                return {'object': {'type': 'commit', 'sha': self.sha}} if self.sha else None
            if path.startswith('releases/tags/'):
                return None if self.release and self.release['draft'] else self.release
            if path.startswith('releases?'):
                return [self.release] if self.release else []
            if '/assets?' in path:
                page = int(path.split('page=')[-1])
                return self.assets[(page-1)*100:page*100]
        if method == 'POST' and path == 'releases':
            if self.fail_recreate and data.get('draft') is False and data.get('prerelease'):
                raise RuntimeError('Simulated nightly listing refresh failure')
            self.release = dict(data, id=7 if self.release is None else self.release.get('id', 7) + 1)
            if self.tag_created_with_draft:
                self.sha = data['target_commitish']
            return self.release
        if path.startswith('git/'):
            self.sha = data['sha']
        if method == 'PATCH' and path.startswith('releases/'):
            if self.fail_promote and data.get('draft') is False:
                raise RuntimeError('Simulated promotion failure')
            if self.release:
                self.release.update(data)
        if method == 'DELETE':
            if self.fail_delete:
                raise RuntimeError('Simulated stale-asset cleanup failure')
            if path.startswith('releases/assets/'):
                asset_id = int(path.split('/')[-1])
                self.assets = [a for a in self.assets if a['id'] != asset_id]
            elif path.startswith('releases/'):
                release_id = int(path.split('/')[-1])
                if self.release and self.release['id'] == release_id:
                    self.release = None
                    self.assets = []
        return None

    def upload(self, tag, files, *, clobber):
        self.events.append(('UPLOAD', tag, clobber))
        self.upload_draft_states.append(self.release['draft'])
        if self.fail_upload:
            raise RuntimeError('Simulated network interruption')
        for index, path in enumerate(files):
            if self.fail_upload_after is not None and index >= self.fail_upload_after:
                raise RuntimeError("Simulated partial upload")
            if clobber:
                self.assets = [a for a in self.assets if a['name'] != path.name]
            self.next_asset_id += 1
            self.assets.append({'id': self.next_asset_id, 'name': path.name,
                                'size': path.stat().st_size,
                                'digest': 'sha256:' + hashlib.sha256(path.read_bytes()).hexdigest()})
        if self.omit_digest:
            self.assets[-1].pop('digest')
        if self.corrupt_upload:
            self.assets[-1]['size'] += 1


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.artifacts = self.root / 'artifacts'
        files = [
            ('klogg_version/klogg_version.txt', VERSION),
            (f'packages-linux/ZzLogg-{VERSION}-Linux.deb', 'deb'),
            (f'packages-linux/ZzLogg-{VERSION}-Linux.rpm', 'rpm'),
            (f'packages-macos-intel-qt6/ZzLogg-{VERSION}-mac-x64.dmg', 'intel'),
            (f'packages-macos-arm-qt6/ZzLogg-{VERSION}-mac-arm64.dmg', 'arm'),
            (f'packages-windows-x64-qt6/ZzLogg-{VERSION}-x64-Qt6-setup.exe', 'installer'),
            (f'packages-windows-x64-qt6/ZzLogg-{VERSION}-x64-Qt6-portable/ZzLogg.exe', 'portable'),
        ]
        for name, value in files:
            path = self.artifacts / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value)
        self.output = self.root / 'release'
        self.changelog = self.root / 'CHANGELOG.md'
        self.changelog.write_text(f'## [Unreleased]\n- Future improvement\n\n## [{VERSION}]\n- New feature\n',
                                  encoding='utf-8')
        fixture = patch('publish_release.DEFAULT_CHANGELOG', self.changelog)
        fixture.start()
        self.addCleanup(fixture.stop)

    def bundle(self, nightly=False):
        return assemble(self.artifacts, self.output, VERSION,
                        'Continuous-Build' if nightly else VERSION,
                        SHA, 'continuous-build' if nightly else 'v' + VERSION, 'https://example.com/run/123')

    def test_version_uses_cmake_export(self):
        (self.root / 'CMakeLists.txt').write_text('project(\n  Test\n  VERSION 26.9.0\n)\n')
        self.assertEqual(project_version(self.root), VERSION)

    def test_tag_must_match_canonical_project_version(self):
        validate_tag('v26.09.00', VERSION)
        for invalid in ['v26.9.0', 'v26.13.00', 'v26.09.01', 'v26.09.00-rc1', 'continuous-build']:
            with self.subTest(tag=invalid), self.assertRaises(ValueError):
                validate_tag(invalid, VERSION)

    def test_complete_bundle_has_six_distinct_packages_and_metadata(self):
        info = self.bundle()
        self.assertEqual(len(info['assets']), 6)
        self.assertEqual(len(list(self.output.iterdir())), 8)
        archive = next(self.output.glob('*.zip'))
        with zipfile.ZipFile(archive) as zipped:
            self.assertEqual(zipped.read(f'ZzLogg-{VERSION}-windows-x64-portable/ZzLogg.exe'), b'portable')
        self.assertEqual((self.output / f'ZzLogg-{VERSION}-macos-x64.dmg').read_text(), 'intel')
        self.assertEqual((self.output / f'ZzLogg-{VERSION}-macos-arm64.dmg').read_text(), 'arm')
        for line in next(self.output.glob('SHA256SUMS-*')).read_text().splitlines():
            checksum, name = line.split('  ')
            self.assertEqual(checksum, hashlib.sha256((self.output / name).read_bytes()).hexdigest())

    def test_portable_zip_digest_does_not_depend_on_artifact_download_time(self):
        first = self.bundle(True)
        portable_exe = next(self.artifacts.rglob('*portable/ZzLogg.exe'))
        os.utime(portable_exe, (1800000000, 1800000000))
        second_output = self.root / 'rerun'
        second = assemble(self.artifacts, second_output, VERSION, first['artifact_label'],
                          SHA, first['tag'], first['workflow_run'])
        self.assertEqual(first['assets'], second['assets'])

    def test_missing_platform_fails_before_creating_release_directory(self):
        next(self.artifacts.rglob('*.rpm')).unlink()
        with self.assertRaises(ValueError):
            self.bundle()
        self.assertFalse(self.output.exists())

    def test_version_mismatch_rejected(self):
        (self.artifacts / 'klogg_version/klogg_version.txt').write_text('26.09.01')
        with self.assertRaises(ValueError):
            self.bundle()

    def test_extra_build_files_are_not_published(self):
        (self.artifacts / 'packages-linux/CMakeCache.txt').write_text('not a download')
        self.bundle()
        self.assertFalse((self.output / 'CMakeCache.txt').exists())

    def test_stable_publishes_only_after_verified_upload(self):
        github = FakeGitHub()
        publish(github, self.output, self.bundle(), 'stable')
        self.assertFalse(github.release['draft'])
        self.assertFalse(github.release['prerelease'])
        self.assertEqual(github.release['name'], 'v' + VERSION)
        self.assertEqual(github.release['make_latest'], 'legacy')
        self.assertIn('New feature', github.release['body'])
        self.assertNotIn('Future improvement', github.release['body'])
        self.assertNotIn('Source commit:', github.release['body'])
        self.assertIn(f'<!-- source-commit: {SHA} -->', github.release['body'])
        methods = [event[0] for event in github.events]
        self.assertLess(methods.index('UPLOAD'), methods.index('PATCH'))
        self.assertFalse(any(method != 'GET' and path.startswith('git/') for method, path, _ in github.events))

    def test_published_stable_release_cannot_be_overwritten(self):
        github = FakeGitHub({'id': 7, 'draft': False})
        with self.assertRaises(ValueError):
            publish(github, self.output, self.bundle(), 'stable')
        self.assertFalse(any(e[0] == 'UPLOAD' for e in github.events))

    def test_missing_changelog_fails_before_any_remote_operation(self):
        self.changelog.write_text('# Empty changelog\n', encoding='utf-8')
        github = FakeGitHub()
        with self.assertRaises(ValueError):
            publish(github, self.output, self.bundle(), 'stable')
        self.assertEqual(github.events, [])

    def test_moved_stable_tag_rejected(self):
        with self.assertRaises(ValueError):
            publish(FakeGitHub(sha='b' * 40), self.output, self.bundle(), 'stable')

    def test_existing_draft_is_found_and_reused_after_upload_failure(self):
        draft = {'id': 7, 'draft': True, 'prerelease': False,
                 'tag_name': 'v' + VERSION, 'target_commitish': SHA}
        github = FakeGitHub(draft)
        publish(github, self.output, self.bundle(), 'stable')
        self.assertFalse(github.release['draft'])
        self.assertFalse(any(method == 'POST' and path == 'releases'
                             for method, path, _ in github.events))

    def test_stable_upload_failure_leaves_draft(self):
        github = FakeGitHub()
        github.fail_upload = True
        with self.assertRaises(RuntimeError):
            publish(github, self.output, self.bundle(), 'stable')
        self.assertTrue(github.release['draft'])

    def test_tampered_package_rejected_before_any_api_call(self):
        info = self.bundle()
        next(self.output.glob('*.deb')).write_bytes(b'tampered')
        github = FakeGitHub()
        with self.assertRaises(ValueError):
            publish(github, self.output, info, 'stable')
        self.assertEqual(github.events, [])

    def test_nightly_upload_failure_hides_release_and_preserves_tag(self):
        github = FakeGitHub({'id': 7, 'draft': False, 'prerelease': True, 'body': 'previous'}, 'b' * 40)
        github.fail_upload = True
        with self.assertRaises(RuntimeError):
            publish(github, self.output, self.bundle(True), 'nightly')
        self.assertEqual(github.sha, 'b' * 40)
        self.assertEqual(github.release['body'], 'previous')
        self.assertTrue(github.release['draft'])
        self.assertFalse(any(e[0] != 'GET' and e[1].startswith('git/') for e in github.events))

    def test_bad_remote_asset_prevents_promotion(self):
        github = FakeGitHub()
        github.corrupt_upload = True
        with self.assertRaises(ValueError):
            publish(github, self.output, self.bundle(), 'stable')
        self.assertTrue(github.release['draft'])

    def test_first_nightly_handles_tag_created_with_draft(self):
        github = FakeGitHub(sha=None)
        github.tag_created_with_draft = True
        publish(github, self.output, self.bundle(True), 'nightly')
        self.assertEqual(github.sha, SHA)
        self.assertFalse(any(method == 'POST' and path == 'git/refs' for method, path, _ in github.events))

    def test_nightly_prunes_before_promotion_and_is_not_latest(self):
        github = FakeGitHub({'id': 7, 'draft': False, 'prerelease': True}, 'b' * 40)
        github.assets = [{'id': i, 'name': f'old-{i}', 'size': 1} for i in range(105)]
        publish(github, self.output, self.bundle(True), 'nightly')
        self.assertEqual(len(github.assets), 8)
        self.assertEqual(github.sha, SHA)
        self.assertEqual(github.release['make_latest'], 'false')
        self.assertEqual(github.release['name'], 'Continuous Build')
        self.assertEqual(github.upload_draft_states, [True, False])
        self.assertIn('Future improvement', github.release['body'])
        self.assertIn('New feature', github.release['body'])
        promote = next(i for i, e in enumerate(github.events)
                        if e[0:2] == ('POST', 'releases') and e[2].get('draft') is False and e[2].get('prerelease'))
        prune = next(i for i, e in enumerate(github.events) if e[0] == 'DELETE')
        self.assertLess(prune, promote)
        self.assertTrue(github.upload_draft_states[0])

    def test_failed_cleanup_rerun_reuses_current_assets_without_upload(self):
        github = FakeGitHub({'id': 7, 'draft': False, 'prerelease': True}, 'b' * 40)
        github.assets = [{'id': 1, 'name': 'old.deb', 'size': 1}]
        github.fail_delete = True
        info = self.bundle(True)
        with self.assertRaises(RuntimeError):
            publish(github, self.output, info, 'nightly')
        self.assertTrue(github.release['draft'])
        self.assertEqual(github.sha, 'b' * 40)
        github.release['tag_name'] = 'continuous-build'
        github.fail_delete = False
        github.fail_upload = False
        publish(github, self.output, info, 'nightly')
        self.assertEqual(len(github.assets), 8)
        self.assertEqual(sum(e[0] == 'UPLOAD' for e in github.events), 2)

    def test_nightly_replaces_conflicting_asset_only_while_hidden(self):
        github = FakeGitHub({'id': 7, 'draft': False, 'prerelease': True}, 'b' * 40)
        info = self.bundle(True)
        name = info['assets'][0]['name']
        github.assets = [{'id': 1, 'name': name, 'size': 1, 'digest': 'wrong'}]
        publish(github, self.output, info, 'nightly')
        self.assertEqual(github.upload_draft_states, [True, False])
        self.assertEqual(sum(a['name'] == name for a in github.assets), 1)
        self.assertEqual(len(github.assets), 8)
        self.assertFalse(github.release['draft'])
        hide = next(i for i, e in enumerate(github.events)
                    if e[:2] == ('PATCH', 'releases/7') and e[2].get('draft') is True)
        upload = next(i for i, e in enumerate(github.events) if e[0] == 'UPLOAD')
        self.assertLess(hide, upload)

    def test_partial_preview_replacement_stays_hidden_and_retry_recovers(self):
        github = FakeGitHub({'id': 7, 'draft': False, 'prerelease': True,
                             'tag_name': 'continuous-build'}, 'b' * 40)
        info = self.bundle(True)
        github.assets = [{'id': 9, 'name': info['assets'][0]['name'], 'size': 1, 'digest': 'old'}]
        github.fail_upload_after = 3
        with self.assertRaises(RuntimeError):
            publish(github, self.output, info, 'nightly')
        uploaded_ids = {a['name']: a['id'] for a in github.assets}
        self.assertEqual(len(uploaded_ids), 3)
        self.assertTrue(github.release['draft'])
        self.assertEqual(github.sha, 'b' * 40)
        github.fail_upload_after = None
        publish(github, self.output, info, 'nightly')
        self.assertEqual(len(github.assets), 8)
        self.assertFalse(github.release['draft'])
        self.assertEqual(github.sha, SHA)
        for asset in github.assets:
            if asset['name'] in uploaded_ids:
                self.assertGreaterEqual(asset['id'], uploaded_ids[asset['name']])

    def test_final_preview_publication_failure_is_resumable_after_tag_moves(self):
        github = FakeGitHub({'id': 7, 'draft': False, 'prerelease': True,
                             'tag_name': 'continuous-build'}, 'b' * 40)
        info = self.bundle(True)
        github.fail_recreate = True
        with self.assertRaises(RuntimeError):
            publish(github, self.output, info, 'nightly')
        self.assertTrue(github.release is None or github.release.get('draft'))
        self.assertEqual(github.sha, SHA)
        github.fail_recreate = False
        publish(github, self.output, info, 'nightly')
        self.assertFalse(github.release['draft'])

    def test_missing_remote_digest_prevents_publication(self):
        github = FakeGitHub()
        github.omit_digest = True
        with self.assertRaises(ValueError):
            publish(github, self.output, self.bundle(True), 'nightly')
        self.assertTrue(github.release['draft'])

    def test_nightly_names_are_short_and_zip_root_matches(self):
        info = self.bundle(True)
        self.assertTrue(all(asset['name'].startswith('ZzLogg-Continuous-Build-') for asset in info['assets']))
        self.assertTrue((self.output / 'release-info-Continuous-Build.json').is_file())
        self.assertTrue((self.output / 'SHA256SUMS-Continuous-Build.txt').is_file())
        with zipfile.ZipFile(next(self.output.glob('*.zip'))) as archive:
            self.assertEqual(archive.namelist(), ['ZzLogg-Continuous-Build-windows-x64-portable/ZzLogg.exe'])

    def test_prepare_uses_fixed_preview_label_without_run_identity_suffix(self):
        output = self.root / 'github-output'
        with patch.dict(os.environ, {'GITHUB_REF_NAME': 'master', 'GITHUB_EVENT_NAME': 'workflow_dispatch',
                                     'GITHUB_OUTPUT': str(output)}, clear=True), \
                patch('sys.argv', ['prepare_release.py', '--mode', 'nightly']), \
                patch.object(prepare_release, 'project_version', return_value=VERSION), \
                patch.object(prepare_release, 'release_changes', return_value='Changes'), \
                patch.object(prepare_release.subprocess, 'check_output', return_value=SHA):
            prepare_release.main()
        self.assertIn('label=Continuous-Build\n', output.read_text())
        self.assertIn(f'sha={SHA}\n', output.read_text())

    def test_only_completed_matching_snapshot_is_skipped(self):
        github = FakeGitHub({'draft': False, 'prerelease': True, 'body': f'<!-- source-commit: {SHA} -->'})
        self.assertTrue(snapshot_unchanged(github, SHA))
        github.sha = 'b' * 40
        self.assertFalse(snapshot_unchanged(github, SHA))
        github.sha = SHA
        github.release['draft'] = True
        self.assertFalse(snapshot_unchanged(github, SHA))


if __name__ == '__main__':
    unittest.main()
