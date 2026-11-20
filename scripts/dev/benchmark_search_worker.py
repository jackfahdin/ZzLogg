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


RESOURCE_FIELDS = ('process_user_cpu_ns', 'process_system_cpu_ns',
                   'voluntary_context_switches', 'involuntary_context_switches')


def cpu_topology(cpus, root=Path('/sys/devices/system/cpu')):
    """Report selected logical CPUs without assuming they are distinct physical cores."""
    if not cpus:
        return None
    records = []
    for cpu in sorted(set(cpus)):
        directory = root / f'cpu{cpu}' / 'topology'
        def read(name):
            try:
                return (directory / name).read_text().strip()
            except (OSError, UnicodeError):
                return None
        def identity(name):
            try:
                value = int(read(name))
                return value if value >= 0 else None
            except (TypeError, ValueError):
                return None
        records.append(dict(cpu=cpu, package_id=identity('physical_package_id'),
                            core_id=identity('core_id'), thread_siblings_list=read('thread_siblings_list')))
    complete = all(r['package_id'] is not None and r['core_id'] is not None for r in records)
    physical = len({(r['package_id'], r['core_id']) for r in records}) if complete else None
    return dict(source=str(root), cpus=records, selected_physical_cores=physical,
                shares_physical_core=physical < len(records) if complete else None)


def validate_process_usage(row):
    # Old probe output has only total CPU time. New fields must arrive as one group.
    if not any(field in row for field in RESOURCE_FIELDS):
        return
    fields = ('process_cpu_ns', *RESOURCE_FIELDS)
    if not all(field in row for field in fields):
        raise ValueError('Incomplete process resource counters')
    values = [row[field] for field in fields]
    if not all(type(v) is int or (type(v) is float and math.isfinite(v) and v.is_integer()) for v in values):
        raise ValueError('Process resource counters must be integral numbers')
    if all(v == -1 for v in values):
        return
    if any(v < 0 for v in values) or values[0] != values[1] + values[2]:
        raise ValueError('Inconsistent process CPU or context-switch counters')


def validate_tbb_limit(row, requested):
    if requested is None:
        return  # Old probes remain compatible when no budget override was requested.
    if (type(row.get('tbb_global_limit')) is not int or row['tbb_global_limit'] != requested
            or type(row.get('tbb_default_concurrency')) is not int or row['tbb_default_concurrency'] < 1):
        raise ValueError('Probe did not report the requested effective TBB budget')


def validate_assist_profile(row, candidate):
    if candidate != 'assist':
        return
    expected = dict(assist_searches=1, wake_searches=0, poll_sleeps=0, capacity_waits=0, capacity_releases=0)
    if (any(type(row.get(key)) is not int or row[key] != value for key, value in expected.items())
            or any(type(row.get(key)) is not int or row[key] < 0
                   for key in ('producer_drains', 'producer_callbacks'))):
        raise ValueError('Probe did not execute the requested producer assistance policy')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('out/benchmarks/p11-worker/results'))
    parser.add_argument('--candidate', choices=('memchr', 'pcre2', 'wake', 'assist'), default='memchr')
    parser.add_argument('--isolated-profiles', action='store_true', help='Fresh process per scenario for peak RSS')
    parser.add_argument('--rounds', type=int, default=4)
    source = parser.add_mutually_exclusive_group()
    source.add_argument('--case')
    source.add_argument('--corpus', type=Path)
    parser.add_argument('--timeout', type=float, default=600, help='Hard timeout per probe process in seconds')
    parser.add_argument('--cpus', type=int, nargs='+')
    parser.add_argument('--tbb-limit', type=int, help='Explicit probe global TBB budget (1..256); omitted uses automatic budget')
    args = parser.parse_args()
    if args.tbb_limit is not None and not 1 <= args.tbb_limit <= 256:
        parser.error('Use --tbb-limit from 1 through 256')
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
        cpus = sorted(set(args.cpus or sorted(allowed)[:2]))
        if not set(cpus) <= allowed:
            raise ValueError('CPUs outside allowed set')
        os.sched_setaffinity(0, set(cpus))
    elif args.cpus:
        raise ValueError('CPU affinity unsupported')
    binary = args.binary.resolve()
    def run(command, label):
        if args.tbb_limit is not None:
            command = [*command, '--tbb-limit', str(args.tbb_limit)]
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
                validate_tbb_limit(row, args.tbb_limit)
                validate_process_usage(row)
                if row['candidate'] != candidate or row['role'] not in ('reference', 'candidate') or row['elapsed_ns'] <= 0:
                    raise RuntimeError('Invalid measurement')
                if row['process_cpu_ns'] < -1:
                    raise RuntimeError('Invalid process CPU time')
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
                validate_tbb_limit(row, args.tbb_limit)
                if row['candidate'] != candidate or (row['input_sha256'], row['result_sha256']) != signatures[row['scenario']]:
                    raise RuntimeError('Profile differs from measured inputs/results')
                validate_assist_profile(row, candidate)
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
                                process_cpu_ns={role: statistics.median(r['process_cpu_ns'] for r in selected
                                    if r['role'] == role and r['process_cpu_ns'] >= 0) if any(r['role'] == role
                                    and r['process_cpu_ns'] >= 0 for r in selected) else None
                                    for role in ('reference', 'candidate')},
                                first_notifications={role: {field: statistics.median(r[field] for r in selected
                                    if r['role'] == role and r[field] >= 0) if any(r['role'] == role and r[field] >= 0
                                    for r in selected) else None for field in ('first_progress_ns', 'first_hit_ns')}
                                    for role in ('reference', 'candidate')}))
    root = Path(__file__).resolve().parents[2]
    files = [Path(__file__).resolve(), Path(__file__).with_name('search_corpus.py').resolve(), root / 'CMakeLists.txt']
    if args.candidate in ('pcre2', 'wake', 'assist'):
        files += [Path(__file__).with_name(name).resolve() for name in
                  ('utf8_worker_corpus.py', 'benchmark_utf8_regex.py', 'generate_android_search_corpus.py')]
    for directory in ['tools/benchmarks/search_worker', 'tools/benchmarks/literal_search', 'tools/benchmarks/utf8_regex',
                      'src/regex', 'src/logdata', 'src/utils', 'src/settings', 'src/ui']:
        files += [p for p in (root / directory).rglob('*') if p.is_file()]
    cache = binary.parent.parent / 'CMakeCache.txt'
    metadata = dict(platform=platform.platform(), cpus=cpus, cpu_topology=cpu_topology(cpus),
        requested_tbb_limit=args.tbb_limit,
        baseline_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        source_sha256={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        cmake_cache=cache.read_text() if cache.exists() else None,
        candidate=args.candidate, isolated_profiles=args.isolated_profiles, qt_jit_environment='1',
        rustc=subprocess.check_output(['rustc', '--version'], text=True).strip() if args.candidate == 'memchr' else None)
    source = re.search(r'^ZZLOGG_SEARCH_WORKER_PCRE2_SOURCE_DIR:PATH=(.+)$', metadata['cmake_cache'] or '', re.MULTILINE)
    if args.candidate == 'pcre2' and not source:
        raise ValueError('PCRE worker requires explicit source in CMakeCache')
    if source:
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
