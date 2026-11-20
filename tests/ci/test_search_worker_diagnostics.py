"""Distinguish physical-core selection and reject misleading resource accounting."""
import sys
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/dev'))
import benchmark_search_worker as runner


class WorkerDiagnosticsTests(unittest.TestCase):
    def test_assist_profile_requires_actual_execution_policy(self):
        row = dict(assist_searches=1, producer_drains=3, producer_callbacks=14,
                   wake_searches=0, poll_sleeps=0, capacity_waits=0, capacity_releases=0)
        runner.validate_assist_profile(row, 'assist')
        runner.validate_assist_profile({}, 'qt')  # Older probe output is still supported.
        for key, value in [('assist_searches', 0), ('wake_searches', 1), ('poll_sleeps', 1),
                           ('capacity_waits', 1), ('capacity_releases', 1),
                           ('producer_drains', -1), ('producer_callbacks', True)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                runner.validate_assist_profile(dict(row, **{key: value}), 'assist')
        with self.assertRaises(ValueError):
            runner.validate_assist_profile({}, 'assist')

    def test_explicit_tbb_limit_requires_actual_matching_budget(self):
        runner.validate_tbb_limit({}, None)  # Older probe output remains usable without override.
        for limit in (1, 2, 3, 256):
            runner.validate_tbb_limit(dict(tbb_global_limit=limit, tbb_default_concurrency=3), limit)
        for row in ({}, dict(tbb_global_limit=3, tbb_default_concurrency=3),
                    dict(tbb_global_limit=2, tbb_default_concurrency=0),
                    dict(tbb_global_limit=True, tbb_default_concurrency=3),
                    dict(tbb_global_limit=2)):
            with self.subTest(row=row), self.assertRaises(ValueError):
                runner.validate_tbb_limit(row, 2)

    def topology(self, root, cpu, package, core, siblings):
        path = root / f'cpu{cpu}' / 'topology'
        path.mkdir(parents=True)
        for name, value in [('physical_package_id', package), ('core_id', core),
                            ('thread_siblings_list', siblings)]:
            if value is not None:
                (path / name).write_text(str(value))

    def test_smt_siblings_are_one_physical_core(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.topology(root, 0, 0, 0, '0-1')
            self.topology(root, 1, 0, 0, '0-1')
            actual = runner.cpu_topology([0, 1], root)
            self.assertEqual(actual['selected_physical_cores'], 1)
            self.assertTrue(actual['shares_physical_core'])
            self.assertEqual(actual['cpus'][1]['thread_siblings_list'], '0-1')

    def test_duplicate_cpu_is_not_misreported_as_smt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.topology(root, 0, 0, 0, '0-1')
            actual = runner.cpu_topology([0, 0], root)
            self.assertEqual(len(actual['cpus']), 1)
            self.assertEqual(actual['selected_physical_cores'], 1)
            self.assertFalse(actual['shares_physical_core'])

    def test_core_identity_includes_socket(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.topology(root, 0, 0, 0, '0')
            self.topology(root, 2, 0, 1, '2')
            self.topology(root, 8, 1, 0, '8')
            actual = runner.cpu_topology([0, 2, 8], root)
            self.assertEqual(actual['selected_physical_cores'], 3)
            self.assertFalse(actual['shares_physical_core'])

    def test_unknown_topology_does_not_guess_from_logical_count(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.topology(root, 0, 0, 0, '0')
            self.topology(root, 1, -1, 'invalid', None)
            actual = runner.cpu_topology([0, 1, 2], root)
            self.assertIsNone(actual['selected_physical_cores'])
            self.assertIsNone(actual['shares_physical_core'])
            self.assertIsNone(actual['cpus'][1]['core_id'])
            self.assertIsNone(actual['cpus'][1]['package_id'])
            self.assertIsNone(actual['cpus'][2]['thread_siblings_list'])
            self.assertIsNone(runner.cpu_topology(None, root))

    def test_resource_accounting_accepts_complete_unsupported_and_old_records(self):
        row = dict(process_cpu_ns=100, process_user_cpu_ns=70, process_system_cpu_ns=30,
                   voluntary_context_switches=4, involuntary_context_switches=2)
        runner.validate_process_usage(row)
        runner.validate_process_usage(dict.fromkeys(row, -1))
        runner.validate_process_usage(dict(process_cpu_ns=100))

    def test_resource_accounting_rejects_partial_inconsistent_and_negative_records(self):
        row = dict(process_cpu_ns=100, process_user_cpu_ns=70, process_system_cpu_ns=30,
                   voluntary_context_switches=4, involuntary_context_switches=2)
        for key, value in [('process_cpu_ns', 101), ('process_user_cpu_ns', -1),
                           ('voluntary_context_switches', -2), ('involuntary_context_switches', 0.5),
                           ('process_system_cpu_ns', True)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                runner.validate_process_usage(dict(row, **{key: value}))
        row.pop('involuntary_context_switches')
        with self.assertRaises(ValueError):
            runner.validate_process_usage(row)


if __name__ == '__main__':
    unittest.main()
