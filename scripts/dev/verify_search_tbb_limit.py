#!/usr/bin/env python3
"""Bounded integration contract for a real probe's explicit TBB budget."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory(prefix='zzlogg-tbb-contract-') as temporary:
        root = Path(temporary)
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', TMPDIR=temporary, TMP=temporary, TEMP=temporary)
        (root/'log.txt').write_bytes(b'ERROR\nok\n' * 128 + b'ERROR\n')
        manifest = root/'queries.json'
        manifest.write_text(json.dumps(dict(version=1, cases=[dict(id='fixed', file='log.txt',
            encoding='UTF-8', pattern='ERROR', kind='literal', threads=2, chunk_lines=1)])))
        # Independent fixed oracle: 129 even rows, width 5, extent 257.
        digest = hashlib.sha256((''.join(f'{i},' for i in range(0, 257, 2)) + '5:257').encode()).hexdigest()
        for limit in (1, 2, 3):
            for backend in ('assist', 'qt', 'wake'):
                for profile in (False, True):
                    command = [str(binary), '--corpus', str(manifest), '--candidate', backend,
                               '--tbb-limit', str(limit)]
                    if profile:
                        command += ['--profile']
                    result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=30)
                    assert result.returncode == 0, result.stderr
                    rows = [json.loads(line) for line in result.stdout.splitlines()]
                    assert len(rows) == (1 if profile else 4)
                    assert all(r['tbb_global_limit'] == limit and r['tbb_default_concurrency'] >= 1 for r in rows)
                    assert all(r['result_sha256'] == digest for r in rows), 'Wrong complete rows/width/extent'
                    if profile:
                        row = rows[0]
                        assert row['assist_searches'] == int(backend == 'assist')
                        if backend == 'assist':
                            assert row['wake_searches'] == row['poll_sleeps'] == row['capacity_waits'] == 0
                            if limit == 1:
                                assert row['producer_drains'] > 0 and row['producer_callbacks'] > 0
                        assert row['live_blocks'] == row['live_matchers'] == 0
                    if not profile:
                        assert all(r['matched'] == 129 and r['rows'] == 257 for r in rows)
            # Also exercise runner forwarding to verification, timing and profile subprocesses.
            output = root/f'report-{limit}'
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('benchmark_search_worker.py')),
                '--binary', str(binary), '--corpus', str(manifest), '--candidate', 'assist',
                '--tbb-limit', str(limit), '--rounds', '1', '--isolated-profiles', '--output', str(output)],
                env=env, text=True, capture_output=True, timeout=90)
            assert result.returncode == 0, result.stderr
            report = json.loads((output/'results.json').read_text())
            assert report['metadata']['requested_tbb_limit'] == limit
            assert len(report['samples']) == 8 and len(report['profiles']) == 2
            assert all(r['tbb_global_limit'] == limit and r['result_sha256'] == digest
                       for field in ('samples', 'profiles') for r in report[field])
            for mode in ('connect', 'read', 'allocation', 'match', 'combine', 'completion'):
                for scheduler in ('poll', 'wake', 'assist'):
                    command = [str(binary), '--verify-recovery', mode, '--tbb-limit', str(limit)]
                    if scheduler == 'wake':
                        command += ['--recovery-wake']
                    if scheduler == 'assist':
                        command += ['--recovery-assist']
                    fault = subprocess.run(command, env=env, text=True, capture_output=True, timeout=30)
                    assert fault.returncode == 0, (limit, mode, scheduler, fault.stderr)
        for value in ('0', '-1', '257', '2.5', 'invalid', '9999999999999999999999999'):
            direct = subprocess.run([str(binary), '--case', 'short1-t1', '--tbb-limit', value],
                env=env, text=True, capture_output=True, timeout=30)
            assert direct.returncode != 0 and not direct.stdout, 'Invalid probe limit accepted'
            invalid_output = root/'invalid-output'
            runner = subprocess.run([sys.executable, str(Path(__file__).with_name('benchmark_search_worker.py')),
                '--binary', str(binary), '--tbb-limit', value, '--output', str(invalid_output)],
                env=env, text=True, capture_output=True, timeout=30)
            assert runner.returncode != 0 and not invalid_output.exists(), 'Invalid runner limit accepted'
    print('TBB limit contracts passed: full results, timing/profile budgets, runner forwarding, exact recovery injections and invalid input')


if __name__ == '__main__':
    main()
