#!/usr/bin/env python3
"""Run real worker fault contracts in bounded, isolated processes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--mode', choices=('connect', 'read', 'allocation', 'match', 'combine', 'completion'))
    args = parser.parse_args()
    for mode in ([args.mode] if args.mode else ('connect', 'read', 'allocation', 'match', 'combine', 'completion')):
        for wake in (False, True):
            with tempfile.TemporaryDirectory(prefix='zzlogg-recovery-') as temporary:
                env = dict(os.environ, TMPDIR=temporary, TMP=temporary, TEMP=temporary, QT_QPA_PLATFORM='offscreen')
                command = [str(args.binary.resolve()), '--verify-recovery', mode]
                if wake:
                    command += ['--recovery-wake']
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=20)
                if result.returncode:
                    raise RuntimeError(f'{mode} wake={wake}: exit {result.returncode}: {result.stderr}')
                print(result.stdout.strip(), flush=True)


if __name__ == '__main__':
    main()
