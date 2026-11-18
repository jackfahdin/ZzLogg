#!/usr/bin/env python3
"""Explicit P11 production-component experiment; timing is never a CI pass threshold."""
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
    parser.add_argument('--output', type=Path, default=Path('out/benchmarks/p11-pipeline/results'))
    parser.add_argument('--rounds', type=int, default=4)
    parser.add_argument('--case')
    parser.add_argument('--cpu', type=int)
    parser.add_argument('--candidates', nargs='+', default=['memchr-line', 'memchr-128', 'memchr-4096', 'qt'],
                        choices=['memchr-line', 'memchr-128', 'memchr-4096', 'qt'])
    args = parser.parse_args()
    if args.rounds < 1 or len(set(args.candidates)) != len(args.candidates):
        parser.error('Use positive rounds and distinct candidates')
    cpu = None
    if hasattr(os, 'sched_getaffinity'):
        allowed = os.sched_getaffinity(0)
        cpu = min(allowed) if args.cpu is None else args.cpu
        if cpu not in allowed:
            parser.error('CPU outside allowed set')
        os.sched_setaffinity(0, {cpu})
    elif args.cpu is not None:
        parser.error('CPU affinity unsupported')
    binary = args.binary.resolve()
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    result_path = args.output / 'results.json'
    result_path.unlink(missing_ok=True)
    def run(arguments, label):
        proc = subprocess.run([str(binary), *arguments], capture_output=True, text=True, timeout=600)
        (args.output / (label + '.jsonl')).write_text(proc.stdout)
        (args.output / (label + '.stderr.log')).write_text(proc.stderr)
        proc.check_returncode()
        return proc.stdout
    run(['--verify'], 'contracts')
    records, profiles, signatures = [], [], {}
    scenarios = None
    for repetition in range(args.rounds):
        order = args.candidates if repetition % 2 == 0 else list(reversed(args.candidates))
        for candidate in order:
            command = ['--candidate', candidate]
            if repetition % 2:
                command.append('--reverse')
            if args.case:
                command += ['--case', args.case]
            rows = [json.loads(line) for line in run(command, f'{repetition}-{candidate}').splitlines()]
            groups = defaultdict(list)
            for row in rows:
                if row['candidate'] != candidate or row['role'] not in ('reference', 'candidate') or row['ns_per_line'] <= 0:
                    raise RuntimeError('Invalid measurement')
                groups[row['scenario'], row['role']].append(row)
                signature = row['input_sha256'], row['result_sha256']
                if signatures.setdefault(row['scenario'], signature) != signature:
                    raise RuntimeError('Input or complete result changed')
                records.append(dict(row, round=repetition))
            measured = {name for name, _ in groups}
            if not measured or (scenarios is not None and measured != scenarios):
                raise RuntimeError('Scenario coverage changed')
            scenarios = measured
            if any(len(groups[name, role]) != 2 for name in measured for role in ('reference', 'candidate')):
                raise RuntimeError('Incomplete ABBA pair')
            print(f'Round {repetition + 1}: {candidate}, {len(measured)} scenarios', flush=True)
    # These runs are separate from hot timings and each starts a fresh process.
    for candidate in args.candidates:
        for scenario in sorted(scenarios):
            rows = [json.loads(line) for line in run(
                ['--candidate', candidate, '--case', scenario, '--profile'], f'profile-{candidate}-{scenario}').splitlines()]
            if len(rows) != 1:
                raise RuntimeError('Invalid profile count')
            row = rows[0]
            if (row['candidate'] != candidate or row['scenario'] != scenario
                    or (row['input_sha256'], row['result_sha256']) != signatures[scenario]):
                raise RuntimeError('Profile differs from measured input/result')
            profiles.append(row)
    summaries = []
    for candidate in args.candidates:
        for scenario in sorted(scenarios):
            selected = [r for r in records if r['candidate'] == candidate and r['scenario'] == scenario]
            base = [r for r in selected if r['role'] == 'reference']
            current = [r for r in selected if r['role'] == 'candidate']
            baseline = statistics.median(r['ns_per_line'] for r in base)
            actual = statistics.median(r['ns_per_line'] for r in current)
            summaries.append(dict(candidate=candidate, scenario=scenario, reference_ns=baseline,
                candidate_ns=actual, speedup=baseline / actual, hot_samples=len(current),
                construct_ns=statistics.median(r['construct_ns'] for r in current),
                reference_construct_ns=statistics.median(r['construct_ns'] for r in base),
                first_ns=statistics.median(r['first_ns'] for r in current),
                reference_first_ns=statistics.median(r['first_ns'] for r in base)))
    files = [Path(__file__).resolve(), root / 'CMakeLists.txt']
    for directory in ['tools/benchmarks/search_pipeline', 'tools/benchmarks/literal_search',
                      'src/regex', 'src/logdata', 'src/utils', 'src/settings']:
        files += [p for p in (root / directory).rglob('*') if p.is_file()]
    cache = binary.parent.parent / 'CMakeCache.txt'
    metadata = dict(platform=platform.platform(), cpu=cpu,
        baseline_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        source_sha256={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        cmake_cache=cache.read_text() if cache.exists() else None,
        rustc=subprocess.check_output(['rustc', '--version'], text=True).strip())
    if Path('/proc/cpuinfo').exists():
        metadata['cpu_model'] = next(line.partition(':')[2].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                                     if line.startswith('model name'))
    result_path.write_text(json.dumps(dict(metadata=metadata, samples=records, profiles=profiles,
                                          summary=summaries), indent=2) + '\n')
    for candidate in args.candidates:
        values = [r['speedup'] for r in summaries if r['candidate'] == candidate and not r['scenario'].startswith('fallback-')]
        if values:
            print(candidate, 'eligible speedup min/median/max:',
                  *(round(v, 3) for v in (min(values), statistics.median(values), max(values))))


if __name__ == '__main__':
    main()
