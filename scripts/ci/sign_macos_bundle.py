#!/usr/bin/env python3
"""Sign embedded Mach-O code and bundles inside-out, including Sparkle helpers."""
import argparse
from pathlib import Path
import plistlib
import subprocess

MACHO_MAGIC = {bytes.fromhex(value) for value in
               ('feedface', 'cefaedfe', 'feedfacf', 'cffaedfe',
                'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca')}


def verify_layout(app):
    contents = app / 'Contents'
    with (contents / 'Info.plist').open('rb') as stream:
        metadata = plistlib.load(stream)
    if metadata.get('SUFeedURL'):
        framework = contents / 'Frameworks/Sparkle.framework/Versions/Current'
        for relative in ('Sparkle', 'Autoupdate', 'Updater.app/Contents/MacOS/Updater',
                         'XPCServices/Downloader.xpc/Contents/MacOS/Downloader',
                         'XPCServices/Installer.xpc/Contents/MacOS/Installer'):
            if not (framework / relative).is_file():
                raise ValueError(f'Official update bundle is missing Sparkle component: {relative}')


def sign_bundle(app, identity):
    verify_layout(app)
    code = []
    bundles = []
    for path in app.rglob('*'):
        if path.is_symlink():
            continue
        if path.is_dir() and path.suffix in ('.app', '.xpc', '.framework', '.bundle'):
            bundles.append(path)
        elif path.is_file():
            with path.open('rb') as stream:
                if stream.read(4) in MACHO_MAGIC:
                    code.append(path)
    # Sign leaves first; then nested bundles before their enclosing framework/app.
    for path in sorted(code) + sorted(bundles, key=lambda path: (-len(path.parts), str(path))) + [app]:
        subprocess.run(['codesign', '--force', '--options', 'runtime', '--timestamp',
                        '--preserve-metadata=entitlements', '--sign', identity, str(path)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', '--verbose=2', str(app)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--identity')
    parser.add_argument('--verify-layout', action='store_true')
    args = parser.parse_args()
    if args.verify_layout:
        verify_layout(args.app)
    elif args.identity:
        sign_bundle(args.app, args.identity)
    else:
        parser.error('--identity or --verify-layout is required')


if __name__ == '__main__':
    main()
