"""Resolve release inputs once, before any platform is built."""
import argparse
from datetime import datetime
import os
import re
import subprocess
from zoneinfo import ZoneInfo

from publish_release import GitHub, snapshot_unchanged
from release_assets import project_version, validate_tag
from release_notes import release_changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=['stable', 'nightly'], required=True)
    args = parser.parse_args()
    version = project_version('.')
    # Fail before the expensive build if this source has no publishable notes.
    release_changes('CHANGELOG.md', version, args.mode)
    sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    tag = os.environ['GITHUB_REF_NAME'] if args.mode == 'stable' else 'continuous-build'
    changed = True
    if args.mode == 'stable':
        validate_tag(tag, version)
        label = version
    else:
        if os.environ['GITHUB_REF_NAME'] != 'master':
            raise ValueError('Snapshots must be built from master')
        run_id, attempt = os.environ['GITHUB_RUN_ID'], os.environ['GITHUB_RUN_ATTEMPT']
        if not re.fullmatch(r'[0-9]+', run_id) or not re.fullmatch(r'[0-9]+', attempt):
            raise ValueError('Invalid workflow run identity')
        date = datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d')
        label = f'continuous-{date}-{run_id}-{attempt}-{sha[:12]}'
        if os.environ['GITHUB_EVENT_NAME'] == 'schedule':
            changed = not snapshot_unchanged(GitHub(os.environ['GITHUB_REPOSITORY']), sha)
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
        for name, value in {'sha': sha, 'version': version, 'tag': tag,
                            'label': label, 'changed': str(changed).lower()}.items():
            output.write(f'{name}={value}\n')


if __name__ == '__main__':
    main()
