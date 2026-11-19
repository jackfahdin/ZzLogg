"""Fixed, independently enumerated results for the explicit P12 experiment."""
import hashlib
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('utf8_benchmark', ROOT / 'scripts/dev/benchmark_utf8_regex.py')
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class Utf8RegexOracleTests(unittest.TestCase):
    def test_bom_bad_bytes_and_utf16_units_keep_full_bitmap_and_routes(self):
        data = b'EDGE\n\xef\xbb\xbfEDGE\n\xff\n\nxx'
        patterns = ['^EDGE', r'\x{FFFD}', r'\C\C']
        result, direct, fallback = benchmark.expected_result(data, patterns, True)
        self.assertEqual(result['result_sha256'], hashlib.sha256(bytes([
            1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1])).hexdigest())
        self.assertEqual(result['hits'], [2, 1, 3])
        self.assertEqual((result['rows'], direct, fallback), (5, 6, 9))
        self.assertEqual(result['file_sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(result['input_bytes'], len(data))

    def test_named_backreference_and_casefold_preserve_pattern_positions(self):
        data = b'com.example.driverlab com\ncom.example.driverlab nope\nTIMEOUT\ntimeout\n'
        patterns = [r'(?<x>com)\.example\.driverlab.*\k<x>', 'timeout']
        result, direct, fallback = benchmark.expected_result(data, patterns, False)
        self.assertEqual(result['result_sha256'], hashlib.sha256(bytes([1,0,0,0,0,1,0,1])).hexdigest())
        self.assertEqual(result['hits'], [1, 2])
        self.assertEqual((result['rows'], direct, fallback), (4, 8, 0))


if __name__ == '__main__':
    unittest.main()
