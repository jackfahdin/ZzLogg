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
import math
import re
from search_corpus import prepare_corpus, validate_output_path, atomic_write_text
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('out/benchmarks/p11-worker/results'))
    parser.add_argument('--candidate', choices=('memchr', 'pcre2'), default='memchr')
    parser.add_argument('--isolated-profiles', action='store_true', help='Fresh process per scenario for peak RSS')
    parser.add_argument('--rounds', type=int, default=4)
    source = parser.add_mutually_exclusive_group()
    source.add_argument('--case')
    source.add_argument('--corpus', type=Path)
    parser.add_argument('--timeout', type=float, default=600, help='Hard timeout per probe process in seconds')
    parser.add_argument('--cpus', type=int, nargs='+')
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('Use positive rounds')
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('Use a positive finite process timeout')
    validate_output_path(args.corpus, args.output)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'results.json').unlink(missing_ok=True)
    with prepare_corpus(args.corpus) as corpus:
        benchmark(args, corpus)


def benchmark(args, corpus):
    cpus = None
    if hasattr(os, 'sched_getaffinity'):
        allowed = os.sched_getaffinity(0)
        cpus = args.cpus or sorted(allowed)[:2]
        if not set(cpus) <= allowed:
            raise ValueError('CPUs outside allowed set')
        os.sched_setaffinity(0, set(cpus))
    elif args.cpus:
        raise ValueError('CPU affinity unsupported')
    binary = args.binary.resolve()
    def run(command, label):
        try:
            # Qt's temporary files must also be reclaimed after a hard process kill.
            with tempfile.TemporaryDirectory(prefix='zzlogg-probe-') as temporary:
                env = dict(os.environ, TMPDIR=temporary, TMP=temporary, TEMP=temporary, QT_ENABLE_REGEXP_JIT='1')
                result = subprocess.run([str(binary), *command], env=env, text=True,
                                        capture_output=True, timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            atomic_write_text(args.output / (label + '.stderr.log'), str(error))
            raise
        atomic_write_text(args.output / (label + '.jsonl'), result.stdout)
        atomic_write_text(args.output / (label + '.stderr.log'), result.stderr)
        result.check_returncode()
        return result.stdout
    run(['--verify'], 'contracts')
    records, profiles, signatures = [], [], {}
    expected_files = corpus['metadata']['case_files'] if corpus else None
    def check_file(row):
        if expected_files is not None:
            expected = expected_files.get(row['scenario'])
            if expected is None or row['file_sha256'] != expected['sha256'] or row['input_bytes'] != expected['bytes']:
                raise RuntimeError('Probe did not measure the expected snapshot')
    scenarios = None
    for repetition in range(args.rounds):
        for candidate in ([args.candidate, 'qt'] if repetition % 2 == 0 else ['qt', args.candidate]):
            command = ['--candidate', candidate]
            if repetition % 2:
                command += ['--reverse']
            if args.case:
                command += ['--case', args.case]
            if corpus:
                command += ['--corpus', str(corpus['manifest'])]
            rows = [json.loads(line) for line in run(command, f'{repetition}-{candidate}').splitlines()]
            groups = defaultdict(list)
            for row in rows:
                check_file(row)
                if row['candidate'] != candidate or row['role'] not in ('reference', 'candidate') or row['elapsed_ns'] <= 0:
                    raise RuntimeError('Invalid measurement')
                for field in ('first_progress_ns', 'first_hit_ns'):
                    if not -1 <= row[field] <= row['elapsed_ns']:
                        raise RuntimeError('Invalid notification timing')
                groups[row['scenario'], row['role']].append(row)
                signature = row['input_sha256'], row['result_sha256']
                if signatures.setdefault(row['scenario'], signature) != signature:
                    raise RuntimeError('Input or complete worker result changed')
                records.append(dict(row, round=repetition))
            measured = {name for name, _ in groups}
            if not measured or (scenarios is not None and measured != scenarios):
                raise RuntimeError('Scenario coverage differs')
            if expected_files is not None and measured != set(expected_files):
                raise RuntimeError('Corpus scenario omitted')
            order = ['candidate', 'reference', 'reference', 'candidate'] if repetition % 2 else ['reference', 'candidate', 'candidate', 'reference']
            if any([r['role'] for r in rows if r['scenario'] == name] != order for name in measured):
                raise RuntimeError('Incorrect paired order')
            scenarios = measured
            if any(len(groups[name, role]) != 2 for name in measured for role in ('reference', 'candidate')):
                raise RuntimeError('Incomplete ABBA pairs')
            print(f'Round {repetition + 1}: {candidate}, {len(measured)} scenarios', flush=True)
    for repetition in range(args.rounds):
        for candidate in ('qt', args.candidate):
            command = ['--candidate', candidate, '--profile']
            if args.case:
                command += ['--case', args.case]
            if corpus:
                command += ['--corpus', str(corpus['manifest'])]
            rows = []
            selections = sorted(scenarios) if args.isolated_profiles else [None]
            for scenario in selections:
                selected = command.copy()
                if scenario is not None:
                    if corpus:
                        selected += ['--profile-case', scenario]
                    elif not args.case:
                        selected += ['--case', scenario]
                label = f'profile-{repetition}-{candidate}' + (f'-{scenario}' if scenario else '')
                measured = [json.loads(line) for line in run(selected, label).splitlines()]
                if scenario is not None and (len(measured) != 1 or measured[0]['scenario'] != scenario):
                    raise RuntimeError('Isolated profile selected wrong scenario')
                rows.extend(measured)
            if len(rows) != len(scenarios) or {row['scenario'] for row in rows} != scenarios:
                raise RuntimeError('Incomplete profile coverage')
            for row in rows:
                check_file(row)
                if row['candidate'] != candidate or (row['input_sha256'], row['result_sha256']) != signatures[row['scenario']]:
                    raise RuntimeError('Profile differs from measured inputs/results')
                if row['live_blocks'] != 0 or row['live_matchers'] != 0 or row['max_batch_rows'] > 128 or row['max_batch_bytes'] > 262144:
                    raise RuntimeError('Resource or batch-bound contract failed')
                profiles.append(dict(row, round=repetition))
    summary = []
    for candidate in (args.candidate, 'qt'):
        for scenario in sorted(scenarios):
            selected = [r for r in records if r['candidate'] == candidate and r['scenario'] == scenario]
            reference = statistics.median(r['elapsed_ns'] for r in selected if r['role'] == 'reference')
            current = statistics.median(r['elapsed_ns'] for r in selected if r['role'] == 'candidate')
            summary.append(dict(candidate=candidate, scenario=scenario, reference_ns=reference,
                                candidate_ns=current, speedup=reference/current,
                                first_notifications={role: {field: statistics.median(r[field] for r in selected
                                    if r['role'] == role and r[field] >= 0) if any(r['role'] == role and r[field] >= 0
                                    for r in selected) else None for field in ('first_progress_ns', 'first_hit_ns')}
                                    for role in ('reference', 'candidate')}))
    root = Path(__file__).resolve().parents[2]
    files = [Path(__file__).resolve(), Path(__file__).with_name('search_corpus.py').resolve(), root / 'CMakeLists.txt']
    if args.candidate == 'pcre2':
        files += [Path(__file__).with_name(name).resolve() for name in
                  ('utf8_worker_corpus.py', 'benchmark_utf8_regex.py', 'generate_android_search_corpus.py')]
    for directory in ['tools/benchmarks/search_worker', 'tools/benchmarks/literal_search', 'tools/benchmarks/utf8_regex',
                      'src/regex', 'src/logdata', 'src/utils', 'src/settings', 'src/ui']:
        files += [p for p in (root / directory).rglob('*') if p.is_file()]
    cache = binary.parent.parent / 'CMakeCache.txt'
    metadata = dict(platform=platform.platform(), cpus=cpus,
        baseline_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        source_sha256={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        cmake_cache=cache.read_text() if cache.exists() else None,
        candidate=args.candidate, isolated_profiles=args.isolated_profiles, qt_jit_environment='1',
        rustc=subprocess.check_output(['rustc', '--version'], text=True).strip() if args.candidate == 'memchr' else None)
    if args.candidate == 'pcre2':
        source = re.search(r'^ZZLOGG_SEARCH_WORKER_PCRE2_SOURCE_DIR:PATH=(.+)$', metadata['cmake_cache'] or '', re.MULTILINE)
        if not source:
            raise ValueError('PCRE worker requires explicit source in CMakeCache')
        pcre = Path(source[1])
        metadata['pcre_source'] = str(pcre)
        metadata['pcre_source_sha256'] = {str(p.relative_to(pcre)): hashlib.sha256(p.read_bytes()).hexdigest()
                                         for p in pcre.rglob('*') if p.is_file()}
    if corpus:
        metadata['corpus'] = corpus['metadata']
    if Path('/proc/cpuinfo').exists():
        metadata['cpu_model'] = next(line.partition(':')[2].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                                     if line.startswith('model name'))
    atomic_write_text(args.output / 'results.json', json.dumps(dict(metadata=metadata, samples=records,
                                                           profiles=profiles, summary=summary), indent=2) + '\n')
    for candidate in (args.candidate, 'qt'):
        values = [r['speedup'] for r in summary if r['candidate'] == candidate]
        print(candidate, 'all-case speedup min/median/max:',
              *(round(x, 3) for x in (min(values), statistics.median(values), max(values))))


if __name__ == '__main__':
    main()
