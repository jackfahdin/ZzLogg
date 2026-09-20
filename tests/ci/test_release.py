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
        self.corrupt_upload = False
        self.fail_delete = False
        self.tag_created_with_draft = False

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
            self.release = dict(data, id=7)
            if self.tag_created_with_draft:
                self.sha = data['target_commitish']
            return self.release
        if path.startswith('git/'):
            self.sha = data['sha']
        if method == 'PATCH' and path == 'releases/7':
            self.release.update(data)
        if method == 'DELETE':
            if self.fail_delete:
                raise RuntimeError('Simulated stale-asset cleanup failure')
            self.assets = [a for a in self.assets if str(a['id']) != path.split('/')[-1]]
        return None

    def upload(self, tag, files, *, clobber):
        self.events.append(('UPLOAD', tag, clobber))
        if self.fail_upload:
            raise RuntimeError('Simulated network interruption')
        for path in files:
            self.assets.append({'id': 1000 + len(self.assets), 'name': path.name,
                                'size': path.stat().st_size,
                                'digest': 'sha256:' + hashlib.sha256(path.read_bytes()).hexdigest()})
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
                        'continuous-20260920-123-1-aaaaaaaaaaaa' if nightly else VERSION,
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
        self.assertEqual(github.release['make_latest'], 'legacy')
        self.assertIn('New feature', github.release['body'])
        self.assertNotIn('Future improvement', github.release['body'])
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

    def test_nightly_upload_failure_preserves_old_release_and_tag(self):
        github = FakeGitHub({'id': 7, 'draft': False, 'prerelease': True, 'body': 'previous'}, 'b' * 40)
        github.fail_upload = True
        with self.assertRaises(RuntimeError):
            publish(github, self.output, self.bundle(True), 'nightly')
        self.assertEqual(github.sha, 'b' * 40)
        self.assertEqual(github.release['body'], 'previous')
        self.assertFalse(any(e[0] in ['DELETE', 'PATCH'] for e in github.events))

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

    def test_nightly_promotes_before_pruning_old_assets_and_is_not_latest(self):
        github = FakeGitHub({'id': 7, 'draft': False, 'prerelease': True}, 'b' * 40)
        github.assets = [{'id': i, 'name': f'old-{i}', 'size': 1} for i in range(105)]
        publish(github, self.output, self.bundle(True), 'nightly')
        self.assertEqual(len(github.assets), 8)
        self.assertEqual(github.sha, SHA)
        self.assertEqual(github.release['make_latest'], 'false')
        self.assertIn('Future improvement', github.release['body'])
        self.assertIn('New feature', github.release['body'])
        promote = next(i for i, e in enumerate(github.events) if e[0:2] == ('PATCH', 'releases/7'))
        prune = next(i for i, e in enumerate(github.events) if e[0] == 'DELETE')
        self.assertLess(promote, prune)

    def test_failed_cleanup_rerun_reuses_current_assets_without_upload(self):
        github = FakeGitHub({'id': 7, 'draft': False, 'prerelease': True}, 'b' * 40)
        github.assets = [{'id': 1, 'name': 'old.deb', 'size': 1}]
        github.fail_delete = True
        info = self.bundle(True)
        with self.assertRaises(RuntimeError):
            publish(github, self.output, info, 'nightly')
        self.assertFalse(github.release['draft'])
        github.fail_delete = False
        github.fail_upload = True
        publish(github, self.output, info, 'nightly')
        self.assertEqual(len(github.assets), 8)
        self.assertEqual(sum(e[0] == 'UPLOAD' for e in github.events), 1)

    def test_nightly_refuses_conflicting_asset_without_deleting_it(self):
        github = FakeGitHub({'id': 7, 'draft': False, 'prerelease': True}, 'b' * 40)
        info = self.bundle(True)
        github.assets = [{'id': 1, 'name': info['assets'][0]['name'], 'size': 1, 'digest': 'wrong'}]
        with self.assertRaises(ValueError):
            publish(github, self.output, info, 'nightly')
        self.assertFalse(any(e[0] in ['DELETE', 'PATCH', 'UPLOAD'] for e in github.events))

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
