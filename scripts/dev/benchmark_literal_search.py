#!/usr/bin/env python3
"""Run explicit literal-search experiments; no performance threshold is a CI gate."""
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
    parser.add_argument('--output', type=Path, default=Path('out/benchmarks/p11-literal/results'))
    parser.add_argument('--rounds', type=int, default=4)
    parser.add_argument('--candidates', nargs='+', default=['std', 'qstring', 'qt'],
                        choices=['std', 'qstring', 'memchr', 'aho', 'qt', 'stringzilla', 'stringzilla-serial'])
    parser.add_argument('--reference', default='qt',
                        choices=['std', 'qstring', 'memchr', 'aho', 'qt', 'stringzilla', 'stringzilla-serial'])
    parser.add_argument('--cpu', type=int)
    parser.add_argument('--case')
    args = parser.parse_args()
    if args.rounds < 1 or len(set(args.candidates)) != len(args.candidates):
        parser.error('Use positive rounds and distinct candidates')
    binary = args.binary.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'results.json').unlink(missing_ok=True)
    cpu = None
    if hasattr(os, 'sched_getaffinity'):
        allowed = os.sched_getaffinity(0)
        cpu = min(allowed) if args.cpu is None else args.cpu
        if cpu not in allowed:
            parser.error('CPU is outside the allowed affinity set')
        os.sched_setaffinity(0, {cpu})
    elif args.cpu is not None:
        parser.error('Explicit CPU affinity is unsupported on this host')
    verification = subprocess.run([str(binary), '--verify'], capture_output=True, text=True, check=True)
    (args.output / 'contracts.log').write_text(verification.stdout + verification.stderr)
    records = []
    signatures = {}
    scenarios = None
    for round_number in range(args.rounds):
        order = args.candidates if round_number % 2 == 0 else list(reversed(args.candidates))
        for candidate in order:
            command = [str(binary), '--candidate', candidate, '--reference', args.reference]
            if round_number % 2:
                command.append('--reverse')
            if args.case:
                command += ['--case', args.case]
            process = subprocess.run(command, capture_output=True, text=True, timeout=600)
            prefix = args.output / f'{round_number}-{candidate}'
            prefix.with_suffix('.jsonl').write_text(process.stdout)
            prefix.with_suffix('.stderr.log').write_text(process.stderr)
            process.check_returncode()
            rows = [json.loads(line) for line in process.stdout.splitlines()]
            groups = defaultdict(list)
            for row in rows:
                if (row['candidate'] != candidate or row['reference'] != args.reference
                        or row['ns_per_line'] <= 0):
                    raise RuntimeError('Invalid measurement')
                groups[(row['scenario'], row['role'])].append(row)
                signature = (row['input_sha256'], row['result_sha256'])
                if signatures.setdefault(row['scenario'], signature) != signature:
                    raise RuntimeError('Input or result changed between runs/backends')
                records.append(dict(row, round=round_number))
            measured = {name for name, _ in groups}
            if not measured or (scenarios is not None and scenarios != measured):
                raise RuntimeError('Scenario coverage differs')
            scenarios = measured
            for scenario in measured:
                for role in ('reference', 'candidate'):
                    if len(groups[(scenario, role)]) != 2:
                        raise RuntimeError('Incomplete ABBA pair')
            print(f'Round {round_number + 1}: {candidate}, {len(measured)} scenarios', flush=True)
    summaries = []
    for candidate in args.candidates:
        for scenario in sorted(scenarios):
            selected = [r for r in records if r['candidate'] == candidate and r['scenario'] == scenario]
            reference = [r for r in selected if r['role'] == 'reference']
            current = [r for r in selected if r['role'] == 'candidate']
            baseline = statistics.median(r['ns_per_line'] for r in reference)
            actual = statistics.median(r['ns_per_line'] for r in current)
            summaries.append(dict(candidate=candidate, reference_backend=args.reference, scenario=scenario,
                reference_ns=baseline, candidate_ns=actual, speedup=baseline / actual,
                hot_samples=len(current),
                construct_ns=statistics.median(r['construct_ns'] for r in current),
                reference_construct_ns=statistics.median(r['construct_ns'] for r in reference),
                first_batch_ns=statistics.median(r['first_batch_ns'] for r in current),
                reference_first_batch_ns=statistics.median(r['first_batch_ns'] for r in reference),
                automaton_bytes=current[0]['automaton_bytes']))
    root = Path(__file__).resolve().parents[2]
    source = root / 'tools/benchmarks/literal_search'
    files = [p for p in source.rglob('*') if p.is_file()]
    files += [root / 'src/regex/include/hsregularexpression.h',
              root / 'src/regex/include/regularexpressionpattern.h', Path(__file__).resolve()]
    version = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root, text=True,
                             capture_output=True, check=True).stdout.strip()
    build_cache = binary.parent / 'CMakeCache.txt'
    metadata = dict(platform=platform.platform(), processor=platform.processor(), cpu=cpu,
        reference_backend=args.reference,
        baseline_commit=version, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        source_sha256={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        cmake_cache=build_cache.read_text() if build_cache.exists() else None)
    cpuinfo = Path('/proc/cpuinfo')
    if cpuinfo.exists():
        metadata['cpu_model'] = next((line.partition(':')[2].strip() for line in cpuinfo.read_text().splitlines()
                                      if line.startswith('model name')), None)
    try:
        metadata['rustc'] = subprocess.run(['rustc', '--version'], text=True, capture_output=True,
                                          check=True).stdout.strip()
    except FileNotFoundError:
        metadata['rustc'] = None
    (args.output / 'results.json').write_text(json.dumps(dict(metadata=metadata, samples=records,
                                                            summary=summaries), indent=2) + '\n')
    for candidate in args.candidates:
        valid = [r['speedup'] for r in summaries if r['candidate'] == candidate
                 and r['scenario'].startswith(('ascii-', 'utf8-'))]
        if valid:
            print(candidate, 'vs', args.reference, 'valid-case speedup min/median/max:',
                  *(round(x, 3) for x in (min(valid), statistics.median(valid), max(valid))))


if __name__ == '__main__':
    main()
