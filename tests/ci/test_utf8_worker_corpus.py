"""Fixed results catch oracle bit order, UTF-16 fallback and oversize accounting."""
import hashlib
import copy
import json
import tempfile
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/dev'))
import utf8_worker_corpus as worker


class WorkerOracleTests(unittest.TestCase):
    def test_boundary_oracle_distinguishes_row_and_pattern_fallback(self):
        data = b'EDGE\n\xef\xbb\xbfEDGE\n\xff\n\nxx\n' + b'EDGE' + b'x' * 262144
        spec = dict(patterns=['^EDGE', r'\C\C'], sensitive=True, combine='or', inverse=False)
        result = worker.expected(data, spec)
        self.assertEqual(result['result_sha256'], hashlib.sha256(b'0,1,4,5,262148:6').hexdigest())
        self.assertEqual((result['rows'], result['matched'], result['oversized']), (6, 4, 1))
        self.assertEqual((result['direct'], result['fallback'], result['fallback_patterns']), (3, 7, 1))

    def test_boolean_not_then_inverse_keeps_regex_semantics(self):
        data = b'i2c timeout\ni2c probe timeout\nspi timeout\ni2c\n'
        spec = dict(patterns=['i2c(?=.*timeout)', 'probe'], sensitive=True, combine='and-not', inverse=True)
        result = worker.expected(data, spec)
        self.assertEqual(result['result_sha256'], hashlib.sha256(b'1,2,3,17:4').hexdigest())
        self.assertEqual((result['matched'], result['direct'], result['fallback']), (3, 8, 0))

    def test_report_requires_full_results_routes_and_both_backends(self):
        case = dict(rows=2, matched=1, threads=1, oversized=0, programs=1, fallback_patterns=0,
                    direct=2, fallback=0, input_bytes=8, file_sha256='file', input_sha256='query',
                    result_sha256=hashlib.sha256(b'0,4:2').hexdigest())
        report = dict(metadata=dict(candidate='pcre2', isolated_profiles=True), samples=[], profiles=[])
        for backend in ('pcre2', 'qt'):
            common = dict(scenario='fixed', candidate=backend, round=0,
                **{k: case[k] for k in ('input_bytes', 'file_sha256', 'input_sha256', 'result_sha256')})
            for role in ('reference', 'candidate', 'candidate', 'reference'):
                report['samples'].append(dict(common, role=role, rows=2, matched=1, threads=1,
                    elapsed_ns=100, first_progress_ns=50, first_hit_ns=50))
            fast = int(backend == 'pcre2')
            report['profiles'].append(dict(common, complete_lines=2, live_blocks=0, live_matchers=0,
                matchers=fast, peak_matchers=fast, batched_rows=2 * fast, oversized_qt_lines=0,
                pcre_direct=2 * fast, pcre_fallback=0, pcre_programs=fast, pcre_jit_programs=fast,
                pcre_fallback_patterns=0, pcre_runtime_errors=0, max_batch_rows=2 * fast,
                max_batch_bytes=6 * fast, process_peak_rss_bytes=4096))
        with tempfile.TemporaryDirectory() as temporary:
            expected = Path(temporary) / 'expected.json'
            actual = Path(temporary) / 'report.json'
            expected.write_text(json.dumps(dict(cases={'fixed': case})))
            actual.write_text(json.dumps(report))
            worker.verify(expected, actual)
            wake_report = copy.deepcopy(report)
            wake_report['metadata']['candidate'] = 'wake'
            for row in wake_report['samples']:
                if row['candidate'] == 'pcre2': row['candidate'] = 'wake'
                row['process_cpu_ns'] = 80
            for row in wake_report['profiles']:
                wake = row['candidate'] == 'pcre2'
                if wake: row['candidate'] = 'wake'
                for key in ('matchers', 'peak_matchers', 'batched_rows', 'pcre_direct', 'pcre_programs',
                            'pcre_jit_programs', 'max_batch_rows', 'max_batch_bytes'):
                    row[key] = 0
                row.update(wake_searches=int(wake), capacity_releases=int(wake),
                    capacity_waits=int(wake), capacity_timeouts=0, poll_sleeps=0)
            actual.write_text(json.dumps(wake_report))
            worker.verify(expected, actual)
            assist_report = copy.deepcopy(wake_report)
            assist_report['metadata']['candidate'] = 'assist'
            for field in ('samples', 'profiles'):
                for row in assist_report[field]:
                    if row['candidate'] == 'wake': row['candidate'] = 'assist'
            for row in assist_report['profiles']:
                row.update(assist_searches=int(row['candidate'] == 'assist'), producer_drains=0,
                    producer_callbacks=2, wake_searches=0, capacity_waits=0, capacity_releases=0)
            actual.write_text(json.dumps(assist_report))
            worker.verify(expected, actual)
            for key, value in [('assist_searches', 0), ('capacity_waits', 1), ('poll_sleeps', 1),
                               ('matchers', 1), ('producer_callbacks', -1)]:
                broken = copy.deepcopy(assist_report)
                broken['profiles'][0][key] = value
                actual.write_text(json.dumps(broken))
                with self.subTest(assist_key=key), self.assertRaises(ValueError):
                    worker.verify(expected, actual)
            for key, value in [('capacity_releases', 0), ('wake_searches', 0), ('poll_sleeps', 1), ('matchers', 1)]:
                broken = copy.deepcopy(wake_report)
                broken['profiles'][0][key] = value
                actual.write_text(json.dumps(broken))
                with self.subTest(wake_key=key), self.assertRaises(ValueError):
                    worker.verify(expected, actual)
            for field, key, value in [('samples', 'result_sha256', 'wrong'),
                ('samples', 'threads', 2), ('samples', 'first_hit_ns', -1),
                ('profiles', 'pcre_direct', 0), ('profiles', 'live_matchers', 1),
                ('profiles', 'pcre_jit_programs', 0)]:
                broken = copy.deepcopy(report)
                broken[field][0][key] = value
                actual.write_text(json.dumps(broken))
                with self.subTest(field=field, key=key), self.assertRaises(ValueError):
                    worker.verify(expected, actual)
            for field in ('samples', 'profiles'):
                broken = copy.deepcopy(report)
                broken[field].pop()
                actual.write_text(json.dumps(broken))
                with self.subTest(missing=field), self.assertRaises(ValueError):
                    worker.verify(expected, actual)


if __name__ == '__main__':
    unittest.main()
