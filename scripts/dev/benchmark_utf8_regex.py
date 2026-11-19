#!/usr/bin/env python3
"""Measure explicit UTF-8 regex experiments against full independent bitmaps."""
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

CASES = {
    'dmesg-errno': ('dmesg', [r'\berror -(?:5|19|110)\b'], True),
    'dmesg-lookahead': ('dmesg', [r'i2c(?=.*timeout)'], True),
    'dmesg-casefold': ('dmesg', ['TIMEOUT'], False),
    'logcat-levels': ('logcat', [r'\s[EF]\s+(?:AndroidRuntime|CameraService)\s*:'], True),
    'logcat-negative': ('logcat', [r'binder(?! debug)'], True),
    'app-unicode': ('app', ['温度=[0-9]+℃'], True),
    'app-multi16': ('app', [r'ioctl.*(?:completed|failed)', 'heartbeat', '温度=[0-9]+', 'SIGSEGV', 'ANR',
        'Exception', 'retry=[1-3]', 'payload_end', 'onResume', 'checksum=[0-9a-f]+', '(?:DriverClient|NativeBridge)',
        r'(?<=message":")java\.io\.IOException', '/dev/my_sensor', '采集正常😀',
        r'(?<x>com)\.example\.driverlab.*\k<x>', 'zzlogg-never'], True),
    'boundaries': ('boundaries', ['^EDGE', r'\x{FFFD}', r'\x{0}', '^$', r'\C\C', 'payload_end', '😀', '$'], True),
}


def expected_result(data, patterns, sensitive):
    # These translations are ONLY for the fixed patterns above, not a general
    # PCRE/Qt oracle. \C's UTF-16 semantics are derived separately.
    regexes = [None if p == r'\C\C' else re.compile(p.replace(r'\x{FFFD}', '\ufffd')
        .replace(r'\x{0}', '\x00').replace('(?<x>', '(?P<x>').replace(r'\k<x>', '(?P=x)'),
        0 if sensitive else re.IGNORECASE) for p in patterns]
    rows = data.split(b'\n')
    if rows[-1] == b'':
        rows.pop()
    hits, direct, fallback = [0] * len(patterns), 0, 0
    digest = hashlib.sha256()
    for raw in rows:
        try:
            raw.decode('utf-8')
            valid = not raw.startswith(b'\xef\xbb\xbf')
        except UnicodeDecodeError:
            valid = False
        text = raw.decode('utf-8', errors='replace').removeprefix('\ufeff')
        bitmap = bytes(bool(r.search(text)) if r else len(text.encode('utf-16-le')) >= 4 for r in regexes)
        digest.update(bitmap)
        hits = [a + b for a, b in zip(hits, bitmap)]
        fast = sum(r is not None for r in regexes) if valid else 0
        direct += fast
        fallback += len(patterns) - fast
    return dict(patterns=patterns, case_sensitive=sensitive, rows=len(rows), hits=hits,
        input_bytes=len(data), file_sha256=hashlib.sha256(data).hexdigest(), result_sha256=digest.hexdigest()), direct, fallback


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=4)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('Use positive rounds')
    if args.output.exists() and (not args.output.is_dir() or any(args.output.iterdir())):
        parser.error('Use an empty output directory')
    binary = args.binary.resolve()
    cache = (binary.parent / 'CMakeCache.txt').read_text()
    source_path = re.search(r'^PCRE2_QT_SOURCE_DIR:PATH=(.+)$', cache, re.MULTILINE)
    if not source_path:
        parser.error('Binary must have its standalone CMakeCache with explicit PCRE source path')
    cpus = None
    if hasattr(os, 'sched_getaffinity'):
        cpus = sorted(os.sched_getaffinity(0))[:2]
        os.sched_setaffinity(0, cpus)
    args.output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, QT_ENABLE_REGEXP_JIT='1')
    records, profiles, expected, routes = [], [], {}, {}
    def run(command, label):
        result = subprocess.run([str(binary), *command], env=env, text=True, capture_output=True, timeout=120)
        (args.output / (label + '.jsonl')).write_text(result.stdout)
        (args.output / (label + '.stderr.log')).write_text(result.stderr)
        result.check_returncode()
        return result.stdout
    run(['--verify'], 'contracts')
    versions = set()
    with tempfile.TemporaryDirectory(prefix='zzlogg-utf8-regex-') as temporary:
        snapshot = Path(temporary)
        data_by_file = {}
        for name in ('dmesg', 'logcat', 'app', 'boundaries'):
            data = (args.corpus / (name + '.log')).read_bytes()
            (snapshot / (name + '.log')).write_bytes(data)
            data_by_file[name] = data
        for case, (name, patterns, sensitive) in CASES.items():
            result, direct, fallback = expected_result(data_by_file[name], patterns, sensitive)
            expected[case], routes[case] = result, (direct, fallback)
        del data_by_file
        for repetition in range(args.rounds):
            for candidate in (['pcre2', 'qt'] if repetition % 2 == 0 else ['qt', 'pcre2']):
                command = ['--corpus', str(snapshot), '--candidate', candidate]
                if repetition % 2:
                    command += ['--reverse']
                rows = [json.loads(line) for line in run(command, f'{repetition}-{candidate}').splitlines()]
                if {r['case'] for r in rows} != set(CASES) or len(rows) != len(CASES) * 7:
                    raise ValueError('Incomplete scenario coverage')
                for case in CASES:
                    selected = [r for r in rows if r['case'] == case]
                    cold = ['candidate', 'reference'] if repetition % 2 else ['reference', 'candidate']
                    hot = ['candidate','reference','reference','candidate'] if repetition % 2 else ['reference','candidate','candidate','reference']
                    if [(r['phase'],r.get('role')) for r in selected] != [('cold', x) for x in cold] + [('hot', x) for x in hot] + [('profile', None)]:
                        raise ValueError('Incomplete or reordered measurement pairs')
                    for row in selected:
                        if row['candidate'] != candidate or row['qt_jit_environment'] != '1':
                            raise ValueError('Candidate/configuration differs')
                        if any(row[k] != v for k,v in expected[case].items()):
                            raise ValueError(f'Independent complete result differs: {case}')
                        versions.add((row['qt_version'], row['pcre2_version'], row['unicode_version']))
                        if row['phase'] == 'profile':
                            count = len(CASES[case][1])
                            programs = count - (case == 'boundaries') if candidate == 'pcre2' else 0
                            direct, fallback = routes[case] if candidate == 'pcre2' else (0, row['rows'] * count)
                            if (row['direct'],row['fallback'],row['runtime_errors'],row['programs'],row['jit_programs']) != (direct,fallback,0,programs,programs):
                                raise ValueError('Actual PCRE/JIT routing differs')
                            if row['fallback_patterns'] != (int(case == 'boundaries') if candidate == 'pcre2' else 0):
                                raise ValueError('Unexpected pattern fallback')
                            if programs and (row['code_bytes'] <= 0 or row['jit_bytes'] <= 0):
                                raise ValueError('Missing compiled code evidence')
                            profiles.append(dict(row, round=repetition))
                        else:
                            if type(row['elapsed_ns']) is not int or row['elapsed_ns'] <= 0:
                                raise ValueError('Invalid timing')
                            if row['phase'] == 'cold' and (row['build_ns'] <= 0 or row['total_ns'] != row['build_ns'] + row['elapsed_ns']):
                                raise ValueError('Invalid cold timing')
                            records.append(dict(row, round=repetition))
            print(f'Round {repetition + 1}: full results and actual routing verified', flush=True)
    if len(versions) != 1:
        raise ValueError('Library versions changed between measurements')
    summary = []
    for candidate in ('pcre2','qt'):
        for case in CASES:
            for phase, field in [('hot','elapsed_ns'),('cold','build_ns'),('cold','elapsed_ns'),('cold','total_ns')]:
                selected = [r for r in records if r['case'] == case and r['candidate'] == candidate and r['phase'] == phase]
                reference = statistics.median(r[field] for r in selected if r['role'] == 'reference')
                current = statistics.median(r[field] for r in selected if r['role'] == 'candidate')
                summary.append(dict(candidate=candidate, case=case, phase=phase, field=field,
                                    reference_ns=reference, candidate_ns=current, speedup=reference/current))
    root = Path(__file__).resolve().parents[2]
    external = Path(source_path.group(1))
    metadata = dict(platform=platform.platform(), cpus=cpus, rounds=args.rounds, versions=list(versions)[0],
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), cmake_cache=cache,
        baseline_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        source_sha256={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
            [Path(__file__).resolve(), *sorted((root/'tools/benchmarks/utf8_regex').glob('*'))] if p.is_file()},
        pcre2_source_dir=str(external),
        pcre2_sha256={str(p.relative_to(external)):hashlib.sha256(p.read_bytes()).hexdigest() for p in external.rglob('*') if p.is_file()})
    (args.output/'results.json').write_text(json.dumps(dict(metadata=metadata, expected=expected,
        samples=records, profiles=profiles, summary=summary),indent=2)+'\n')
    for row in summary:
        if row['phase']=='hot': print(row['candidate'],row['case'],round(row['speedup'],3))


if __name__ == '__main__':
    main()
