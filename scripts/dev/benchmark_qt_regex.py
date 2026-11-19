#!/usr/bin/env python3
"""Explicit before/after Qt prefilter probe on generated UTF-8 Android logs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import tempfile

QUERIES = {
    'dmesg': [r'i2c(?=.*timeout)'],
    'logcat': [r'binder(?! debug)'],
    'app': [r'(?<=message":")java\.io\.IOException', r'(?<=message":")SIGSEGV', r'(?<=message":")ANR'],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=4)
    parser.add_argument('--jit', choices=['0', '1'], default='1')
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('Use positive rounds')
    # Dedicated empty output avoids overwriting data or following old report links.
    if args.output.exists() and (not args.output.is_dir() or any(args.output.iterdir())):
        parser.error('Use an empty output directory')
    binaries = {'before': args.before.resolve(), 'after': args.after.resolve()}
    for binary in binaries.values():
        if not binary.is_file():
            parser.error(f'Missing binary: {binary}')
    cpus = None
    if hasattr(os, 'sched_getaffinity'):
        cpus = sorted(os.sched_getaffinity(0))[:2]
        os.sched_setaffinity(0, cpus)
    args.output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, QT_ENABLE_REGEXP_JIT=args.jit)
    records, expected = [], {}
    with tempfile.TemporaryDirectory(prefix='zzlogg-qt-regex-') as temporary:
        snapshot = Path(temporary)
        for name, patterns in QUERIES.items():
            data = (args.corpus / (name + '.log')).read_bytes()
            (snapshot / (name + '.log')).write_bytes(data)
            lines = data.split(b'\n')
            if lines[-1] == b'':
                lines.pop()
            regexes = [re.compile(p) for p in patterns]
            hits = [0] * len(regexes)
            digest = hashlib.sha256()
            for line in lines:
                text = line.decode('utf-8').removeprefix('\ufeff')
                row = bytes(bool(regex.search(text)) for regex in regexes)
                digest.update(row)
                hits = [total + matched for total, matched in zip(hits, row)]
            expected[name] = dict(patterns=patterns, rows=len(lines), bytes=len(data), hits=hits,
                input_sha256=hashlib.sha256(data).hexdigest(), result_sha256=digest.hexdigest())
        for repetition in range(args.rounds):
            order = ['before', 'after', 'after', 'before'] if repetition % 2 == 0 else ['after', 'before', 'before', 'after']
            for name in QUERIES:
                for engine in ('prefilter', 'qt'):
                    for position, version in enumerate(order):
                        run = subprocess.run([str(binaries[version]), '--case', name, '--input',
                            str(snapshot / (name + '.log')), '--engine', engine],
                            env=env, text=True, capture_output=True, timeout=120)
                        label = f'{repetition}-{name}-{engine}-{position}-{version}'
                        (args.output / (label + '.stdout.json')).write_text(run.stdout)
                        (args.output / (label + '.stderr.log')).write_text(run.stderr)
                        run.check_returncode()
                        record = json.loads(run.stdout)
                        if record['case'] != name or record['engine'] != engine or record['jit_environment'] != args.jit:
                            raise ValueError('Probe configuration differs')
                        if any(record[key] != value for key, value in expected[name].items()):
                            raise ValueError(f'Complete independent result differs: {label}')
                        if any(type(record[field]) is not int or record[field] <= 0 for field in
                               ('setup_ns', 'first_line_ns', 'first_scan_ns', 'hot_scan_ns')):
                            raise ValueError('Invalid timing')
                        records.append(dict(record, version=version, round=repetition, position=position))
            print(f'Round {repetition + 1}: complete results and inputs verified', flush=True)
    summary = []
    for name in QUERIES:
        for engine in ('prefilter', 'qt'):
            selected = [r for r in records if r['case'] == name and r['engine'] == engine]
            for phase in ('setup_ns', 'first_line_ns', 'first_scan_ns', 'hot_scan_ns'):
                medians = {v: statistics.median(r[phase] for r in selected if r['version'] == v) for v in binaries}
                summary.append(dict(case=name, engine=engine, phase=phase, **medians,
                                    speedup=medians['before'] / medians['after']))
    root = Path(__file__).resolve().parents[2]
    sources = [Path(__file__).resolve(), root / 'src/regex/include/hsregularexpression.h',
               root / 'src/regex/src/hsregularexpression.cpp', *sorted((root / 'tools/benchmarks/qt_regex').glob('*'))]
    metadata = dict(platform=platform.platform(), cpus=cpus, jit=args.jit, rounds=args.rounds,
        baseline_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        binaries={v: dict(path=str(p), sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for v, p in binaries.items()},
        source_sha256={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources if p.is_file()})
    (args.output / 'results.json').write_text(json.dumps(dict(metadata=metadata, expected=expected,
                                                          samples=records, summary=summary), indent=2) + '\n')
    for row in summary:
        if row['phase'] == 'hot_scan_ns':
            print(row['case'], row['engine'], 'hot scan speedup', round(row['speedup'], 3))


if __name__ == '__main__':
    main()
