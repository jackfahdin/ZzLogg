"""Android-shaped synthetic logs must have independent, reproducible oracles."""
import hashlib
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('android_corpus', ROOT / 'scripts/dev/generate_android_search_corpus.py')
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)


class AndroidCorpusTests(unittest.TestCase):
    def test_oracle_tracks_full_rows_byte_width_and_unterminated_tail(self):
        lines = [b'irq\tready', '温度 ERROR'.encode(), b'plain', b'ERROR\0tail']
        actual = android.oracle(lines, lambda text: 'ERROR' in text)
        self.assertEqual(actual, dict(rows=4, matched=2, max_length=12,
            result_sha256=hashlib.sha256(b'1,3,12:4').hexdigest()))

    def test_fixed_query_predicates_cover_driver_and_android_semantics(self):
        queries = {q['id']: q for q in android.query_specs()}
        lines = ['i2c transfer timeout error -110', 'spi timeout', 'binder debug txn',
                 'binder transaction failed', 'FATAL EXCEPTION: main', 'heartbeat', '温度=42']
        expected = {'dmesg-i2c': [0], 'logcat-binder': [3], 'logcat-fatal': [4],
                    'app-non-heartbeat': [0, 1, 2, 3, 4, 6], 'app-temperature': [6]}
        for name, wanted in expected.items():
            self.assertIn(name, queries)
            self.assertEqual([i for i, line in enumerate(lines) if queries[name]['predicate'](line)], wanted)

    def test_generation_is_reproducible_varied_and_preserves_boundary_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, seed in [('a', 17), ('b', 17), ('c', 18)]:
                android.generate(root / name, rows=1024, seed=seed)
            for name in ['dmesg.log', 'logcat.log', 'app.log', 'boundaries.log', 'queries.json', 'expected.json']:
                self.assertEqual((root / 'a' / name).read_bytes(), (root / 'b' / name).read_bytes())
            for name in ['dmesg.log', 'logcat.log', 'app.log']:
                data = (root / 'a' / name).read_bytes()
                self.assertEqual(len(data.split(b'\n')) - 1, 1024)
                self.assertNotEqual(data, (root / 'c' / name).read_bytes())
                self.assertGreater(len(set(data.splitlines())), 900)
            boundary = (root / 'a' / 'boundaries.log').read_bytes()
            self.assertTrue(boundary.startswith(b'\xef\xbb\xbf'))
            self.assertIn(b'\xff', boundary)
            self.assertIn(b'\0', boundary)
            self.assertIn(b'\r\n', boundary)
            self.assertFalse(boundary.endswith(b'\n'))
            self.assertEqual(sum(len(line) > 262144 for line in boundary.split(b'\n')), 2)
            expected = json.loads((root / 'a' / 'expected.json').read_text())
            self.assertTrue(expected['synthetic'])
            self.assertEqual(len(expected['cases']), 34)
            self.assertEqual(expected['cases']['dmesg-miss-t1']['matched'], 0)

    def test_report_verification_requires_full_pairs_and_both_backends(self):
        case = dict(rows=3, matched=1, max_length=6,
                    result_sha256=hashlib.sha256(b'1,6:3').hexdigest(),
                    file_sha256=hashlib.sha256(b'a\nneedle\nz\n').hexdigest(),
                    input_bytes=11, eligible=True, oversized_lines=0, threads=1)
        common = dict(scenario='fixed', round=0, result_sha256=case['result_sha256'],
                      file_sha256=case['file_sha256'], input_bytes=11,
                      input_sha256=hashlib.sha256(b'query configuration').hexdigest())
        case['input_sha256'] = common['input_sha256']
        report = dict(samples=[], profiles=[])
        for candidate in ('qt', 'memchr'):
            for role in ('reference', 'candidate', 'candidate', 'reference'):
                report['samples'].append(dict(common, candidate=candidate, role=role,
                                              rows=3, matched=1, elapsed_ns=100))
            fast = candidate == 'memchr'
            report['profiles'].append(dict(common, candidate=candidate, complete_lines=3,
                live_blocks=0, matchers=int(fast), oversized_qt_lines=0,
                batched_rows=3 if fast else 0, max_batch_rows=3 if fast else 0,
                max_batch_bytes=8 if fast else 0))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            expected = root / 'expected.json'
            output = root / 'report.json'
            expected.write_text(json.dumps(dict(version=1, synthetic=True, cases={'fixed': case})))
            output.write_text(json.dumps(report))
            self.assertEqual(android.verify_report(expected, output), (8, 2))
            broken = []
            incomplete = copy.deepcopy(report)
            incomplete['samples'].pop()
            broken.append(incomplete)
            qt_only = copy.deepcopy(report)
            for field in ('samples', 'profiles'):
                qt_only[field] = [r for r in qt_only[field] if r['candidate'] == 'qt']
            broken.append(qt_only)
            duplicate = copy.deepcopy(report)
            duplicate['profiles'].append(copy.deepcopy(duplicate['profiles'][0]))
            broken.append(duplicate)
            corrupt = copy.deepcopy(report)
            corrupt['samples'][0]['result_sha256'] = 'wrong'
            broken.append(corrupt)
            routing = copy.deepcopy(report)
            routing['profiles'][1]['matchers'] = 0
            broken.append(routing)
            wrong_config = copy.deepcopy(report)
            for field in ('samples', 'profiles'):
                for record in wrong_config[field]:
                    record['input_sha256'] = '0' * 64
            broken.append(wrong_config)
            for index, bad in enumerate(broken):
                with self.subTest(index=index):
                    output.write_text(json.dumps(bad))
                    with self.assertRaises(ValueError):
                        android.verify_report(expected, output)

    def test_existing_directory_and_invalid_size_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'keep.log'
            source.write_text('keep')
            with self.assertRaises(ValueError):
                android.generate(root, rows=1024, seed=1)
            self.assertEqual(source.read_text(), 'keep')
            with self.assertRaises(ValueError):
                android.generate(root / 'invalid', rows=0, seed=1)
            self.assertFalse((root / 'invalid').exists())


if __name__ == '__main__':
    unittest.main()
