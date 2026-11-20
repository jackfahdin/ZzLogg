#!/usr/bin/env python3
"""Prepare/verify the fixed SYNTHETIC Android PCRE worker experiment, not arbitrary regexes."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re

from benchmark_utf8_regex import CASES
from benchmark_search_worker import validate_assist_profile
from generate_android_search_corpus import Oracle, input_signature


def specs():
    result = {}
    for name, (file, patterns, sensitive) in CASES.items():
        if name != 'boundaries':
            result[name] = dict(file=file, patterns=patterns, sensitive=sensitive, combine='or', inverse=False)
    for inverse in (False, True):
        result['dmesg-boolean' + ('-inverse' if inverse else '')] = dict(file='dmesg',
            patterns=['i2c(?=.*timeout)', 'probe'], sensitive=True, combine='and-not', inverse=inverse)
    result['dmesg-miss'] = dict(file='dmesg', patterns=['zzlogg-never'], sensitive=True, combine='or', inverse=False)
    for name, pattern in [('marker', '^EDGE'), ('replacement', r'\x{FFFD}'), ('empty', '^$'),
                          ('code-unit', r'\C\C'), ('payload', 'payload_end')]:
        result['boundary-' + name] = dict(file='boundaries', patterns=[pattern], sensitive=True,
                                         combine='or', inverse=False)
    return result


def expected(data, spec):
    patterns = spec['patterns']
    # Only these fixed probe patterns: not a general Python-to-PCRE translator.
    regexes = [None if p == r'\C\C' else re.compile(p.replace(r'\x{FFFD}', '\ufffd')
        .replace('(?<x>', '(?P<x>').replace(r'\k<x>', '(?P=x)'),
        0 if spec['sensitive'] else re.IGNORECASE) for p in patterns]
    def predicate(text):
        bits = [bool(r.search(text)) if r else len(text.encode('utf-16-le')) >= 4 for r in regexes]
        hit = bits[0] and not bits[1] if spec['combine'] == 'and-not' else any(bits)
        return not hit if spec['inverse'] else hit
    oracle = Oracle(predicate)
    lines = data.split(b'\n')
    if lines[-1] == b'':
        lines.pop()
    direct = fallback = oversized = 0
    for raw in lines:
        oracle.add(raw)
        if len(raw) > 262144:
            oversized += 1
            continue
        try:
            raw.decode('utf-8')
            valid = not raw.startswith(b'\xef\xbb\xbf')
        except UnicodeDecodeError:
            valid = False
        fast = sum(r is not None for r in regexes) if valid else 0
        direct += fast
        fallback += len(regexes) - fast
    return dict(oracle.finish(), direct=direct, fallback=fallback, oversized=oversized,
                programs=sum(r is not None for r in regexes), fallback_patterns=sum(r is None for r in regexes))


def generate(corpus, output):
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('Use an empty output directory')
    output.mkdir(parents=True, exist_ok=True)
    cases, wanted = [], {}
    for name, spec in specs().items():
        source = (corpus / (spec['file'] + '.log')).resolve()
        data = source.read_bytes()
        stats = expected(data, spec)
        file_hash = hashlib.sha256(data).hexdigest()
        if len(spec['patterns']) == 1:
            pattern = spec['patterns'][0]
        else:
            quoted = ['"' + p.replace('\\', '\\\\').replace('"', '\\"') + '"' for p in spec['patterns']]
            pattern = quoted[0] + ' and not(' + quoted[1] + ')' if spec['combine'] == 'and-not' else ' or '.join(quoted)
        for threads in (1, 2):
            case = dict(id=f'{name}-t{threads}', file=str(source), encoding='UTF-8', pattern=pattern,
                kind='regex' if len(spec['patterns']) == 1 else 'boolean-regex',
                case_sensitive=spec['sensitive'], inverse=spec['inverse'], threads=threads,
                chunk_lines=1024, timeout_ms=120000)
            cases.append(case)
            wanted[case['id']] = dict(stats, file_sha256=file_hash, input_bytes=len(data), threads=threads,
                                      input_sha256=input_signature(case, file_hash), chunk_lines=case['chunk_lines'])
    (output / 'queries.json').write_text(json.dumps(dict(version=1, cases=cases), ensure_ascii=False, indent=2) + '\n')
    (output / 'expected.json').write_text(json.dumps(dict(synthetic=True, cases=wanted), indent=2) + '\n')
    print('SYNTHETIC Android worker scenarios:', len(cases))


def verify(expected_path, report_path):
    wanted = json.loads(expected_path.read_text())['cases']
    report = json.loads(report_path.read_text())
    candidate = report['metadata']['candidate']
    if candidate not in ('pcre2', 'wake', 'assist') or not report['metadata']['isolated_profiles']:
        raise ValueError('Expected PCRE, wake or assist candidate and isolated process profiles')
    groups = {field: defaultdict(list) for field in ('samples', 'profiles')}
    for field, group in groups.items():
        for row in report[field]:
            name = row['scenario']
            case = wanted[name]
            backend = row['candidate']
            if backend not in (candidate, 'qt') or type(row['round']) is not int or row['round'] < 0:
                raise ValueError('Invalid backend or round')
            group[name, backend, row['round']].append(row)
            for key in ('result_sha256', 'file_sha256', 'input_sha256', 'input_bytes'):
                if row[key] != case[key]:
                    raise ValueError(f'Independent oracle mismatch: {name} {key}')
            if field == 'samples':
                if (row['rows'], row['matched'], row['threads']) != (case['rows'], case['matched'], case['threads']):
                    raise ValueError('Result counts or threads differ')
                if candidate in ('wake', 'assist') and row['process_cpu_ns'] < -1:
                    raise ValueError('Invalid process CPU time')
                if row['elapsed_ns'] <= 0 or any(not -1 <= row[k] <= row['elapsed_ns']
                    for k in ('first_progress_ns', 'first_hit_ns')) or (row['first_hit_ns'] >= 0) != (case['matched'] > 0):
                    raise ValueError('Invalid elapsed/notification timing')
            else:
                fast = backend == 'pcre2'
                expected_metrics = dict(complete_lines=case['rows'], live_blocks=0, live_matchers=0,
                    matchers=case['threads'] if fast else 0, peak_matchers=case['threads'] if fast else 0,
                    batched_rows=case['rows'] - case['oversized'] if fast else 0,
                    oversized_qt_lines=case['oversized'] if fast else 0,
                    pcre_direct=case['direct'] if fast else 0, pcre_fallback=case['fallback'] if fast else 0,
                    pcre_programs=case['programs'] * case['threads'] if fast else 0,
                    pcre_jit_programs=case['programs'] * case['threads'] if fast else 0,
                    pcre_fallback_patterns=case['fallback_patterns'] * case['threads'] if fast else 0,
                    pcre_runtime_errors=0)
                if candidate in ('wake', 'assist'):
                    wake = backend == 'wake'
                    expected_metrics.update(wake_searches=int(wake),
                        capacity_releases=(case['rows'] + case.get('chunk_lines', 1024) - 1) // case.get('chunk_lines', 1024) if wake else 0)
                    if wake:
                        expected_metrics['poll_sleeps'] = 0
                    elif row['capacity_waits'] != 0 or row['capacity_timeouts'] != 0:
                        raise ValueError('Qt control entered capacity wait')
                    if not 0 <= row['capacity_timeouts'] <= row['capacity_waits'] or row['poll_sleeps'] < 0:
                        raise ValueError('Invalid scheduling counts')
                if candidate == 'assist':
                    expected_metrics['assist_searches'] = int(backend == 'assist')
                    validate_assist_profile(row, backend)
                for key, value in expected_metrics.items():
                    if row[key] != value:
                        raise ValueError(f'Routing/resource mismatch: {name} {key}: {row[key]} != {value}')
                if row['max_batch_rows'] > 128 or row['max_batch_bytes'] > 262144:
                    raise ValueError('Batch bounds exceeded')
                if row['process_peak_rss_bytes'] != -1 and row['process_peak_rss_bytes'] <= 0:
                    raise ValueError('Invalid process peak RSS')
                # Main files must demonstrate effective early cancellation; 12-row boundary file need not.
                if case['rows'] >= 65536 and (not row['cancel_requested'] or row['cancel_processed'] >= case['rows']):
                    raise ValueError('Cancellation missed active main-file search')
    rounds = {key[2] for key in groups['samples']}
    if not rounds or rounds != set(range(max(rounds) + 1)):
        raise ValueError('Incomplete rounds')
    keys = {(name, backend, r) for name in wanted for backend in (candidate, 'qt') for r in rounds}
    if any(set(g) != keys for g in groups.values()):
        raise ValueError('Incomplete case/backend/round coverage')
    for key in keys:
        order = ['candidate', 'reference', 'reference', 'candidate'] if key[2] % 2 else ['reference', 'candidate', 'candidate', 'reference']
        if [r['role'] for r in groups['samples'][key]] != order or len(groups['profiles'][key]) != 1:
            raise ValueError('Incorrect pairs or profile count')
    print(f"Independent worker oracle passed: {len(report['samples'])} timings, {len(report['profiles'])} profiles")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    create = subs.add_parser('generate')
    create.add_argument('--corpus', type=Path, required=True)
    create.add_argument('--output', type=Path, required=True)
    check = subs.add_parser('verify')
    check.add_argument('--expected', type=Path, required=True)
    check.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'generate':
        generate(args.corpus, args.output)
    else:
        verify(args.expected, args.report)


if __name__ == '__main__':
    main()
