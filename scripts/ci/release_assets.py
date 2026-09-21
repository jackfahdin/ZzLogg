"""Prepare a complete, auditable release from this workflow run's artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile


PREVIEW_LABEL = 'Continuous-Build'


def is_preview_label(label):
    # Accept historical attachments until the next preview replaces them.
    return isinstance(label, str) and (label == PREVIEW_LABEL or bool(
        re.fullmatch(r'continuous-[0-9]{8}-[0-9]+-[0-9]+-[0-9a-f]{12}', label)))


def project_version(source):
    with tempfile.TemporaryDirectory() as temporary:
        env_file = Path(temporary) / 'version.env'
        subprocess.run(['cmake', f'-DSOURCE_ROOT={Path(source).resolve()}',
                        f'-DVERSION_ENV_FILE={env_file}', '-P',
                        str(Path(__file__).resolve().parents[2] / 'cmake/ExportCiVersion.cmake')],
                       check=True, capture_output=True, text=True)
        return env_file.read_text().strip().removeprefix('KLOGG_VERSION=')


def validate_tag(tag, version):
    if not re.fullmatch(r'v[0-9]{2}\.(?:0[1-9]|1[0-2])\.[0-9]{2}', tag):
        raise ValueError('Release tag must be vYY.MM.PP, for example v26.09.00')
    if tag != f'v{version}':
        raise ValueError(f'Tag {tag} does not match project version {version}')


def assemble(artifacts, output, version, label, sha, tag, run_url):
    if not re.fullmatch(r'[0-9a-f]{40}', sha):
        raise ValueError('Source commit must be a full SHA')
    if not re.fullmatch(r'[A-Za-z0-9._-]+', label):
        raise ValueError('Invalid artifact label')
    artifacts, output = Path(artifacts), Path(output)
    version_file = artifacts / 'klogg_version' / 'klogg_version.txt'
    if version_file.read_text().strip() != version:
        raise ValueError('Build artifact version does not match release version')
    sources = {
        f'ZzLogg-{label}-linux-x64.deb': artifacts / 'packages-linux' / f'ZzLogg-{version}-Linux.deb',
        f'ZzLogg-{label}-linux-x64.rpm': artifacts / 'packages-linux' / f'ZzLogg-{version}-Linux.rpm',
        f'ZzLogg-{label}-macos-x64.dmg': artifacts / 'packages-macos-intel-qt6' / f'ZzLogg-{version}-mac-x64.dmg',
        f'ZzLogg-{label}-macos-arm64.dmg': artifacts / 'packages-macos-arm-qt6' / f'ZzLogg-{version}-mac-arm64.dmg',
        f'ZzLogg-{label}-windows-x64-setup.exe': artifacts / 'packages-windows-x64-qt6' / f'ZzLogg-{version}-x64-Qt6-setup.exe',
    }
    portable = artifacts / 'packages-windows-x64-qt6' / f'ZzLogg-{version}-x64-Qt6-portable'
    for path in [*sources.values(), portable / 'ZzLogg.exe']:
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f'Missing or empty release payload: {path}')
    if output.exists() and any(output.iterdir()):
        raise ValueError('Release output directory must be empty')
    output.mkdir(parents=True, exist_ok=True)
    for name, path in sources.items():
        shutil.copyfile(path, output / name)
    portable_name = f'ZzLogg-{label}-windows-x64-portable'
    with zipfile.ZipFile(output / f'{portable_name}.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(portable.rglob('*')):
            if path.is_symlink():
                raise ValueError(f'Unexpected symlink in Windows portable package: {path}')
            if path.is_file():
                # download-artifact resets mtimes. Keep identical bytes on a
                # failed-job rerun so already uploaded snapshot assets are reusable.
                member = zipfile.ZipInfo((Path(portable_name) / path.relative_to(portable)).as_posix(),
                                         date_time=(1980, 1, 1, 0, 0, 0))
                member.compress_type = zipfile.ZIP_DEFLATED
                member.external_attr = 0o100644 << 16
                with path.open('rb') as source, archive.open(member, 'w') as target:
                    shutil.copyfileobj(source, target)
    payloads = []
    for path in sorted(output.iterdir()):
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        payloads.append({'name': path.name, 'size': path.stat().st_size, 'sha256': digest})
    info = {'schema': 1, 'version': version, 'artifact_label': label,
            'source_commit': sha, 'tag': tag, 'workflow_run': run_url, 'assets': payloads}
    metadata = output / f'release-info-{label}.json'
    metadata.write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    checksums = []
    for path in sorted(output.iterdir()):
        with path.open('rb') as stream:
            checksums.append(f'{hashlib.file_digest(stream, "sha256").hexdigest()}  {path.name}\n')
    (output / f'SHA256SUMS-{label}.txt').write_text(''.join(checksums), encoding='utf-8')
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--sha', required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--run-url', required=True)
    args = parser.parse_args()
    assemble(args.artifacts, args.output, args.version, args.label, args.sha, args.tag, args.run_url)


if __name__ == '__main__':
    main()
