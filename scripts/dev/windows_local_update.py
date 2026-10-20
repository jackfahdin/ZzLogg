#!/usr/bin/env python3
"""Incrementally build Windows test installers and test updates without GitHub CI.

Run in an x64 MSVC developer terminal. Python 3.11+, CMake and Ninja are required.
All build, packaging and feed files stay below out/local-windows.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'out/local-windows'
DEFAULT_QT = r'D:\SoftWare\Qt\6.11.0'


def version_argument(value):
    if not re.fullmatch(r'[0-9]{2}\.(?:0[1-9]|1[0-2])\.[0-9]{2}', value):
        raise argparse.ArgumentTypeError('Use YY.MM.PP, for example 26.10.06')
    return value


def resolve_qt(root):
    root = Path(root).resolve()

    def complete(path):
        return all((path / file).is_file() for file in
                   ('bin/qmake.exe', 'bin/windeployqt.exe', 'lib/cmake/Qt6/Qt6Config.cmake'))

    if complete(root):
        if 'mingw' in root.name.lower() or 'arm' in root.name.lower():
            raise ValueError('An x64 MSVC Qt kit is required, not MinGW or ARM')
        return root
    candidates = [path for path in root.glob('msvc*_64') if complete(path)]
    if len(candidates) == 1:
        return candidates[0]
    if len(candidates) > 1:
        raise ValueError('Multiple MSVC Qt kits found; pass --qt with the full kit directory')
    raise ValueError(f'No complete x64 MSVC Qt kit below {root}; pass --qt with its directory')


def run(command, *, cwd=ROOT, env=None):
    command = [str(value) for value in command]
    print('+ ' + subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def stage_installer(repository, runtime, transaction, workspace):
    for name in ('ZzLogg.exe', 'ZzLoggUpdate.exe', 'ZzLoggPortableUpdate.exe'):
        if not (runtime / name).is_file():
            raise ValueError(f'Missing runtime file: {runtime / name}')
    if not transaction.is_file():
        raise ValueError(f'Missing transaction engine: {transaction}')
    # Reuse the existing installer layout in an isolated copy. Never clear or
    # overwrite the checkout's release/ or txpayload/ staging directories.
    shutil.copytree(runtime, workspace / 'release')
    (workspace / 'txpayload').mkdir()
    shutil.copy2(transaction, workspace / 'txpayload/ZzLoggUpdateTx.exe')
    scripts = workspace / 'packaging/windows'
    scripts.mkdir(parents=True)
    for name in ('ZzLogg.iss', 'Build-InnoInstaller.ps1', 'GenerateInstallerManifest.ps1',
                 'InstallerStagingFiles.ps1'):
        shutil.copy2(repository / 'packaging/windows' / name, scripts / name)
    (workspace / 'packaging/assets').mkdir()
    shutil.copy2(repository / 'packaging/assets/ZzLogg.ico', workspace / 'packaging/assets/ZzLogg.ico')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_build_record(directory, package, version):
    record = {'version': version, 'channel': 'stable', 'local_feed': True,
              'package': package.name, 'size': package.stat().st_size, 'sha256': digest(package)}
    temporary = directory / 'build.json.tmp'
    temporary.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    temporary.replace(directory / 'build.json')


def verified_package(directory, version):
    record = json.loads((directory / 'build.json').read_text(encoding='utf-8'))
    name = record.get('package', '')
    if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9._-]+\.exe', name):
        raise ValueError('Invalid package filename in build record')
    package = directory / name
    if (record.get('version') != version or record.get('channel') != 'stable'
            or record.get('local_feed') is not True or not package.is_file()
            or package.stat().st_size != record.get('size') or digest(package) != record.get('sha256')):
        raise ValueError('Package changed or does not match its local build record; rebuild it')
    return package


def build(args):
    qt = resolve_qt(args.qt)
    environment = dict(os.environ, PATH=str(qt / 'bin') + os.pathsep + os.environ.get('PATH', ''))
    for command in ('cl', 'cmake', 'ninja', 'git'):
        if not shutil.which(command, path=environment['PATH']):
            raise ValueError(f'{command} is not on PATH; use an x64 MSVC developer terminal')
    if os.environ.get('VSCMD_ARG_TGT_ARCH', 'x64').lower() != 'x64':
        raise ValueError('Use the x64 MSVC developer environment')
    if not (ROOT / '3rdparty/vendor/ZzPureTools/CMakeLists.txt').is_file():
        raise ValueError('Initialize dependencies first: git submodule update --init --recursive')
    shell = shutil.which('pwsh') or shutil.which('powershell')
    if not shell:
        raise ValueError('PowerShell is required by the existing installer scripts')
    WORK.mkdir(parents=True, exist_ok=True)
    build_dir = WORK / 'build'
    run(['cmake', '-S', ROOT, '-B', build_dir, '-G', 'Ninja',
         f'-DCMAKE_PREFIX_PATH={qt}', '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl',
         '-DCMAKE_BUILD_TYPE=Release', f'-DBUILD_VERSION={args.version}', '-DBUILD_NUMBER=',
         '-DKLOGG_USE_LTO=OFF', '-DKLOGG_BUILD_TESTS=OFF', '-DKLOGG_BUILD_UI_TESTS=OFF',
         '-DKLOGG_USE_HYPERSCAN=OFF', '-DKLOGG_USE_VECTORSCAN=OFF',
         '-DKLOGG_OVERRIDE_MALLOC=OFF', '-DKLOGG_GENERIC_CPU=ON',
         f'-DCPM_SOURCE_CACHE={ROOT / "out/cpm-cache"}',
         '-DZZLOGG_OFFICIAL_RELEASE=ON', '-DZZLOGG_RELEASE_CHANNEL=stable',
         f'-DZZLOGG_RELEASE_SEQUENCE={int(args.version.replace(".", ""))}',
         '-DZZLOGG_UPDATE_LOCAL_FEED=ON', '-DZZLOGG_ENABLE_GITHUB_FEED=OFF',
         '-DZZLOGG_UPDATE_STABLE_URL=', '-DZZLOGG_UPDATE_PREVIEW_URL=',
         '-DZZLOGG_UPDATE_PUBLIC_KEYS=', '-DZZLOGG_UPDATE_ALLOWED_HOSTS='], env=environment)
    run(['cmake', '--build', build_dir, '--target', 'zzlogg_runtime_folder', 'ZzLoggUpdateTx',
         '--parallel', args.jobs], env=environment)
    with tempfile.TemporaryDirectory(prefix='package-', dir=WORK) as temporary:
        stage = Path(temporary)
        stage_installer(ROOT, build_dir / 'runtime/Release/ZzLogg-runtime',
                        build_dir / 'output/ZzLoggUpdateTx.exe', stage)
        documentation = build_dir / 'generated/documentation.html'
        if documentation.is_file():
            shutil.copy2(documentation, stage / 'release/documentation.html')
        powershell = [shell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File']
        run([*powershell, stage / 'packaging/windows/GenerateInstallerManifest.ps1',
             '-StagingDirectory', stage / 'release'], cwd=stage, env=environment)
        run([*powershell, stage / 'packaging/windows/Build-InnoInstaller.ps1',
             '-Version', args.version, '-Platform', 'x64'], cwd=stage, env=environment)
        name = f'ZzLogg-{args.version}-x64-Qt6-setup.exe'
        installer = stage / name
        if not installer.is_file() or installer.stat().st_size == 0:
            raise ValueError(f'Installer was not produced: {installer}')
        destination = WORK / 'packages' / args.version
        destination.mkdir(parents=True, exist_ok=True)
        package = destination / name
        shutil.copy2(installer, destination / (name + '.tmp'))
        (destination / (name + '.tmp')).replace(package)
        write_build_record(destination, package, args.version)
    print(f'\nLocal TEST installer: {package}\nNot for distribution.', flush=True)
    print('Install the base package manually; close ZzLogg before using the serve command.')


def serve(args):
    package = verified_package(WORK / 'packages' / args.version, args.version)
    application = args.app.resolve()
    if not application.is_file() or application.name.lower() != 'zzlogg.exe':
        raise ValueError('--app must point to the installed test ZzLogg.exe')
    try:
        import cryptography  # noqa: F401
    except ImportError:
        raise ValueError('Run: python -m pip install -r scripts/ci/requirements-update.txt') from None
    data = (WORK / 'user-data').resolve()
    data.mkdir(parents=True, exist_ok=True)
    import ctypes
    if ctypes.windll.shell32.IsUserAnAdmin():
        print('This terminal is elevated: installation may require a manual restart. '
              'Use a non-elevated terminal to test automatic restart.', flush=True)
    print('Close all existing ZzLogg instances before continuing. Keep this terminal open.', flush=True)
    print(f'Test session/configuration directory: {data}', flush=True)
    run([sys.executable, '-u', ROOT / 'scripts/dev/local_update_feed.py',
         '--installer', package, '--version', args.version, '--port', args.port,
         '--workspace', WORK / f'feed-{args.port}', '--launch', application,
         '--launch-args', '--data-dir', data])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    builder = actions.add_parser('build', help='Incrementally build and package a local test version')
    builder.add_argument('--version', required=True, type=version_argument)
    builder.add_argument('--qt', type=Path, default=Path(DEFAULT_QT))
    builder.add_argument('--jobs', type=int, default=min(os.cpu_count() or 4, 8))
    server = actions.add_parser('serve', help='Serve a built update and launch the installed base version')
    server.add_argument('--version', required=True, type=version_argument, help='Newer package version')
    server.add_argument('--app', type=Path, required=True, help='Installed base-version ZzLogg.exe')
    server.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Run this entry on Windows in an x64 MSVC developer terminal')
    if args.action == 'build' and args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.action == 'serve' and not 1 <= args.port <= 65535:
        parser.error('--port must be between 1 and 65535')
    try:
        (build if args.action == 'build' else serve)(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'\nLocal update workflow failed: {error}\n')
    except KeyboardInterrupt:
        parser.exit(130, '\nStopped.\n')


if __name__ == '__main__':
    main()
