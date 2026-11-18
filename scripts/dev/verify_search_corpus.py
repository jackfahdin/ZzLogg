#!/usr/bin/env python3
"""Explicit contracts against a built probe; never require Rust in default CI."""
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
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    with tempfile.TemporaryDirectory(prefix='zzlogg-corpus-contract-') as temporary:
        root = Path(temporary)
        source = root / 'input with spaces.log'
        content = b'ERROR\nok\nERROR'
        source.write_bytes(content)
        manifest = root / 'queries.json'
        base = dict(id='tail', file=source.name, encoding='UTF-8', pattern='ERROR', kind='literal')
        def write(cases):
            manifest.write_text(json.dumps(dict(version=1, cases=cases)), encoding='utf-8')
        def run(case, success=True):
            write([case])
            result = subprocess.run([str(binary), '--corpus', str(manifest), '--candidate', 'memchr'],
                                    env=env, text=True, capture_output=True, timeout=60)
            if not success:
                assert result.returncode != 0, 'Invalid corpus accepted'
                return
            assert result.returncode == 0, result.stderr
            rows = [json.loads(line) for line in result.stdout.splitlines()]
            assert len(rows) == 4 and [r['role'] for r in rows] == ['reference','candidate','candidate','reference']
            assert len({r['input_sha256'] for r in rows}) == 1
            assert all(r['file_sha256'] == hashlib.sha256(source.read_bytes()).hexdigest() for r in rows)
            return rows[0]
        first = run(base)
        assert first['rows'] == 3 and first['matched'] == 2
        assert first['result_sha256'] == hashlib.sha256(b'0,2,5:3').hexdigest(), 'Wrong complete row/width digest'
        for change in [dict(case_sensitive=False), dict(inverse=True), dict(kind='regex'),
                       dict(encoding='ISO-8859-1'), dict(prefilter='never-match'),
                       dict(threads=2), dict(chunk_lines=1), dict(timeout_ms=40000)]:
            changed = run(dict(base, **change))
            assert changed['input_sha256'] != first['input_sha256'], 'Configuration missing from signature'
        inverse = run(dict(base, inverse=True))
        assert inverse['result_sha256'] == hashlib.sha256(b'1,2:3').hexdigest()
        boolean = run(dict(base, pattern='"ERROR" or "ok"', kind='boolean-literal'))
        assert boolean['result_sha256'] == hashlib.sha256(b'0,1,2,5:3').hexdigest()
        source.write_bytes('ERROR\nok\nERROR'.encode('utf-16-le'))
        encoded = run(dict(base, encoding='UTF-16LE'))
        assert encoded['result_sha256'] == first['result_sha256']
        source.write_bytes(content)
        for change in [dict(encoding='not-a-codec'), dict(kind='wrong'), dict(threads=0),
                       dict(threads=True), dict(chunk_lines=0), dict(timeout_ms=0),
                       dict(case_sensitive='false'), dict(file='missing'), dict(file='.'),
                       dict(kind='regex', pattern='('), dict(prefilter='('), dict(unknown=True)]:
            run(dict(base, **change), False)
        write([base])
        ignored = subprocess.run([str(binary), '--corpus', str(manifest), '--verify'],
                                 env=env, capture_output=True, timeout=60)
        assert ignored.returncode != 0, 'Corpus silently ignored by --verify'
        write([base, base])
        duplicate = subprocess.run([str(binary), '--corpus', str(manifest)], env=env, capture_output=True, timeout=60)
        assert duplicate.returncode != 0, 'Duplicate scenario IDs accepted'
        write([base])
        output = root / 'results'
        output.mkdir()
        os.link(source, output / 'contracts.jsonl')
        os.link(manifest, output / '0-memchr.stderr.log')
        original_manifest = manifest.read_bytes()
        scratch = root / 'scratch'
        scratch.mkdir()
        runner_env = dict(env, TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch))
        runner = Path(__file__).with_name('benchmark_search_worker.py')
        command = [sys.executable, str(runner), '--binary', str(binary), '--corpus', str(manifest),
                   '--rounds', '1', '--output', str(output), '--timeout', '60']
        result = subprocess.run(command, env=runner_env, text=True, capture_output=True, timeout=120)
        assert result.returncode == 0, result.stderr
        report = json.loads((output / 'results.json').read_text())
        assert len(report['samples']) == 8 and len(report['profiles']) == 2
        assert report['metadata']['corpus']['files'][0]['sha256'] == hashlib.sha256(content).hexdigest()
        assert source.read_bytes() == content, 'Source modified by runner'
        assert manifest.read_bytes() == original_manifest, 'Manifest modified through output link'
        write([dict(base, file='missing')])
        failed = subprocess.run(command, env=runner_env, capture_output=True, timeout=60)
        assert failed.returncode != 0 and not (output / 'results.json').exists(), 'Stale success report retained'
        write([base])
        timed = subprocess.run(command[:-1] + ['0.001'], env=runner_env, capture_output=True, timeout=60)
        assert timed.returncode != 0 and b'TimeoutExpired' in timed.stderr
        assert not (output / 'results.json').exists(), 'Timeout published a report'
        assert not list(scratch.iterdir()), 'Timeout leaked process or corpus snapshots'
        # Output logs must not overwrite a source, including a results.json input.
        protected = output / 'results.json'
        protected.write_bytes(content)
        write([dict(base, file=str(protected))])
        overlap = subprocess.run(command, env=runner_env, capture_output=True, timeout=60)
        assert overlap.returncode != 0 and protected.read_bytes() == content, 'Output overwrote source'
    print('Corpus contracts passed: real files, fixed complete results, encodings, signatures, invalid inputs and runner snapshots')


if __name__ == '__main__':
    main()
