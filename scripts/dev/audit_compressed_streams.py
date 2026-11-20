#!/usr/bin/env python3
"""Explicit full-content stream audit; reports gaps and exits 1, never blesses partial output."""
import argparse
import bz2
import gzip
import hashlib
import json
import lzma
import os
from pathlib import Path
import random
import subprocess
import sys


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True, help='zzlogg_archive_test executable')
    parser.add_argument('--output', type=Path, required=True, help='Empty directory for inputs and report')
    args = parser.parse_args()
    binary = args.binary.resolve()
    if args.output.exists() and any(args.output.iterdir()):
        parser.error('Use an empty output directory; retain previous evidence')
    args.output.mkdir(parents=True, exist_ok=True)
    first, second = b'FIRST\n', b'SECOND\n'
    large = random.Random(271828).randbytes(16384)
    encoders = dict(gz=lambda data: gzip.compress(data, mtime=0), bz2=bz2.compress, xz=lzma.compress)
    cases = []
    for suffix, encode in encoders.items():
        cases += [(f'{suffix}-two-members', suffix, encode(first) + encode(second), first + second),
                  (f'{suffix}-large-first-member', suffix, encode(large) + encode(second), large + second),
                  (f'{suffix}-empty-first-member', suffix, encode(b'') + encode(second), second),
                  # Explicit strict-consumption goal, not a claim every CLI rejects trailing bytes.
                  (f'{suffix}-trailing-garbage', suffix, encode(first) + b'NOT-A-STREAM', None)]
    cases += [('xz-padding', 'xz', lzma.compress(first) + b'\0' * 4, first),
              ('xz-padding-between-members', 'xz', lzma.compress(first) + b'\0' * 4 + lzma.compress(second), first + second),
              ('xz-invalid-padding', 'xz', lzma.compress(first) + b'\0', None),
              ('legacy-lzma', 'lzma', lzma.compress(first, format=lzma.FORMAT_ALONE), first)]
    rows = []
    for name, suffix, encoded, expected in cases:
        source = args.output / (name + '.' + suffix)
        output = args.output / (name + '.decoded')
        source.write_bytes(encoded)
        row = dict(case=name, input_bytes=len(encoded), input_sha256=digest(encoded),
                   expected_success=expected is not None,
                   expected_bytes=len(expected) if expected is not None else None,
                   expected_sha256=digest(expected) if expected is not None else None)
        try:
            process = subprocess.run([str(binary), '--stream-probe', str(source.resolve()), str(output.resolve())],
                text=True, capture_output=True, timeout=10, env=dict(os.environ, QT_QPA_PLATFORM='offscreen'))
            row.update(exit_code=process.returncode, stderr=process.stderr)
            if process.returncode:
                row.update(passed=False, reason='Probe process failed')
            else:
                actual = json.loads(process.stdout)
                decoded = output.read_bytes()
                identity = actual['bytes'] == len(decoded) and actual['sha256'] == digest(decoded)
                completed = actual['started'] and actual['timely'] and actual['signals'] == 1
                success = actual['success'] == (expected is not None) and actual['signal_success'] == actual['success']
                content = expected is None or decoded == expected
                row.update(actual=actual, passed=identity and completed and success and content)
        except (subprocess.TimeoutExpired, ValueError, OSError, KeyError, TypeError) as error:
            row.update(passed=False, reason=str(error))
        rows.append(row)
        print(('PASS' if row['passed'] else 'GAP'), name, row.get('actual', row.get('reason')), flush=True)
    report = dict(binary_sha256=digest(binary.read_bytes()), script_sha256=digest(Path(__file__).read_bytes()),
                  python=sys.version, cases=rows, passed=sum(row['passed'] for row in rows), total=len(rows))
    (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if all(row['passed'] for row in rows) else 1


if __name__ == '__main__':
    sys.exit(main())
