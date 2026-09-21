"""Build a DMG without Finder automation; retry only transient create failures."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile
from time import sleep


def logged_run(command, cwd, log):
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run(command, cwd=cwd, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=600)
    output = log.read_text(encoding='utf-8', errors='replace')
    print(output, end='', flush=True)
    return result.returncode, output


def package(build_root, version):
    build_root = Path(build_root).resolve(strict=True)
    if not re.fullmatch(r'[0-9]{2}\.[0-9]{2}\.[0-9]{2}', version):
        raise ValueError('Invalid package version')
    packages = build_root / 'packages'
    packages.mkdir(exist_ok=True)
    filename = f'ZzLogg-{version}-OSX.dmg'
    destination = packages / filename
    if destination.exists():
        raise ValueError(f'Refusing to reuse an existing package: {destination}')

    for attempt in range(1, 4):
        # Keep failed attempts for diagnostics. Never reuse or delete a possibly
        # busy image, or detach a volume merely because its name is ZzLogg.
        staging = Path(tempfile.mkdtemp(prefix='dmg-attempt-', dir=build_root))
        status, output = logged_run([
            'cpack', '--verbose', '-G', 'DragNDrop', '-B', str(staging),
            # The project sets a relative output prefix. -B isolates CPack's
            # work tree but does not override that final destination itself.
            '-D', f'CPACK_OUTPUT_FILE_PREFIX={staging}',
            # CPack only remounts the image for the volume icon/Finder script.
            # These overrides leave the app icon and Applications link intact.
            '-D', 'CPACK_PACKAGE_ICON=',
            '-D', 'CPACK_DMG_DS_STORE_SETUP_SCRIPT=',
            '-D', 'CPACK_DMG_BACKGROUND_IMAGE=',
        ], build_root, build_root / f'cpack-dmg-{attempt}.log')
        if status == 0:
            image = staging / filename
            if not image.is_file() or image.stat().st_size == 0:
                raise ValueError('CPack succeeded without producing a nonempty DMG')
            status, _ = logged_run(['hdiutil', 'verify', str(image)], build_root,
                                   build_root / f'cpack-dmg-{attempt}-verify.log')
            if status == 0:
                image.replace(destination)
            return status

        # Diagnostic errors cannot turn the failed packaging operation into a
        # success. Preserve the image list for investigating runner contention.
        logged_run(['hdiutil', 'info', '-plist'], build_root,
                   build_root / f'cpack-dmg-{attempt}-images.log')
        busy_create = ('Error generating temporary disk image.' in output and
                       'hdiutil: create failed - Resource busy' in output)
        if not busy_create or attempt == 3:
            return status
        delay = 5 * attempt
        print(f'Image creation is busy; retrying in {delay}s with a fresh directory.', flush=True)
        sleep(delay)
    raise AssertionError('unreachable')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', required=True)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    raise SystemExit(package(args.build_root, args.version))


if __name__ == '__main__':
    main()
