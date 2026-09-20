"""GitHub release operations. Only called by the publication jobs, never by CI tests."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


class GitHub:
    def __init__(self, repository):
        if not re.fullmatch(r'[\w.-]+/[\w.-]+', repository):
            raise ValueError('Invalid repository')
        self.repository = repository

    def api(self, path, *, method='GET', data=None, optional=False):
        command = ['gh', 'api', f'repos/{self.repository}/{path}', '--method', method]
        if data is not None:
            command += ['--input', '-']
        result = subprocess.run(command, input=json.dumps(data) if data is not None else None,
                                capture_output=True, text=True)
        if result.returncode:
            if optional and 'HTTP 404' in result.stderr:
                return None
            raise RuntimeError(result.stderr.strip())
        return json.loads(result.stdout) if result.stdout.strip() else None

    def upload(self, tag, files, *, clobber):
        command = ['gh', 'release', 'upload', tag, *map(str, files), '--repo', self.repository]
        if clobber:
            command.append('--clobber')
        subprocess.run(command, check=True)


def tag_commit(github, tag):
    ref = github.api(f'git/ref/tags/{tag}', optional=True)
    if ref is None:
        return None
    obj = ref['object']
    for _ in range(10):
        if obj['type'] == 'commit':
            return obj['sha']
        if obj['type'] != 'tag':
            raise ValueError('Release tag does not point to a commit')
        obj = github.api(f'git/tags/{obj["sha"]}')['object']
    raise ValueError('Too many nested annotated tags')


def find_release(github, tag):
    # GitHub's tag endpoint only locates published releases. Draft recovery
    # must also search the authenticated release list (as gh release does).
    release = github.api(f'releases/tags/{tag}', optional=True)
    if release is not None:
        return release
    page = 1
    while True:
        releases = github.api(f'releases?per_page=100&page={page}')
        for release in releases:
            if release['tag_name'] == tag:
                return release
        if len(releases) < 100:
            return None
        page += 1


def snapshot_unchanged(github, sha):
    release = github.api('releases/tags/continuous-build', optional=True)
    return bool(release and not release['draft'] and release['prerelease']
                and f'<!-- source-commit: {sha} -->' in (release.get('body') or '')
                and tag_commit(github, 'continuous-build') == sha)


def verified_files(directory, info):
    directory = Path(directory)
    label = info['artifact_label']
    expected = {asset['name'] for asset in info['assets']}
    if len(expected) != 6:
        raise ValueError('Release must contain exactly six application packages')
    expected |= {f'release-info-{label}.json', f'SHA256SUMS-{label}.txt'}
    files = sorted(directory.iterdir())
    if {p.name for p in files} != expected or any(not p.is_file() for p in files):
        raise ValueError('Unexpected or missing release files')
    for asset in info['assets']:
        path = directory / asset['name']
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if path.stat().st_size != asset['size'] or digest != asset['sha256']:
            raise ValueError(f'Package changed after assembly: {path.name}')
    return files


def list_assets(github, release_id):
    assets = []
    page = 1
    while True:
        batch = github.api(f'releases/{release_id}/assets?per_page=100&page={page}')
        assets.extend(batch)
        if len(batch) < 100:
            return assets
        page += 1


def file_digest(path):
    with path.open('rb') as stream:
        return 'sha256:' + hashlib.file_digest(stream, 'sha256').hexdigest()


def publish(github, directory, info, mode):
    tag, sha = info['tag'], info['source_commit']
    files = verified_files(directory, info)
    nightly = mode == 'nightly'
    if nightly and tag != 'continuous-build':
        raise ValueError('Nightly publication must use continuous-build')
    if not nightly and tag != f'v{info["version"]}':
        raise ValueError('Stable tag/version mismatch')
    old_sha = tag_commit(github, tag)
    if not nightly and old_sha != sha:
        raise ValueError('Stable tag moved or no longer exists; refusing publication')
    release = find_release(github, tag)
    if release and not nightly and not release['draft']:
        raise ValueError('Published stable releases are immutable; create a new version')
    if release and not nightly and release['target_commitish'] != sha:
        raise ValueError('Existing draft targets a different source commit')
    if release and nightly and not release['prerelease']:
        raise ValueError('Refusing to overwrite a stable release as a snapshot')
    if release is None:
        release = github.api('releases', method='POST', data={
            'tag_name': tag, 'target_commitish': sha, 'draft': True,
            'prerelease': nightly, 'name': f'ZzLogg {tag}', 'make_latest': 'false'})
    # A failed-job rerun reuses prepare's old label, even when the run attempt
    # increments. Never clobber an already public snapshot file on that retry.
    upload = files
    if nightly:
        existing = {asset['name']: asset for asset in list_assets(github, release['id'])}
        upload = []
        for path in files:
            asset = existing.get(path.name)
            if asset is None:
                upload.append(path)
            elif asset['size'] != path.stat().st_size or asset.get('digest') != file_digest(path):
                raise ValueError(f'Conflicting snapshot asset: {path.name}; rerun all jobs for a new label')
    # Old snapshot files remain available if this upload or verification fails.
    if upload:
        github.upload(tag, upload, clobber=not nightly)
    assets = list_assets(github, release['id'])
    remote = {asset['name']: asset for asset in assets}
    for path in files:
        asset = remote.get(path.name)
        if asset is None or asset['size'] != path.stat().st_size:
            raise ValueError(f'Uploaded asset is missing or incomplete: {path.name}')
        if asset.get('digest'):
            if asset['digest'] != file_digest(path):
                raise ValueError(f'Uploaded asset digest mismatch: {path.name}')
    if nightly:
        # Only the rolling tag may move. Version tags are never created/moved here.
        if tag_commit(github, tag) is None:
            github.api('git/refs', method='POST', data={'ref': f'refs/tags/{tag}', 'sha': sha})
        else:
            github.api(f'git/refs/tags/{tag}', method='PATCH', data={'sha': sha, 'force': True})
    elif tag_commit(github, tag) != sha:
        raise ValueError('Stable tag changed during upload')
    notes = (
        f'ZzLogg {info["version"]}' + (' — 每日测试版' if nightly else '') + '\n\n'
        f'Source commit: {sha}\n\nBuild: {info["workflow_run"]}\n\n'
        '包含 Windows x64 安装包和便携包、macOS x64/arm64 DMG、Linux x64 DEB/RPM。\n\n'
        'Linux 基线：Ubuntu 24.04。安装包签名状态以各平台验证结果为准。\n'
        '本次仅提供手动下载安装，不启用应用内自动更新。\n\n'
        + ('每日北京时间 00:00 触发，有新提交时更新；GitHub 调度及构建可能延迟。\n'
           '应用内版本号沿用源码版本；同版本 Linux 快照可能需要显式重新安装。\n\n' if nightly else '')
        + f'<!-- source-commit: {sha} -->\n')
    github.api(f'releases/{release["id"]}', method='PATCH', data={
        'draft': False, 'prerelease': nightly, 'name': 'ZzLogg Continuous Build' if nightly else f'ZzLogg {tag}',
        'body': notes, 'make_latest': 'false' if nightly else 'legacy'})
    if nightly:
        keep = {path.name for path in files}
        for asset in assets:
            if asset['name'] not in keep:
                github.api(f'releases/assets/{asset["id"]}', method='DELETE')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--directory', required=True)
    parser.add_argument('--mode', choices=['stable', 'nightly'], required=True)
    args = parser.parse_args()
    metadata = list(Path(args.directory).glob('release-info-*.json'))
    if len(metadata) != 1:
        raise ValueError('Exactly one release metadata file is required')
    publish(GitHub(args.repository), args.directory, json.loads(metadata[0].read_text()), args.mode)


if __name__ == '__main__':
    main()
