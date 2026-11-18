#!/usr/bin/env python3
"""Measure explicitly built full-worker probes; no speed threshold is a CI gate."""
import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('out/benchmarks/p11-worker/results'))
    parser.add_argument('--rounds', type=int, default=4)
    parser.add_argument('--case')
    parser.add_argument('--cpus', type=int, nargs='+')
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('Use positive rounds')
    cpus = None
    if hasattr(os, 'sched_getaffinity'):
        allowed = os.sched_getaffinity(0)
        cpus = args.cpus or sorted(allowed)[:2]
        if not set(cpus) <= allowed:
            parser.error('CPUs outside allowed set')
        os.sched_setaffinity(0, set(cpus))
    elif args.cpus:
        parser.error('CPU affinity unsupported')
    binary = args.binary.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'results.json').unlink(missing_ok=True)
    def run(command, label):
        result = subprocess.run([str(binary), *command], text=True, capture_output=True, timeout=600)
        (args.output / (label + '.jsonl')).write_text(result.stdout)
        (args.output / (label + '.stderr.log')).write_text(result.stderr)
        result.check_returncode()
        return result.stdout
    run(['--verify'], 'contracts')
    records, profiles, signatures = [], [], {}
    scenarios = None
    for repetition in range(args.rounds):
        for candidate in (['memchr', 'qt'] if repetition % 2 == 0 else ['qt', 'memchr']):
            command = ['--candidate', candidate]
            if repetition % 2:
                command += ['--reverse']
            if args.case:
                command += ['--case', args.case]
            rows = [json.loads(line) for line in run(command, f'{repetition}-{candidate}').splitlines()]
            groups = defaultdict(list)
            for row in rows:
                if row['candidate'] != candidate or row['role'] not in ('reference', 'candidate') or row['elapsed_ns'] <= 0:
                    raise RuntimeError('Invalid measurement')
                groups[row['scenario'], row['role']].append(row)
                signature = row['input_sha256'], row['result_sha256']
                if signatures.setdefault(row['scenario'], signature) != signature:
                    raise RuntimeError('Input or complete worker result changed')
                records.append(dict(row, round=repetition))
            measured = {name for name, _ in groups}
            if not measured or (scenarios is not None and measured != scenarios):
                raise RuntimeError('Scenario coverage differs')
            scenarios = measured
            if any(len(groups[name, role]) != 2 for name in measured for role in ('reference', 'candidate')):
                raise RuntimeError('Incomplete ABBA pairs')
            print(f'Round {repetition + 1}: {candidate}, {len(measured)} scenarios', flush=True)
    for repetition in range(args.rounds):
        for candidate in ('qt', 'memchr'):
            command = ['--candidate', candidate, '--profile']
            if args.case:
                command += ['--case', args.case]
            rows = [json.loads(line) for line in run(command, f'profile-{repetition}-{candidate}').splitlines()]
            if len(rows) != len(scenarios) or {row['scenario'] for row in rows} != scenarios:
                raise RuntimeError('Incomplete profile coverage')
            for row in rows:
                if row['candidate'] != candidate or (row['input_sha256'], row['result_sha256']) != signatures[row['scenario']]:
                    raise RuntimeError('Profile differs from measured inputs/results')
                if row['live_blocks'] != 0 or row['max_batch_rows'] > 128 or row['max_batch_bytes'] > 262144:
                    raise RuntimeError('Resource or batch-bound contract failed')
                profiles.append(dict(row, round=repetition))
    summary = []
    for candidate in ('memchr', 'qt'):
        for scenario in sorted(scenarios):
            selected = [r for r in records if r['candidate'] == candidate and r['scenario'] == scenario]
            reference = statistics.median(r['elapsed_ns'] for r in selected if r['role'] == 'reference')
            current = statistics.median(r['elapsed_ns'] for r in selected if r['role'] == 'candidate')
            summary.append(dict(candidate=candidate, scenario=scenario, reference_ns=reference,
                                candidate_ns=current, speedup=reference/current))
    root = Path(__file__).resolve().parents[2]
    files = [Path(__file__).resolve(), root / 'CMakeLists.txt']
    for directory in ['tools/benchmarks/search_worker', 'tools/benchmarks/literal_search',
                      'src/regex', 'src/logdata', 'src/utils', 'src/settings']:
        files += [p for p in (root / directory).rglob('*') if p.is_file()]
    cache = binary.parent.parent / 'CMakeCache.txt'
    metadata = dict(platform=platform.platform(), cpus=cpus,
        baseline_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        source_sha256={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        cmake_cache=cache.read_text() if cache.exists() else None,
        rustc=subprocess.check_output(['rustc', '--version'], text=True).strip())
    if Path('/proc/cpuinfo').exists():
        metadata['cpu_model'] = next(line.partition(':')[2].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                                     if line.startswith('model name'))
    (args.output / 'results.json').write_text(json.dumps(dict(metadata=metadata, samples=records,
                                                           profiles=profiles, summary=summary), indent=2) + '\n')
    for candidate in ('memchr', 'qt'):
        values = [r['speedup'] for r in summary if r['candidate'] == candidate]
        print(candidate, 'all-case speedup min/median/max:',
              *(round(x, 3) for x in (min(values), statistics.median(values), max(values))))


if __name__ == '__main__':
    main()
